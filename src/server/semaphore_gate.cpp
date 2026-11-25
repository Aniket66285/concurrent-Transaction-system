#include "minidb/server/semaphore_gate.hpp"

#include <cerrno>
#include <system_error>

namespace minidb {

SemaphoreGate::SemaphoreGate(unsigned int initial_count) {
    if (sem_init(&semaphore_, 0, initial_count) != 0) {
        throw std::system_error(errno, std::generic_category(), "sem_init failed");
    }
}

SemaphoreGate::~SemaphoreGate() { sem_destroy(&semaphore_); }

void SemaphoreGate::Wait() {
    while (sem_wait(&semaphore_) != 0) {
        if (errno == EINTR) continue;
        throw std::system_error(errno, std::generic_category(), "sem_wait failed");
    }
}

void SemaphoreGate::Post() {
    if (sem_post(&semaphore_) != 0) {
        throw std::system_error(errno, std::generic_category(), "sem_post failed");
    }
}

} // namespace minidb
