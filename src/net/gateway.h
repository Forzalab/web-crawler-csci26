// gateway.h - upload a graph to BRIDGES through gateway/ (the gateway holds the BRIDGES keys).
//   POST <gateway>?assignment=N&sub=K   body = bridges_json(g)
//   -> 200 {"ok":true,"url":"..."}  |  4xx/5xx {"ok":false,"error":"..."}
// POSTing the same assignment + sub again replaces the graph: the old link shows the new one.
#pragma once

#include <string>

#include "net/http.h"

namespace wc {

struct GatewayReply {
    std::string url;    // where BRIDGES shows the graph
    std::string error;  // "" = uploaded. Else the gateway's own error, or the transport's
};

// token: sent as X-Gateway-Token when not empty (gateway env GATEWAY_TOKEN).
GatewayReply gateway_upload(HttpClient& http, const std::string& gateway, const std::string& assignment, int sub,
                            const std::string& json, const std::string& token = "");

// The string value of "key" in a flat JSON object ("" if absent). Enough for the gateway's reply.
std::string json_string(const std::string& json, const std::string& key);

}  // namespace wc
