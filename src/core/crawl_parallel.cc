// crawl_parallel.cc - parallel BFS for the TUI (see resume_crawl in crawl.h).
// Fetch threads only call src.links() and drop the result in the pool. The calling
// thread commits results strictly in node-id order (= crawl_bfs's queue order) with
// crawl_bfs's child loop, so the graph matches the sequential crawl exactly.
// Cancel: stop committing, return now; in-flight fetches finish on detached threads
// (they own the pool via shared_ptr) and their results are dropped.
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

#include "core/crawl.h"

namespace wc {

namespace {

using Links = std::optional<std::vector<std::string>>;

struct Pool {
    std::mutex mu;
    std::condition_variable cv;                     // new job / quit (workers), result in (committer)
    std::deque<std::pair<int, std::string>> jobs;   // node id order
    std::map<int, Links> done;                      // id -> result, until committed
    bool quit = false;
};

bool is_pending(const Node& n, int maxDepth) { return n.status == Status::Unfetched && n.depth < maxDepth; }

}  // namespace

int pending(const CrawlGraph& g, int maxDepth) {
    int n = 0;
    for (const Node& node : g.nodes) n += is_pending(node, maxDepth);
    return n;
}

CrawlGraph resume_crawl(CrawlGraph g, const CrawlConfig& cfg, LinkSource& src) {
    if (cfg.maxSize < 1) return g;  // crawl_bfs: size 1 > maxSize -> not even the start is fetched
    auto pool = std::make_shared<Pool>();
    for (size_t id = 0; id < g.nodes.size(); ++id)
        if (is_pending(g.nodes[id], cfg.maxDepth)) pool->jobs.emplace_back(int(id), g.nodes[id].url);

    std::vector<std::thread> threads;
    for (int i = 0; i < cfg.workers; ++i) {
        try {
            threads.emplace_back([pool, &src] {
                std::unique_lock<std::mutex> lk(pool->mu);
                while (true) {
                    pool->cv.wait(lk, [&] { return pool->quit || !pool->jobs.empty(); });
                    if (pool->quit) return;
                    auto [id, url] = std::move(pool->jobs.front());
                    pool->jobs.pop_front();
                    lk.unlock();
                    Links r = src.links(url);
                    lk.lock();
                    pool->done.emplace(id, std::move(r));
                    pool->cv.notify_all();
                }
            });
        } catch (const std::system_error&) {
            break;  // no more threads: the ones we have do the work (none: fetch inline below)
        }
    }

    bool stopped = false;
    int size = int(g.nodes.size());  // crawl_bfs's __size: pages found so far
    for (size_t id = 0; id < g.nodes.size() && !stopped; ++id) {
        if (!is_pending(g.nodes[id], cfg.maxDepth)) continue;
        Links r;
        {
            std::unique_lock<std::mutex> lk(pool->mu);
            while (!pool->done.count(int(id))) {
                if (cfg.cancel && *cfg.cancel) {
                    stopped = true;
                    break;
                }
                if (threads.empty()) {  // jobs are in id order: the front one is this id
                    auto job = std::move(pool->jobs.front());
                    pool->jobs.pop_front();
                    lk.unlock();
                    Links got = src.links(job.second);
                    lk.lock();
                    pool->done.emplace(job.first, std::move(got));
                    continue;
                }
                pool->cv.wait_for(lk, std::chrono::milliseconds(50));  // wake to check cancel
            }
            if (stopped) break;
            auto it = pool->done.find(int(id));
            r = std::move(it->second);
            pool->done.erase(it);
        }
        // Commit: crawl_bfs's fetch + child loop, unchanged.
        const std::string url = g.nodes[id].url;  // copy: add_node may reallocate nodes
        const int depth = g.nodes[id].depth;
        g.record_fetch(url, r.has_value());
        if (!r) continue;
        for (const auto& child : *r) {
            bool seen_edge = g.has_edge(url, child);
            bool seen_node = g.has(child);
            if (size >= cfg.maxSize && !seen_node) continue;
            if (!seen_edge) g.add_edge(url, child, seen_node);
            if (seen_node) continue;
            int cid = g.add_node(child, depth + 1);
            ++size;
            if (depth + 1 < cfg.maxDepth) {
                std::lock_guard<std::mutex> lk(pool->mu);
                pool->jobs.emplace_back(cid, child);
                pool->cv.notify_all();
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(pool->mu);
        pool->quit = true;
    }
    pool->cv.notify_all();
    for (auto& t : threads) stopped ? t.detach() : t.join();  // stopped: <= workers fetches still in flight
    return g;
}

}  // namespace wc
