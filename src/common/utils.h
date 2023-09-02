#ifndef FCOPY_UTILS_H
#define FCOPY_UTILS_H

#include <string>
#include <cstdint>
#include <chrono>
#include <vector>

struct FileDesc {
    std::string name;
    std::string dir;
    std::string path;
    std::string fullpath;
    std::size_t size;
};

int64_t current_usec();

std::string format_bps(std::size_t size, int64_t usec);

bool get_local_addr(std::vector<std::string> &addrs);

// fs utils

// no exception
std::string default_basedir();
std::string current_dir();
bool is_regular_file(const std::string &path);
int get_abs_path(const std::string &base, const std::string &relative, std::string &abs_path);
int get_abs_path(const std::string &base, const std::string &relative,
                 const std::string &filename, std::string &abs_path);
int create_dirs(const std::string &path, bool remove_filename);

// may throw exception
void load_files(const std::vector<std::string> &paths, std::vector<FileDesc> &files);

#endif // FCOPY_UTILS_H
