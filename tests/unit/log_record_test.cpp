
#include "minidb/recovery/log_record.hpp"

#include <cassert>
#include <iostream>

using namespace minidb;

int main() {

    // Test 1: INSERT
    LogRecord insertRecord;

    insertRecord.txn_id = 1;
    insertRecord.type = LogRecordType::INSERT;
    insertRecord.has_page = true;
    insertRecord.page_id = 2;
    insertRecord.slot_id = 5;
    insertRecord.after_image = {0xE8, 0x03};

    assert(IsWellFormedLogRecord(insertRecord));

    assert(IsUndoableLogRecord(insertRecord.type));

    assert(IsRedoableLogRecord(insertRecord.type));

    std::cout << "INSERT test: PASS\n";


    // Test 2: UPDATE
    LogRecord updateRecord;

    updateRecord.txn_id = 2;
    updateRecord.type = LogRecordType::UPDATE;
    updateRecord.has_page = true;
    updateRecord.page_id = 2;
    updateRecord.slot_id = 5;

    updateRecord.before_image = {0xE8, 0x03};
    updateRecord.after_image = {0xDC, 0x05};

    assert(IsWellFormedLogRecord(updateRecord));

    std::cout << "UPDATE test: PASS\n";


    // Test 3: DELETE
    LogRecord deleteRecord;

    deleteRecord.txn_id = 3;
    deleteRecord.type = LogRecordType::DELETE;
    deleteRecord.has_page = true;
    deleteRecord.page_id = 2;
    deleteRecord.slot_id = 5;

    deleteRecord.before_image = {0xDC, 0x05};

    assert(IsWellFormedLogRecord(deleteRecord));

    std::cout << "DELETE test: PASS\n";


    // Test 4: Invalid UPDATE
    LogRecord invalidRecord;

    invalidRecord.txn_id = 4;
    invalidRecord.type = LogRecordType::UPDATE;
    invalidRecord.has_page = true;
    invalidRecord.page_id = 2;
    invalidRecord.slot_id = 5;

    // Both images are empty, so this record is invalid.
    assert(!IsWellFormedLogRecord(invalidRecord));

    std::cout << "INVALID UPDATE test: PASS\n";


    std::cout << "\nAll LogRecord tests passed!\n";

    return 0;
}
