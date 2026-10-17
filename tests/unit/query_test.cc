// query_test.cc - unit tests for src/ui/omni_query + src/ui/osc52.
// Run: build/wc_query_test   (exit 1 if any check fails)
#include <iostream>

#include "ui/omni_query.h"
#include "ui/osc52.h"

using namespace wc;

static int failures = 0, checks = 0;

#define CHECK_EQ(got, want)                                                                         \
    do {                                                                                            \
        ++checks;                                                                                   \
        auto g_ = (got);                                                                            \
        auto w_ = (want);                                                                           \
        if (!(g_ == w_)) {                                                                          \
            ++failures;                                                                             \
            std::cout << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #got "\n  got:  " << g_     \
                      << "\n  want: " << w_ << "\n";                                                \
        }                                                                                           \
    } while (0)

static std::string ids(const std::vector<int>& v) {
    std::string s;
    for (int i : v) s += (s.empty() ? "" : ",") + std::to_string(i);
    return s;
}

static void test_parse() {
    Query q = parse_query("/bridges");
    CHECK_EQ(q.verb, std::string("bridges"));
    CHECK_EQ(q.text, std::string(""));

    q = parse_query("depth:<3");
    CHECK_EQ(q.depth_op, '<');
    CHECK_EQ(q.depth, 3);
    q = parse_query("depth:2");
    CHECK_EQ(q.depth_op, '=');
    CHECK_EQ(q.depth, 2);

    q = parse_query("status:failed");
    CHECK_EQ(q.status.has_value(), true);
    CHECK_EQ(q.status == Status::Failed, true);

    q = parse_query("  Wiki status:fetched  News depth:>1");
    CHECK_EQ(q.text, std::string("wiki news"));
    CHECK_EQ(q.status == Status::Fetched, true);
    CHECK_EQ(q.depth_op, '>');
    CHECK_EQ(q.depth, 1);
    CHECK_EQ(q.verb, std::string(""));

    q = parse_query("wiki /help");  // verb only as first token
    CHECK_EQ(q.verb, std::string(""));
    CHECK_EQ(q.text, std::string("wiki /help"));
}

static CrawlGraph fake() {
    CrawlGraph g;
    g.add_node("https://start.org/", 0);         // 0
    g.add_node("https://w-i-k-i.com/", 1);       // 1 subsequence "wiki"
    g.add_node("https://en.wikipedia.org/", 2);  // 2 substring
    g.add_node("https://wiki.org/", 1);          // 3 substring, shallower
    g.add_node("https://other.net/", 1);         // 4
    g.add_node("https://wiki.net/", 1);          // 5 substring, same depth as 3
    g.record_fetch("https://start.org/", true);
    g.record_fetch("https://other.net/", false);
    return g;
}

static void test_match() {
    CrawlGraph g = fake();
    CHECK_EQ(ids(match(g, parse_query("WIKI"))), std::string("3,5,2,1"));
    CHECK_EQ(ids(match(g, parse_query("wiki"), 2)), std::string("3,5"));
    CHECK_EQ(ids(match(g, parse_query("wiki depth:<2"))), std::string("3,5,1"));
    CHECK_EQ(ids(match(g, parse_query("status:failed"))), std::string("4"));
    CHECK_EQ(ids(match(g, parse_query("status:fetched"))), std::string("0"));
    CHECK_EQ(ids(match(g, parse_query("depth:1"))), std::string("1,3,4,5"));
    CHECK_EQ(ids(match(g, parse_query(""))), std::string("0,1,3,4,5,2"));
    CHECK_EQ(ids(match(g, parse_query("zzz"))), std::string(""));
}

static void test_complete() {
    CHECK_EQ(complete("/br"), std::string("/bridges"));
    CHECK_EQ(complete("/h"), std::string("/help"));
    CHECK_EQ(complete("/q"), std::string("/quit"));
    CHECK_EQ(complete("/bridges"), std::string(""));
    CHECK_EQ(complete("/x"), std::string(""));
    CHECK_EQ(complete("stat"), std::string("status:"));
    CHECK_EQ(complete("wiki status:fa"), std::string("wiki status:failed"));
    CHECK_EQ(complete("status:u"), std::string("status:unfetched"));
    CHECK_EQ(complete("dep"), std::string("depth:"));
    CHECK_EQ(complete("wiki "), std::string(""));
    CHECK_EQ(complete(""), std::string(""));
    CHECK_EQ(complete("wiki"), std::string(""));
}

static void test_base64() {  // RFC 4648 section 10
    CHECK_EQ(base64(""), std::string(""));
    CHECK_EQ(base64("f"), std::string("Zg=="));
    CHECK_EQ(base64("fo"), std::string("Zm8="));
    CHECK_EQ(base64("foo"), std::string("Zm9v"));
    CHECK_EQ(base64("foob"), std::string("Zm9vYg=="));
    CHECK_EQ(base64("fooba"), std::string("Zm9vYmE="));
    CHECK_EQ(base64("foobar"), std::string("Zm9vYmFy"));
    CHECK_EQ(base64("\xff\xfe"), std::string("//4="));
}

static void test_osc52() {
    CHECK_EQ(osc52("foo"), std::string("\x1b]52;c;Zm9v\x07"));
    CHECK_EQ(osc52(""), std::string("\x1b]52;c;\x07"));
}

int main() {
    test_parse();
    test_match();
    test_complete();
    test_base64();
    test_osc52();
    std::cout << (failures ? "FAILED " : "ok ") << checks - failures << "/" << checks << " checks\n";
    return failures ? 1 : 0;
}
