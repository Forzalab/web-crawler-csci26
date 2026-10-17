// fixture_source.h - WC_FIXTURE=<file> makes the TUI crawl a fake web instead of
// the network (offline smoke tests, tests/smoke/). The file is the A3 pipe-mode
// format (same as tests/crawl/inputfile*); its header line is ignored by the TUI
// (start URL, depth and size are typed at the prompts). WC_FIXTURE_DELAY_MS=<n>
// sleeps n ms per fetch so the loading screen is visible. Unset = nullptr.
#pragma once

#include <memory>

#include "core/link_source.h"

namespace wc {

// nullptr when WC_FIXTURE is unset. Bad file -> message on stderr, exit(2).
std::unique_ptr<LinkSource> fixture_source_from_env();

}  // namespace wc
