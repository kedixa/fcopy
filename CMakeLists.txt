cmake_minimum_required(VERSION 3.6)

project(
    fcopy
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "cmake build type")
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-format-truncation -Wno-unused-parameter")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

find_package(Workflow REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Coke REQUIRED)
include_directories(${WORKFLOW_INCLUDE_DIR})
include_directories(${OPENSSL_INCLUDE_DIR})
include_directories(${COKE_INCLUDE_DIR})

add_subdirectory(src)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "fcopy")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "fcopy: copy file to remote server")
set(CPACK_PACKAGE_VENDOR "kedixa")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
# set(CPACK_RPM_PACKAGE_GROUP "")
set(CPACK_RPM_PACKAGE_URL "https://github.com/kedixa/fcopy/")
# set(CPACK_RPM_PACKAGE_DESCRIPTION "")
set(CPACK_RPM_PACKAGE_LICENSE "TODO")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
set(CPACK_RPM_PACKAGE_RELEASE "0")

include(CPack)
