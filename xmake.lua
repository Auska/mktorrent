-- xmake build script for mktorrent
--
-- Mirrors the behaviour of the shipped Makefile/GNUmakefile:
--
--   xmake                              default build
--   xmake f --pthreads=y               use pthreads for hashing
--   xmake f --openssl=y                use OpenSSL SHA1 (requires libcrypto)
--   xmake f --long_options=y           enable long command line options
--   xmake f --large_files=y            32-bit support for files > 2GB
--   xmake f --no_hash_check=y          disable the redundant byte-count check
--   xmake f --max_openfd=256           directory walker fd limit (default 100)
--   xmake f --debug=y                  enable leftover debugging code
--   xmake f --allinone=y               single translation unit build
--
-- Options are re-applied with `xmake f --<option>=<value>` before building.

set_project("mktorrent")
set_version("V" .. os.date("%Y%m%d"))
set_xmakever("2.8.6")

option("pthreads")
    set_default(false)
    set_showmenu(true)
    set_description("Use multiple POSIX threads for calculating hashes")

option("openssl")
    set_default(false)
    set_showmenu(true)
    set_description("Use the SHA1 implementation in OpenSSL instead of the built-in one")

option("long_options")
    set_default(false)
    set_showmenu(true)
    set_description("Enable long command line options, started with two dashes")

option("large_files")
    set_default(false)
    set_showmenu(true)
    set_description("Support files and torrents larger than 2GB on 32-bit systems")

option("no_hash_check")
    set_default(false)
    set_showmenu(true)
    set_description("Disable the redundant bytes-hashed versus reported-size check")

option("max_openfd")
    set_default(100)
    set_showmenu(true)
    set_description("Maximum number of file descriptors the directory walker opens")

option("debug")
    set_default(false)
    set_showmenu(true)
    set_description("Enable leftover debugging code")

option("allinone")
    set_default(false)
    set_showmenu(true)
    set_description("Build a single translation unit via -DALLINONE")

option("tests")
    set_default(false)
    set_showmenu(true)
    set_description("Build the Unity unit tests (requires unity_test)")

target("mktorrent")
    set_kind("binary")
    set_optimize("fast")

    on_load(function (target)
        -- options shared by every build mode
        if get_config("openssl") then
            target:add("defines", "USE_OPENSSL")
            target:add("links", "crypto")
        end
        if get_config("pthreads") then
            target:add("defines", "USE_PTHREADS")
            target:add("links", "pthread")
        end
        if get_config("long_options") then
            target:add("defines", "USE_LONG_OPTIONS")
        end
        if get_config("large_files") then
            target:add("defines", "_LARGEFILE_SOURCE", "_FILE_OFFSET_BITS=64")
        end
        if get_config("no_hash_check") then
            target:add("defines", "NO_HASH_CHECK")
        end
        local max_openfd = get_config("max_openfd")
        if max_openfd ~= 100 then
            target:add("defines", "MAX_OPENFD=" .. tostring(max_openfd))
        end
        if get_config("debug") then
            target:add("defines", "DEBUG")
        end
        target:add("defines", 'VERSION="V' .. os.date("%Y%m%d") .. '"')

        -- source selection
        if get_config("allinone") then
            -- main.c #includes all other .c files in alphabetical order
            target:add("files", "main.c")
            target:add("defines", "ALLINONE")
        else
            target:add("files", "ftw.c", "init.c", "output.c", "main.c", "msg.c", "ll.c")
            if get_config("openssl") then
                -- SHA1 comes from libcrypto, the built-in sha1.c is not compiled
            else
                target:add("files", "sha1.c")
            end
            if get_config("pthreads") then
                target:add("files", "hash_pthreads.c")
            else
                target:add("files", "hash.c")
            end
        end
    end)

    -- compiler flags (POSIX toolchains): C23 (GNU dialect) + warnings
    if is_plat("windows") then
        add_cxflags("/W4")
    else
        add_cxflags("-std=gnu23", "-Wall", "-Wextra", "-Wpedantic")
    end
target_end()

if has_config("tests") then
    add_requires("unity_test")

    target("mktorrent-tests")
        set_kind("binary")
        set_optimize("fast")
        add_includedirs(".")
        add_packages("unity_test")
        add_defines('VERSION="Vtest"')
        add_files(
            "tests/test_main.c",
            "tests/test_ll.c",
            "tests/test_sha1.c",
            "tests/test_hash.c",
            "tests/test_output.c",
            "tests/test_ftw.c",
            "tests/test_util.c")
        -- library under test (main.c and init.c are not unit tested)
        add_files("ll.c", "sha1.c", "msg.c", "hash.c", "output.c", "ftw.c")

        if is_plat("windows") then
            add_cxflags("/W4")
        else
            add_cxflags("-std=gnu23", "-Wall", "-Wextra", "-Wpedantic")
        end
end
