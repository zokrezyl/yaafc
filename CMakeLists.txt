cmake_minimum_required(VERSION 3.20)
project(yaafc C CXX)

# C++17 is the floor (simdjson requires it). The C bits stay C17; only
# src/yaafc/yjson/yjson.cpp gets compiled as C++.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(YAAFC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_compile_options(
    -Wall -Wextra -Wno-attributes
    -Werror=incompatible-pointer-types
)

find_package(Threads REQUIRED)

# 3rdparty deps (built locally via build-tools/3rdparty/<lib>/_build.sh)
include(build-tools/yaafc/libs/co.cmake)
include(build-tools/yaafc/libs/uv.cmake)
include(build-tools/yaafc/libs/yaml.cmake)
include(build-tools/yaafc/libs/simdjson.cmake)
include(build-tools/yaafc/libs/sqlite3.cmake)

# ---- yplatform backend selection -------------------------------------
#
# yplatform abstractions get one backend file per platform. Backends
# under src/yaafc/yplatform/<module>/<platform>.c. Today only `time`
# is platform-split; future modules (paths, threads, sockets) follow
# the same shape.
if(WIN32)
    set(YAAFC_YPLATFORM_TIME_SRC src/yaafc/yplatform/time/windows.c)
else()
    set(YAAFC_YPLATFORM_TIME_SRC src/yaafc/yplatform/time/posix.c)
endif()

# ---- yaafc runtime ---------------------------------------------------
add_library(yaafc_runtime STATIC
    src/yaafc/ycore/result.c
    src/yaafc/ycore/ytrace.c
    src/yaafc/yclass/class.c
    src/yaafc/yclass/rpc.c
    src/yaafc/yclass/jinvoke.c
    src/yaafc/yco/coro.c
    src/yaafc/yloop/yloop.c
    src/yaafc/yengine/engine.c
    src/yaafc/frontends/yrpc/yrpc.c
    src/yaafc/frontends/yttp/yttp.c
    src/yaafc/frontends/yhttp/yhttp.c
    src/yaafc/frontends/yhttp/frontend.c
    src/yaafc/frontends/cli/cli.c
    third_party/picohttpparser/picohttpparser.c
    src/yaafc/yconfig/yconfig.c
    src/yaafc/yargv/yargv.c
    src/yaafc/yjson/yjson.cpp
    ${YAAFC_YPLATFORM_TIME_SRC}
)
target_include_directories(yaafc_runtime PUBLIC
    include
    third_party/uthash
    third_party/picohttpparser
)

# All plugin public headers now land in include/yaafc/plugin/<name>/
# (codegen output target). They're reachable through the repo-wide
# `include/` already PUBLIC-included above — no plugin-specific
# include directories needed.
target_link_libraries(yaafc_runtime PUBLIC co uv yaml simdjson Threads::Threads)
target_compile_definitions(yaafc_runtime PUBLIC _GNU_SOURCE)

# ---- codegen helper --------------------------------------------------
#
# yaafc_add_plugin(<name>
#                  SOURCES <annotated-c-files...>)
#
# Drives src/yaafc/yclass/gen/codegen.py for one plugin module.
#
# Layout invariant:
#   - PUBLIC generated headers land in  include/yaafc/plugin/<name>/   (repo-wide include root)
#   - INTERNAL .gen.c files land next to the annotated sources under  src/yaafc/plugins/<name>/
#
# Defines an OBJECT library `yaafc_plugin_<name>` linked into the
# driver binary.
function(yaafc_add_plugin NAME)
    cmake_parse_arguments(PLUG "" "" "SOURCES;EXTRA_INCLUDES" ${ARGN})
    set(_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_PUB_DIR "${YAAFC_ROOT}/include/yaafc/plugin/${NAME}")
    file(MAKE_DIRECTORY "${_PUB_DIR}")

    # EXTRA_INCLUDES are dirs the annotated source needs at AST-dump
    # time (codegen runs clang -fsyntax-only) AND at compile time.
    # Each path is joined with ':' for the YAAFC_CODEGEN_EXTRA_INCLUDES
    # env var that codegen.py splits. Plugins compose via each other's
    # PUBLIC generated stubs (e.g. session.c -> storage_sql_set) and
    # those all live under <repo>/include/yaafc/plugin/*, which is
    # part of YAAFC_ROOT/include — so we don't need to enumerate
    # sibling plugin include dirs here anymore.
    set(_EXTRA_INCLUDE_PATH "${YAAFC_ROOT}/include")
    foreach(_d ${PLUG_EXTRA_INCLUDES})
        set(_EXTRA_INCLUDE_PATH "${_EXTRA_INCLUDE_PATH}:${_d}")
    endforeach()

    # Outputs that codegen.py writes for this plugin:
    #   include/yaafc/plugin/<name>/<name>.h       — combined public API
    #   src/yaafc/plugins/<name>/<name>.internal.h — codegen-only types
    #   src/yaafc/plugins/<name>/<stem>.gen.c      — class-accessor body
    #   src/yaafc/plugins/<name>/methods.gen.c     — public-stub bodies
    #   src/yaafc/plugins/<name>/rpc.gen.c         — skel + jinvoke + create
    #   src/yaafc/plugins/<name>/model.yaml        — parsed model echo
    set(_GEN_PUBLIC "${_PUB_DIR}/${NAME}.h")
    set(_GEN_INTERNAL
         "${_SRC_DIR}/${NAME}.internal.h"
         "${_SRC_DIR}/methods.gen.c"
         "${_SRC_DIR}/rpc.gen.c"
         "${_SRC_DIR}/model.yaml")
    foreach(src ${PLUG_SOURCES})
        get_filename_component(_stem "${src}" NAME_WE)
        list(APPEND _GEN_INTERNAL "${_SRC_DIR}/${_stem}.gen.c")
    endforeach()

    set(_SOURCES_FULL "")
    foreach(src ${PLUG_SOURCES})
        list(APPEND _SOURCES_FULL "${_SRC_DIR}/${src}")
    endforeach()

    add_custom_command(
        OUTPUT ${_GEN_PUBLIC} ${_GEN_INTERNAL}
        COMMAND ${CMAKE_COMMAND} -E env
            "YAAFC_CODEGEN_EXTRA_INCLUDES=${_EXTRA_INCLUDE_PATH}"
            uv run ${YAAFC_ROOT}/src/yaafc/yclass/gen/codegen.py
                ${NAME}
                ${YAAFC_ROOT}/include
                ${_SRC_DIR}
                ${_SOURCES_FULL}
        DEPENDS ${_SOURCES_FULL} ${YAAFC_ROOT}/src/yaafc/yclass/gen/codegen.py
        WORKING_DIRECTORY ${_SRC_DIR}
        COMMENT "codegen[${NAME}]: regenerating public/internal artefacts"
        VERBATIM
    )
    add_custom_target(yaafc_plugin_${NAME}_codegen
                      DEPENDS ${_GEN_PUBLIC} ${_GEN_INTERNAL})

    # The annotated <stem>.c #include's its sibling <stem>.gen.c, so we
    # don't add the .gen.c files separately — only methods.gen.c and
    # rpc.gen.c are independent translation units.
    add_library(yaafc_plugin_${NAME} OBJECT
        ${_SOURCES_FULL}
        ${_SRC_DIR}/methods.gen.c
        ${_SRC_DIR}/rpc.gen.c
    )
    add_dependencies(yaafc_plugin_${NAME} yaafc_plugin_${NAME}_codegen)
    # All plugins (and the rest of the tree) reach generated public
    # headers via <yaafc/plugin/<name>/...>, which resolves under
    # the repo-wide include/ root.
    target_include_directories(yaafc_plugin_${NAME} PUBLIC ${YAAFC_ROOT}/include)
    target_link_libraries(yaafc_plugin_${NAME} PUBLIC yaafc_runtime)
endfunction()

# ---- plugins ---------------------------------------------------------
#
# Adding a new plugin: drop a dir under src/yaafc/plugins/<name>/
# with a CMakeLists.txt that calls `yaafc_add_plugin(<name> SOURCES …)`,
# then append <name> to YAAFC_PLUGINS below.
set(YAAFC_PLUGINS
    storage
    calculator
    time
    accounts
    portalloc
    session
    password_authn
    github_authn
    token_issuer
    issues
    git_repo
    git_pipeline
    personal_access_tokens
    mesh
)
foreach(_p ${YAAFC_PLUGINS})
    add_subdirectory(src/yaafc/plugins/${_p})
endforeach()

# Some plugins #include other plugins' generated headers (session,
# accounts, password_authn etc. delegate to storage_sql). The codegen
# of each consumer must therefore run AFTER the codegen of every
# plugin it pulls a header from, otherwise the AST-dump step fails
# with "use of undeclared identifier" before the produces step has
# regenerated the header. List the producer→consumer edges here.
foreach(_consumer session accounts password_authn token_issuer
                  personal_access_tokens issues git_repo git_pipeline
                  github_authn)
    add_dependencies(yaafc_plugin_${_consumer}_codegen
                     yaafc_plugin_storage_codegen)
endforeach()

# The runtime's frontend (yhttp/frontend.c) and ymain's smoke `client`
# subcommand both pull in plugin public headers. They must therefore
# wait for every plugin's codegen to have produced those headers
# before they get compiled.
foreach(_p ${YAAFC_PLUGINS})
    add_dependencies(yaafc_runtime yaafc_plugin_${_p}_codegen)
endforeach()

# ---- driver entry point (ymain) -------------------------------------
#
# The main() now lives under src/yaafc/ymain/ — see that module's
# CMakeLists for the rationale (yaafc/<name>/ is the only sanctioned
# place for compiled yaafc sources; bin/ used to host the driver but
# was a wart).
add_subdirectory(src/yaafc/ymain)
foreach(_p ${YAAFC_PLUGINS})
    add_dependencies(yaafc_ymain yaafc_plugin_${_p}_codegen)
endforeach()

# ---- driver binary ---------------------------------------------------
#
# Plugin object libraries are linked as *direct sources* of the
# executable rather than via target_link_libraries — object-file
# sources never get dropped by the linker, so the codegen constructor
# hooks (`__attribute__((constructor))`) fire reliably. The driver
# entry point (yaafc_ymain) gets the same treatment.
set(YAAFC_PLUGIN_OBJECTS "")
set(YAAFC_PLUGIN_TARGETS "")
foreach(_p ${YAAFC_PLUGINS})
    list(APPEND YAAFC_PLUGIN_OBJECTS  $<TARGET_OBJECTS:yaafc_plugin_${_p}>)
    list(APPEND YAAFC_PLUGIN_TARGETS  yaafc_plugin_${_p})
endforeach()

add_executable(yaafc
    $<TARGET_OBJECTS:yaafc_ymain>
    ${YAAFC_PLUGIN_OBJECTS})
add_dependencies(yaafc yaafc_ymain ${YAAFC_PLUGIN_TARGETS})
target_link_libraries(yaafc PRIVATE yaafc_runtime sqlite3)

# ---- tests (smoke) ---------------------------------------------------
add_subdirectory(tests)
