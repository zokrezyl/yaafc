# yaafc — yet another application framework in C
#
# Top-level wrapper around CMake/Ninja. The build name format is
# `build-<platform>-<config>` to match the yetty convention.
#
# Targets (run `make` with no args to list):
#   build-desktop-release    — release build (default platform = host)
#   build-desktop-debug      — debug build
#   compile_commands.json    — symlink to the last configured build database
#   clean                    — wipe every build-* directory
#   help                     — this list

CMAKE   ?= cmake
NINJA   ?= ninja
JOBS    ?= $(shell nproc 2>/dev/null || echo 4)

BUILD_DIR_RELEASE := build-desktop-release
BUILD_DIR_DEBUG   := build-desktop-debug
BUILD_DIR_RISCV   := build-linux-riscv64-release

.PHONY: help all build-desktop-release build-desktop-debug build-linux-riscv64-release build-webasm-yemu-release clean

help:
	@awk 'BEGIN{FS=":"} /^## / {sub(/^## /,""); print "  " $$0}' $(MAKEFILE_LIST)

## build-desktop-release  configure + build the release variant
build-desktop-release:
	@mkdir -p tmp
	$(CMAKE) -S . -B $(BUILD_DIR_RELEASE) -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON 
	@ln -sfn $(BUILD_DIR_RELEASE)/compile_commands.json compile_commands.json
	$(CMAKE) --build $(BUILD_DIR_RELEASE) --parallel $(JOBS) 
	@echo "build-desktop-release: see tmp/build-release.log"

## build-desktop-debug    configure + build the debug variant
build-desktop-debug:
	@mkdir -p tmp
	$(CMAKE) -S . -B $(BUILD_DIR_DEBUG) -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@ln -sfn $(BUILD_DIR_DEBUG)/compile_commands.json compile_commands.json
	$(CMAKE) --build $(BUILD_DIR_DEBUG) --parallel $(JOBS)
	@echo "build-desktop-debug: see tmp/build-debug.log"

## build-webasm-yemu-release  compile tinyemu to wasm (yaafc-yemu.{js,wasm})
##                            for the in-browser demo. Stages kernel +
##                            opensbi + alpine-rootfs from yemu/build/
##                            under web/build/assets/.
##
## Prereqs:
##   - ./Makefile build-linux-riscv64-release   (yaafc binaries)
##   - scenarios/git-yaafc/yemu/build-image.sh  (VM rootfs + kernel)
##   - Emscripten SDK installed at $$HOME/.local/emsdk (or EMSDK=…)
build-webasm-yemu-release:
	bash scenarios/git-yaafc/yemu/web/build.sh

## build-linux-riscv64-release  cross-compile yaafc + yaafc-frontend for
##                              riscv64 (static, for the yemu demo VM
##                              or any riscv64 Linux target). Needs
##                              gcc-riscv64-linux-gnu / g++-riscv64-linux-gnu
##                              on the host (Ubuntu: apt install
##                              gcc-riscv64-linux-gnu g++-riscv64-linux-gnu).
build-linux-riscv64-release:
	@mkdir -p tmp
	$(CMAKE) -S . -B $(BUILD_DIR_RISCV) -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/build-tools/yaafc/cross/linux-riscv64.cmake
	$(CMAKE) --build $(BUILD_DIR_RISCV) --parallel $(JOBS)
	@echo "build-linux-riscv64-release: see tmp/build-riscv.log"
	@file $(BUILD_DIR_RISCV)/yaafc 2>/dev/null || true

