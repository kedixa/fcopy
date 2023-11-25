#ifndef FCOPY_FILE_MANAGER_H
#define FCOPY_FILE_MANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <vector>
#include <map>

#include "common/structures.h"

struct FileInfo {
    int fd;
    std::size_t chunk_size;
    std::size_t total_size; // total file size(bytes)
    std::string file_name;
    std::string file_path;
    std::string file_token;

    std::vector<ChainTarget> targets;
};

class FileManager {
private:
    static std::string get_full_path(const std::string &name);
    static std::string get_token(const std::string &path);

public:
    FileManager();
    FileManager(const FileManager &) = delete;
    ~FileManager();

    int create_file(const std::string &name, std::size_t size,
                    std::size_t chunk_size, bool directio, std::string &file_token);
    int close_file(const std::string &file_token);
    int set_chain_targets(const std::string &file_token, const std::vector<ChainTarget> &targets);
    bool has_file(const std::string &file_token) const;

    int get_fd(const std::string &file_token, std::vector<ChainTarget> &targets);
    int set_range(const std::string &file_token, long offset, long length);

private:
    std::map<std::string, FileInfo> fmap;
    std::map<std::string, std::string> token_map;  // filepath -> token
    mutable std::mutex mtx;
};

#endif // FCOPY_FILE_MANAGER_H
