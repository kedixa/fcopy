#ifndef FCOPY_COMMON_H
#define FCOPY_COMMON_H

#include <string>

struct ChainTarget {
    std::string host;
    uint16_t port;
    std::string file_token;
};

#endif // FCOPY_COMMON_H
