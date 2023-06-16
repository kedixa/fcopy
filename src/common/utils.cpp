#include "utils.h"
#include "fcopy_log.h"

#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/time.h>

void fcopy_get_time_str(char time_buf[FCOPY_TIME_BUF_SIZE]) {
    struct timespec ts;
    struct tm tm;

    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);

    snprintf(time_buf, FCOPY_TIME_BUF_SIZE,
        "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
        tm.tm_year + 1900, tm.tm_mon+1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000
    );
}

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
