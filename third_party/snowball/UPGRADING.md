# Upgrading the vendored Snowball

The FTS extension bundles a snapshot of the [Snowball](https://snowballstem.org/)
stemmer library under this directory. This document explains how to refresh
that snapshot when a new Snowball release is available.

## What is vendored

Only the C sources strictly required at runtime are vendored. Build helpers,
regeneration scripts, language bindings, and tests are intentionally omitted.

| Path                          | Source in upstream             |
|-------------------------------|--------------------------------|
| `include/libstemmer.h`        | `include/libstemmer.h`         |
| `libstemmer/libstemmer.c`     | `libstemmer/libstemmer.c`      |
| `libstemmer/modules.h`        | `libstemmer/modules.h`         |
| `runtime/api.c`               | `runtime/api.c`                |
| `runtime/api.h`               | `runtime/api.h`                |
| `runtime/utilities.c`         | `runtime/utilities.c`          |
| `runtime/snowball_runtime.h`  | `runtime/snowball_runtime.h`   |
| `src_c/stem_*.c`              | `src_c/stem_*.c` (all)         |
| `src_c/stem_*.h`              | `src_c/stem_*.h` (all)         |
| `LICENSE`                     | `COPYING`                      |

Notes:

- `modules.h` is imported a generated artifact; we use it as-is.
- All encodings (`UTF_8`, `ISO_8859_*`, `KOI8_R`) are vendored even though FTS
  uses only `UTF_8`, because `modules.h` references every stemmer and dropping
  files would break the link. The extra source files cost a small amount of
  binary size and zero maintenance.

## Upgrade procedure

1. Get the latest Snowball:

   ```sh
   git clone https://github.com/snowballstem/snowball
   ```

2. Build it:

   ```sh
   cd snowball
   make
   ```

3. Replace the vendored files from the duckdb-fts repository:

   ```sh
   cd duckdb-fts
   SB=~/projects/snowball
   DEST=third_party/snowball

   rm -rf $DEST/include $DEST/libstemmer $DEST/runtime $DEST/src_c
   mkdir -p $DEST/{include,libstemmer,runtime,src_c}

   cp $SB/include/libstemmer.h                $DEST/include/
   cp $SB/libstemmer/libstemmer.c             $DEST/libstemmer/
   cp $SB/libstemmer/modules.h                $DEST/libstemmer/
   cp $SB/runtime/api.c $SB/runtime/api.h     $DEST/runtime/
   cp $SB/runtime/utilities.c                 $DEST/runtime/
   cp $SB/runtime/snowball_runtime.h          $DEST/runtime/
   cp $SB/src_c/stem_*.c $SB/src_c/stem_*.h   $DEST/src_c/
   cp $SB/COPYING                             $DEST/LICENSE
   ```

4. Build FTS to confirm the snapshot compiles and links:

   ```sh
   make
   ```

   The CMake source globs pick up new `stem_*.c` files automatically, so no
   `CMakeLists.txt` edits are needed when stemmers are added or removed.

5. Check the stemmer list in `README.md`. Snowball releases occasionally
   add, remove, or rename algorithms. Diff the set of `stem_UTF_8_*.c` files
   against the previous snapshot and update the two stemmer enumerations in
   `README.md` accordingly (`PRAGMA create_fts_index` and `stem` function
   sections).

6. Run the FTS test suite to catch regressions in algorithm behaviour.
   The extension is built with `DONT_LINK` (see `extension_config.cmake`),
   so the test runner needs to be told where to find the loadable
   `fts.duckdb_extension`. Point it at the local extension repository
   produced by the build:

   ```sh
   LOCAL_EXTENSION_REPO="$(pwd)/build/release/repository" \
     ./build/release/test/unittest "[fts]"
   ```

   Without `LOCAL_EXTENSION_REPO`, `require fts` directives silently skip
   every test.

## Public API contract

FTS depends only on the public Snowball API declared in `libstemmer.h`:
`sb_stemmer_new`, `sb_stemmer_stem`, `sb_stemmer_length`, `sb_stemmer_delete`,
`sb_stemmer_list`. Internal runtime types (`SN_env`, `SN_new_env`, etc.) are
not used by FTS, so upstream changes to the internal ABI are not a concern.
