# Convenience make targets for building, testing, and rendering the Rexc book.
PYTHON ?= python3

.PHONY: build test

build:
	cmake -S . -B build
	cmake --build build

test: build
	ctest --test-dir build --output-on-failure

include docs/build.mk
