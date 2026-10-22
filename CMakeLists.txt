cmake_minimum_required(VERSION 3.16)
project(web_crawler_csci26 LANGUAGES CXX)

# C++23 (Kerney's demo uses std::print). If the compiler lacks it, CMake
# decays to the newest standard it has. Note: g++ 13 accepts -std=c++23 but
# its library has no <print>; std::print needs g++ 14+. Our code avoids it.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- third_party: FTXUI v7.0.3, vendored, built static ----------------------
set(BUILD_SHARED_LIBS OFF)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_QUIET ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/ftxui EXCLUDE_FROM_ALL)

# --- wc_core: core + mocks (no network, no FTXUI, no BRIDGES) ---------------
add_library(wc_core STATIC
  src/core/graph.cc
  src/core/sleeper.cc
  src/core/crawl.cc          # Tony's #2
  src/core/crawl_hybrid.cc   # Claude: BFS layers, trie order inside a layer
  src/core/crawl_parallel.cc # TUI: parallel BFS + resume (same graph as crawl_bfs)
  src/core/tree.cc           # spanning tree: layout, LOD, BRIDGES subset
  src/mocks/mock_fetcher.cc
  src/mocks/mock_renderer.cc
  src/bridges/bridges_json.cc # BRIDGES JSON (no post)
)
target_include_directories(wc_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(wc_core PUBLIC Threads::Threads)
target_compile_options(wc_core PRIVATE -Wall -Wextra)

# --- wc_net: our fetch path, http + https + redirects (src/net/) ------------
# OpenSSL is optional: without it the build still works and https:// fetches
# fail with "tls: built without OpenSSL". Debian/Ubuntu: apt install libssl-dev.
add_library(wc_net STATIC
  src/net/url.cc
  src/net/html_links.cc
  src/net/http.cc
  src/net/https_fetcher.cc
  src/net/cache.cc
  src/net/robots.cc
  src/net/gateway.cc  # TUI /bridges: POST the graph to gateway/
)
target_include_directories(wc_net PUBLIC src)
target_link_libraries(wc_net PUBLIC wc_core)  # Sleeper
target_compile_options(wc_net PRIVATE -Wall -Wextra)
find_package(OpenSSL)
if(OPENSSL_FOUND)
  target_compile_definitions(wc_net PRIVATE WC_HAVE_OPENSSL)
  target_link_libraries(wc_net PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
  message(WARNING "OpenSSL not found: https:// fetches will fail (install libssl-dev)")
endif()
# Boost (header-only: asio + beast) for src/team/josh.cc, spec #1 (wc and josh_test).
# Optional: without it josh_get_links() is a stub (always fails) and the build still works.
# Debian/Ubuntu: apt install libboost-dev.
find_package(Boost 1.70 QUIET)
if(NOT Boost_FOUND)
  message(STATUS "Boost not found: spec #1 josh_get_links() is a stub (install libboost-dev)")
endif()

# --- wc_ui_logic: pure TUI logic (no FTXUI): layout, replay, omnibar, osc52 -
add_library(wc_ui_logic STATIC
  src/ui/layout.cc
  src/ui/replay.cc
  src/ui/scene.cc
  src/ui/omni_query.cc
  src/ui/osc52.cc
  src/ui/camera.cc
  src/ui/html_text.cc
  src/ui/progress.cc
  src/ui/anim.cc
  src/ui/yes_no.cc
  src/ui/report_pdf.cc  # --report summary.pdf (no deps)
)
target_include_directories(wc_ui_logic PUBLIC src)
target_link_libraries(wc_ui_logic PUBLIC wc_core)
target_compile_options(wc_ui_logic PRIVATE -Wall -Wextra)

# --- wc_ui_views: FTXUI elements (state in, Element out; headless-testable) -
add_library(wc_ui_views STATIC
  src/ui/views.cc
  src/ui/theme.cc
  src/ui/matrix.cc
)
target_link_libraries(wc_ui_views PUBLIC wc_ui_logic ftxui::dom ftxui::screen)
target_compile_options(wc_ui_views PRIVATE -Wall -Wextra)

# --- wc: the binary (pipe mode + TUI + teammate adapters) -------------------
add_executable(wc
  src/main.cc
  src/ui/pipe_main.cc
  src/ui/tui_main.cc
  src/ui/fixture_source.cc
  src/ui/term_lock.cc             # one bracketed write per frame (no tearing)
  src/ui/links_main.cc
  src/ui/report_main.cc            # --report URL (headless report crawls)
  src/ui/matrix_demo.cc            # WC_MATRIX_DEMO=1 (hidden)
  src/adapters/josh_fetcher.cc
  src/adapters/logan_renderer.cc
  src/team/josh.cc                 # #1 Josh's slot (spec #1 fallback by Claude)
  src/team/logan.cc                # #3 Logan writes here
)
target_link_libraries(wc PRIVATE wc_core wc_net wc_ui_views ftxui::component ftxui::dom ftxui::screen)
target_compile_options(wc PRIVATE -Wall -Wextra)
if(Boost_FOUND)
  target_compile_definitions(wc PRIVATE WC_HAVE_BOOST)
  target_include_directories(wc PRIVATE ${Boost_INCLUDE_DIRS})
endif()

# --- unit tests (no network): tests/unit/ ------------------------------------
add_executable(wc_net_test tests/unit/net_test.cc)
target_link_libraries(wc_net_test PRIVATE wc_net wc_core)
target_compile_options(wc_net_test PRIVATE -Wall -Wextra)
add_executable(wc_graph_test tests/unit/graph_test.cc)
target_link_libraries(wc_graph_test PRIVATE wc_core)
target_compile_options(wc_graph_test PRIVATE -Wall -Wextra)

# Crawl regression (BFS empty-queue front()). Not in `make unit` until crawl.cc is fixed: make crawl-regress
add_executable(wc_crawl_test tests/unit/crawl_test.cc)
target_link_libraries(wc_crawl_test PRIVATE wc_core)
target_compile_options(wc_crawl_test PRIVATE -Wall -Wextra)
add_executable(wc_hybrid_test tests/unit/hybrid_test.cc)  # crawl_hybrid + depth_rgb
target_link_libraries(wc_hybrid_test PRIVATE wc_ui_views)
target_compile_options(wc_hybrid_test PRIVATE -Wall -Wextra)
add_executable(wc_parallel_test tests/unit/parallel_test.cc)  # parallel BFS == sequential, cancel, resume
target_link_libraries(wc_parallel_test PRIVATE wc_core)
target_compile_options(wc_parallel_test PRIVATE -Wall -Wextra)

add_executable(wc_ui_test tests/unit/ui_test.cc)        # layout + replay
target_link_libraries(wc_ui_test PRIVATE wc_ui_logic)
add_executable(wc_query_test tests/unit/query_test.cc)  # omnibar + osc52
target_link_libraries(wc_query_test PRIVATE wc_ui_logic)
add_executable(wc_views_test tests/unit/views_test.cc)  # headless snapshots
target_link_libraries(wc_views_test PRIVATE wc_ui_views ftxui::component)  # real Input in test_input_colors
add_executable(wc_matrix_test tests/unit/matrix_test.cc)  # matrix rain
target_link_libraries(wc_matrix_test PRIVATE wc_ui_views)
add_executable(wc_report_pdf_test tests/unit/report_pdf_test.cc)  # summary.pdf writer
target_link_libraries(wc_report_pdf_test PRIVATE wc_ui_logic)
foreach(t wc_report_pdf_test wc_ui_test wc_query_test wc_views_test wc_matrix_test)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()

# --- test_bridges_json: golden tests for the BRIDGES JSON writer -----------
add_executable(test_bridges_json tests/bridges/test_bridges_json.cc)
target_link_libraries(test_bridges_json PRIVATE wc_core)
target_compile_options(test_bridges_json PRIVATE -Wall -Wextra)

# --- josh_test: 12 checks for josh_get_links() vs our fetcher (make josh) ---
# Not in `make`, `make test` or CI: case 10 is live (real sites).
add_executable(josh_test EXCLUDE_FROM_ALL tests/team/josh_test.cc src/team/josh.cc)
target_link_libraries(josh_test PRIVATE wc_net wc_core)
target_compile_definitions(josh_test PRIVATE JOSH_FIXTURE="${CMAKE_SOURCE_DIR}/tests/team/josh_fixture.py")
target_compile_options(josh_test PRIVATE -Wall -Wextra)
if(Boost_FOUND)
  target_compile_definitions(josh_test PRIVATE WC_HAVE_BOOST)
  target_include_directories(josh_test PRIVATE ${Boost_INCLUDE_DIRS})
endif()
