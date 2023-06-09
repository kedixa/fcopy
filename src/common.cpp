#include "common.h"

#include <sstream>
#include <iomanip>

int64_t current_usec() {
    auto dur = std::chrono::steady_clock::now().time_since_epoch();
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(dur);
    return usec.count();
}

std::string format_bps(std::size_t size, int64_t usec) {
    constexpr int steps = 4;
    static std::string suffix[4] = {"B", "KB", "MB", "GB"};
    std::string result;
    double d = static_cast<double>(size);
    int i = 0;

    d = d * 1000000 / usec;
    for (i = 0; i + 1 < steps; i++) {
        if (d < 512.0)
            break;

        d /= 1024;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4) << d << suffix[i] << "/s";
    return oss.str();
}
