cmake_minimum_required(VERSION 3.20)
# Pure C: the only former C++ translation unit (the JSON shim over simdjson)
# now parses with yyjson, a single-file C library, so the tree carries no C++
# dependency and needs no C++ toolchain.
project(picomesh C)

set(PICOMESH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PICOMESH_ENABLE_CODEGEN "Regenerate checked-in plugin codegen outputs" OFF)

if(MSVC)
    # Native MSVC (cl.exe). The GNU/clang warning flags below are not valid
    # for cl; use the MSVC equivalents. /W3 is the usual baseline; the
    # incompatible-pointer-type diagnostic maps to C4133, promoted to an
    # error to match the -Werror=incompatible-pointer-types intent.
    add_compile_options(/W3 /we4133 /D_CRT_SECURE_NO_WARNINGS)
    # Link the DYNAMIC Microsoft C runtime (msvcrt — /MD, /MDd for Debug),
    # the cl default. The 3rdparty static libs are all built /MD, so the exe
    # MUST match or the link hits CRT-conflict (LNK4098) / duplicate symbols.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
    add_compile_options(
        -Wall -Wextra -Wno-attributes
        -Werror=incompatible-pointer-types
    )
endif()

find_package(Threads REQUIRED)

# 3rdparty deps (built locally via build-tools/3rdparty/<lib>/_build.sh)
include(build-tools/picomesh/libs/co.cmake)
include(build-tools/picomesh/libs/uv.cmake)
include(build-tools/picomesh/libs/yaml.cmake)
include(build-tools/picomesh/libs/sqlite3.cmake)
include(build-tools/picomesh/libs/libmdbx.cmake)
include(build-tools/picomesh/libs/libgit2.cmake)
include(build-tools/picomesh/libs/libcurl.cmake)

# ---- platform backend selection -------------------------------------
#
# platform abstractions get one backend file per platform. Backends
# under src/picomesh/platform/<module>/<platform>.c. Today only `time`
# is platform-split; future modules (paths, threads, sockets) follow
# the same shape.
if(WIN32)
    set(PICOMESH_PLATFORM_TIME_SRC src/picomesh/platform/time/windows.c)
else()
    set(PICOMESH_PLATFORM_TIME_SRC src/picomesh/platform/time/posix.c)
endif()

# ---- picomesh runtime ---------------------------------------------------
add_library(picomesh_runtime STATIC
    src/picomesh/core/result.c
    src/picomesh/core/ytrace.c
    src/picomesh/core/yspan.c
    src/picomesh/core/ytelemetry.c
    src/picomesh/core/ytelemetry_store.c
    src/picomesh/core/yperf.c
    src/picomesh/allocator/allocator.c
    src/picomesh/picoclass/class.c
    src/picomesh/picoclass/rpc.c
    src/picomesh/picoclass/jinvoke.c
    src/picomesh/picoclass/minvoke.c
    src/picomesh/picoclass/yheaders.c
    src/picomesh/security/sha256.c
    src/picomesh/security/base64.c
    src/picomesh/security/jwt.c
    src/picomesh/security/jwt_verifier.c
    src/picomesh/security/secret.c
    src/picomesh/authenticators/registry.c
    src/picomesh/authenticators/session_cookie/session_cookie.c
    src/picomesh/authenticators/bearer_jwt_token/bearer_jwt_token.c
    src/picomesh/authenticators/bearer_opaque_token/bearer_opaque_token.c
    src/picomesh/authorizers/registry.c
    src/picomesh/authorizers/policy/policy.c
    src/picomesh/authorizers/none/none.c
    src/picomesh/msgpack/msgpack.c
    src/cmp/cmp.c
    src/picomesh/picoco/coro.c
    src/picomesh/loop/loop.c
    src/picomesh/loop/exec.c
    src/picomesh/engine/engine.c
    src/picomesh/engine/resolve.c
    src/picomesh/frontends/yrpc/yrpc.c
    src/picomesh/frontends/yttp/yttp.c
    src/picomesh/frontends/yhttp/yhttp.c
    src/picomesh/frontends/yhttp/frontend.c
    src/picomesh/frontends/alpine/alpine.c
    src/picomesh/frontends/msgpack/msgpack.c
    src/picomesh/frontends/cli/cli.c
    src/picohttpparser/picohttpparser.c
    src/picomesh/config/config.c
    src/picomesh/argv/argv.c
    src/picomesh/json/json.c
    src/yyjson/yyjson.c
    ${PICOMESH_PLATFORM_TIME_SRC}
)
target_include_directories(picomesh_runtime PUBLIC
    include
    include/uthash
    include/picohttpparser
)
# Vendored yyjson (single-file C JSON parser, MIT). Only the JSON shim
# (src/picomesh/json/json.c) needs the header, so keep it PRIVATE; SYSTEM so
# the third-party header/source don't trip the tree's -Wall -Wextra.
target_include_directories(picomesh_runtime SYSTEM PRIVATE include/yyjson)
set_source_files_properties(src/yyjson/yyjson.c PROPERTIES COMPILE_OPTIONS "-w")
# Vendored MessagePack codec (cmp, MIT). SYSTEM so its header doesn't trip the
# tree's -Wall -Wextra; the .c is third-party, compiled with warnings off.
target_include_directories(picomesh_runtime SYSTEM PUBLIC src/cmp)
set_source_files_properties(src/cmp/cmp.c PROPERTIES COMPILE_OPTIONS "-w")

# All plugin public headers now land in include/picomesh/plugin/<name>/
# (codegen output target). They're reachable through the repo-wide
# `include/` already PUBLIC-included above — no plugin-specific
# include directories needed.
target_link_libraries(picomesh_runtime PUBLIC co uv yaml Threads::Threads)
target_compile_definitions(picomesh_runtime PUBLIC _GNU_SOURCE)

# ---- codegen helper --------------------------------------------------
#
# picomesh_add_plugin(<name>
#                  SOURCES <annotated-c-files...>)
#
# Drives src/picomesh/picoclass/gen/codegen.py for one plugin module.
#
# Layout invariant:
#   - PUBLIC generated headers land in  include/picomesh/plugin/<name>/   (repo-wide include root)
#   - INTERNAL .gen.c files land next to the annotated sources under  src/picomesh/plugins/<name>/
#
# Defines an OBJECT library `picomesh_plugin_<name>` linked into the
# driver binary.
function(picomesh_add_plugin NAME)
    cmake_parse_arguments(PLUG "" "" "SOURCES;EXTRA_INCLUDES" ${ARGN})
    set(_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_PUB_DIR "${PICOMESH_ROOT}/include/picomesh/plugin/${NAME}")
    file(MAKE_DIRECTORY "${_PUB_DIR}")

    # EXTRA_INCLUDES are dirs the annotated source needs at AST-dump
    # time (codegen runs clang -fsyntax-only) AND at compile time.
    # Each path is joined with ':' for the PICOMESH_CODEGEN_EXTRA_INCLUDES
    # env var that codegen.py splits. Plugins compose via each other's
    # PUBLIC generated stubs (e.g. session.c -> storage_set) and
    # those all live under <repo>/include/picomesh/plugin/*, which is
    # part of PICOMESH_ROOT/include — so we don't need to enumerate
    # sibling plugin include dirs here anymore.
    # An EXTRA_INCLUDES entry may be a generator expression that expands to a
    # MULTI-DIR list at GENERATE time (e.g. libgit2's INTERFACE_INCLUDE_DIRECTORIES,
    # which also pulls in zlib-ng). A plain configure-time ':'-concat would leave
    # the genex's ';' separators in the path, so collect into a CMake list and
    # JOIN with ':' via a genex — that flattens any expanded sublist into the
    # single ':'-separated string codegen.py splits.
    set(_EXTRA_INCLUDE_DIRS "${PICOMESH_ROOT}/include")
    foreach(_d ${PLUG_EXTRA_INCLUDES})
        list(APPEND _EXTRA_INCLUDE_DIRS "${_d}")
    endforeach()
    set(_EXTRA_INCLUDE_PATH "$<JOIN:${_EXTRA_INCLUDE_DIRS},:>")

    # Outputs that codegen.py writes for this plugin:
    #   include/picomesh/plugin/<name>/<name>.h       — combined public API
    #   src/picomesh/plugins/<name>/<name>.internal.h — codegen-only types
    #   src/picomesh/plugins/<name>/<stem>.gen.c      — class-accessor body
    #   src/picomesh/plugins/<name>/methods.gen.c     — public-stub bodies
    #   src/picomesh/plugins/<name>/rpc.gen.c         — skel + jinvoke + create
    #   src/picomesh/plugins/<name>/model.yaml        — parsed model echo
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

    if(PICOMESH_ENABLE_CODEGEN)
        add_custom_command(
            OUTPUT ${_GEN_PUBLIC} ${_GEN_INTERNAL}
            COMMAND ${CMAKE_COMMAND} -E env
                "PICOMESH_CODEGEN_EXTRA_INCLUDES=${_EXTRA_INCLUDE_PATH}"
                uv run ${PICOMESH_ROOT}/src/picomesh/picoclass/gen/codegen.py
                    ${NAME}
                    ${PICOMESH_ROOT}/include
                    ${_SRC_DIR}
                    ${_SOURCES_FULL}
            DEPENDS ${_SOURCES_FULL} ${PICOMESH_ROOT}/src/picomesh/picoclass/gen/codegen.py
            WORKING_DIRECTORY ${_SRC_DIR}
            COMMENT "codegen[${NAME}]: regenerating public/internal artefacts"
            VERBATIM
        )
        add_custom_target(picomesh_plugin_${NAME}_codegen
                          DEPENDS ${_GEN_PUBLIC} ${_GEN_INTERNAL})
    endif()

    # The annotated <stem>.c #include's its sibling <stem>.gen.c, so we
    # don't add the .gen.c files separately — only methods.gen.c and
    # rpc.gen.c are independent translation units.
    add_library(picomesh_plugin_${NAME} OBJECT
        ${_SOURCES_FULL}
        ${_SRC_DIR}/methods.gen.c
        ${_SRC_DIR}/rpc.gen.c
    )
    # All plugins (and the rest of the tree) reach generated public
    # headers via <picomesh/plugin/<name>/...>, which resolves under
    # the repo-wide include/ root.
    target_include_directories(picomesh_plugin_${NAME} PUBLIC ${PICOMESH_ROOT}/include)
    target_link_libraries(picomesh_plugin_${NAME} PUBLIC picomesh_runtime)
endfunction()

# ---- plugins ---------------------------------------------------------
#
# Adding a new plugin: drop a dir under src/picomesh/plugins/<name>/
# with a CMakeLists.txt that calls `picomesh_add_plugin(<name> SOURCES …)`,
# then append <name> to PICOMESH_PLUGINS below.
set(PICOMESH_PLUGINS
    storage
    sharded_storage
    relational_storage
    calculator
    time
    accounts
    portalloc
    registry
    session
    password_authn
    github_authn
    token_issuer
    issues
    git_repo
    git_pipeline
    personal_access_tokens
    runner_agent
    trace_collector
    mesh
)
foreach(_p ${PICOMESH_PLUGINS})
    add_subdirectory(src/picomesh/plugins/${_p})
endforeach()

if(PICOMESH_ENABLE_CODEGEN)
    add_custom_target(picomesh_codegen)
    foreach(_p ${PICOMESH_PLUGINS})
        add_dependencies(picomesh_codegen picomesh_plugin_${_p}_codegen)
    endforeach()

    # Some plugins #include other plugins' generated headers. When explicitly
    # regenerating checked-in outputs, preserve producer-before-consumer order.
    foreach(_consumer session accounts password_authn token_issuer
                      personal_access_tokens issues git_repo git_pipeline
                      github_authn runner_agent mesh)
        add_dependencies(picomesh_plugin_${_consumer}_codegen
                         picomesh_plugin_storage_codegen
                         picomesh_plugin_sharded_storage_codegen)
    endforeach()

    add_dependencies(picomesh_plugin_token_issuer_codegen
                     picomesh_plugin_password_authn_codegen
                     picomesh_plugin_accounts_codegen)
    add_dependencies(picomesh_plugin_runner_agent_codegen
                     picomesh_plugin_token_issuer_codegen)
    add_dependencies(picomesh_plugin_git_repo_codegen
                     picomesh_plugin_accounts_codegen)
    add_dependencies(picomesh_plugin_github_authn_codegen
                     picomesh_plugin_accounts_codegen)
    add_dependencies(picomesh_plugin_issues_codegen
                     picomesh_plugin_git_repo_codegen)
    add_dependencies(picomesh_plugin_git_pipeline_codegen
                     picomesh_plugin_git_repo_codegen)
    add_dependencies(picomesh_plugin_mesh_codegen
                     picomesh_plugin_registry_codegen
                     picomesh_plugin_portalloc_codegen)
endif()

# ---- driver entry point (main) -------------------------------------
#
# The main() now lives under src/picomesh/main/ — see that module's
# CMakeLists for the rationale (picomesh/<name>/ is the only sanctioned
# place for compiled picomesh sources; bin/ used to host the driver but
# was a wart).
add_subdirectory(src/picomesh/main)
# ---- driver binary ---------------------------------------------------
#
# Plugin object libraries are linked as *direct sources* of the
# executable rather than via target_link_libraries — object-file
# sources never get dropped by the linker, so the codegen constructor
# hooks (`__attribute__((constructor))`) fire reliably. The driver
# entry point (picomesh_main) gets the same treatment.
set(PICOMESH_PLUGIN_OBJECTS "")
set(PICOMESH_PLUGIN_TARGETS "")
foreach(_p ${PICOMESH_PLUGINS})
    list(APPEND PICOMESH_PLUGIN_OBJECTS  $<TARGET_OBJECTS:picomesh_plugin_${_p}>)
    list(APPEND PICOMESH_PLUGIN_TARGETS  picomesh_plugin_${_p})
endforeach()

add_executable(picomesh
    $<TARGET_OBJECTS:picomesh_main>
    ${PICOMESH_PLUGIN_OBJECTS}
    src/picomesh/platform/longjmp_chk_stub.c)
add_dependencies(picomesh picomesh_main ${PICOMESH_PLUGIN_TARGETS})
# Plugins are consumed as raw $<TARGET_OBJECTS:>, so their PUBLIC link deps do
# NOT propagate — the plugin objects' external libs must be named here too
# (libgit2 for git_repo, libcurl for github_authn's GitHub OAuth HTTPS calls).
target_link_libraries(picomesh PRIVATE picomesh_runtime sqlite3 libmdbx libgit2 libcurl)

# ---- picoforge-webapp (standalone HTML web app) --------------------------
#
# The picoforge page tier: a separate binary that mirrors the yaapp
# git-yaapp page server — own HTTP listener, talks to a gateway via
# POST /_rpc. No plugins, no mesh, no picoclass dispatch — pure HTTP-client +
# template-renderer process. Designed to work against either the picomesh
# C gateway or the yaapp Python gateway interchangeably (the `/_rpc`
# wire shape is the contract).
add_executable(picoforge-webapp
    src/picoforge/webapp/main.c
    src/picoforge/webapp/webapp.c
    src/picoforge/webapp/http_client.c
    src/picomesh/platform/longjmp_chk_stub.c
)
target_link_libraries(picoforge-webapp PRIVATE picomesh_runtime)
target_include_directories(picoforge-webapp PRIVATE
    src/picoforge/webapp
    include/picohttpparser
)

# ---- picotrace (internal trace browser) ------------------------------
#
# Generic Picomesh core app for browsing the trace collector. It binds
# loopback by default and talks directly to trace_collector over yrpc; it is
# deliberately separate from the public Picoforge webapp sidecar.
# Embed the vendored ECharts bundle into the binary so picotrace serves it
# locally — no CDN, works in an airgapped environment.
set(PICOTRACE_ECHARTS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/picomesh/core-apps/picotrace/vendor/echarts.min.js)
set(PICOTRACE_ECHARTS_GEN ${CMAKE_CURRENT_BINARY_DIR}/picotrace_echarts.c)
add_custom_command(
    OUTPUT ${PICOTRACE_ECHARTS_GEN}
    COMMAND ${CMAKE_COMMAND}
        -DBIN2C_SRC=${PICOTRACE_ECHARTS_SRC}
        -DBIN2C_DST=${PICOTRACE_ECHARTS_GEN}
        -DBIN2C_SYM=picotrace_echarts_js
        -P ${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/bin2c.cmake
    DEPENDS ${PICOTRACE_ECHARTS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/build-tools/cmake/bin2c.cmake
    COMMENT "Embedding vendored echarts.min.js into picotrace"
    VERBATIM)

add_executable(picotrace
    src/picomesh/core-apps/picotrace/main.c
    ${PICOTRACE_ECHARTS_GEN}
    $<TARGET_OBJECTS:picomesh_plugin_trace_collector>
    src/picomesh/platform/longjmp_chk_stub.c
)
target_link_libraries(picotrace PRIVATE picomesh_runtime)
# The per-plugin codegen targets only exist when PICOMESH_ENABLE_CODEGEN is ON
# (see the guarded block above). picotrace links trace_collector via
# TARGET_OBJECTS, which does not carry the codegen ordering dependency, so add
# it explicitly — but ONLY when codegen is enabled, else a normal build
# references a non-existent target and fails to configure.
if(PICOMESH_ENABLE_CODEGEN)
    add_dependencies(picotrace picomesh_plugin_trace_collector_codegen)
endif()
target_include_directories(picotrace PRIVATE
    src/picomesh/core-apps/picotrace
    include/picohttpparser
)

# ---- tests (smoke) ---------------------------------------------------
add_subdirectory(tests)
