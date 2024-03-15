//协程类的实现
#pragma once
#include <functional>
#include <memory>
#include <ucontext.h>
#include "Thread.h"

namespace myconcurrent{

    //协程类
class Fiber :  public std::enable_shared_from_this<Fiber>{//会获得一个成员函数shared_from_this(),指向当前对象
    public:
        typedef std::shared_ptr<Fiber> ptr;
        
        /*
            枚举成员表示状态：准备态（READY）,要运行结束态（Term）,运行态（RUNNING）        
        */
       enum State{
        //就绪态，刚创建或者yield之后的状态
        READY,
        //运行态，resume之后的状态
        RUNNING,
        //结束态，回调函数执行完成之后
        TERM
       };
    private:
        //用于创建第一个协程
        Fiber();
    public:
        Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);
        //重置，复用栈空间，减少malloc的使用
        void reset(std::function<void()> cb);

        //将当前协程切到执行状态
        //当前协程和正在运行的协程进行切换，前者变为RUNNING后者变为READY
        void resume();

        //当前协程让出执行权
        void yield();

        //获取协程的ID
        uint64_t getId() const {return m_id;}

        //获取协程状态
        State getState() const { return m_state; }

    public:
        //设置正在运行协程，即设置线程局部变量t_fiber的值
        static void SetThis(Fiber *f);

        /*
            返回当前线程正在执行的协程
            如果当前线程还未创建协程，则创建线程的第一个协程
            这也就是当前线程的主协程，其他协程都通过这个主协程来调度
        */
       static Fiber::ptr GetThis();

       //获取总协程数
       static uint64_t TotalFibers();

       //获取当前协程的id
        static uint64_t GetFiberId();
       //协程入口函数
        static void MainFunc();
    private:
        //协程ID
        uint64_t m_id = 0;
        //协程栈的大小
        uint32_t m_stacksize = 0;
        //协程状态
        State m_state = READY;

        ucontext_t m_ctx; //协程的上下文
        //协程栈地址
        void *m_stack = nullptr;
        //协程入口函数
        std::function<void()>m_cb;
        //本协程是否参与调度器调度
        bool m_runInScheduler;
    };
}//myconcurrent