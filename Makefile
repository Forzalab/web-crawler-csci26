# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build

.PHONY: all test unit clean

all:
	cmake -S . -B $(BUILD)
	cmake --build $(BUILD) -j

unit: all
	$(BUILD)/wc_net_test
	$(BUILD)/test_bridges_json tests/bridges/golden

test: unit
	tests/run.sh $(BUILD)/wc tests/crawl

clean:
	rm -rf $(BUILD)
