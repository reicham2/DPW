cmake_minimum_required(VERSION 3.20)
project(activities_backend CXX C)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

include(FetchContent)

FetchContent_Declare(uwebsockets
    GIT_REPOSITORY https://github.com/uNetworking/uWebSockets.git
    GIT_TAG        v20.62.0
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(usockets
    GIT_REPOSITORY https://github.com/uNetworking/uSockets.git
    GIT_TAG        v0.8.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(uwebsockets usockets)

# Find libpq
find_library(PQ_LIB pq REQUIRED)
find_path(PQ_INCLUDE libpq-fe.h PATH_SUFFIXES postgresql)

# Find libuv
find_library(UV_LIB uv REQUIRED)
find_path(UV_INCLUDE uv.h)

add_executable(server
    src/main.cpp
    src/db.cpp
    src/handlers.cpp
    src/ws_manager.cpp
    # uSockets C/C++ sources
    ${usockets_SOURCE_DIR}/src/eventing/libuv.c
    ${usockets_SOURCE_DIR}/src/crypto/openssl.c
    ${usockets_SOURCE_DIR}/src/crypto/sni_tree.cpp
    ${usockets_SOURCE_DIR}/src/bsd.c
    ${usockets_SOURCE_DIR}/src/context.c
    ${usockets_SOURCE_DIR}/src/loop.c
    ${usockets_SOURCE_DIR}/src/socket.c
    ${usockets_SOURCE_DIR}/src/udp.c
    ${usockets_SOURCE_DIR}/src/quic.c
)

target_include_directories(server PRIVATE
    src/
    deps/
    ${uwebsockets_SOURCE_DIR}/src
    ${usockets_SOURCE_DIR}/src
    ${PQ_INCLUDE}
    ${UV_INCLUDE}
)

target_compile_definitions(server PRIVATE
    LIBUS_USE_LIBUV=1
    LIBUS_USE_OPENSSL=1
    UWS_HTTPRESPONSE_NO_WRITEMARK=0
)

target_link_libraries(server PRIVATE
    ${PQ_LIB}
    ${UV_LIB}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
)

target_compile_options(server PRIVATE -O2 -Wall -Wextra)
