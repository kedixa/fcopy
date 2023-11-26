#ifndef FCOPY_ERROR_CODE_H
#define FCOPY_ERROR_CODE_H

enum {
    FCOPY_OK = 0,                   // Everything is ok
    FCOPY_ERR_START = 1024,
    ERR_USER_NO_ACCESS = 1024,      // No such user or bad token
    ERR_ADDRESS_NO_ACCESS = 1025,      // Client ip no access
    ERR_NO_PARTITION = 1026,
    ERR_NO_FILE = 1027,
};

#endif // FCOPY_ERROR_CODE_H
