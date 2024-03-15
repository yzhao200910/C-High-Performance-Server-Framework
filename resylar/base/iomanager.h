#pragma once

#include "scheduler.h"
#include "timer.h"

namespace myconcurrent{
    
class IOManager : public Scheduler, public TimerManager{
    typedef std::shared_ptr<IOManager> ptr;

    /**
     ** I/O事件，继承epoll对事件的定义
     **主要关心读写事件，其他的事件会归分明成这两大事件
    */
   enum Event{
   
    //*noting
    NONE = 0x0,
    //*读事件（EPOLLIN）
    READ = 0x1,
    //*写事件（EPOLLOUT）
    WRITE = 0x3,
   };
private:
    //?fd上下文类
    //*每个socket fd 都对应一个FdContext,包括fd的值，fd上的事件，以及fd的读写上下文
    struct FdContext{
        //*事件上下文类
        struct EventContext{
            //*执行事件回调的调度器
            Scheduler *scheduler = nullptr;

            //*事件回调协程
            Fiber::ptr fiber;

            //*事件回调函数
            std::function<void()> cb;
        };

        //*获取事件上下文的类
        EventContext &getEventContext(Event event);

        //* 重置事件上下文
        void resetEventContext(EventContext &ctx);

        //*触发事件
        //*根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
        void triggerEvent(Event event);

        //*读事件上下文
        EventContext read;

        //*写事件上下文
        EventContext write;

        //*事件关联的句柄
        int fd = 0;

        //*该fd添加了哪些事件的回调函数，或者说该fd关心哪些事件
        Event events = NONE;

        //*事件的Mutex

        MutexLock m_mutex;
    };
public:
    /**
     **构造函数
     **threads 线程的数量
     **use_caller 是否将调用线程包含进去
     **调度器名称
    */
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");

   /**
    **析构函数
   */
    ~IOManager();

    //*增添事件
    //* cb事件回调函数，如果为空，默认把当前协程作为回调执行体
    int addEvent(int fd, Event event, std::function<void()>cb=nullptr);

    //*删除事件
    bool delEvent(int fd, Event event);

    //*取消事件
    bool cancelEvent(int fd, Event event);

    //*取消所有事件
    bool cancelAll(int fd);

    //*返回当前的IOManager
    static IOManager *GetThis();

protected:
    //*通知调度器有任务要调度
    void tickle() override;

    //判断是否可以停止
    bool stopping() override;

    //*idle协程
    void idle() override;

    bool stopping(uint64_t& timeout);

    //*当有定时器插入到头部时，要重新更新epoll_wait的超时时间，这里是唤醒idle协程以便于使用新的超时时间
    void onTimerInsertedAtFront() override;

    //*重置socket句柄上下文的容器大小
    void contextResize(size_t size);

private:
    //* epoll 文件句柄
    int m_epfd = 0;
    //* pipe 文件句柄，fd[0]读端，fd[1]写端
    int m_tickleFds[2];
    //* 当前等待执行的IO事件数量
    std::atomic<size_t> m_pendingEventCount = {0};
    //* IOManager的Mutex
    mutable MutexLock m_mutex;
    //* socket事件上下文的容器
    std::vector<FdContext *> m_fdContexts;
};


} // over
