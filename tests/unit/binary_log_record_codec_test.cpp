
#include "minidb/recovery/binary_log_record_codec.hpp"
#include "minidb/common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace minidb;

int main() {

    // Test 1: UPDATE record round trip.
    LogRecord original;

    original.lsn = 100;
    original.txn_id = 7;
    original.prev_lsn = 50;
    original.type = LogRecordType::UPDATE;
    original.has_page = true;
    original.page_id = 3;
    original.slot_id = 2;

    original.before_image = {0xE8, 0x03};
    original.after_image = {0xDC, 0x05};

    // Convert object into bytes.
    std::vector<std::uint8_t> encoded =
        EncodeLogRecord(original);

    // Convert bytes back into an object.
    LogRecord decoded = DecodeLogRecord(encoded);

    assert(decoded.txn_id == original.txn_id);
    assert(decoded.prev_lsn == original.prev_lsn);
    assert(decoded.type == original.type);
    assert(decoded.has_page == original.has_page);
    assert(decoded.page_id == original.page_id);
    assert(decoded.slot_id == original.slot_id);

    assert(decoded.before_image == original.before_image);
    assert(decoded.after_image == original.after_image);

    // LSN is assigned by LogManager, not the codec.
    assert(decoded.lsn == kInvalidLSN);

    std::cout << "UPDATE round-trip test: PASS\n";


    // Test 2: INSERT record round trip.
    LogRecord insertRecord;

    insertRecord.txn_id = 8;
    insertRecord.type = LogRecordType::INSERT;
    insertRecord.has_page = true;
    insertRecord.page_id = 4;
    insertRecord.slot_id = 1;
    insertRecord.after_image = {0x41, 0x42, 0x43};

    auto insertBytes = EncodeLogRecord(insertRecord);

    LogRecord decodedInsert =
        DecodeLogRecord(insertBytes);

    assert(decodedInsert.type == LogRecordType::INSERT);
    assert(decodedInsert.after_image ==
           insertRecord.after_image);
    assert(decodedInsert.before_image.empty());

    std::cout << "INSERT round-trip test: PASS\n";


    // Test 3: Corrupted magic signature must be rejected.
    auto corruptedBytes = encoded;

    corruptedBytes[0] = 0x00;

    bool corruptionRejected = false;

    try {
        DecodeLogRecord(corruptedBytes);
    }
    catch (const StorageException&) {
        corruptionRejected = true;
    }

    assert(corruptionRejected);

    std::cout << "Corrupted record test: PASS\n";


    // Test 4: Truncated record must be rejected.
    auto truncatedBytes = encoded;

    truncatedBytes.pop_back();

    bool truncationRejected = false;

    try {
        DecodeLogRecord(truncatedBytes);
    }
    catch (const StorageException&) {
        truncationRejected = true;
    }

    assert(truncationRejected);

    std::cout << "Truncated record test: PASS\n";


    std::cout << "\nAll BinaryLogRecordCodec tests passed!\n";

    return 0;
}
