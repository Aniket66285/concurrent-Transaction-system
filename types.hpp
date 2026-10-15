#pragma once
#include <cstddef>
#include <cstdint>
namespace minidb {
using namespace std;
using PageId = uint64_t;
using TxnId = uint64_t;
using Lsn = uint64_t;
using SlotId = uint16_t;
constexpr size_t kPageSizeBytes = 4096;
struct RecordId { PageId pageId; SlotId slotId; };
}
