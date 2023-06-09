#ifndef FCOPY_COMMON_H
#define FCOPY_COMMON_H

#include <string>
#include <cstdint>
#include <chrono>

struct ChainTarget {
    std::string host;
    uint16_t port;
    std::string file_token;
};

int64_t current_usec();

std::string format_bps(std::size_t size, int64_t usec);

#endif // FCOPY_COMMON_H
