#include <float.h>

#include "debug.h"

struct timestamp {
    std::chrono::steady_clock::time_point t;
};

struct measurement {
        const char *name;

        double total_time = 0.0;
        double min_ms = DBL_MAX;
        double max_ms = 0.0;

        uint64_t skip = 60;
        uint64_t count = 0;

        void update(std::chrono::steady_clock::time_point start_time, std::chrono::steady_clock::time_point end_time);
        void print();
};

inline void measurement::update(std::chrono::steady_clock::time_point start_time, std::chrono::steady_clock::time_point end_time) {
        if (count < skip) {
                count++;
                return;
        }
        
        auto diff = end_time - start_time;
        double duration = std::chrono::duration<double, std::milli>(diff).count();

        count++;
        total_time += duration;
        if (duration < min_ms) min_ms = duration;
        if (duration > max_ms) max_ms = duration;
}

inline void measurement::print() {
        if (count < skip) {
                log_msg(LOG_LEVEL_INFO, "%s: not enough samples\n", name);
                return;
        }
        
        double avg = total_time / (count - skip);

        log_msg(LOG_LEVEL_INFO, "%s: avg=%.3f ms  min=%.3f ms  max=%.3f ms\n", name, avg, min_ms, max_ms);
}