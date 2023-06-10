#ifndef FCOPY_LOG_H
#define FCOPY_LOG_H

#include <cstdio>

constexpr inline int FCOPY_LOG_LEVEL_ERROR  = 5;
constexpr inline int FCOPY_LOG_LEVEL_WARN   = 4;
constexpr inline int FCOPY_LOG_LEVEL_INFO   = 3;
constexpr inline int FCOPY_LOG_LEVEL_DEBUG  = 2;
constexpr inline int FCOPY_LOG_LEVEL_TRACE  = 1;
constexpr inline int FCOPY_TIME_BUF_SIZE    = 32;

inline int fcopy_log_level = 0;
inline FILE *fcopy_log_file = nullptr;

inline void fcopy_set_log_level(int level) {
    fcopy_log_level = level;
}

inline int fcopy_get_log_level() {
    return fcopy_log_level;
}

inline void fcopy_close_log_file() {
    if (fcopy_log_file)
        std::fclose(fcopy_log_file);
    fcopy_log_file = nullptr;
}

inline int fcopy_open_log_file(const char *filename) {
    fcopy_log_file = std::fopen(filename, "a");
    if (fcopy_log_file == nullptr)
        return -1;
    return 0;
}

inline void fcopy_set_log_stream(FILE *log_file) {
    fcopy_log_file = log_file;
}

void fcopy_get_time_str(char time_buf[FCOPY_TIME_BUF_SIZE]);

#define FLOG(level, fmt, ...) do { \
    if (fcopy_log_file && FCOPY_LOG_LEVEL_##level >= fcopy_get_log_level()) { \
        char time_buf[FCOPY_TIME_BUF_SIZE]; \
        fcopy_get_time_str(time_buf); \
        std::fprintf(fcopy_log_file, "[%s] [%s] " fmt "\n", time_buf, #level, ##__VA_ARGS__); \
    } \
} while (0)

#define FLOG_ERROR(fmt, ...) FLOG(ERROR, fmt, ##__VA_ARGS__)
#define FLOG_WARN(fmt, ...) FLOG(WARN, fmt, ##__VA_ARGS__)
#define FLOG_INFO(fmt, ...) FLOG(INFO, fmt, ##__VA_ARGS__)
#define FLOG_DEBUG(fmt, ...) FLOG(DEBUG, fmt, ##__VA_ARGS__)
#define FLOG_TRACE(fmt, ...) FLOG(TRACE, fmt, ##__VA_ARGS__)

#endif // FCOPY_LOG_H
