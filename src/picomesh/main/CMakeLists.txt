# picomesh/main — driver entry point.
#
# The `picomesh` executable is a thin shell: argv parsing, engine
# construction, and a small subcommand router. Everything substantive
# lives in the plugins / runtime. The main() lived under bin/ for a
# while; it's now a regular module under src/picomesh/<name>/ so it can
# participate in the build the same way every other picomesh module
# does, and is includable as an object library if a downstream binary
# (tests, embedding host) wants to swap the entry point.

add_library(picomesh_main OBJECT main.c autoport.c)
target_link_libraries(picomesh_main PUBLIC picomesh_runtime)
# main.c uses a couple of plugin public stubs (the legacy `client`
# smoke). All public headers live under
# <repo>/include/picomesh/plugin/<name>/, which is part of picomesh_runtime's
# PUBLIC include dirs — nothing to add here.
