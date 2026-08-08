# AGENTS.md

This file provides guidance to CodeBuddy Code when working with code in this repository.

## Project overview

mktorrent is a C tool that creates BitTorrent metainfo (`.torrent`) files. It walks a target file or directory, hashes the content in fixed-size pieces with SHA-1, and writes a bencoded metainfo file. GPLv2. There is no test suite and no CI.

## Build

Two build systems are in sync: GNU/BSD make (shipped) and xmake (added locally).

```sh
make                 # default build, produces ./mktorrent
make clean           # remove binary and *.o
make allinone        # single translation unit (-DALLINONE, main.c includes all .c files)
make install PREFIX=/usr/local    # install binary
make -f BSDmakefile  # for old BSD make
```

Feature flags (pass as `make FLAG=1`; `CFLAGS ?= -O2 -Wall -Wextra -Wpedantic` by default):

| Flag | Effect |
|------|--------|
| `USE_PTHREADS=1` | compile `hash_pthreads.c` instead of `hash.c`, link `-lpthread` |
| `USE_OPENSSL=1` | use OpenSSL EVP SHA-1 instead of bundled `sha1.c`, link `-lcrypto` |
| `USE_LONG_OPTIONS=1` | enable `--announce` style long options |
| `USE_LARGE_FILES=1` | `-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64` for >2GB files on 32-bit |
| `NO_HASH_CHECK=1` | skip the "bytes hashed == reported size" verification |
| `MAX_OPENFD=n` | fd cap for the directory walker (default 100) |
| `DEBUG=1` | leftover debug code |

xmake equivalent (options map 1:1 to the flags above, except `allinone`):

```sh
xmake                              # default build
xmake f --pthreads=y --long_options=y --openssl=y   # re-configure, then xmake
xmake install --installdir=<dir>
```

Version string is compiled in as `-DVERSION="VYYYYMMDD"` (Makefile: `version = V$(shell date +%Y%m%d)`). When passing VERSION yourself, GCC needs the literal quotes spelling `-DVERSION="V..."`.

### Run

```sh
./mktorrent -a http://tracker/announce -o out.torrent <file-or-dir>
./mktorrent -h   # help
```

There is no lint/test target; the build is expected to stay warning-free under `-Wall -Wextra -Wpedantic`.

## Architecture

The program is a linear pipeline, driven by `main()` (`main.c`):

1. **`init()`** (`init.c`) — parse CLI options with `getopt`/`getopt_long`, resolve the absolute output path (`set_absolute_file_path`, `strdup` so it is freeable), decide target type, and if it's a directory `chdir()` into it and walk it with `file_tree_walk`. Then sort the file list (`ll_sort`, by name), auto-pick piece length, and compute piece count.
2. **`open_file()`** (`main.c`) — create the output file with `O_EXCL` unless `-f`. Note it runs *after* the directory scan, so an output file placed inside the target directory is not hashed into itself.
3. **`make_hash()`** (`hash.c` serial, or `hash_pthreads.c` with threads) — reads every file, splits into `piece_length` blocks, SHA-1 hashes each, and concatenates the 20-byte digests into one `hash_string`. Crosses file boundaries; the last piece may be short.
4. **`write_metainfo()`** (`output.c`) — bencodes the metainfo to the `FILE*`: root dict, `announce`/`announce-list`, `comment`, `created by`, `creation date`, `publisher`/`publisher-url`, the `info` dict (`name`, `piece length`, `pieces`, `length` or `files`, optional `private`/`source`/`x_cross_seed`), and `url-list`.
5. **`cleanup_metafile()`** (`init.c`) — free the lists.

### Data structures

- `struct metafile` (`mktorrent.h`) — the central object: options + computed results (`size`, `file_list`, `pieces`). Passed everywhere.
- `struct ll` / `struct ll_node` (`ll.h`/`ll.c`) — intrusive doubly-linked list used for announce tiers, web seeds, file list, exclude patterns. `ll_extend` concatenates (destroying the second list); `ll_sort` is a stable recursive merge sort. Nodes store a copied `data_size`-byte payload, or a raw pointer when `data_size == 0`.
- `struct file_data` — `path` + `size`, stored in `metafile.file_list`.
- `struct queue` / `struct piece` (`hash_pthreads.c`) — producer-consumer: `read_files` (the calling thread) fills `piece` buffers, N `worker` threads hash them; two mutexes (`free`/`full`) + two cond vars, up to `3*threads` buffers.

### Hashing

SHA-1 comes either from the bundled public-domain `sha1.c` (Steve Reid) or from OpenSSL, selected by `USE_OPENSSL`. Every hash call site has `#ifdef USE_OPENSSL` branches; `SHA_DIGEST_LENGTH` (20) sizes the digest array.

The pthread build runs a dedicated progress-printing thread (every `PROGRESS_PERIOD` µs, default 200000) plus `-t` worker threads (default = CPU count, capped at 20).

### Directory walker

`ftw.c` is a custom non-recursive `file_tree_walk` that keeps at most `MAX_OPENFD` directories open, using `telldir`/`seekdir` to reopen closed ones, and calls a callback per entry. The callback `process_node` (`init.c`) only accepts `S_ISREG` files, skips unreadable ones, and relies on the walk starting from `"." DIRSEP` so `path += 2` strips the `"./"` prefix. Exclude patterns (`-e`) are matched with `fnmatch` against the entry basename.

### Output bencoding

`output.c` writes bencode with `fprintf`. `write_file_list` temporarily replaces `DIRSEP_CHAR` with `'\0'` in the stored path strings to emit each path component as a list element, then restores the separator.

## Conventions and gotchas

- `struct metafile` is initialized positionally in `main.c:113`; adding a field to the struct requires keeping that initializer list in sync (no compiler error if it drifts).
- `-l` piece length is the power-of-2 exponent (valid range 15–28, default auto-picked from total size; >12.8TB falls back to 2^24).
- Repeated `-a` flags create backup-tracker *tiers*; comma-separated URLs within one `-a` are one tier.
- `-s` (source) and `-x` (cross-seed) put extra fields inside the `info` dict, so they affect the infohash.
- `EXPORT` (`export.h`) is `static` when `ALLINONE` is defined, empty otherwise.
- `ftw.c` reaches into `struct metafile` (as its opaque callback `data`) to read `exclude_list` — the walker is coupled to metafile.
- The pthread path prints `\r`-based progress to stdout even when piped.

## Known issues (verified in analysis)

- **Data race** in `hash_pthreads.c`: `q->pieces_hashed` is written under `mutex_free` (`put_free`) but read without synchronization by the progress thread (`print_progress`). Reproducible with ThreadSanitizer.
- **Symlinks are followed** by `ftw.c` (`stat`, not `lstat`): a symlink cycle aborts the whole run with `ELOOP` (fatal), and a symlink to a regular file hashes the content twice.
- **Integer overflow risk**: `m->pieces * SHA_DIGEST_LENGTH` is computed in 32-bit `unsigned int` before `malloc` in `make_hash` (`hash.c`, `hash_pthreads.c`).
