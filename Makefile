# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build
# make V=1 -> show the full compiler log while building
V ?= 0

.PHONY: all test unit lint clean crawl-regress

all:
	@VERBOSE=$(V) scripts/build.sh $(BUILD)

unit: all
	$(BUILD)/wc_net_test
	$(BUILD)/wc_graph_test
	$(BUILD)/wc_ui_test
	$(BUILD)/wc_query_test
	NO_COLOR=1 $(BUILD)/wc_views_test
	$(BUILD)/test_bridges_json tests/bridges/golden

# BFS empty-queue regression (fails until src/core/crawl.cc is patched; then move into unit)
crawl-regress: all
	$(BUILD)/wc_crawl_test

test: unit
	tests/run.sh $(BUILD)/wc tests/crawl

lint:
	@scripts/lint.sh

clean:
	rm -rf $(BUILD)
