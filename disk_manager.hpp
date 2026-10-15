#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include "minidb/common/config.hpp"
#include "minidb/common/types.hpp"

namespace minidb {
using namespace std;

class DiskManager {
 public:
  DiskManager() = default;
  ~DiskManager();
  DiskManager(const DiskManager&) = delete;
  DiskManager& operator=(const DiskManager&) = delete;

  bool open(const string& path);

  void close();

  bool readPage(PageId pageId, char* out);

  bool writePage(PageId pageId, const char* data);

  PageId allocatePage();

  bool sync();

 private:
  int fd_ = -1;
  atomic<PageId> numPages_{0};
  mutex allocMutex_;
};

}
