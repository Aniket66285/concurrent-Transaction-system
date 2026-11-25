#pragma once

#include <semaphore.h>

namespace minidb {

class SemaphoreGate {
public:
    explicit SemaphoreGate(unsigned int initial_count);
    ~SemaphoreGate();

    SemaphoreGate(const SemaphoreGate&) = delete;
    SemaphoreGate& operator=(const SemaphoreGate&) = delete;

    void Wait();
    void Post();

private:
    sem_t semaphore_{};
};

} // namespace minidb
