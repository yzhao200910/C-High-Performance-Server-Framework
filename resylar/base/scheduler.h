#pragma once
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <atomic>
#include "fiber.h"
#include "Logging.h"
#include "Thread.h"

namespace myconcurrent{
/*
    协程调度器
    封装的是N-M的协程的调度器，内部存在线程池
*/
class Scheduler{
public:
    typedef std::shared_ptr<Scheduler> ptr;
    
    /*
        创建调度器
        threads 线程数
        use_caller是否将当前线程也作为调度器
        name 名称    
    */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string &name ="Scheduler");

    //析构函数

    virtual ~Scheduler();

    //获取调度器的名称
    const std::string &getName() const {return m_name;}

    //获取当前线程调度器指针
    static Scheduler *GetThis();

    //获取当前线程的主协程
    static Fiber *GetMainFiber();
    /**
     * *模板实现增加复用性
     * *添加调度任务 
     * *任何类型为FiberOrcb，可以是协程对象或者是函数指针
     * *thread指定运行该任务的线程号， -1表示任何线程
    */
   template <class FiberOrCb>
   void schedule(FiberOrCb fc, int thread = -1){
        bool need_tickle = false;
        {
            MutexLockGuard Mutex(m_mutex);
            need_tickle = scheduleNoLock(fc,thread);
        }
        if(need_tickle) // 唤醒idle协程
            tickle();
   }
    //*启动调度器
    void start();

    //*停止调度器
    void stop();
protected:
    //*用于通知协程调度器工作
    virtual void tickle();

    //*协程调度函数
    void run();

    //*无任务调度时执行idle协程
    virtual void idle();

    //*返回是否可以停止
    virtual bool stopping();

    //*设置当前协程调度器
    void setThis();

    /**
     *  *返回是否有空闲线程
     * * 当调度协程进入idle时空闲线程数加1，从idle协程返回时空闲线程数减1
     */
    bool hasIdleThreads() { return m_idleThreadCount > 0; }
private:
    template <class FiberOrcb>
    bool scheduleNoLock(FiberOrcb fc,int thread){
        bool need_tickle = m_tasks.empty();
        ScheduleTask task(fc, thread);
        if(task.fiber || task.cb)/调度对象有回调函数或者协程
            m_tasks.push_back(task);
        return need_tickle; 
    }   

private:

    struct ScheduleTask{
        Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask(Fiber::ptr f, int thr){
            fiber = f;
            thread = thr;
        }
        ScheduleTask(Fiber::ptr *f, int thr){
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()> f, int thr){
            cb = f;
            thread = thr;
        }
        ScheduleTask(){thread = -1;}

        void reset(){
            fiber = nullptr;
            cb =nullptr;
            thread = -1;
        }
    };



private:
    //*调度器名称
    std::string m_name;

    //互斥锁
    MutexLock m_mutex;

    //线程池；
    std::vector<Thread::ptr> m_threads;

    //任务队列
    std::list<ScheduleTask> m_tasks;
    //记录线程的ID的数组
    std::vector<int> m_threadIds;
    
    //工作线程数量
    size_t m_threadCount = 0;
    //活跃的线程数
    std::atomic<size_t> m_activeThreadCount = {0};

    // idle线程数
    std::atomic<size_t> m_idleThreadCount = {0};

    //是否use caller；为true时，调度器所在线程的调度协程
    bool m_useCaller;

    Fiber::ptr m_rootFiber;
    //user_caller 为true时，调度器所在线程的id
    int m_rootThread=0;

    //是否停止
    bool m_stopping = false;
};//


}//myconcurrent