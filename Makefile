.DEFAULT_GOAL := help

CMAKE ?= cmake
CTEST ?= ctest
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
BUILD_DIR ?= build
BUILD_TYPE ?= Debug
JOBS ?= $(shell getconf _NPROCESSORS_ONLN)
SOURCE ?= media/sample3.mp4

.PHONY: help configure build run test format format-check lint check clean

help:
	@printf '%s\n' \
		'make build                 Configure and build' \
		'make run SOURCE=video.mp4   Build and process a video' \
		'make test                  Build and run integration tests' \
		'make format                Format C++ sources and headers in place' \
		'make format-check          Check formatting without changing files' \
		'make lint                  Run clang-tidy (warnings fail the check)' \
		'make check                 Run format-check, lint, and tests' \
		'make clean                 Remove build outputs, keeping CMake configuration' \
		'' \
		'Overrides: BUILD_DIR=build BUILD_TYPE=Debug JOBS=4' \
		'           CMAKE=cmake CTEST=ctest CLANG_FORMAT=clang-format CLANG_TIDY=clang-tidy'

configure:
	"$(CMAKE)" -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
	"$(CMAKE)" --build "$(BUILD_DIR)" --parallel "$(JOBS)"

run: build
	"$(BUILD_DIR)/main" "$(SOURCE)"

test: build
	"$(CTEST)" --test-dir "$(BUILD_DIR)" --output-on-failure

format:
	find src tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | \
		xargs -0 "$(CLANG_FORMAT)" --style=file -i

format-check:
	find src tests -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | \
		xargs -0 "$(CLANG_FORMAT)" --style=file --dry-run --Werror

lint: configure
	find src tests -type f -name '*.cpp' -print0 | \
		xargs -0 "$(CLANG_TIDY)" -p "$(BUILD_DIR)"

check: format-check lint test

clean:
	@if test -f "$(BUILD_DIR)/CMakeCache.txt"; then \
		"$(CMAKE)" --build "$(BUILD_DIR)" --target clean; \
	fi
