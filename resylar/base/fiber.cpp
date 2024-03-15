#pragma once
#include <atomic>
#include "fiber.h"
#include "Logging.h"

namespace myconcurrent
{
/*
这种类型的变量上，诸如加载、存储、
交换和操作等操作都是原子的，
这意味着这些操作在多线程环境下不会被中断或交错执行
*/
// 全局静态变量，用于生成协程id
static std::atomic<uint64_t> s_fiber_id{0};
// 全局静态变量，用于统计当前的协程数
static std::atomic<uint64_t> s_fiber_count{0};

/// 线程局部变量，当前线程正在运行的协程
static thread_local Fiber *t_fiber = nullptr;
/// 线程局部变量，当前线程的主协程，切换到这个协程，就相当于切换到了主线程中运行，智能指针形式
static thread_local Fiber::ptr t_thread_fiber = nullptr;

//栈内存分配器
class MallocStackAllocator{
    public:
        static void *Alloc(size_t size){return malloc(size);}
        static void Dealloc(void *vp,size_t size){return free(vp);}
};

using StackAllocator = MallocStackAllocator;

uint64_t Fiber::GetFiberId(){
    if(t_fiber)
        return t_fiber->getId();
    return 0;
}

Fiber::Fiber(){
    SetThis(this);//
    m_state = RUNNING;

    if(getcontext(&m_ctx)<0) 
    {
        LOG_INFO<<"getcontext error";
        perror("getcontetx error:");
        assert(false);
    }
    ++s_fiber_count;
    m_id = s_fiber_id++; //协程的id从0，开始
    LOG_DEBUG<<"Main Councurrent id :"<<m_id;
}

void Fiber::SetThis(Fiber *f){
    t_fiber = f ;
}

//获取当前协程，同时充当锤石话但钱线程主协程的作用

Fiber::ptr Fiber::GetThis(){
    if(t_fiber)
        return t_fiber->shared_from_this();//指向当前对象
    Fiber::ptr main_fiber(new Fiber);//调用默认构造，创建主协程
    assert(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;//让当前的线程的局部变量等于主协程
    return t_fiber->shared_from_this();//指向当前对象，获取当前对象
}
//带参的构造函数用于创建其他协程，需要分配栈
Fiber::Fiber(std::function<void()> cb , size_t stacksize, bool run_in_scheduler)
    :m_id(s_fiber_id++),
     m_cb(cb),
     m_runInScheduler(run_in_scheduler)
{
    ++s_fiber_count;//*增加协程的计数数量
   m_stacksize = stacksize ? stacksize : 128*1024;//后者为默认大小
   m_stack    = StackAllocator::Alloc(m_stacksize);
   if(getcontext(&m_ctx)<0){
    perror("getcontex");
    assert(false);
   }
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp   = m_stack;

    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    LOG_DEBUG<<"Fiber::Fiber() id = " <<m_id;
}

Fiber::~Fiber(){
    LOG_DEBUG<<"Fiber::~Fiber() id = " <<m_id;
    --s_fiber_count;
    //*存在栈空间属于子协程，需要确保已经是结束状态
    if(m_stack){
        assert(m_state == TERM);
        StackAllocator::Dealloc(m_stack,m_stacksize);
        LOG_DEBUG<<"StackAllocator::Dealloc() id = " <<m_id;
    }else{//*主协程
        assert(!m_cb); //*主协程没有回调
        //*而且一定要处于执行态
        assert(m_state = RUNNING);

        Fiber *cur = t_fiber; //*当前协程就是自己
        if(cur == this)
            SetThis(nullptr);

    }

}

void Fiber::reset(std::function<void()> cb){

}

void Fiber::resume(){

}

void Fiber::yield(){

}

void Fiber::MainFunc(){

}

}// myconcurrent

