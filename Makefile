# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build
# make V=1 -> show the full compiler log while building
V ?= 0

.PHONY: all test unit lint clean crawl-regress smoke smoke-offline smoke-live

all:
	@VERBOSE=$(V) scripts/build.sh $(BUILD)

unit: all
	$(BUILD)/wc_net_test
	$(BUILD)/wc_graph_test
	$(BUILD)/wc_crawl_test
	$(BUILD)/wc_ui_test
	$(BUILD)/wc_query_test
	NO_COLOR=1 $(BUILD)/wc_views_test
	$(BUILD)/wc_matrix_test
	$(BUILD)/test_bridges_json tests/bridges/golden

# BFS empty-queue regression only (also part of `make unit`)
crawl-regress: all
	$(BUILD)/wc_crawl_test

test: unit
	tests/run.sh $(BUILD)/wc tests/crawl

# TUI smoke suite (tests/smoke/): drives build/wc in tmux panes. FILTER = regex on case names.
smoke: all
	BIN=$(BUILD)/wc tests/smoke/run.sh $(FILTER)

smoke-offline: all
	BIN=$(BUILD)/wc tests/smoke/run.sh --offline $(FILTER)

smoke-live: all
	BIN=$(BUILD)/wc tests/smoke/run.sh --live $(FILTER)

lint:
	@scripts/lint.sh

clean:
	rm -rf $(BUILD)
