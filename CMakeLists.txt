# CMake build for mktorrent
#
# Mirrors the behaviour of the xmake.lua build:
#
#   cmake -B build                                    default build
#   cmake -B build -DUSE_PTHREADS=OFF                 disable pthreads hashing (default ON)
#   cmake -B build -DUSE_OPENSSL=OFF                  use built-in SHA1 instead of OpenSSL (default ON)
#   cmake -B build -DUSE_LONG_OPTIONS=OFF             disable --announce style options (default ON)
#   cmake -B build -DLARGE_FILES=ON                   32-bit support for files > 2GB
#   cmake -B build -DNO_HASH_CHECK=ON                 disable the redundant byte-count check
#   cmake -B build -DMAX_OPENFD=256                   directory walker fd limit (default 100)
#   cmake -B build -DDEBUG=ON                         enable leftover debugging code
#   cmake -B build -DALLINONE=ON                      single translation unit build
#   cmake -B build -DBUILD_TESTS=ON                   build the Unity unit tests
#
# Options are re-applied by re-running cmake -B build -D... before building.

cmake_minimum_required(VERSION 3.20)

project(mktorrent
    VERSION 2.0.0
    DESCRIPTION "Create BitTorrent metainfo (.torrent) files"
    LANGUAGES C)

option(USE_PTHREADS "Use multiple POSIX threads for calculating hashes" ON)
option(USE_OPENSSL "Use the SHA1 implementation in OpenSSL instead of the built-in one" ON)
option(USE_LONG_OPTIONS "Enable long command line options, started with two dashes" ON)
option(LARGE_FILES "Support files and torrents larger than 2GB on 32-bit systems" OFF)
option(NO_HASH_CHECK "Disable the redundant bytes-hashed versus reported-size check" OFF)
option(DEBUG "Enable leftover debugging code" OFF)
option(ALLINONE "Build a single translation unit via -DALLINONE" OFF)
option(BUILD_TESTS "Build the Unity unit tests (uses vendored external/Unity-2.7.0)" OFF)
set(MAX_OPENFD "100" CACHE STRING "Maximum number of file descriptors the directory walker opens")

# -std=gnu23 + warnings; -Werror=format turns printf format-string mismatches
# into hard errors
add_compile_options(-std=gnu23 -Wall -Wextra -Wpedantic -Werror=format)

# version from the latest git tag (e.g. "v1.1" or "v1.1-3-gabc1234"),
# falling back to the short commit hash, or "unknown"
find_package(Git QUIET)
set(MKTORRENT_VERSION "unknown")
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE MKTORRENT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_DESCRIBE_RESULT)
    if(NOT GIT_DESCRIBE_RESULT EQUAL 0)
        set(MKTORRENT_VERSION "unknown")
    endif()
endif()

# core compile definitions shared by mktorrent and the tests
set(MKTORRENT_DEFINES)
if(USE_OPENSSL)
    list(APPEND MKTORRENT_DEFINES USE_OPENSSL)
endif()
if(USE_PTHREADS)
    list(APPEND MKTORRENT_DEFINES USE_PTHREADS)
endif()
if(USE_LONG_OPTIONS)
    list(APPEND MKTORRENT_DEFINES USE_LONG_OPTIONS)
endif()
if(LARGE_FILES)
    list(APPEND MKTORRENT_DEFINES _LARGEFILE_SOURCE _FILE_OFFSET_BITS=64)
endif()
if(NO_HASH_CHECK)
    list(APPEND MKTORRENT_DEFINES NO_HASH_CHECK)
endif()
if(DEBUG)
    list(APPEND MKTORRENT_DEFINES DEBUG)
endif()
if(NOT MAX_OPENFD EQUAL 100)
    list(APPEND MKTORRENT_DEFINES MAX_OPENFD=${MAX_OPENFD})
endif()

if(ALLINONE)
    add_executable(mktorrent main.c)
    target_compile_definitions(mktorrent PRIVATE ALLINONE)
else()
    set(MKTORRENT_SOURCES ftw.c init.c output.c main.c msg.c ll.c)
    if(USE_OPENSSL)
        # SHA1 comes from libcrypto, the built-in sha1.c is not compiled
    else()
        list(APPEND MKTORRENT_SOURCES sha1.c)
    endif()
    if(USE_PTHREADS)
        list(APPEND MKTORRENT_SOURCES hash_pthreads.c)
    else()
        list(APPEND MKTORRENT_SOURCES hash.c)
    endif()
    add_executable(mktorrent ${MKTORRENT_SOURCES})
endif()

target_compile_definitions(mktorrent PRIVATE ${MKTORRENT_DEFINES}
    VERSION="${MKTORRENT_VERSION}")
if(USE_OPENSSL)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(mktorrent PRIVATE OpenSSL::Crypto)
endif()
if(USE_PTHREADS)
    target_link_libraries(mktorrent PRIVATE pthread)
endif()

install(TARGETS mktorrent RUNTIME DESTINATION bin)

if(BUILD_TESTS)
    # vendored Unity (same version the xmake build pins via unity_test)
    set(UNITY_EXTENSION_FIXTURE_ENABLED OFF)
    set(UNITY_EXTENSION_MEMORY_ENABLED OFF)
    add_subdirectory(external/Unity-2.7.0)

    # tests include "unity/unity.h" (the installed layout); the vendored copy
    # keeps the headers in src/, so shim them under unity/ in the build tree
    file(COPY external/Unity-2.7.0/src/ DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/unity-shim/unity
        FILES_MATCHING PATTERN "*.h")

    add_executable(mktorrent-tests
        tests/test_main.c
        tests/test_ll.c
        tests/test_sha1.c
        tests/test_hash.c
        tests/test_output.c
        tests/test_ftw.c
        tests/test_init.c
        tests/test_util.c)
    target_include_directories(mktorrent-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_BINARY_DIR}/unity-shim)
    target_link_libraries(mktorrent-tests PRIVATE unity::framework)
    # library under test (main.c and init.c are not unit tested);
    # mirror the pthreads option so the hash tests also exercise
    # the multithreaded implementation; OpenSSL is never used here
    # (xmake defines only USE_PTHREADS, with the bundled sha1.c compiled in)
    list(REMOVE_ITEM MKTORRENT_DEFINES USE_OPENSSL)
    target_compile_definitions(mktorrent-tests PRIVATE ${MKTORRENT_DEFINES}
        VERSION="Vtest")
    if(USE_PTHREADS)
        target_sources(mktorrent-tests PRIVATE hash_pthreads.c)
        target_link_libraries(mktorrent-tests PRIVATE pthread)
    else()
        target_sources(mktorrent-tests PRIVATE hash.c)
    endif()
    target_sources(mktorrent-tests PRIVATE ll.c sha1.c msg.c output.c ftw.c)

    enable_testing()
    add_test(NAME mktorrent-tests COMMAND mktorrent-tests)
endif()
