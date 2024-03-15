#include "hook.h"
#include <dlfcn.h>
#include "fiber.h"
#include "iomanager.h"
#include "fd_manager.h"
#include "Logging.h"

namespace myconcurrent{

static thread_local bool t_hook_enable = false;

#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt)

void hook_init() {
    static bool is_inited = false;
    if(is_inited) {
        return;
    }
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
    HOOK_FUN(XX);
#undef XX
}

static uint64_t s_connect_timeout = -1;
struct _HookIniter {
    _HookIniter() {
        hook_init();
      // s_connect_timeout = g_tcp_connect_timeout->getValue();

     //   g_tcp_connect_timeout->addListener([](const int& old_value, const int& new_value){
                LOG_INFO<< "tcp connect timeout changed from ";
   //                                      << old_value << " to " << new_value;
   //             s_connect_timeout = new_value;
  //      });
    }
};









}