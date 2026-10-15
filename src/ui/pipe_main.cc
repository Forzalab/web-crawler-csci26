// pipe_main.cc - stdin -> stdout, deterministic (A3 format). Used by tests/run.sh.
// Always MockFetcher + NoSleeper + MockRenderer. Never network, never sleeps.
#include <cstring>
#include <iostream>

#include "core/crawl.h"
#include "mocks/mock_fetcher.h"
#include "mocks/mock_renderer.h"
#include "ui/front_ends.h"

namespace wc {

int pipe_main(int argc, char** argv) {
    bool dot = false, stats = false, verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--dot")) dot = true;
        if (!std::strcmp(argv[i], "--stats")) stats = true;
        if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    }

    PipeInput input;
    std::string err = parse_pipe_input(std::cin, input);
    if (!err.empty()) {
        std::cout << err << '\n';
        return 1;
    }

    MockFetcher fetcher(std::move(input.web));
    fetcher.verbose = verbose;
    NoSleeper sleeper;
    CrawlGraph g = crawl(input.start, input.cfg, fetcher, sleeper);

    MockRenderer renderer(std::cout, dot ? MockRenderer::Fmt::Dot : MockRenderer::Fmt::Text, &sleeper);
    renderer.render(g);

    if (stats)
        std::cerr << "[stats] fetch_calls=" << fetcher.call_log.size() << " misses=" << fetcher.misses
                  << " failures=" << fetcher.failures << " sleeps=" << sleeper.calls
                  << " nodes=" << g.nodes.size() << " edges=" << g.edges.size() << '\n';
    return 0;
}

}  // namespace wc
