// gateway.h - upload a graph to BRIDGES through gateway/ (the gateway holds the BRIDGES keys).
//   POST <gateway>?assignment=N&sub=K   body = bridges_json(g)
//   -> 200 {"ok":true,"url":"..."}  |  4xx/5xx {"ok":false,"error":"..."}
// POSTing the same assignment + sub again replaces the graph: the old link shows the new one.
#pragma once

#include <string>

#include "net/http.h"

namespace wc {

struct GatewayReply {
    std::string url;         // where BRIDGES shows the graph (pdf_upload: the 24 h PDF link)
    std::string error;       // "" = uploaded. Else the gateway's own error, or the transport's
    std::string expires_at;  // pdf_upload only: ISO time the link stops working
};

// token: sent as X-Gateway-Token when not empty (gateway env GATEWAY_TOKEN).
GatewayReply gateway_upload(HttpClient& http, const std::string& gateway, const std::string& assignment, int sub,
                            const std::string& json, const std::string& token = "");

// summary.pdf share link (`wc --report --pdf-link`):
//   POST <endpoint>?name=<name>   body = raw PDF  -> 200 {"ok":true,"url":"...","expires_at":"..."}
// The gateway keeps it 24 h (gateway/pdf.js).
GatewayReply pdf_upload(HttpClient& http, const std::string& endpoint, const std::string& pdf, const std::string& name,
                        const std::string& token = "");

// The /api/pdf endpoint: WC_PDF_URL if set, else WC_GATEWAY_URL with its trailing /api/bridges -> /api/pdf.
// "" = neither gives one (a gateway URL without /api/bridges needs WC_PDF_URL).
std::string pdf_endpoint(const char* pdf_url, const char* gateway_url);

// The string value of "key" in a flat JSON object ("" if absent). Enough for the gateway's reply.
std::string json_string(const std::string& json, const std::string& key);

}  // namespace wc
