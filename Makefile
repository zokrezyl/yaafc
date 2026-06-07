# picomesh — yet another application framework in C
#
# Top-level wrapper around CMake/Ninja. The build name format is
# `build-<platform>-<config>` to match the yetty convention.
#
# Targets (run `make` with no args to list):
#   build-desktop-release    — release build (default platform = host)
#   build-desktop-debug      — debug build
#   build-desktop-asan       — debug build + AddressSanitizer
#   compile_commands.json    — symlink to the last configured build database
#   clean                    — wipe every build-* directory
#   help                     — this list

CMAKE   ?= cmake
NINJA   ?= ninja
JOBS    ?= $(shell nproc 2>/dev/null || echo 4)

BUILD_DIR_RELEASE := build-desktop-release
BUILD_DIR_DEBUG   := build-desktop-debug
BUILD_DIR_ASAN    := build-desktop-asan
BUILD_DIR_RISCV   := build-linux-riscv64-release
BUILD_DIR_YEMU    := build-yemu-release
BUILD_DIR_DEPLOY  := build-deploy
BUILD_DIR_CODEGEN := build-codegen
BUILD_DIR_WEBASM_YEMU := build-webasm-yemu-release

.PHONY: help all build-desktop-release build-desktop-debug build-desktop-asan build-linux-riscv64-release build-deploy build-yemu-release run-qemu build-webasm-yemu-release run-node run-webserver run-codegen perf-picoforge perf-throughput-notracing perf-throughput-tracing clean

# AddressSanitizer flags. Frame pointers for readable traces; the same flags
# go on compile AND link so the asan runtime is pulled in. The vendored static
# 3rdparty libs aren't instrumented (built separately), so asan covers the
# picomesh code + the boundaries — which is what we debug. The libco coroutine
# stacks confuse asan's stack-use-after-return check; run with
# `ASAN_OPTIONS=detect_stack_use_after_return=0` if it false-positives there.
ASAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer

# picoforge perf tunables (override on the command line, e.g.
#   make perf-picoforge DURATION=30 CONNECTIONS=128 GENERATORS=12)
# GENERATORS = parallel load-generator processes; CONNECTIONS is PER generator.
# A single generator caps ~9k req/s, so several are needed to saturate the mesh.
GENERATORS       ?= 8
CONNECTIONS      ?= 256
DURATION         ?= 60
REPOS_PER_WORKER ?= 8
SEED_USERS       ?= 0

help:
	@awk 'BEGIN{FS=":"} /^## / {sub(/^## /,""); print "  " $$0}' $(MAKEFILE_LIST)

## run-codegen            regenerate checked-in plugin codegen outputs
run-codegen:
	$(CMAKE) -S . -B $(BUILD_DIR_CODEGEN) -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DPICOMESH_ENABLE_CODEGEN=ON
	$(CMAKE) --build $(BUILD_DIR_CODEGEN) --target picomesh_codegen --parallel $(JOBS)

## build-desktop-release  configure + build the release variant
build-desktop-release:
	$(CMAKE) -S . -B $(BUILD_DIR_RELEASE) -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ln -sfn $(BUILD_DIR_RELEASE)/compile_commands.json compile_commands.json
	$(CMAKE) --build $(BUILD_DIR_RELEASE) --parallel $(JOBS)

## build-desktop-debug    configure + build the debug variant
build-desktop-debug:
	$(CMAKE) -S . -B $(BUILD_DIR_DEBUG) -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ln -sfn $(BUILD_DIR_DEBUG)/compile_commands.json compile_commands.json
	$(CMAKE) --build $(BUILD_DIR_DEBUG) --parallel $(JOBS)

## build-desktop-asan     configure + build a debug variant with AddressSanitizer
build-desktop-asan:
	$(CMAKE) -S . -B $(BUILD_DIR_ASAN) -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="$(ASAN_FLAGS)" \
		-DCMAKE_CXX_FLAGS="$(ASAN_FLAGS)" \
		-DCMAKE_EXE_LINKER_FLAGS="$(ASAN_FLAGS)" \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ln -sfn $(BUILD_DIR_ASAN)/compile_commands.json compile_commands.json
	$(CMAKE) --build $(BUILD_DIR_ASAN) --parallel $(JOBS)

## perf-picoforge          mixed-load perf on an INDEPENDENT mesh: GENERATORS
##                          parallel load processes, CONNECTIONS each, every
##                          connection a self-registered user issuing a random
##                          op stream (read/KV/file/issue/run/repo/login) for
##                          DURATION secs; throughput is summed across all
##                          generators. One generator caps ~9k req/s, so the
##                          default fans out to saturate the mesh (~50k mixed).
##                          Port-shifted (9xxx) + isolated storage, never
##                          touches a running dev stack.
##                          Tunables: GENERATORS CONNECTIONS DURATION REPOS_PER_WORKER
perf-picoforge: build-desktop-release
	GENERATORS=$(GENERATORS) SEED_USERS=$(SEED_USERS) CONNECTIONS=$(CONNECTIONS) \
	DURATION=$(DURATION) REPOS_PER_WORKER=$(REPOS_PER_WORKER) \
		bash tests/performance/picoforge/perf-run.sh

# --- Throughput A/B: same load, tracing the ONLY variable -------------------
# These two targets are deliberately identical except for PICOMESH_TELEMETRY.
# Run both, compare the `throughput : N req/s` line each prints — the delta IS
# the cost of per-span trace shipping to trace_collector, nothing else. Same
# tunables apply (GENERATORS CONNECTIONS DURATION REPOS_PER_WORKER), so to make
# the comparison meaningful pass the SAME values to both, e.g.:
#   make perf-throughput-notracing GENERATORS=8 CONNECTIONS=256 DURATION=30
#   make perf-throughput-tracing   GENERATORS=8 CONNECTIONS=256 DURATION=30

## perf-throughput-notracing  baseline throughput, tracing OFF (PICOMESH_TELEMETRY=off):
##                          measures raw service cost with no span shipping.
##                          Same mesh/load as perf-picoforge.
perf-throughput-notracing: build-desktop-release
	PICOMESH_TELEMETRY=off \
	GENERATORS=$(GENERATORS) SEED_USERS=$(SEED_USERS) CONNECTIONS=$(CONNECTIONS) \
	DURATION=$(DURATION) REPOS_PER_WORKER=$(REPOS_PER_WORKER) \
		bash tests/performance/picoforge/perf-run.sh

## perf-throughput-tracing    throughput with tracing ON (PICOMESH_TELEMETRY=on):
##                          every span is batched + shipped to trace_collector.
##                          Same mesh/load as perf-throughput-notracing — diff
##                          the two throughputs to read the tracing cost.
perf-throughput-tracing: build-desktop-release
	PICOMESH_TELEMETRY=on \
	GENERATORS=$(GENERATORS) SEED_USERS=$(SEED_USERS) CONNECTIONS=$(CONNECTIONS) \
	DURATION=$(DURATION) REPOS_PER_WORKER=$(REPOS_PER_WORKER) \
		bash tests/performance/picoforge/perf-run.sh

## build-deploy             stage build-deploy/ — host-runnable picomesh tree
##                          (binary + yaml + frontend + run.sh). Reused
##                          verbatim by build-yemu-release as the in-VM
##                          /opt/picoforge payload.
build-deploy: build-desktop-release
	bash tools/picoforge/deploy/stage.sh

## build-yemu-release       bake the riscv64 VM rootfs at
##                          build-yemu-release/ (kernel + opensbi +
##                          alpine-rootfs.img with /opt/picoforge/
##                          injected from build-deploy/). Needs sudo
##                          for losetup/mount.
##
## Prereqs:
##   - make build-linux-riscv64-release   (cross-compiled picomesh binary)
##   - make build-deploy                  (host deploy tree)
build-yemu-release: build-linux-riscv64-release build-deploy
	bash tools/picoforge/yemu/build-image.sh

## run-qemu                 rebuild the riscv64 image from CURRENT source, then
##                          boot it in qemu (gateway → host :18080, webapp →
##                          host :18081). Depends on build-yemu-release so it
##                          ALWAYS bakes the latest binaries — never boots a
##                          stale image (the rebake needs sudo for losetup/
##                          mount). Quit qemu with Ctrl-A X. Override SMP=/MEM=.
##                          Open http://127.0.0.1:18081/-/login after boot
##                          (default admin: root / rootpw, seeded by the probe).
##                          To boot WITHOUT rebuilding, run run-vm.sh directly.
run-qemu: build-yemu-release
	bash tools/picoforge/yemu/run-vm.sh

## build-webasm-yemu-release  compile tinyemu to wasm (picomesh-yemu.{js,wasm})
##                            for the in-browser demo at
##                            build-webasm-yemu-release/. Stages kernel +
##                            opensbi + alpine-rootfs from build-yemu-release/
##                            under build-webasm-yemu-release/assets/.
##
## Prereqs:
##   - make build-yemu-release          (VM rootfs + kernel)
##   - Emscripten SDK installed at $$HOME/.local/emsdk (or EMSDK=…)
build-webasm-yemu-release:
	bash tools/picoforge/yemu/web/build.sh

## run-node                 boot the riscv64 Linux VM INSIDE Node.js (no qemu,
##                          no browser) and proxy the in-guest services to the
##                          host as plain HTTP:
##                            guest :8080 gateway -> http://127.0.0.1:18080
##                            guest :8081 webapp  -> http://127.0.0.1:18081
##                          Builds the wasm VM first if missing. Default admin:
##                          root / rootpw (seeded by the probe). Ctrl-C to stop.
##                          --quiet passthrough: make run-node NODE_ARGS=--quiet
run-node:
	@if [ ! -f $(BUILD_DIR_WEBASM_YEMU)/picomesh-yemu.js ]; then \
		echo "==> wasm VM missing — building it first"; \
		$(MAKE) build-webasm-yemu-release; \
	fi
	node tools/picoforge/yemu/node/picomesh-vm.cjs $(NODE_ARGS)

## run-webserver            serve the wasm bundle over HTTP with python
##                          (the bundle's own serve.py, which sets the
##                          COOP/COEP headers the wasm needs) — exactly how the
##                          GitHub Pages deploy is served. Builds the bundle if
##                          missing. Open http://127.0.0.1:$(WEBSERVER_PORT)/
##                          and the page boots the VM + runs a startup smoke.
##                          Override the port: make run-webserver WEBSERVER_PORT=9000
WEBSERVER_PORT ?= 8000
run-webserver:
	@if [ ! -f $(BUILD_DIR_WEBASM_YEMU)/picomesh-yemu.js ]; then \
		echo "==> wasm bundle missing — building it first"; \
		$(MAKE) build-webasm-yemu-release; \
	fi
	@echo "==> serving $(BUILD_DIR_WEBASM_YEMU) at http://127.0.0.1:$(WEBSERVER_PORT)/  (Ctrl-C to stop)"
	python3 $(BUILD_DIR_WEBASM_YEMU)/serve.py $(WEBSERVER_PORT) $(BUILD_DIR_WEBASM_YEMU)

## build-linux-riscv64-release  cross-compile picomesh + picoforge-webapp for
##                              riscv64 (static, for the yemu demo VM
##                              or any riscv64 Linux target). Needs
##                              gcc-riscv64-linux-gnu / g++-riscv64-linux-gnu
##                              on the host (Ubuntu: apt install
##                              gcc-riscv64-linux-gnu g++-riscv64-linux-gnu).
build-linux-riscv64-release:
	$(CMAKE) -S . -B $(BUILD_DIR_RISCV) -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/build-tools/picomesh/cross/linux-riscv64.cmake
	$(CMAKE) --build $(BUILD_DIR_RISCV) --parallel $(JOBS)
	@file $(BUILD_DIR_RISCV)/picomesh 2>/dev/null || true

## clean                  wipe every build-*/ artifact directory
clean:
	rm -rf $(BUILD_DIR_RELEASE) $(BUILD_DIR_DEBUG) $(BUILD_DIR_RISCV) \
	       $(BUILD_DIR_YEMU) $(BUILD_DIR_DEPLOY) $(BUILD_DIR_CODEGEN) \
	       build-webasm-yemu-release compile_commands.json

