#include "scheduler.h"
#include "hook.h"
#include "CurrentThread.h"

namespace myconcurrent {

//* 当前线程的调度器，同一个调度器下所有线程共享一个实例
static thread_local Scheduler *t_scheduler = nullptr;

//*当前线程的调度协程，每个线程只要一个
static thread_local Fiber *t_scheduler_fiber = nullptr;

Scheduler::Scheduler(size_t threads, bool use_caller,const std::string &name)
{
    assert(threads > 0);
    m_useCaller = use_caller;
    m_name =name;
    if(use_caller){
        --threads;
        myconcurrent::Fiber::GetThis();
        assert(GetThis()== nullptr );
        t_scheduler = this;
        
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run,this),0,false));
        t_scheduler_fiber=m_rootFiber.get();
        m_rootThread = CurrentThread::tid();//*获得当前线程的ID,调度器所在的线程
        m_threadIds.push_back(m_rootThread);  
    }else{
        m_rootThread = -1;
    }
    m_threadCount = threads;
}

Scheduler *Scheduler::GetThis(){
    return t_scheduler;
}

Fiber *Scheduler::GetMainFiber(){
    return t_scheduler_fiber;
}

void Scheduler::setThis(){
    t_scheduler = this;
}

Scheduler::~Scheduler(){
    t_scheduler = this;
    LOG_DEBUG<<"Scheduler::~Scheduler";
    assert(m_stopping);
    if(GetThis() == this)
        t_scheduler = nullptr;
}

void Scheduler::start(){
    LOG_DEBUG<<"Scheduler start()";
    MutexLockGuard mtlock(m_mutex);
    if(m_stopping){
        LOG_ERROR<<"Scheduler is stopped";
        return;
    }
    assert(m_threads.empty());
    m_threads.resize(m_threadCount);
    for(size_t i = 0; i<m_threadCount;i++){
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run,this),m_name+"_"+std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->tid());
    }
}

bool Scheduler::stopping(){
    MutexLockGuard mtlock(m_mutex);

    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle(){
    LOG_DEBUG<<"ticlke";
}

void Scheduler::idle(){
    LOG_DEBUG<<"idle";
    while(!stopping()){
        myconcurrent::Fiber::GetThis()->yield();
    }
}

void Scheduler::stop(){
    LOG_DEBUG<<"stop";
    if(stopping()){
        return;
    }
    m_stopping = true;

    if(m_useCaller){//*如果use_caller,那只能由caller线程发起stop
        assert(GetThis() == this);
    }else{
        assert(GetThis() != this);
    }
    for(size_t i =0 ; i<m_threadCount;i++)
        tickle();
    if(m_rootFiber)
        tickle();
    if(m_rootFiber){
        m_rootFiber->resume();
        LOG_DEBUG<<"m_rootFiber end";
    }

    std::vector<Thread::ptr> thrs;
    {
        MutexLockGuard lock(m_mutex);
        thrs.swap(m_threads);
    }
    for(auto &i : thrs){
        i->join();
    }
}

void Scheduler::run(){
    LOG_DEBUG<<"run";
    /**
     * set_hook_enable(true);
    */
   setThis();

   if(myconcurrent::CurrentThread::tid() != m_rootThread){
        t_scheduler_fiber = myconcurrent::Fiber::GetThis().get();
   }

   Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle,this)));
   Fiber::ptr cb_fiber;

   ScheduleTask task;
   while(true){
    task.reset();
    bool tickle_me = false; // 是否tickle其他线程进行任务调度
    {
        MutexLockGuard lock(m_mutex);
        auto it = m_tasks.begin();
        //遍历所有的调度任务
        while(it != m_tasks.end()){
            if(it->thread != -1 && it->thread != myconcurrent::CurrentThread::tid()){
                //?指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                ++it;
                tickle_me = true;
                continue;;
            }
            assert(it->fiber || it->cb);
            if(it->fiber && it->fiber->getState()== Fiber::RUNNING){
                ++it;
                continue;
            }
            //*当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除。活动线程加1
            task = *it;
            m_tasks.erase(it++);
            ++m_activeThreadCount;
            break;
        }
        //*如果任务队列中还有任务，就tickle其他线程
        tickle_me |=(it != m_tasks.end());
    }
    if(tickle_me){
        tickle();
    }
    if(task.fiber){
        //* resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成，活跃线程数减1
        task.fiber->resume();
        --m_activeThreadCount;
        task.reset();
    }else if(task.cb){
        if(cb_fiber){
            cb_fiber->reset(task.cb);
        }else{
            cb_fiber.reset(new Fiber(task.cb));
        }
        task.reset();
        --m_activeThreadCount;
        cb_fiber.reset();
    }else{//进入这个分支情况一定时任务队列为空，调度idle协程即可
        if(idle_fiber->getState() == Fiber::TERM){
            LOG_DEBUG <<"idle fiber term";
            break;
        }
        ++m_idleThreadCount;
        idle_fiber->resume();
        --m_idleThreadCount;
    }

   }
   LOG_DEBUG<<"Scheduler::run() exit"; 
}


}