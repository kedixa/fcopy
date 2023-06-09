#include "file_manager.h"

#include <filesystem>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace fs = std::filesystem;

class CloseGuard {
public:
    CloseGuard(int fd) : fd(fd) {}
    ~CloseGuard() { if(fd >= 0) close(fd); }

    void clear() { fd = -1; }

private:
    int fd;
};

std::string
FileManager::get_full_path(const std::string &name) {
    fs::path p = fs::current_path();
    p /= name;

    return p;
}

std::string
FileManager::get_token(const std::string &path) {
    std::ostringstream oss;
    std::hash<std::string> h;

    oss << std::hex << h(path);
    // TODO more
    return oss.str();
}

bool create_directories(const std::string &path) {
    fs::path p(path);
    std::error_code ec;

    if (p.has_filename())
        p.remove_filename();

    if (!fs::exists(p, ec))
        fs::create_directories(p, ec);

    return ec.value() == 0;
}

bool is_subpath(const std::string &base, const std::string &path) {
    fs::path base_path(base);
    fs::path another_path(path);
    fs::path relative_path = fs::relative(base_path, another_path);
    std::string relative = relative_path;

    return !relative.starts_with("../");
}

FileManager::FileManager() { }

FileManager::~FileManager() {
    std::error_code ec;
    for (auto it : fmap) {
        FileInfo &info = it.second;
        close(info.fd);
        munmap(info.meta_ptr, info.meta_size);
        fs::remove(info.meta_path, ec);
    }
}

static int create_fd(const char *path) {
    int fd = open(path, O_CREAT|O_RDWR, 0660);
    if (fd > 0) {
        if (ftruncate(fd, 0) == 0)
            return fd;

        close(fd);
    }

    return -1;
}

static unsigned char *mmap_and_clear(const char *path, int size) {
    int fd = open(path, O_CREAT|O_RDWR, 0600);
    void *addr;

    if (fd > 0) {
        if (ftruncate(fd, size) == 0) {
            addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
            close(fd);

            if (addr != MAP_FAILED) {
                memset(addr, 0, size);
                return (unsigned char *)addr;
            }
        }

        close(fd);
    }

    return NULL;
}

constexpr std::size_t PAGE_SIZE = 8 * 1024;
int FileManager::create_file(const std::string &name, std::size_t size,
                             std::size_t chunk_size,
                             std::string &file_token)
{
    std::string path = get_full_path(name);
    std::string token = get_token(path);
    std::string meta_path = get_full_path(token + ".meta");
    unsigned char *meta_ptr = nullptr;
    std::size_t meta_size;
    std::size_t meta_bits;
    int fd = -1;

    auto return_error = [&] (int ret, int error, const char *type) mutable {
        file_token.assign("Error: ").append(type)
            .append(" errno:").append(std::to_string(error));

        if (fd >= 0)
            close(fd);

        if (meta_ptr)
            munmap(meta_ptr, meta_size);

        return ret;
    };

    if (chunk_size == 0 || chunk_size % PAGE_SIZE != 0)
        return return_error(-EINVAL, EINVAL, "chunk_size");

    if (!create_directories(path))
        return return_error(-ENOTDIR, ENOTDIR, "create_directory");

    fd = create_fd(path.c_str());

    if (fd < 0)
        return return_error(-errno, errno, "create_file");

    meta_bits = (size + chunk_size - 1) / chunk_size;
    meta_size = (meta_bits + 7) / 8;
    meta_size += PAGE_SIZE - (meta_size % PAGE_SIZE);
    meta_ptr = mmap_and_clear(meta_path.c_str(), meta_size);

    if (!meta_ptr)
        return return_error(-errno, errno, "mmap_meta");

    FileInfo info;
    info.fd = fd;
    info.meta_ptr = meta_ptr;
    info.meta_size = meta_size;
    info.meta_bits = meta_bits;
    info.total_size = size;
    info.file_name = name;
    info.file_path = path;
    info.file_token = token;
    info.meta_path = meta_path;

    std::lock_guard<std::mutex> lg(this->mtx);
    auto it = fmap.find(token);
    if (it != fmap.end())
        return return_error(-EEXIST, EEXIST, "duplicate_token");

    file_token = token;
    fmap.emplace(token, info);

    return 0;
}

int FileManager::close_file(const std::string &file_token) {
    FileInfo info;
    std::error_code ec;
    {
        std::lock_guard<std::mutex> lg(this->mtx);
        auto it = fmap.find(file_token);
        if (it == fmap.end())
            return -ENOENT;

        info = it->second;
        fmap.erase(it);
    }

    close(info.fd);
    munmap(info.meta_ptr, info.meta_size);
    fs::remove(info.meta_path, ec);
    return 0;
}

int FileManager::set_chain_targets(const std::string &file_token, const std::vector<ChainTarget> &targets) {
    std::lock_guard<std::mutex> lg(this->mtx);
    auto it = fmap.find(file_token);
    if (it == fmap.end())
        return -1;

    it->second.targets = targets;
    return 0;
}

bool FileManager::has_file(const std::string &file_token) const {
    std::lock_guard<std::mutex> lg(this->mtx);
    return fmap.contains(file_token);
}

int FileManager::get_fd(const std::string &file_token, std::vector<ChainTarget> &targets) {
    std::lock_guard<std::mutex> lg(this->mtx);
    auto it = fmap.find(file_token);
    if (it == fmap.end())
        return -1;

    targets = it->second.targets;
    return it->second.fd;
}

int FileManager::set_range(const std::string &file_token,
                           long offset, long length)
{
    // TODO
    return 0;
}
