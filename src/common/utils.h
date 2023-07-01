#ifndef FCOPY_UTILS_H
#define FCOPY_UTILS_H

#include <string>
#include <cstdint>
#include <chrono>
#include <vector>

int64_t current_usec();

std::string format_bps(std::size_t size, int64_t usec);

// fs utils

struct FileDesc {
    std::string name;
    std::string dir;
    std::string path;
    std::string fullpath;
    std::size_t size;
};

std::string default_basedir();
bool is_regular_file(const std::string &path);

void load_files(const std::vector<std::string> &paths, std::vector<FileDesc> &files);

#endif // FCOPY_UTILS_H
