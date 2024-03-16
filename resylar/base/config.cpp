#include "config.h"
#include "Util.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace  myconcurrent{

ConfigVarBase::ptr Config::LookupBase(const std::string &name) {
    MutexLockGuard lock(GetMutex());
    auto it = GetDatas().find(name);
    return it == GetDatas().end() ? nullptr : it->second;
}

void Config::Visit(std::function<void(ConfigVarBase::ptr)> cb) {
    MutexLockGuard lock(GetMutex());
    ConfigVarMap &m = GetDatas();
    for (auto it = m.begin();
         it != m.end(); ++it) {
        cb(it->second);
    }
}























}