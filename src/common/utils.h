#ifndef FCOPY_UTILS_H
#define FCOPY_UTILS_H

#include <string>
#include <cstdint>
#include <chrono>

int64_t current_usec();

std::string format_bps(std::size_t size, int64_t usec);

#endif // FCOPY_UTILS_H
