#pragma once
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "LogStream.h"

namespace myconcurrent{
class AsyncLogging;

class Logger {
 public:
  enum LogLevel
  {
    TRACE, //0
    DEBUG, //1
    INFO,//2
    WARN,//3
    ERROR,//4
    FATAL,//5
    NUM_LOG_LEVELS,//6
  };
  Logger(const char *fileName, int line);
  ~Logger();
  LogStream &stream() { return impl_.stream_; }
  static LogLevel logLevel();
  //static LogLevel setLogLevel();
  static void setLogFileName(std::string fileName) { logFileName_ = fileName; }
  static std::string getLogFileName() { return logFileName_; }

 private:
  class Impl {
   public:
    Impl(const char *fileName, int line);
    void formatTime();

    LogStream stream_;
    int line_;
    std::string basename_;
  };
  Impl impl_;
  static std::string logFileName_;
};
extern Logger::LogLevel eloglevel;

inline Logger::LogLevel Logger::logLevel(){
          return eloglevel;
}

#define LOG_TRACE if(myconcurrent::Logger::logLevel() <= myconcurrent::Logger::LogLevel::TRACE) \
  Logger(__FILE__, __LINE__).stream()<<"[TRACE]"

#define LOG_DEBUG if(myconcurrent::Logger::logLevel()<= myconcurrent::Logger::LogLevel::DEBUG) \
  Logger(__FILE__, __LINE__).stream()<<"[DEBUG]"

#define LOG_INFO if(myconcurrent::Logger::logLevel()<= myconcurrent::Logger::LogLevel::INFO) \
  Logger(__FILE__, __LINE__).stream()<<"[INFO]"

#define LOG_WARN if(myconcurrent::Logger::logLevel()<= myconcurrent::Logger::LogLevel::WARN) \
  Logger(__FILE__, __LINE__).stream()<<"[WARN]"

#define LOG_ERROR if(myconcurrent::Logger::logLevel()<= myconcurrent::Logger::LogLevel::ERROR) \ 
  Logger(__FILE__, __LINE__).stream()<<"[ERROR]"

#define LOG_FATAL if(myconcurrent::Logger::logLevel()<= myconcurrent::Logger::LogLevel::FATAL) \
  Logger(__FILE__, __LINE__).stream()<<"[FATAL]"

#define LOG Logger(__FILE__, __LINE__).stream()
}