# Convenience make targets for building, testing, and rendering the Rexy book.
PYTHON ?= python3

.PHONY: build test build-macos-arm64 test-macos-arm64 package-macos-arm64 \
	install-rxy clean

# FR-054: install rxy into a user-scope prefix (default ~/.local) so the
# binary stays put across rebuilds of the workspace. Override the location
# with `make install-rxy PREFIX=/usr/local`.
PREFIX ?= $(HOME)/.local

install-rxy: build-macos-arm64
	cmake --install build/macos-arm64-release --component rxy --prefix "$(PREFIX)"
	@echo "rxy installed to $(PREFIX)/bin/rxy"
	@echo "ensure $(PREFIX)/bin is on your PATH"

build:
	cmake -S . -B build
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

clean: clean-docs
	rm -rf build

build-macos-arm64:
	cmake --preset macos-arm64-release
	cmake --build --preset macos-arm64-release

test-macos-arm64: build-macos-arm64
	ctest --preset macos-arm64-release --output-on-failure

package-macos-arm64:
	./scripts/package_macos_arm64.sh

include docs/build.mk
