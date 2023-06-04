#ifndef FRIDIS_FILE_MANAGER_H
#define FRIDIS_FILE_MANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <vector>
#include <map>

#include "common.h"

struct FileInfo {
    int fd;
    unsigned char *meta_ptr;
    std::size_t chunk_size;
    std::size_t meta_size; // in bytes
    std::size_t meta_bits; // max valid bits
    std::size_t total_size; // total file size(bytes)
    std::string file_name;
    std::string file_path;
    std::string file_token;
    std::string meta_path; // path for meta info file

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
                    std::size_t chunk_size, std::string &file_token);
    int close_file(const std::string &file_token);
    int set_chain_targets(const std::string &file_token, const std::vector<ChainTarget> &targets);

    int get_fd(const std::string &file_token, std::vector<ChainTarget> &targets);
    int set_range(const std::string &file_token, long offset, long length);

private:
    std::map<std::string, FileInfo> fmap;
    std::map<std::string, std::string> token_map;  // filepath -> token
    std::mutex mtx;
};

#endif // FRIDIS_FILE_MANAGER_H
