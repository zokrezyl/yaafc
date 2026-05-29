# yaafc-yemu wasm build — adapted wholesale from yetty's
# build-tools/yetty/{tinyemu,tinyemu-iframe}.cmake, brotli removed.
#
# Layout:
#   1. tinyemu static lib — the RISC-V emulator core + slirp + RV32/RV64
#      CPU variants. Mirrors yetty's tinyemu.cmake exactly.
#   2. tinyemu_vm executable — links tinyemu + tinyemu-bridge.c. No
#      brotli, so the bridge's session API is exported but
#      _yetty_brotli_decode is NOT.
#   3. --pre-js=yetty-assets-preload.js installs the asset-preload
#      hook on Module before main() runs.
#
# Build:
#   ./build.sh
# Serve:
#   python3 build/serve.py 8000 build
cmake_minimum_required(VERSION 3.20)
project(yaafc-yemu C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "yaafc-yemu must be configured via emcmake.")
endif()

set(YAAFC_ROOT     "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(TINYEMU_DIR    "${YAAFC_ROOT}/src/tinyemu")
set(BRIDGE_SOURCE  "${CMAKE_CURRENT_SOURCE_DIR}/tinyemu-bridge.c")

if(NOT EXISTS ${TINYEMU_DIR}/machine.c)
    message(FATAL_ERROR "TinyEMU source not found at ${TINYEMU_DIR}")
endif()

# ------------------------------------------------------------------ tinyemu static lib

set(TINYEMU_SOURCES
    ${TINYEMU_DIR}/virtio.c
    ${TINYEMU_DIR}/pci.c
    ${TINYEMU_DIR}/fs.c
    ${TINYEMU_DIR}/cutils.c
    ${TINYEMU_DIR}/iomem.c
    ${TINYEMU_DIR}/simplefb.c
    ${TINYEMU_DIR}/json.c
    ${TINYEMU_DIR}/machine.c
    ${TINYEMU_DIR}/riscv_machine.c
    ${TINYEMU_DIR}/softfp.c
    ${TINYEMU_DIR}/elf.c
    ${TINYEMU_DIR}/smp.c
    ${TINYEMU_DIR}/fs_disk.c
)
set(TINYEMU_SLIRP_SOURCES
    ${TINYEMU_DIR}/slirp/bootp.c    ${TINYEMU_DIR}/slirp/ip_icmp.c
    ${TINYEMU_DIR}/slirp/mbuf.c     ${TINYEMU_DIR}/slirp/slirp.c
    ${TINYEMU_DIR}/slirp/tcp_output.c ${TINYEMU_DIR}/slirp/cksum.c
    ${TINYEMU_DIR}/slirp/ip_input.c ${TINYEMU_DIR}/slirp/misc.c
    ${TINYEMU_DIR}/slirp/socket.c   ${TINYEMU_DIR}/slirp/tcp_subr.c
    ${TINYEMU_DIR}/slirp/udp.c      ${TINYEMU_DIR}/slirp/if.c
    ${TINYEMU_DIR}/slirp/ip_output.c ${TINYEMU_DIR}/slirp/sbuf.c
    ${TINYEMU_DIR}/slirp/tcp_input.c ${TINYEMU_DIR}/slirp/tcp_timer.c
    ${TINYEMU_DIR}/slirp/chr-backend.c
)

add_library(tinyemu STATIC ${TINYEMU_SOURCES} ${TINYEMU_SLIRP_SOURCES})

# riscv_cpu.c specialized via wrappers — yetty pattern.
set(RV32_W ${CMAKE_BINARY_DIR}/tinyemu_riscv_cpu32.c)
file(WRITE ${RV32_W} "#define MAX_XLEN 32\n#include \"${TINYEMU_DIR}/riscv_cpu.c\"\n")
set(RV64_W ${CMAKE_BINARY_DIR}/tinyemu_riscv_cpu64.c)
file(WRITE ${RV64_W} "#define MAX_XLEN 64\n#include \"${TINYEMU_DIR}/riscv_cpu.c\"\n")
target_sources(tinyemu PRIVATE ${RV32_W} ${RV64_W})

# <tinyemu/...> wrapper directory — yetty's bridge.c uses these paths.
set(TINYEMU_INCLUDE_WRAPPER ${CMAKE_BINARY_DIR}/tinyemu-include)
file(MAKE_DIRECTORY ${TINYEMU_INCLUDE_WRAPPER}/tinyemu)
file(MAKE_DIRECTORY ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/slirp)
set(TINYEMU_HEADERS
    cutils.h iomem.h virtio.h machine.h json.h fs.h list.h fbuf.h softfp.h
    riscv_cpu.h pci.h elf.h smp.h ydebug.h
)
foreach(H ${TINYEMU_HEADERS})
    if(EXISTS ${TINYEMU_DIR}/${H})
        configure_file(${TINYEMU_DIR}/${H}
                       ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/${H} COPYONLY)
    endif()
endforeach()
foreach(H libslirp.h chr-backend.h)
    if(EXISTS ${TINYEMU_DIR}/slirp/${H})
        configure_file(${TINYEMU_DIR}/slirp/${H}
                       ${TINYEMU_INCLUDE_WRAPPER}/tinyemu/slirp/${H} COPYONLY)
    endif()
endforeach()

target_include_directories(tinyemu PUBLIC  ${TINYEMU_INCLUDE_WRAPPER})
target_include_directories(tinyemu PRIVATE ${TINYEMU_DIR} ${TINYEMU_DIR}/slirp)

file(READ ${TINYEMU_DIR}/VERSION TINYEMU_VERSION)
string(STRIP "${TINYEMU_VERSION}" TINYEMU_VERSION)
target_compile_definitions(tinyemu PRIVATE
    _FILE_OFFSET_BITS=64
    _LARGEFILE_SOURCE
    _GNU_SOURCE
    CONFIG_VERSION="${TINYEMU_VERSION}"
    CONFIG_SLIRP
    CONFIG_RISCV_MAX_XLEN=64
)
target_compile_options(tinyemu PRIVATE
    -Wno-deprecated-declarations -Wno-unused-function -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-sign-compare
)

# ------------------------------------------------------------------ tinyemu_vm wasm executable

add_executable(tinyemu_vm ${BRIDGE_SOURCE})
target_link_libraries(tinyemu_vm PRIVATE tinyemu)
target_compile_definitions(tinyemu_vm PRIVATE CONFIG_SLIRP=1 _GNU_SOURCE)
set_target_properties(tinyemu_vm PROPERTIES
    OUTPUT_NAME "yaafc-yemu"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Identical to yetty's tinyemu-iframe.cmake link options, minus
# `_yetty_brotli_decode` from the export list (no brotli linked).
target_link_options(tinyemu_vm PRIVATE
    -sFILESYSTEM=1
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=64MB
    -sSTACK_SIZE=1048576
    -sWASM_BIGINT
    -sASSERTIONS=1
    -sEXIT_RUNTIME=0
    # NB: NO --pre-js. yetty's tinyemu_vm doesn't use one — the iframe
    # HTML (tinyemu-iframe.html) does its own asset preload before
    # calling _tinyemu_bridge_start. The yetty-assets-preload.js shim
    # is yetty's main-page concern (yetty.js), not the VM iframe's.
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','HEAPU8','HEAPU32','callMain','FS']"
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_tinyemu_bridge_input','_tinyemu_bridge_resize','_tinyemu_bridge_start','_tinyemu_session_open','_tinyemu_session_send','_tinyemu_session_close']"
)

# Stage page assets next to the wasm.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tinyemu-iframe.html
               ${CMAKE_BINARY_DIR}/tinyemu-iframe.html COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/index.html
               ${CMAKE_BINARY_DIR}/index.html         COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/yaafc.cfg
               ${CMAKE_BINARY_DIR}/yaafc.cfg          COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/serve.py
               ${CMAKE_BINARY_DIR}/serve.py           COPYONLY)
