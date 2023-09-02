cmake_minimum_required(VERSION 3.6)

set(ALL_TARGETS
    fcopy-server
    fcopy-cli
)

add_executable(fcopy-server
    common/fcopy_message.cpp
    common/co_fcopy.cpp
    common/utils.cpp
    server/load_config.cpp
    server/file_manager.cpp
    server/service.cpp
    server/fcopy_server.cpp
)

add_executable(fcopy-cli
    common/fcopy_message.cpp
    common/co_fcopy.cpp
    common/utils.cpp
    common/localaddr.cpp
    client/fcopy_handler.cpp
    client/fcopy_cli.cpp
)

install(TARGETS ${ALL_TARGETS}
    DESTINATION bin
)

foreach(target ${ALL_TARGETS})
    target_include_directories(${target} PRIVATE
        common
        client
        server
    )

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
