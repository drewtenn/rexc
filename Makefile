# Convenience make targets for building, testing, and rendering the Rexy book.
PYTHON ?= python3

.PHONY: build test build-macos-arm64 test-macos-arm64 package-macos-arm64

build:
	cmake -S . -B build
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

build-macos-arm64:
	cmake --preset macos-arm64-release
	cmake --build --preset macos-arm64-release

test-macos-arm64: build-macos-arm64
	ctest --preset macos-arm64-release --output-on-failure

package-macos-arm64:
	./scripts/package_macos_arm64.sh

include docs/build.mk
