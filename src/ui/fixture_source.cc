#include "ui/fixture_source.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

#include "mocks/mock_fetcher.h"

namespace wc {

namespace {

class SlowMock : public LinkSource {
public:
    SlowMock(FakeWeb web, int delay_ms) : mock_(std::move(web)), delay_ms_(delay_ms) {}
    std::optional<std::vector<std::string>> links(const std::string& url) override {
        if (delay_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        return mock_.links(url);
    }

private:
    MockFetcher mock_;
    int delay_ms_;
};

}  // namespace

std::unique_ptr<LinkSource> fixture_source_from_env() {
    const char* path = std::getenv("WC_FIXTURE");
    if (!path || !*path) return nullptr;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "WC_FIXTURE: cannot open " << path << '\n';
        std::exit(2);
    }
    PipeInput input;
    std::string err = parse_pipe_input(in, input);
    if (!err.empty()) {
        std::cerr << "WC_FIXTURE " << path << ": " << err << '\n';
        std::exit(2);
    }
    const char* d = std::getenv("WC_FIXTURE_DELAY_MS");
    return std::make_unique<SlowMock>(std::move(input.web), d ? std::atoi(d) : 0);
}

}  // namespace wc
