# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build

.PHONY: all test clean

all:
	cmake -S . -B $(BUILD)
	cmake --build $(BUILD) -j

test: all
	$(BUILD)/test_bridges_json tests/bridges/golden
	tests/run.sh $(BUILD)/wc tests/crawl

clean:
	rm -rf $(BUILD)
