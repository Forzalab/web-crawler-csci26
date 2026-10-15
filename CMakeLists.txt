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
  src/mocks/mock_fetcher.cc
  src/mocks/mock_renderer.cc
  src/bridges/bridges_json.cc # BRIDGES JSON (no post)
)
target_include_directories(wc_core PUBLIC src)
target_compile_options(wc_core PRIVATE -Wall -Wextra)

# --- wc: the binary (pipe mode + TUI + teammate adapters) -------------------
add_executable(wc
  src/main.cc
  src/ui/pipe_main.cc
  src/ui/tui_main.cc
  src/adapters/josh_fetcher.cc     # WIRE: #1 Josh
  src/adapters/logan_renderer.cc   # WIRE: #3 Logan
)
target_link_libraries(wc PRIVATE wc_core ftxui::component ftxui::dom ftxui::screen)
target_compile_options(wc PRIVATE -Wall -Wextra)

# --- test_bridges_json: golden tests for the BRIDGES JSON writer -----------
add_executable(test_bridges_json tests/bridges/test_bridges_json.cc)
target_link_libraries(test_bridges_json PRIVATE wc_core)
target_compile_options(test_bridges_json PRIVATE -Wall -Wextra)
