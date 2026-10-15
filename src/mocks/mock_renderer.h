// mock_renderer.h - prints what WOULD go to BRIDGES.
//   Text = the A3 pipe-mode report (this IS what tests/crawl diffs).
//   Dot  = Graphviz, for eyeballing:  ./build/wc --pipe --dot < in | dot -Tpng -o g.png
#pragma once

#include <ostream>

#include "core/renderer.h"
#include "core/sleeper.h"

namespace wc {

class MockRenderer : public Renderer {
public:
    enum class Fmt { Text, Dot };
    // `sleeper` (optional) supplies the SLEEPS count for the Text report.
    MockRenderer(std::ostream& out, Fmt fmt, const NoSleeper* sleeper = nullptr)
        : out_(out), fmt_(fmt), sleeper_(sleeper) {}
    void render(const CrawlGraph& g) override;

private:
    void text(const CrawlGraph& g);
    void dot(const CrawlGraph& g);

    std::ostream& out_;
    Fmt fmt_;
    const NoSleeper* sleeper_;
};

}  // namespace wc
