# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build
# make V=1 -> show the full compiler log while building
V ?= 0

.PHONY: all test unit clean

all:
	@VERBOSE=$(V) scripts/build.sh $(BUILD)

unit: all
	$(BUILD)/wc_net_test
	$(BUILD)/test_bridges_json tests/bridges/golden

test: unit
	tests/run.sh $(BUILD)/wc tests/crawl

clean:
	rm -rf $(BUILD)
