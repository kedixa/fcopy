#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <limits>
#include <map>
#include <cmath>

#include "structures.h"
#include "fcopy_log.h"

template<typename T>
using config_map_t = std::map<std::string, T *>;

static const char *
unescape(const char *str, const char *end, std::string &s) {
    // \n \r \t ' " backslash
    while (str != end && *str != '\"') {
        if (*str == '\\') {
            ++str;
            if (str == end)
                return end; // failed

            switch (*str) {
            case 'n': s.push_back('\n'); break;
            case 'r': s.push_back('\r'); break;
            case 't': s.push_back('\t'); break;
            case '\'': case '\"': case '\\':
                s.push_back(*str);
                break;
            default:
                return end;
            }
        }
        else
            s.push_back(*str);

        ++str;
    }

    return str;
}

static int
parse_line(const std::string &line, std::string &key, std::vector<std::string> &args) {
    std::vector<std::string> vec;
    std::string tmp;
    const char *str = line.data();
    const char *end = line.data() + line.size();

    while (str != end) {
        while (str != end && std::isspace(*str)) ++str;
        if (str == end || *str == '#') break;

        tmp.clear();
        if (*str == '"') {
            str = unescape(str + 1, end, tmp);
            if (str == end || *str != '"')
                return -1;
        }
        else {
            while (str != end && !std::isspace(*str) && *str != '#')
                tmp.push_back(*(str++));
        }

        vec.push_back(std::move(tmp));
    }

    if (vec.empty())
        return 0;

    key = vec[0];
    args.assign(vec.begin() + 1, vec.end());
    return 1;
}

template<typename T>
static int
parse_unsigned(T *p, const std::vector<std::string> &args) {
    if (p == nullptr || args.size() != 1)
        return -1;

    std::size_t pos = 0;
    unsigned long long u;
    try {
        u = std::stoull(args[0], &pos);
    }
    catch (const std::exception &) {
        return -1;
    }

    if (pos != args[0].size() || std::numeric_limits<T>::max() < u)
        return -1;

    *p = static_cast<T>(u);
    return 0;
}

template<typename T>
static int
parse_signed(T *p, const std::vector<std::string> &args) {
    if (p == nullptr || args.size() != 1)
        return -1;

    std::size_t pos = 0;
    long long u;
    try {
        u = std::stoll(args[0], &pos);
    }
    catch (const std::exception &) {
        return -1;
    }

    if (pos != args[0].size() || std::numeric_limits<T>::max() < u ||
        std::numeric_limits<T>::min() > u)
    {
        return -1;
    }

    *p = static_cast<T>(u);
    return 0;
}

static int
parse_size(std::size_t *p, const std::vector<std::string> &args) {
    if (p == nullptr || args.size() != 1)
        return -1;

    std::size_t pos = 0;
    long double u = 0;
    int m = 1;

    try {
        u = std::stold(args[0], &pos);
    }
    catch (const std::exception &) {
        return -1;
    }

    if (pos != args[0].size()) {
        switch (args[0][pos]) {
        case 'B': m = 1; break;
        case 'K': m = 10; break;
        case 'M': m = 20; break;
        case 'G': m = 30; break;
        case 'T': m = 40; break;
        default: return -1;
        }
    }

    u *= std::exp2l(m);
    u = std::ceil(u);
    if (!std::isfinite(u) || u < 0 || u > std::exp2l(50.0))
        return -1;

    *p = static_cast<std::size_t>(u);
    return 0;
}

static int
parse_string(std::string *p, const std::vector<std::string> &args) {
    if (p == nullptr || args.size() != 1)
        return -1;

    *p = args[0];
    return 0;
}

static int
parse_partition(std::map<std::string, FsPartition> *p, const std::vector<std::string> &args) {
    if (p == nullptr || args.size() != 2)
        return -1;

    p->insert({args[0], FsPartition{args[0], args[1]}});
    return 0;
}

int load_service_config(const std::string &filepath, FcopyConfig &p,
                        std::string &err) {
    std::ifstream ifs(filepath);
    std::string line;
    std::string key;
    std::vector<std::string> args;
    int lineno = 0;
    int ret;

    if (!ifs) {
        err.assign("Open ").append(filepath).append(" failed");
        return -1;
    }

    config_map_t<int> int_map;
    config_map_t<std::size_t> size_map;
    config_map_t<std::size_t> cap_map;
    config_map_t<std::string> str_map;

    config_map_t<int>::iterator int_it;
    config_map_t<std::size_t>::iterator size_it, cap_it;
    config_map_t<std::string>::iterator str_it;

    int_map.emplace("port", &p.port);
    int_map.emplace("srv_max_conn", &p.srv_max_conn);
    int_map.emplace("srv-peer-response-timeout", &p.srv_peer_response_timeout);
    int_map.emplace("srv-receive-timeout", &p.srv_receive_timeout);
    int_map.emplace("srv-keep-alive-timeout", &p.srv_keep_alive_timeout);
    int_map.emplace("cli-retry-max", &p.cli_retry_max);
    int_map.emplace("cli-send-timeout", &p.cli_send_timeout);
    int_map.emplace("cli-receive-timeout", &p.cli_receive_timeout);
    int_map.emplace("cli-keep-alive-timeout", &p.cli_keep_alive_timeout);

    cap_map.emplace("request-size-limit", &p.srv_size_limit);

    str_map.emplace("logfile", &p.logfile);
    str_map.emplace("pidfile", &p.pidfile);
    str_map.emplace("basedir", &p.basedir);
    str_map.emplace("default-partition", &p.default_partition);

    while (std::getline(ifs, line)) {
        ret = parse_line(line, key, args);
        lineno++;

        if (ret < 0) {
            err.assign("Parse config failed line:").append(std::to_string(lineno));
            return -1;
        }

        if (ret == 0)
            continue;

        if ((int_it = int_map.find(key)) != int_map.end())
            ret = parse_signed<int>(int_it->second, args);
        else if ((size_it = size_map.find(key)) != size_map.end())
            ret = parse_unsigned<std::size_t>(size_it->second, args);
        else if ((cap_it = cap_map.find(key)) != cap_map.end())
            ret = parse_size(cap_it->second, args);
        else if ((str_it = str_map.find(key)) != str_map.end())
            ret = parse_string(str_it->second, args);
        else if (key == "partitions")
            ret = parse_partition(&p.partitions, args);
        else
            ret = 0;

        if (ret < 0) {
            err.assign("Parse config failed line:").append(std::to_string(lineno))
                .append(" key:").append(key);
            return -1;
        }
    }

    return 0;
}
