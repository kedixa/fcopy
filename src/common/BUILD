package(default_visibility = ["//visibility:public"])

cc_library(
    name = "common",
    srcs = [
        "co_fcopy.cpp",
        "localaddr.cpp",
        "message.cpp",
        "utils.cpp",
    ],
    hdrs = [
        "co_fcopy.h",
        "error_code.h",
        "fcopy_log.h",
        "message.h",
        "structures.h",
        "utils.h",
    ],
    includes = [".."],
    deps = [
        "@coke//:common",
        "@coke//:net",
    ],
)
