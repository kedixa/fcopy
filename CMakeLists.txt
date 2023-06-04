cmake_minimum_required(VERSION 3.6)

project(
    fcopy
    VERSION 0.1.0
    LANGUAGES CXX
)

set(COKE_LIBRARY_DIR ${PROJECT_BINARY_DIR}/lib)
set(COKE_INCLUDE_DIR ${PROJECT_BINARY_DIR}/include)

set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "cmake build type")
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-parameter")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${COKE_LIBRARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${COKE_LIBRARY_DIR})

find_package(Workflow REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Coke REQUIRED)
include_directories(${WORKFLOW_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${COKE_INCLUDE_DIR})

add_subdirectory(src)
