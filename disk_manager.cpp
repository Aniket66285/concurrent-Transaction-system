#include "minidb/storage/disk_manager.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>

using namespace std;

namespace minidb {

DiskManager::~DiskManager() { close(); }

bool DiskManager::open(const string& path) {
  if (fd_ >= 0) return false;
  error_code ec;
  const filesystem::path p(path);
  if (p.has_parent_path()) {
    filesystem::create_directories(p.parent_path(), ec);
    if (ec) return false;
  }
  int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd < 0) return false;
  struct stat st;
  if (::fstat(fd, &st) != 0 ||
      static_cast<uint64_t>(st.st_size) % kPageSizeBytes != 0) {
    ::close(fd);
    return false;
  }
  fd_ = fd;
  numPages_.store(static_cast<uint64_t>(st.st_size) / kPageSizeBytes);
  return true;
}

void DiskManager::close() {
  if (fd_ < 0) return;
  ::fsync(fd_);
  ::close(fd_);
  fd_ = -1;
  numPages_.store(0);
}

bool DiskManager::readPage(PageId pageId, char* out) {
  if (fd_ < 0 || out == nullptr || pageId >= numPages_.load()) return false;
  const off_t base = static_cast<off_t>(pageId * kPageSizeBytes);
  size_t done = 0;
  while (done < kPageSizeBytes) {
    ssize_t n = ::pread(fd_, out + done, kPageSizeBytes - done,
                        base + static_cast<off_t>(done));
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    done += static_cast<size_t>(n);
  }
  return true;
}

bool DiskManager::writePage(PageId pageId, const char* data) {
  if (fd_ < 0 || data == nullptr || pageId >= numPages_.load()) return false;
  const off_t base = static_cast<off_t>(pageId * kPageSizeBytes);
  size_t done = 0;
  while (done < kPageSizeBytes) {
    ssize_t n = ::pwrite(fd_, data + done, kPageSizeBytes - done,
                         base + static_cast<off_t>(done));
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    done += static_cast<size_t>(n);
  }
  return true;
}

PageId DiskManager::allocatePage() {
  if (fd_ < 0) return kInvalidPageId;
  lock_guard<mutex> g(allocMutex_);
  const PageId id = numPages_.load();
  if (::ftruncate(fd_, static_cast<off_t>((id + 1) * kPageSizeBytes)) != 0)
    return kInvalidPageId;
  numPages_.store(id + 1);
  return id;
}

bool DiskManager::sync() { return fd_ >= 0 && ::fsync(fd_) == 0; }

}
