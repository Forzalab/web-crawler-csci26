# Thin wrapper so `make` works out of habit. The real build is CMake.
BUILD ?= build
# make V=1 -> show the full compiler log while building
V ?= 0

.PHONY: all test unit lint clean josh josh-full spec1 crawl-regress smoke smoke-offline smoke-live

all:
	@VERBOSE=$(V) scripts/build.sh $(BUILD)
	@cp -f $(BUILD)/wc a.out    # ./a.out = build/wc (real copy; grader runs ./a.out)

unit: all
	$(BUILD)/wc_net_test
	$(BUILD)/wc_graph_test
	$(BUILD)/wc_crawl_test
	$(BUILD)/wc_parallel_test
	$(BUILD)/wc_hybrid_test
	$(BUILD)/wc_ui_test
	$(BUILD)/wc_query_test
	NO_COLOR=1 $(BUILD)/wc_views_test
	$(BUILD)/wc_matrix_test
	$(BUILD)/wc_report_pdf_test
	$(BUILD)/test_bridges_json tests/bridges/golden

# BFS empty-queue regression only (also part of `make unit`)
crawl-regress: all
	$(BUILD)/wc_crawl_test

test: unit
	tests/team/spec1_selftest.sh -q
	tests/report/run.sh $(BUILD)/wc
	tests/run.sh $(BUILD)/wc tests/crawl

# TUI smoke suite (tests/smoke/): drives build/wc in tmux panes. FILTER = regex on case names.
smoke: all
	BIN=$(BUILD)/wc tests/smoke/run.sh $(FILTER)

smoke-offline: all
	BIN=$(BUILD)/wc tests/smoke/run.sh --offline $(FILTER)
	BIN=$(BUILD)/wc tests/smoke/tear_check.sh
	BIN=$(BUILD)/wc tests/smoke/stop_resume.sh

smoke-live: all
	BIN=$(BUILD)/wc tests/smoke/run.sh --live $(FILTER)

# Josh's josh_get_links() vs our fetcher, 12 cases (tests/team/). Not part of `make test`.
josh: all
	cmake --build $(BUILD) --target josh_test
	$(BUILD)/josh_test

# Static spec #1 check of src/team/josh.cc (boost tcp stream + readlib, not our fetch code).
# Self-test on the samples first (quiet), then the real file. No compiler needed.
spec1:
	@tests/team/spec1_selftest.sh -q
	@tests/team/spec1_check.sh src/team/josh.cc

# Static check, then the runtime cases.
josh-full: spec1
	@$(MAKE) --no-print-directory josh

lint:
	@scripts/lint.sh

clean:
	rm -rf $(BUILD) a.out
