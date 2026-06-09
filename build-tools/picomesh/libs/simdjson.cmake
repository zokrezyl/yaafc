# simdjson — fast JSON parser (C++).
#
# Built locally via build-tools/3rdparty/simdjson/_build.sh, then
# exposed here as the IMPORTED static target `simdjson`. The picomesh
# C code uses simdjson via a thin C ABI shim under src/picomesh/json/.

include_guard(GLOBAL)
include(${PICOMESH_ROOT}/build-tools/picomesh/3rdparty-fetch.cmake)

if(TARGET simdjson)
    return()
endif()

picomesh_3rdparty_fetch(simdjson _SJ_DIR)

set(_SJ_LIB "${_SJ_DIR}/lib/libsimdjson.a")
if(NOT EXISTS "${_SJ_LIB}")
    message(FATAL_ERROR "simdjson: ${_SJ_LIB} not present after fetch")
endif()
if(NOT EXISTS "${_SJ_DIR}/include/simdjson.h")
    message(FATAL_ERROR "simdjson: include/simdjson.h missing after fetch")
endif()

add_library(simdjson STATIC IMPORTED GLOBAL)
set_target_properties(simdjson PROPERTIES
    IMPORTED_LOCATION             "${_SJ_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${_SJ_DIR}/include"
)

message(STATUS "simdjson: prebuilt v${PICOMESH_3RDPARTY_simdjson_VERSION} (${_SJ_LIB})")
