#include "utils.h"
#include "fcopy_log.h"

#include <sstream>
#include <iomanip>
#include <set>
#include <ctime>
#include <filesystem>
#include <sys/time.h>
#include <unistd.h>

namespace fs = std::filesystem;

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

std::string default_basedir() {
    const char *home = getenv("HOME");
    std::string basedir;

    if (home) {
        fs::path path(home);
        path /= ".fcopy";
        basedir = path.string();
    }

    return basedir;
}

bool is_regular_file(const std::string &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

struct LoadState {
    int max_depth;
    int max_files;
    std::set<std::string> unique_path;
    std::vector<FileDesc> &files;
};

static void load_file(const fs::directory_entry &entry, LoadState &state) {
    const fs::path &path = entry.path();
    const char *fn = path.c_str();

    if (access(fn, R_OK) != 0)
        throw fs::filesystem_error("No read permission", path, std::make_error_code(std::errc::permission_denied));

    FileDesc desc {
        .name = path.filename(),
        .dir = path.parent_path(),
        .path = path.string(),
        .fullpath = fs::canonical(path).string(),
        .size = entry.file_size()
    };

    if (!state.unique_path.insert(desc.fullpath).second)
        throw fs::filesystem_error("Duplicate files", path, std::error_code());

    FLOG_INFO("FindFile size:%zu path:%s realpath:%s",
        desc.size, path.c_str(), desc.fullpath.c_str());

    state.files.push_back(std::move(desc));
}

static void load_dir(const fs::directory_entry &dir, LoadState &state, int depth) {
    const fs::path &path = dir.path();
    std::error_code ec;

    if (depth > state.max_depth)
        throw fs::filesystem_error("Traversing folders encountered maximum depth", path, ec);

    for (auto entry : fs::directory_iterator(path)) {
        if (entry.is_directory())
            load_dir(entry, state, depth + 1);
        else if (entry.is_regular_file())
            load_file(entry, state);
        else
            throw fs::filesystem_error("Unsupported file type", entry.path(), ec);
    }
}

void load_files(const std::vector<std::string> &paths, std::vector<FileDesc> &files) {
    LoadState state {
        .max_depth = 16,
        .max_files = 65536,
        .unique_path = {},
        .files = files
    };

    for (const std::string &path : paths) {
        fs::directory_entry e{fs::path(path)};
        if (e.is_directory())
            load_dir(e, state, 1);
        else if (e.is_regular_file())
            load_file(e, state);
        else
            throw fs::filesystem_error("Unsupported file type", fs::path(path), std::error_code());
    }
}
