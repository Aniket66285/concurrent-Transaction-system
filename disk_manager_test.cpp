#include <gtest/gtest.h>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "minidb/storage/disk_manager.hpp"

using namespace std;

using minidb::DiskManager;
using minidb::kInvalidPageId;
using minidb::kPageSizeBytes;
using minidb::PageId;

namespace {

struct TempDb {
  filesystem::path dir;
  string path;
  TempDb() {
    static atomic<int> counter{0};
    dir = filesystem::temp_directory_path() /
          ("minidb_dm_test_" + to_string(::getpid()) + "_" +
           to_string(counter++));
    filesystem::create_directories(dir);
    path = (dir / "test.db").string();
  }
  ~TempDb() {
    error_code ec;
    filesystem::remove_all(dir, ec);
  }
};

void fillPattern(char* buf, unsigned seed) {
  for (size_t i = 0; i < kPageSizeBytes; ++i)
    buf[i] = static_cast<char>((i * 31 + seed * 7 + (i >> 8)) & 0xFF);
}

}

TEST(DiskManagerTest, OpenCreatesEmptyFile) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  EXPECT_EQ(filesystem::file_size(t.path), 0u);
}

TEST(DiskManagerTest, OpenCreatesMissingParentDirectory) {
  TempDb t;
  DiskManager dm;
  const string nested = (t.dir / "a" / "b" / "x.db").string();
  ASSERT_TRUE(dm.open(nested));
  EXPECT_TRUE(filesystem::exists(nested));
}

TEST(DiskManagerTest, AllocateGivesSequentialZeroFilledPages) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  vector<char> buf(kPageSizeBytes, 0x55);
  for (PageId expect = 0; expect < 4; ++expect) {
    EXPECT_EQ(dm.allocatePage(), expect);
    ASSERT_TRUE(dm.readPage(expect, buf.data()));
    for (size_t i = 0; i < kPageSizeBytes; ++i) ASSERT_EQ(buf[i], 0);
    fill(buf.begin(), buf.end(), 0x55);
  }
  EXPECT_EQ(filesystem::file_size(t.path), 4u * kPageSizeBytes);
}

TEST(DiskManagerTest, WriteCloseReopenCompareEveryByte) {
  TempDb t;
  vector<char> p0(kPageSizeBytes), p1(kPageSizeBytes), p2(kPageSizeBytes);
  fillPattern(p0.data(), 1);
  fillPattern(p1.data(), 2);
  fillPattern(p2.data(), 3);
  {
    DiskManager dm;
    ASSERT_TRUE(dm.open(t.path));
    ASSERT_EQ(dm.allocatePage(), 0u);
    ASSERT_EQ(dm.allocatePage(), 1u);
    ASSERT_EQ(dm.allocatePage(), 2u);
    ASSERT_TRUE(dm.writePage(0, p0.data()));
    ASSERT_TRUE(dm.writePage(1, p1.data()));
    ASSERT_TRUE(dm.writePage(2, p2.data()));
    ASSERT_TRUE(dm.sync());
    dm.close();
  }
  DiskManager dm2;
  ASSERT_TRUE(dm2.open(t.path));
  EXPECT_EQ(filesystem::file_size(t.path), 3u * kPageSizeBytes);
  vector<char> got(kPageSizeBytes);
  const vector<char>* want[3] = {&p0, &p1, &p2};
  for (PageId id = 0; id < 3; ++id) {
    ASSERT_TRUE(dm2.readPage(id, got.data()));
    for (size_t i = 0; i < kPageSizeBytes; ++i)
      ASSERT_EQ(got[i], (*want[id])[i]);
  }
}

TEST(DiskManagerTest, PagesAreIndependent) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  dm.allocatePage();
  dm.allocatePage();
  vector<char> a(kPageSizeBytes, 'A'), b(kPageSizeBytes), zero(kPageSizeBytes, 0);
  ASSERT_TRUE(dm.writePage(1, a.data()));
  ASSERT_TRUE(dm.readPage(0, b.data()));
  EXPECT_TRUE(memcmp(b.data(), zero.data(), kPageSizeBytes) == 0);
  ASSERT_TRUE(dm.readPage(1, b.data()));
  EXPECT_TRUE(memcmp(b.data(), a.data(), kPageSizeBytes) == 0);
}

TEST(DiskManagerTest, ReadOutOfRangeFails) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  vector<char> buf(kPageSizeBytes);
  EXPECT_FALSE(dm.readPage(0, buf.data()));
  dm.allocatePage();
  EXPECT_TRUE(dm.readPage(0, buf.data()));
  EXPECT_FALSE(dm.readPage(1, buf.data()));
  EXPECT_FALSE(dm.readPage(kInvalidPageId, buf.data()));
}

TEST(DiskManagerTest, WriteToUnallocatedPageFails) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  vector<char> buf(kPageSizeBytes, 1);
  EXPECT_FALSE(dm.writePage(0, buf.data()));
  EXPECT_EQ(filesystem::file_size(t.path), 0u);
}

TEST(DiskManagerTest, NullBufferRejected) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  dm.allocatePage();
  EXPECT_FALSE(dm.readPage(0, nullptr));
  EXPECT_FALSE(dm.writePage(0, nullptr));
}

TEST(DiskManagerTest, OperationsOnClosedFileFail) {
  TempDb t;
  DiskManager dm;
  vector<char> buf(kPageSizeBytes);
  EXPECT_FALSE(dm.readPage(0, buf.data()));
  EXPECT_FALSE(dm.writePage(0, buf.data()));
  EXPECT_EQ(dm.allocatePage(), kInvalidPageId);
  EXPECT_FALSE(dm.sync());
  ASSERT_TRUE(dm.open(t.path));
  dm.close();
  dm.close();
  EXPECT_EQ(dm.allocatePage(), kInvalidPageId);
}

TEST(DiskManagerTest, DoubleOpenFails) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  EXPECT_FALSE(dm.open(t.path));
  EXPECT_EQ(dm.allocatePage(), 0u);
}

TEST(DiskManagerTest, MisalignedFileRejected) {
  TempDb t;
  {
    ofstream f(t.path, ios::binary);
    string junk(kPageSizeBytes + 100, 'x');
    f.write(junk.data(), static_cast<streamsize>(junk.size()));
  }
  DiskManager dm;
  EXPECT_FALSE(dm.open(t.path));
  EXPECT_EQ(dm.allocatePage(), kInvalidPageId);
}

TEST(DiskManagerTest, ConcurrentAllocateGivesUniqueIds) {
  TempDb t;
  DiskManager dm;
  ASSERT_TRUE(dm.open(t.path));
  constexpr int kThreads = 8, kPerThread = 50;
  vector<vector<PageId>> ids(kThreads);
  vector<thread> ts;
  for (int i = 0; i < kThreads; ++i)
    ts.emplace_back([&, i] {
      for (int k = 0; k < kPerThread; ++k) ids[i].push_back(dm.allocatePage());
    });
  for (auto& th : ts) th.join();
  set<PageId> all;
  for (auto& v : ids)
    for (PageId id : v) {
      ASSERT_NE(id, kInvalidPageId);
      all.insert(id);
    }
  EXPECT_EQ(all.size(), static_cast<size_t>(kThreads * kPerThread));
  EXPECT_EQ(*all.rbegin(), static_cast<PageId>(kThreads * kPerThread - 1));
  EXPECT_EQ(filesystem::file_size(t.path),
            static_cast<uintmax_t>(kThreads * kPerThread) * kPageSizeBytes);
}
