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

.PHONY: help all build-desktop-release build-desktop-debug clean

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

