#include "Bridges.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "net/gateway.h"

namespace bridges {

namespace datastructure {

std::string json_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) < 0x20) { char u[8]; std::snprintf(u, sizeof u, "\\u%04x", c); out += u; }
        else out += c;
    }
    return out + "\"";
}

std::string json_num(double x) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof buf, x);
    std::string s(buf, r.ptr);
    if (s.find_first_of(".e") == std::string::npos) s += ".0";
    return s;
}

std::string json_color(const Color& c) {
    return "[" + std::to_string(c.getRed()) + "," + std::to_string(c.getGreen()) + "," + std::to_string(c.getBlue()) +
           "," + json_num(c.getAlpha() / 255.0) + "]";
}

}  // namespace datastructure

Bridges::Bridges(int assignment, const std::string&, const std::string&) : assignment_(assignment) {}

std::string Bridges::to_json() const {
    return "{\"visual\":\"GraphAdjacencyList\",\"title\":" + json_quote(title_) +
           ",\"description\":\"\",\"map_overlay\":false,\"map\":\"\",\"element_label_flag\":false,"
           "\"link_label_flag\":false,\"coord_system_type\":\"cartesian\"," +
           (ds_ ? ds_->nodes_links_json() : "\"nodes\":[],\"links\":[]") + "}";
}

void Bridges::visualize() {
    const char* gateway = std::getenv("WC_GATEWAY_URL");
    if (!gateway || !*gateway) {
        std::cerr << "BRIDGES (logan): WC_GATEWAY_URL not set, nothing posted\n";
        return;
    }
    const char* token = std::getenv("WC_GATEWAY_TOKEN");
    const char* sub = std::getenv("WC_BRIDGES_SUB");
    wc::HttpOptions opt;
    opt.timeout_ms = 20000;  // the gateway waits up to 15 s for BRIDGES
    wc::HttpClient http(opt);
    wc::GatewayReply r = wc::gateway_upload(http, gateway, std::to_string(assignment_), sub ? std::atoi(sub) : 0,
                                            to_json(), token ? token : "");
    if (r.error.empty()) std::cout << "BRIDGES (logan): " << r.url << '\n';
    else std::cerr << "BRIDGES (logan): " << r.error << '\n';
}

}  // namespace bridges
