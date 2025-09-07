.PHONY: all build clean install run test

# Simple wrapper around CMake for convenience.

BUILD_DIR ?= build
CONFIGURE_ARGS ?=
BUILD_ARGS ?=
INSTALL_PREFIX ?=

all: build

build:
	cmake -S . -B $(BUILD_DIR) $(CONFIGURE_ARGS)
	cmake --build $(BUILD_DIR) $(BUILD_ARGS)

install: build
	cmake --install $(BUILD_DIR) $(if $(INSTALL_PREFIX),--prefix $(INSTALL_PREFIX),)

clean:
	rm -rf $(BUILD_DIR)

# Convenience: build and run the viewer with a file
run: build
	./$(BUILD_DIR)/json-view $(ARGS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

