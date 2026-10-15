// mock_fetcher.h - fake web for pipe mode and tests (A3 format), never touches the network.
#pragma once

#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/crawl.h"
#include "core/link_source.h"

namespace wc {

// url -> links (page order, duplicates kept). nullopt = listed with k = -1.
// A url that is not a key at all also fails.
using FakeWeb = std::unordered_map<std::string, std::optional<std::vector<std::string>>>;

struct PipeInput {
    CrawlConfig cfg;
    std::string start;
    FakeWeb web;
};

// Parses the whole A3 input:  MODE START MAXDEPTH MAXSIZE  P  {url k link*}*P
// Returns "" on success, otherwise the exact `ERROR ...` line to print.
std::string parse_pipe_input(std::istream& in, PipeInput& out);

class MockFetcher : public LinkSource {
public:
    explicit MockFetcher(FakeWeb web) : web_(std::move(web)) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override;

    // telemetry (stderr only, stdout stays deterministic)
    std::vector<std::string> call_log;  // every url asked, in order
    int misses = 0;                     // urls not listed in the fake web
    int failures = 0;                   // misses + k = -1 pages
    bool verbose = false;               // print each call to stderr with [mock]

private:
    FakeWeb web_;
};

}  // namespace wc
