// html_links.h - pull <a href> links out of an HTML page. No I/O.
#pragma once

#include <string>
#include <vector>

#include "net/url.h"

namespace wc {

// Every <a ... href=...> in page order, resolved against `base`, normalized
// (Url::str()), fragment dropped, duplicates kept (crawl() dedupes).
// Skips empty and "#..." hrefs and ones that are not http/https (mailto:, javascript:...).
std::vector<std::string> extract_links(const std::string& html, const Url& base);

}  // namespace wc
