cmake_minimum_required(VERSION 3.20)
project(activities_backend CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(ENABLE_DEBUG_AUTH "Enable debug authentication and impersonation flows" OFF)

if (ENABLE_DEBUG_AUTH AND CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "ENABLE_DEBUG_AUTH is not allowed with Release builds")
endif()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)

find_package(CURL REQUIRED)

# Local cached deps
set(UWEBSOCKETS_DIR ${CMAKE_SOURCE_DIR}/deps/uWebSockets)
set(USOCKETS_DIR    ${CMAKE_SOURCE_DIR}/deps/uSockets)

# Find libpq
find_library(PQ_LIB pq REQUIRED)
find_path(PQ_INCLUDE libpq-fe.h PATH_SUFFIXES postgresql)

# Find libuv
find_library(UV_LIB uv REQUIRED)
find_path(UV_INCLUDE uv.h)

# uSockets sources (third-party) — compiled separately to suppress warnings
set(USOCKETS_SOURCES
    ${USOCKETS_DIR}/src/eventing/libuv.c
    ${USOCKETS_DIR}/src/crypto/openssl.c
    ${USOCKETS_DIR}/src/crypto/sni_tree.cpp
    ${USOCKETS_DIR}/src/bsd.c
    ${USOCKETS_DIR}/src/context.c
    ${USOCKETS_DIR}/src/loop.c
    ${USOCKETS_DIR}/src/socket.c
    ${USOCKETS_DIR}/src/udp.c
    ${USOCKETS_DIR}/src/quic.c
)
set_source_files_properties(${USOCKETS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(server
    src/main.cpp
    src/cache.cpp
    src/db.cpp
    src/handlers.cpp
    src/auth.cpp
    src/ws_manager.cpp
    src/graph.cpp
    src/utils.cpp
    src/wp_client.cpp
    ${USOCKETS_SOURCES}
)

target_include_directories(server PRIVATE
    src/
    deps/
    ${UWEBSOCKETS_DIR}/src
    ${USOCKETS_DIR}/src
    ${PQ_INCLUDE}
    ${UV_INCLUDE}
)

target_compile_definitions(server PRIVATE
    LIBUS_USE_LIBUV=1
    LIBUS_USE_OPENSSL=1
    UWS_HTTPRESPONSE_NO_WRITEMARK=0
)

if (ENABLE_DEBUG_AUTH)
    target_compile_definitions(server PRIVATE DPW_ENABLE_DEBUG_AUTH=1)
else()
    target_compile_definitions(server PRIVATE DPW_ENABLE_DEBUG_AUTH=0)
endif()

target_link_libraries(server PRIVATE
    ${PQ_LIB}
    ${UV_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    CURL::libcurl
)

target_compile_options(server PRIVATE -O2 -Wall -Wextra -Wno-nonnull)

# Precompile heavy headers (json.hpp ~25k lines, uWS App.h)
target_precompile_headers(server PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/deps/json.hpp>"
    "$<$<COMPILE_LANGUAGE:CXX>:${UWEBSOCKETS_DIR}/src/App.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:<string$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<vector$<ANGLE-R>>"
    "$<$<COMPILE_LANGUAGE:CXX>:<optional$<ANGLE-R>>"
)

# ── Unit Tests ───────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build unit tests" OFF)

if (BUILD_TESTS)
    enable_testing()

    add_executable(run_tests
        tests/test_main.cpp
        src/utils.cpp
        src/auth.cpp
    )

    target_include_directories(run_tests PRIVATE
        src/
        deps/
        ${PQ_INCLUDE}
    )

    target_link_libraries(run_tests PRIVATE
        OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
    )

    target_compile_options(run_tests PRIVATE -O2 -Wall -Wextra)

    add_test(NAME unit_tests COMMAND run_tests)
endif()
