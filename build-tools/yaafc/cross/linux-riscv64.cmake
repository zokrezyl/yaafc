# CMake toolchain for cross-compiling yaafc from host x86_64 to
# linux-riscv64. Mirrors yetty's CMAKE_CROSS_RISCV setup.
#
# Usage:
#   cmake -B build-linux-riscv64-release \
#         -DCMAKE_TOOLCHAIN_FILE=build-tools/yaafc/cross/linux-riscv64.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Host setup (Ubuntu 24.04 / Debian 12):
#   sudo apt-get install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
#
# Output: a static-linked yaafc binary that runs inside the riscv64
# Alpine VM (build-tools/yemu/run-vm.sh) without ABI mismatch against
# musl. The Makefile target wraps this and the 3rdparty fetch sets
# TARGET_PLATFORM=linux-riscv64 to pick up the same cross toolchain
# inside each lib's _build.sh.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_C_COMPILER   riscv64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER riscv64-linux-gnu-g++)
set(CMAKE_AR           riscv64-linux-gnu-ar)
set(CMAKE_RANLIB       riscv64-linux-gnu-ranlib)

# Find libs/headers under the multiarch /usr/riscv64-linux-gnu/ tree
# first; never look in /usr/lib/x86_64 by mistake.
set(CMAKE_LIBRARY_ARCHITECTURE     riscv64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH           /usr/riscv64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Plugin sources carry `[[clang::annotate(...)]]` C23 scoped
# attributes the codegen reads at AST-dump time. gcc <= 14 only parses
# the `ns::name` form in `-std=c2x`/`c23` mode; gnu2x adds the GNU
# extensions on top. Forcing the standard here keeps the toolchain
# self-contained — top-level CMakeLists sets CMAKE_C_STANDARD=17 for
# the host build (gcc 14 accepts the syntax under C17 too).
set(CMAKE_C_FLAGS_INIT "-std=gnu2x -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
set(CMAKE_CXX_FLAGS_INIT "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")

# Static link: the alpine VM is musl; cross-glibc binaries can't load
# musl's ld.so. -static eliminates the loader entirely; -static-libgcc
# is automatic with -static but spelled out for clarity.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc")
