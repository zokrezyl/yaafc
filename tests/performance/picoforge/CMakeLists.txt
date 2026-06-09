# picoforge performance harness.
#
# Hybrid load generator: M OS threads, each running a libuv loop + libco
# coroutine scheduler with K coroutines, so C connections cost M threads (not
# C). It therefore links picomesh_runtime (loop + coroutines). It still only
# speaks the gateway's HTTP contract, so it also runs against the yaapp gateway.
add_executable(picoforge-perf
    picoforge-perf.c
    ${CMAKE_SOURCE_DIR}/src/picomesh/platform/longjmp_chk_stub.c)
target_link_libraries(picoforge-perf PRIVATE picomesh_runtime)
set_target_properties(picoforge-perf PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
