#pragma once
#include <memory>
#include <string>
#include "FileUtil.h"
#include "MutexLock.h"
#include <unistd.h>
#include "noncopyable.h"

namespace myconcurrent{
// TODO 提供自动归档功能
class LogFile : noncopyable {
 public:
  // 每被append flushEveryN次，flush一下，会往文件写，只不过，文件也是带缓冲区的
  LogFile(const std::string& basename, int flushEveryN = 1024);
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  void rollFile();

 private:
  void append_unlocked(const char* logline, int len);
  std::string getFilename();
  const std::string basename_;
  const int flushEveryN_;
  static const int KMAXFileSize=1024*1024*1024;
  int count_;
  int curSize_;
  std::unique_ptr<MutexLock> mutex_;
  std::unique_ptr<AppendFile> file_;
};
}