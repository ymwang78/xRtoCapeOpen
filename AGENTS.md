# AGENTS.md

## Cursor Cloud specific instructions

### What this repo is
`xRtoCapeOpen` is a **C++17 / CMake** integration layer bridging the CAPE-OPEN MINLP
standard and the xOpt/xRto optimization framework. It is **Windows-first** (COM + CORBA/TAO)
and is a slice of the monorepo `ymwang78/zd-cxxproj` (it normally lives at
`libsrc/xRtoCapeOpen`). There is no README; the source of truth is `docs/*.md` and the
`CMakeLists.txt` files. There are no runnable web services or databases — the "app" is a
static library exercised by GTest suites and by the exported C-ABI entry point
`xOptModel_createModel`.

### External dependency: xOpt headers (critical, non-obvious)
`core/` and `xOptMINLPco/` `#include "xOpt/xOptModel.h"`, `xOptProblem.h`, `xOptInterface.h`.
Those headers are **not in this repo** — CMake hard-codes `XOPT_INCLUDE_DIR = ../../../include`
(the monorepo root). They are fetched from the public parent repo `ymwang78/zd-cxxproj`
(`include/xOpt/`). The update script clones them to `~/.xopt-deps/zd-cxxproj/include`.
Because CMake sets `XOPT_INCLUDE_DIR` unconditionally (a plain `set()`, not a cache var),
`-D` cannot override it; instead pass the real include dir through `CMAKE_CXX_FLAGS`
(`-I$HOME/.xopt-deps/zd-cxxproj/include`). The stale `../../../include` entry is harmless.

### Two GCC/Linux gotchas when building here
1. `/usr/bin/c++` resolves to **Clang**, which cannot find `-lstdc++` in this VM. Always
   configure with `-DCMAKE_CXX_COMPILER=g++`.
2. The upstream `xOpt/xOptModel.h` uses bare `size_t` without `#include <cstddef>` (fine on
   MSVC, breaks on GCC). Do **not** edit the header — force-include it via
   `-include cstddef` in `CMAKE_CXX_FLAGS`.

### What builds on Linux vs. Windows-only
On Linux (this VM) only the transport-agnostic pieces build:
- `capeopen_core` (mock backend) + `test_capeopen_problem` (16 tests).
- `xoptminlpco` adapter + `test_xoptminlpco_adapter`, `test_xoptminlpco_capi`.

Windows-only / not buildable here: the COM backend, the CORBA/TAO backend, `xOptMINLPco.dll`
and `xOptMINLPcoCorbaServer.exe` (they need ACE **and** TAO; TAO is not apt-packaged on
Ubuntu). Their CMake options are guarded by `if(WIN32)` and forced OFF otherwise, so they
simply drop out of the build rather than failing to configure.

### Build & test (Linux)
```bash
XOPT_INC="$HOME/.xopt-deps/zd-cxxproj/include"
FLAGS="-I$XOPT_INC -include cstddef"

# capeopen_core + tests
cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS="$FLAGS"
cmake --build core/build -j"$(nproc)"
(cd core/build && ctest --output-on-failure)

# xOptMINLPco (transport-agnostic subset) + tests
cmake -S xOptMINLPco -B xOptMINLPco/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS="$FLAGS"
cmake --build xOptMINLPco/build -j"$(nproc)"
(cd xOptMINLPco/build && ctest --output-on-failure)
```

### Style and lint
This repo carries no `.clang-format`/`.clang-tidy` of its own, but that does **not** mean there
is no style. Both live in the parent monorepo `ymwang78/zd-cxxproj`, whose `.clang-format` sits
at its root — one level above where this slice normally lives (`libsrc/xRtoCapeOpen`). In a
standalone checkout that file is absent, so running `clang-format` here silently falls back to
LLVM defaults and reformats everything wrongly. Fetch the parent's `.clang-format` (the same
place the xOpt headers come from) before formatting, or do not format at all.

Conventions the parent repo mandates, none of them clang-format's defaults:
- Classes `PascalCase`, functions `camelCase`, variables `snake_case`, members `snake_case_`.
- Tests use GTest, files named `test_*.cpp`, and any `main` wrapped in `#ifndef USE_GTEST_MAIN`.
- **All `.c/.cpp/.h/.hpp` are UTF-8 *with BOM*.** This is not cosmetic. The sources carry
  Chinese comments; strip the BOM and MSVC reads them as the local ANSI codepage (CP936), where
  a trailing byte of a multi-byte character can pair with the newline and swallow the following
  line into the comment. The failure surfaces as a syntax error on a line that looks fine.
  Editors and scripts that rewrite whole files are the usual culprit — check the BOM survived.

Beyond that, treat a warning-clean `g++ -Wall -Wextra` build as the bar; only benign
`-Wunused-parameter` warnings exist today.

### IDL regeneration (maintainer tool, optional)
`tools/gen_capeopen_minlp_idl.py` regenerates `CAPEOPEN100_Minlp.idl` from CAPE-OPEN PDFs
(gitignored, not in repo). Needs `pip install pypdf` and the PDFs; not part of normal dev/test.
