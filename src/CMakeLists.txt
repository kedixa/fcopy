cmake_minimum_required(VERSION 3.6)

set(ALL_TARGETS
    fcopy-server
    fcopy-cli
)

add_executable(fcopy-server
    fcopy_server.cpp
    fcopy_message.cpp
    file_manager.cpp
    co_fcopy.cpp
)

add_executable(fcopy-cli
    fcopy_cli.cpp
    fcopy_message.cpp
    co_fcopy.cpp
    fcopy_handler.cpp
)

foreach(target ${ALL_TARGETS})
    target_link_directories(${target} PRIVATE
        ${WORKFLOW_LIB_DIR}
        ${COKE_LIBRARY_DIR}
    )

    target_link_libraries(${target}
        libcoke.a
        libworkflow.a
        Threads::Threads
        OpenSSL::SSL OpenSSL::Crypto
    )
endforeach()
