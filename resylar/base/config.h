
/**
 ** 配置文件,不会使用yaml来配置文件，这里简化一下
 ** 类型的转换使用std::to_string来完成类型转换
*/
#pragma once
#include <memory>
#include "Util.h"
#include <string>
#include <sstream>
#include <boost/lexical_cast.hpp>
//#include <yaml-cpp/yaml.h>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "MutexLock.h"
#include "Logging.h"

namespace  myconcurrent{
//*配置变量的基类
class ConfigVarBase{
public:
    typedef std::shared_ptr<ConfigVarBase> ptr;

    /**
     * *构造函数
     * *name配置参数名称
     * *description
    */

   ConfigVarBase(const std::string &name, const std::string &description = "")
        :m_name(name),
         m_description(description)
   {
        std::transform(m_name.begin(),m_name.end(),m_name.begin(),::tolower);
   }
   
   //*析构函数
   virtual ~ConfigVarBase(){}

   //*返回配置参数名称
   const std::string &getName()const{return m_name;} 

   //*f返回配置参数的描述
   const std::string &getDescription() const {return m_description;}

   virtual std::string toString() = 0;

   virtual std::string getTypeName() const = 0;

protected:
    //配置参数的名称
    std::string m_name;
    //配置参数的描述
    std::string m_description;
};

template <class T>
class ConfigVar : public ConfigVarBase{
public:
    typedef std::shared_ptr<ConfigVar> ptr;
    typedef std::function<void(const T &old_vuale, const T &new_value>) on_change_cb;

     ConfigVar(const std::string &name, const T &default_value, const std::string &description = "")
        : ConfigVarBase(name, description)
        , m_val(default_value) {
    }

    std::string toString() override{
        try{
            MutexLockGuard Mutex(m_mutex);
            return std::to_string(m_val);
        }catch(std::exception &e){
            LOG_ERROR<<"the exception:"<<e.what();
        }
        return "";
    }
    /**
     * sylar的原本代码还要从yaml中取字符，这里不考虑用yaml配置，所以不写
    */

    //*获取当前参数的值
    const T getValue(){
        MutexLockGuard lock(m_mutex);
        return m_val;
    }

    //*设置当前参数的值
    //*如果参数的值有变化，则通知对于的注册回调函数
    void setValue(const T &v){
        {
        MutexLockGuard Mutex(m_mutex);
        if(v == m_val){
            return;
        }
        for(auto &i : m_cbs){
            i.second(m_val,v);//*出入对于的key和value
        }
        }
        MutexLockGuard Mutex(m_mutex);
        m_val = v;
    }
    std::string getTypeName() const override { return TypeToName<T>(); }

    uint64_t addListener(on_change_cb cb){
        static uint64_t s_fun_id = 0;
        MutexLockGuard Mutex(m_mutex);
        ++s_fun_id;
        m_cbs[s_fun_id] = cb;
        return s_fun_id;
    }

    void delListener(uint64_t key) {
        MutexLockGuard lock(m_mutex);
        m_cbs.erase(key);
    }
    on_change_cb getListener(uint64_t key) {
        MutexLockGuard lock(m_mutex);
        auto it = m_cbs.find(key);
        return it == m_cbs.end() ? nullptr : it->second;
    }
    void clearListener() {
        MutexLockGuard lock(m_mutex);
        m_cbs.clear();
    }


private:
    MutexLock m_mutex;
    T m_val;
    //*变更回调函数数组，uint64_t key要求唯一
    std::map<uint64_t, on_change_cb> m_cbs;

};

class Config {
public:
    typedef std::unordered_map<std::string, ConfigVarBase::ptr> ConfigVarMap;
    

    /**
     * @brief 获取/创建对应参数名的配置参数
     * @param[in] name 配置参数名称
     * @param[in] default_value 参数默认值
     * @param[in] description 参数描述
     * @details 获取参数名为name的配置参数,如果存在直接返回
     *          如果不存在,创建参数配置并用default_value赋值
     * @return 返回对应的配置参数,如果参数名存在但是类型不匹配则返回nullptr
     * @exception 如果参数名包含非法字符[^0-9a-z_.] 抛出异常 std::invalid_argument
     */
    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string &name,
                                             const T &default_value, const std::string &description = "") {
        RWMutexType::WriteLock lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it != GetDatas().end()) {
            auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
            if (tmp) {
                LOG_INFO << "Lookup name=" << name << " exists";
                return tmp;
            } else {
                LOG_ERROR << "Lookup name=" << name << " exists but type not "
                                                  << TypeToName<T>() << " real_type=" << it->second->getTypeName()
                                                  << " " << it->second->toString();
                return nullptr;
            }
        }

        if (name.find_first_not_of("abcdefghikjlmnopqrstuvwxyz._012345678") != std::string::npos) {
            LOG_ERROR(SYLAR_LOG_ROOT()) << "Lookup name invalid " << name;
            throw std::invalid_argument(name);
        }

        typename ConfigVar<T>::ptr v(new ConfigVar<T>(name, default_value, description));
        GetDatas()[name] = v;
        return v;
    }


    template <class T>
    static typename ConfigVar<T>::ptr Lookup(const std::string &name) {
        MutexLockGuard lock(GetMutex());
        auto it = GetDatas().find(name);
        if (it == GetDatas().end()) {
            return nullptr;
        }
        return std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
    }



    /**
     * @brief 加载path文件夹里面的配置文件
     */
   // static void LoadFromConfDir(const std::string &path, bool force = false);

    /**
     * @brief 查找配置参数,返回配置参数的基类
     * @param[in] name 配置参数名称
     */
    static ConfigVarBase::ptr LookupBase(const std::string &name);

    /**
     * @brief 遍历配置模块里面所有配置项
     * @param[in] cb 配置项回调函数
     */
    static void Visit(std::function<void(ConfigVarBase::ptr)> cb);

private:
    /**
     * @brief 返回所有的配置项
     */
    static ConfigVarMap &GetDatas() {
        static ConfigVarMap s_datas;
        return s_datas;
    }

   
    static MutexLock &GetMutex() {
        static MutexLock m_mutex;
        return m_mutex;
    }
};

} 




