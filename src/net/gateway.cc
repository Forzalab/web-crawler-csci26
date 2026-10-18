// gateway.cc - see gateway.h
#include "net/gateway.h"

#include <map>

namespace wc {

std::string json_string(const std::string& json, const std::string& key) {
    size_t k = json.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    size_t i = json.find_first_not_of(" \t\r\n", k + key.size() + 2);
    if (i == std::string::npos || json[i] != ':') return "";
    i = json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos || json[i] != '"') return "";
    std::string out;
    for (++i; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;  // \" \\ \/ -> the char itself
        out += json[i];
    }
    return out;
}

GatewayReply gateway_upload(HttpClient& http, const std::string& gateway, const std::string& assignment, int sub,
                            const std::string& json, const std::string& token) {
    GatewayReply out;
    std::string target = gateway + (gateway.find('?') == std::string::npos ? "?" : "&") +
                         "assignment=" + assignment + "&sub=" + std::to_string(sub);
    auto u = parse_url(target);
    if (!u) {
        out.error = "bad gateway url: " + gateway;
        return out;
    }
    std::map<std::string, std::string> headers;
    if (!token.empty()) headers["X-Gateway-Token"] = token;
    HttpResponse r = http.post(*u, json, "application/json", headers);
    if (!r.error.empty()) {
        out.error = r.error;
        return out;
    }
    if (r.status == 200 && !(out.url = json_string(r.body, "url")).empty()) return out;
    out.url.clear();
    out.error = json_string(r.body, "error");
    if (out.error.empty()) out.error = "gateway: HTTP " + std::to_string(r.status);
    return out;
}

}  // namespace wc
