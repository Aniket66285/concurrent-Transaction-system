
#pragma once

#include <stdexcept>
#include <string>

namespace minidb {

class MiniDBException : public std::runtime_error {
public:
    explicit MiniDBException(const std::string& message)
        : std::runtime_error(message) {
    }
};

class PageNotFoundException : public MiniDBException {
public:
    explicit PageNotFoundException(const std::string& message)
        : MiniDBException(message) {
    }
};

class TransactionException : public MiniDBException {
public:
    explicit TransactionException(const std::string& message)
        : MiniDBException(message) {
    }
};

class StorageException : public MiniDBException {
public:
    explicit StorageException(const std::string& message)
        : MiniDBException(message) {
    }
};

} // namespace minidb
