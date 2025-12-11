#include <float.h>
#include <vector>
#include <cmath>

#include "debug.h"

struct timestamp {
    std::chrono::steady_clock::time_point t;
};

struct measurement {
        const char *name;

        std::vector<double> values{};

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
        values.push_back(duration);
        if (duration < min_ms) min_ms = duration;
        if (duration > max_ms) max_ms = duration;
}

inline void measurement::print() {
        if (count < skip) {
                log_msg(LOG_LEVEL_INFO, "%s: not enough samples\n", name);
                return;
        }

        const size_t n = values.size();

        double sum = 0.0;
        for (double v : values) sum += v;
        double avg = sum / n;

        double variance_sum = 0.0;
        for (double v : values) {
                double diff = v - avg;
                variance_sum += diff * diff;
        }

        double stddev = (n > 1) ? std::sqrt(variance_sum / (n - 1)) : 0.0;

        log_msg(LOG_LEVEL_INFO, "%s: avg=%.3f ms  min=%.3f ms  max=%.3f ms  stddev=%.3f ms\n", name, avg, min_ms, max_ms, stddev);
}