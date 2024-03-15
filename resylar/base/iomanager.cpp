#include <unistd.h>    // for pipe()
#include <sys/epoll.h> // for epoll_xxx()
#include <fcntl.h> 
#include "iomanager.h"
#include <stdexcept>
#include "Logging.h"

namespace  myconcurrent{

enum EpollCtlop{};

IOManager::FdContext::EventContext &IOManager::FdContext::getEventContext(IOManager::Event event){
    switch(event)
    {
    case IOManager::READ:
        return read;
        break;
    case IOManager::WRITE:
        return write;
    
    default:
        LOG_ERROR<<"ASSERT ERROR";
        assert(false);
    }
    throw std::invalid_argument("getContext invalid event");
}

void IOManager::FdContext::resetEventContext(EventContext &ctx) {
    ctx.scheduler = nullptr;
    ctx.fiber.reset();
    ctx.cb = nullptr;
}

void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    //待触发事件必须已经被注册过
    if(events & event){
        LOG_ERROR<<"This event is not registered";
        assert(false);
    }
    /**
     *  *清除该事件，表示不再关注该事件了
     *  *也就是说，注册的IO事件是一次性的，如果想持续关注某个socket fd的读写事件，那么每次触发事件之后都要重新添加
     */
    events = (Event)(events & ~event);

    //*调度对应的协程
    EventContext &ctx = getEventContext(event);
    if (ctx.cb) {
        ctx.scheduler->schedule(ctx.cb);
    } else {
        ctx.scheduler->schedule(ctx.fiber);
    }
    resetEventContext(ctx);
    return;
}

IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
    : Scheduler(threads, use_caller, name){
        m_epfd = epoll_create(500);
        assert(m_epfd> 0);

    int rt = pipe(m_tickleFds);
    assert(!rt);

    //* 关注pipe读句柄的可读事件，用于tickle协程
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events  = EPOLLIN | EPOLLET;
    event.data.fd = m_tickleFds[0];

    //*非堵塞
    rt = fcntl(m_tickleFds[0],F_SETFL,O_NONBLOCK);
    assert(!rt);

    //*对管道读端进行检测
    rt = epoll_ctl(m_epfd,EPOLL_CTL_ADD,m_tickleFds[0],&event);
    assert(!rt);

    contextResize(32);

    start();
    }

IOManager::~IOManager() {
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (m_fdContexts[i]) {
            delete m_fdContexts[i];
        }
    }
}

void IOManager::contextResize(size_t size) {
    m_fdContexts.resize(size);

    for (size_t i = 0; i < m_fdContexts.size(); ++i) {
        if (!m_fdContexts[i]) {
            //*这里可以用内存池优化
            m_fdContexts[i] = new FdContext;
            m_fdContexts[i]->fd = i;
        }
    }
}

int IOManager::addEvent(int fd, Event event, std::function<void()> cb){
    FdContext *fd_ctx = nullptr;
    if((int)m_fdContexts.size() > fd){
        MutexLockGuard Mutex(m_mutex);
        fd_ctx = m_fdContexts[fd];
    }else{
        MutexLockGuard Mutex(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    //*同一个fd不允许重复添加相同的事件
    MutexLockGuard mutex(fd_ctx->m_mutex);

    if(fd_ctx->events & event){
        LOG_ERROR<<"addEvent assert fd=" << fd
                                  << " event=" << (EPOLL_EVENTS)event
                                  << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        assert(!(fd_ctx->events & event));
    }

    //*将新的事件加入epoll_wait,使用epoll_event的私有指针存储FdContext的位置
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt){
        LOG_ERROR<<"addEvents in epoll_ctl apper error";
        return -1;
    }
    //待执行I/O事件数+1
    ++m_pendingEventCount;
    
    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fd_ctx->events = (Event)(fd_ctx->events | event);
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

     //* 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体
    event_ctx.scheduler = Scheduler::GetThis();

    if(cb){
        event_ctx.cb.swap(cb);
    }else{
        event_ctx.fiber = Fiber::GetThis();
        assert(event_ctx.fiber->getState() == Fiber::RUNNING);
    }
    return 0;
}

bool IOManager::delEvent(int fd, Event event){
    //找到fd对应的fdContext
    FdContext *fd_ctx;
    {
    MutexLockGuard Mutex(m_mutex);
    if((int)m_fdContexts.size()<= fd) return false;
    
    fd_ctx= m_fdContexts[fd];
    
    }

    MutexLockGuard FdMutex(fd_ctx->m_mutex);
    if(!(fd_ctx->events & event)){
        return false;
    }

    Event new_events = static_cast<Event>(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op , fd, &epevent);
    if(rt){
        LOG_ERROR<< "epoll_ctl(" << m_epfd << ", "
                                      << (EpollCtlop)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
        
    }
    //*待执行事件数减一
    --m_pendingEventCount;
    //*重置该fd对应的event事件上下文
    fd_ctx->events = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;  
}

bool IOManager::cancelEvent(int fd, Event event){
    //*找到fd对应的FdContext
    FdContext *fd_ctx = m_fdContexts[fd];
    {
    MutexLockGuard Mutex(m_mutex);
    if((int)m_fdContexts.size() <= fd){
        return false;
    }  
    fd_ctx = m_fdContexts[fd];
    }
    MutexLockGuard FdMutex(fd_ctx->m_mutex);
    if(!(fd_ctx->events & event)){
        return false;
    }

    Event new_events = static_cast<Event>(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt= epoll_ctl(m_epfd, op, fd , &epevent);
    if(rt){
        LOG_ERROR << "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlop)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

     // 删除之前触发一次事件
    fd_ctx->triggerEvent(event);
    // 活跃事件数减1
    --m_pendingEventCount;
    return true;
}


bool IOManager::cancelAll(int fd) {
    FdContext *fd_ctx;
    {
    MutexLockGuard Mutex(m_mutex);
         if ((int)m_fdContexts.size() <= fd) {
            return false;
    }
    fd_ctx = m_fdContexts[fd];
   
    }

    MutexLockGuard FdMutex(fd_ctx->m_mutex);
    if(!fd_ctx->events){
        return false;
    }

     int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        LOG_ERROR<< "epoll_ctl(" << m_epfd << ", "
                                  << (EpollCtlop)op << ", " << fd << ", " << (EPOLL_EVENTS)epevent.events << "):"
                                  << rt << " (" << errno << ") (" << strerror(errno) << ")";
        return false;
    }

    // 触发全部已注册的事件
    if (fd_ctx->events & READ) {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if (fd_ctx->events & WRITE) {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    assert(fd_ctx->events == 0);
    return true;
}

IOManager *IOManager::GetThis() {
    return dynamic_cast<IOManager *>(Scheduler::GetThis());
}
/**
 * *通知调度协程、也就是Scheduler::run()从idle中退出
 * *Scheduler::run()每次从idle协程中退出之后，都会重新把任务队列里的所有任务执行完了再重新进入idle
 * *如果没有调度线程处理于idle状态，那也就没必要发通知了
 */

void IOManager::tickle(){
    LOG_DEBUG<<"tickle";
    if(!hasIdleThreads()) return;
    int rt = write(m_tickleFds[1],"T",1);
    assert(rt ==1 ); 
}

bool IOManager::stopping(){
    uint64_t timeout = 0;
    return stopping(timeout);
}

bool IOManager::stopping(uint64_t &timeout){
    //对于IOManager二元，必须等所有待调度的IO事件执行完才能退出
    //而且得保证没有剩余的定时器触发
     timeout = getNextTimer();
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}
/**
 * *调度器无调度任务时会阻塞idle协程上，对IO调度器而言，idle状态应该关注两件事，一是有没有新的调度任务，对应Schduler::schedule()，
 * *如果有新的调度任务，那应该立即退出idle状态，并执行对应的任务；二是关注当前注册的所有IO事件有没有触发，如果有触发，那么应该执行
 * *IO事件对应的回调函数
 */

void IOManager::idle(){
    LOG_DEBUG<<"idle";

    //一次epoll_wait最多检测256个就绪事件，如果就绪事件数超过，那么会在下一轮epoll_wait继续处理
    const uint64_t MAX_EVNETS = 256;
    epoll_event *events = new epoll_event[MAX_EVNETS]();
    std::shared_ptr<epoll_event> shared_events(events,[](epoll_event *ptr){
        delete[]  ptr;
    });

    while(true){
        //获取定时器下一个超时的时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if(stopping(next_timeout)){
            LOG_DEBUG<<"name = "<<getName()<<"idle stopping exit";
            break;
        }
        //* 阻塞在epoll_wait上，等待事件发生或定时器超时
        int rt = 0;
        do{
            // *默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时，避免定时器超时时间太大时，epoll_wait一直阻塞
            static const int MAX_TIMEOUT = 5000;
            if(next_timeout != ~0ull) {
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            } else {
                next_timeout = MAX_TIMEOUT;
            }
            rt = epoll_wait(m_epfd, events, MAX_EVNETS, (int)next_timeout);
            if(rt < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        } while(true);

          // 收集所有已超时的定时器，执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) {
            for(const auto &cb : cbs) {
                schedule(cb);
            }
            cbs.clear();
        }
         // 遍历所有发生的事件，根据epoll_event的私有指针找到对应的FdContext，进行事件处理
        for (int i = 0; i < rt; ++i) {
            epoll_event &event = events[i];
            if (event.data.fd == m_tickleFds[0]) {
                // ticklefd[0]用于通知协程调度，这时只需要把管道里的内容读完即可
                uint8_t dummy[256];
                while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0)
                    ;
                continue;
            }

            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            MutexLockGuard Mutex(fd_ctx->m_mutex);
            
              /**
             * EPOLLERR: 出错，比如写读端已经关闭的pipe
             * EPOLLHUP: 套接字对端关闭
             * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执行不到的情况
             */ 
            if (event.events & (EPOLLERR | EPOLLHUP)) {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if (event.events & EPOLLIN) {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }

            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            // 剔除已经发生的事件，将剩下的事件重新加入epoll_wait
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events    = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) {
                LOG_ERROR<< "epoll_ctl(" << m_epfd << ", "
                                          << (EpollCtlop)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                                          << rt2 << " (" << errno << ") (" << strerror(errno) << ")";
                continue;
            }

            // 处理已经发生的事件，也就是让调度器调度指定的函数或协程
            if (real_events & READ) {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            if (real_events & WRITE) {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        /**
         * 一旦处理完所有的事件，idle协程yield，这样可以让调度协程(Scheduler::run)重新检查是否有新任务要调度
         * 上面triggerEvent实际也只是把对应的fiber重新加入调度，要执行的话还要等idle协程退出
         */ 
        Fiber::ptr cur = Fiber::GetThis();
        auto raw_ptr   = cur.get();
        cur.reset();

        raw_ptr->yield();
    }
  
}
    void IOManager::onTimerInsertedAtFront() {
    tickle();
}

}

