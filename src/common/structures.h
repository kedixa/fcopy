#ifndef FCOPY_STRUCTURES_H
#define FCOPY_STRUCTURES_H

#include <string>
#include <cstdint>

struct ChainTarget {
    std::string host;
    uint16_t port;
    std::string file_token;
};

#endif // FCOPY_STRUCTURES_H
