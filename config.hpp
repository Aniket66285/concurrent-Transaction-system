#pragma once
#include <limits>
#include "minidb/common/types.hpp"
namespace minidb {
using namespace std;
constexpr const char* kDefaultDbPath = "data/minidb.db";
constexpr const char* kDefaultWalPath = "data/minidb.wal";
constexpr const char* kDefaultCkptPath = "data/minidb.ckpt";
constexpr PageId kInvalidPageId = numeric_limits<PageId>::max();
}
