cmake_minimum_required(VERSION 3.20)
project(picomesh C CXX)

# C++17 is the floor (simdjson requires it). The C bits stay C17; only
# src/picomesh/yjson/yjson.cpp gets compiled as C++.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PICOMESH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

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
include(build-tools/picomesh/libs/co.cmake)
include(build-tools/picomesh/libs/uv.cmake)
include(build-tools/picomesh/libs/yaml.cmake)
include(build-tools/picomesh/libs/simdjson.cmake)
include(build-tools/picomesh/libs/sqlite3.cmake)
include(build-tools/picomesh/libs/libmdbx.cmake)
include(build-tools/picomesh/libs/libgit2.cmake)

# ---- yplatform backend selection -------------------------------------
#
# yplatform abstractions get one backend file per platform. Backends
# under src/picomesh/yplatform/<module>/<platform>.c. Today only `time`
# is platform-split; future modules (paths, threads, sockets) follow
# the same shape.
if(WIN32)
    set(PICOMESH_YPLATFORM_TIME_SRC src/picomesh/yplatform/time/windows.c)
else()
    set(PICOMESH_YPLATFORM_TIME_SRC src/picomesh/yplatform/time/posix.c)
endif()

# ---- picomesh runtime ---------------------------------------------------
add_library(picomesh_runtime STATIC
    src/picomesh/ycore/result.c
    src/picomesh/ycore/ytrace.c
    src/picomesh/ycore/yspan.c
    src/picomesh/ycore/ytelemetry.c
    src/picomesh/ycore/ytelemetry_store.c
    src/picomesh/ycore/yperf.c
    src/picomesh/yclass/class.c
    src/picomesh/yclass/rpc.c
    src/picomesh/yclass/jinvoke.c
    src/picomesh/yclass/minvoke.c
    src/picomesh/yclass/yheaders.c
    src/picomesh/ysecurity/sha256.c
    src/picomesh/ysecurity/base64.c
    src/picomesh/ysecurity/jwt.c
    src/picomesh/ysecurity/jwt_verifier.c
    src/picomesh/ysecurity/secret.c
    src/picomesh/authenticators/registry.c
    src/picomesh/authenticators/session_cookie/session_cookie.c
    src/picomesh/authenticators/bearer_jwt_token/bearer_jwt_token.c
    src/picomesh/authenticators/bearer_opaque_token/bearer_opaque_token.c
    src/picomesh/authorizers/registry.c
    src/picomesh/authorizers/policy/policy.c
    src/picomesh/authorizers/none/none.c
    src/picomesh/msgpack/msgpack.c
    vendor/cmp/cmp.c
    src/picomesh/yco/coro.c
    src/picomesh/yloop/yloop.c
    src/picomesh/yengine/engine.c
    src/picomesh/yengine/resolve.c
    src/picomesh/frontends/yrpc/yrpc.c
    src/picomesh/frontends/yttp/yttp.c
    src/picomesh/frontends/yhttp/yhttp.c
    src/picomesh/frontends/yhttp/frontend.c
    src/picomesh/frontends/alpine/alpine.c
    src/picomesh/frontends/msgpack/msgpack.c
    src/picomesh/frontends/cli/cli.c
    src/picohttpparser/picohttpparser.c
    src/picomesh/yconfig/yconfig.c
    src/picomesh/yargv/yargv.c
    src/picomesh/yjson/yjson.cpp
    ${PICOMESH_YPLATFORM_TIME_SRC}
)
target_include_directories(picomesh_runtime PUBLIC
    include
    include/uthash
    include/picohttpparser
)
# Vendored MessagePack codec (cmp, MIT). SYSTEM so its header doesn't trip the
# tree's -Wall -Wextra; the .c is third-party, compiled with warnings off.
target_include_directories(picomesh_runtime SYSTEM PUBLIC vendor/cmp)
set_source_files_properties(vendor/cmp/cmp.c PROPERTIES COMPILE_OPTIONS "-w")

# All plugin public headers now land in include/picomesh/plugin/<name>/
# (codegen output target). They're reachable through the repo-wide
# `include/` already PUBLIC-included above — no plugin-specific
# include directories needed.
target_link_libraries(picomesh_runtime PUBLIC co uv yaml simdjson Threads::Threads)
target_compile_definitions(picomesh_runtime PUBLIC _GNU_SOURCE)

# ---- codegen helper --------------------------------------------------
#
# picomesh_add_plugin(<name>
#                  SOURCES <annotated-c-files...>)
#
# Drives src/picomesh/yclass/gen/codegen.py for one plugin module.
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

    add_custom_command(
        OUTPUT ${_GEN_PUBLIC} ${_GEN_INTERNAL}
        COMMAND ${CMAKE_COMMAND} -E env
            "PICOMESH_CODEGEN_EXTRA_INCLUDES=${_EXTRA_INCLUDE_PATH}"
            uv run ${PICOMESH_ROOT}/src/picomesh/yclass/gen/codegen.py
                ${NAME}
                ${PICOMESH_ROOT}/include
                ${_SRC_DIR}
                ${_SOURCES_FULL}
        DEPENDS ${_SOURCES_FULL} ${PICOMESH_ROOT}/src/picomesh/yclass/gen/codegen.py
        WORKING_DIRECTORY ${_SRC_DIR}
        COMMENT "codegen[${NAME}]: regenerating public/internal artefacts"
        VERBATIM
    )
    add_custom_target(picomesh_plugin_${NAME}_codegen
                      DEPENDS ${_GEN_PUBLIC} ${_GEN_INTERNAL})

    # The annotated <stem>.c #include's its sibling <stem>.gen.c, so we
    # don't add the .gen.c files separately — only methods.gen.c and
    # rpc.gen.c are independent translation units.
    add_library(picomesh_plugin_${NAME} OBJECT
        ${_SOURCES_FULL}
        ${_SRC_DIR}/methods.gen.c
        ${_SRC_DIR}/rpc.gen.c
    )
    add_dependencies(picomesh_plugin_${NAME} picomesh_plugin_${NAME}_codegen)
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

# Some plugins #include other plugins' generated headers (session,
# accounts, password_authn etc. delegate to storage_sql). The codegen
# of each consumer must therefore run AFTER the codegen of every
# plugin it pulls a header from, otherwise the AST-dump step fails
# with "use of undeclared identifier" before the produces step has
# regenerated the header. List the producer→consumer edges here.
foreach(_consumer session accounts password_authn token_issuer
                  personal_access_tokens issues git_repo git_pipeline
                  github_authn runner_agent)
    add_dependencies(picomesh_plugin_${_consumer}_codegen
                     picomesh_plugin_storage_codegen)
    # These consume sharded_storage's generated header too (db_get/db_set/…
    # and now db_list), so their AST-dump must run after its codegen.
    add_dependencies(picomesh_plugin_${_consumer}_codegen
                     picomesh_plugin_sharded_storage_codegen)
endforeach()

# token_issuer.login delegates to password_authn and loads groups from
# accounts (issue #19), so it #includes their generated headers — its
# AST-dump must run after both have regenerated.
add_dependencies(picomesh_plugin_token_issuer_codegen
                 picomesh_plugin_password_authn_codegen
                 picomesh_plugin_accounts_codegen)

# runner_agent.exchange mints a runner JWT via token_issuer, so it #includes
# token_issuer's generated header — AST-dump must run after token_issuer's codegen.
add_dependencies(picomesh_plugin_runner_agent_codegen
                 picomesh_plugin_token_issuer_codegen)

# The runtime's frontend (yhttp/frontend.c) and ymain's smoke `client`
# subcommand both pull in plugin public headers. They must therefore
# wait for every plugin's codegen to have produced those headers
# before they get compiled.
foreach(_p ${PICOMESH_PLUGINS})
    add_dependencies(picomesh_runtime picomesh_plugin_${_p}_codegen)
endforeach()

# ---- driver entry point (ymain) -------------------------------------
#
# The main() now lives under src/picomesh/ymain/ — see that module's
# CMakeLists for the rationale (picomesh/<name>/ is the only sanctioned
# place for compiled picomesh sources; bin/ used to host the driver but
# was a wart).
add_subdirectory(src/picomesh/ymain)
foreach(_p ${PICOMESH_PLUGINS})
    add_dependencies(picomesh_ymain picomesh_plugin_${_p}_codegen)
endforeach()

# ---- driver binary ---------------------------------------------------
#
# Plugin object libraries are linked as *direct sources* of the
# executable rather than via target_link_libraries — object-file
# sources never get dropped by the linker, so the codegen constructor
# hooks (`__attribute__((constructor))`) fire reliably. The driver
# entry point (picomesh_ymain) gets the same treatment.
set(PICOMESH_PLUGIN_OBJECTS "")
set(PICOMESH_PLUGIN_TARGETS "")
foreach(_p ${PICOMESH_PLUGINS})
    list(APPEND PICOMESH_PLUGIN_OBJECTS  $<TARGET_OBJECTS:picomesh_plugin_${_p}>)
    list(APPEND PICOMESH_PLUGIN_TARGETS  picomesh_plugin_${_p})
endforeach()

add_executable(picomesh
    $<TARGET_OBJECTS:picomesh_ymain>
    ${PICOMESH_PLUGIN_OBJECTS}
    src/picomesh/yplatform/longjmp_chk_stub.c)
add_dependencies(picomesh picomesh_ymain ${PICOMESH_PLUGIN_TARGETS})
target_link_libraries(picomesh PRIVATE picomesh_runtime sqlite3 libmdbx libgit2)

# ---- picoforge-webapp (standalone HTML web app) --------------------------
#
# The picoforge page tier: a separate binary that mirrors the yaapp
# git-yaapp page server — own HTTP listener, talks to a gateway via
# POST /_rpc. No plugins, no mesh, no yclass dispatch — pure HTTP-client +
# template-renderer process. Designed to work against either the picomesh
# C gateway or the yaapp Python gateway interchangeably (the `/_rpc`
# wire shape is the contract).
add_executable(picoforge-webapp
    src/picoforge/webapp/main.c
    src/picoforge/webapp/webapp.c
    src/picoforge/webapp/http_client.c
    src/picomesh/yplatform/longjmp_chk_stub.c
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
add_executable(picotrace
    src/picomesh/core-apps/picotrace/main.c
    $<TARGET_OBJECTS:picomesh_plugin_trace_collector>
    src/picomesh/yplatform/longjmp_chk_stub.c
)
target_link_libraries(picotrace PRIVATE picomesh_runtime)
add_dependencies(picotrace picomesh_plugin_trace_collector_codegen)
target_include_directories(picotrace PRIVATE
    src/picomesh/core-apps/picotrace
    include/picohttpparser
)

# ---- tests (smoke) ---------------------------------------------------
add_subdirectory(tests)
