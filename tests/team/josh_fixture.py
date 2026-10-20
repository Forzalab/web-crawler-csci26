#!/usr/bin/env python3
"""Tiny fake web for tests/team/josh_test.cc.

Listens on 127.0.0.1, random port, prints the port on the first stdout line.
Every path returns one canned answer (see ROUTES). Exits by itself after 120 s.
"""
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def html(body):
    return "<!doctype html><html><head><title>t</title></head><body>" + body + "</body></html>"


BIG_LINKS = 20000  # ~1 MB of HTML, about the size of the Wikipedia page
BIG = html("".join('<p>row %d <a href="/big/%d">item %d</a></p>\n' % (i, i, i) for i in range(BIG_LINKS)))

# path -> (status, extra headers, body, chunked?)
ROUTES = {
    "/basic": (200, {"Content-Type": "text/html"},
               html('<a href="http://a.example/one">1</a> <a href="https://b.example/two">2</a>'
                    ' <a href="http://a.example/one">1 again</a>'), False),
    "/dir/sub/rel": (200, {"Content-Type": "text/html"},
                     html('<a href="page2">x</a> <a href="../up">x</a> <a href="/root">x</a>'
                          ' <a href="?q=1">x</a> <a href="//other.example/p">x</a>'), False),
    "/nolinks": (200, {"Content-Type": "text/html"}, html("<p>no links on this page</p>"), False),
    "/missing": (404, {"Content-Type": "text/html"},
                 html('<h1>Not Found</h1><a href="/home">home</a>'), False),
    "/broken": (500, {"Content-Type": "text/html"},
                html('<h1>Server error</h1><a href="/home">home</a>'), False),
    "/image.png": (200, {"Content-Type": "image/png"},
                   "\x89PNG\r\n\x1a\n" + '<a href="/fake">', False),
    "/doc.pdf": (200, {"Content-Type": "application/pdf"},
                 "%PDF-1.4 <a href=\"/fake\">", False),
    "/charset-chunked": (200, {"Content-Type": "text/html; charset=UTF-8"},
                         html('<a href="/c1">c1</a>' + "x" * 5000 + '<a href="/c2">c2</a>'), True),
    "/big": (200, {"Content-Type": "text/html; charset=utf-8"}, BIG, False),
    # wowcubedocs.com style: 308 with a relative Location
    "/old/308": (308, {"Location": "/new/final"}, "", False),
    "/new/final": (200, {"Content-Type": "text/html"},
                   html('<a href="next">next</a> <a href="https://c.example/x">x</a>'), False),
    # 301 -> 302 -> page
    "/chain/a": (301, {"Location": "/chain/b"}, "", False),
    "/chain/b": (302, {"Location": "http://127.0.0.1:%PORT%/chain/end"}, "", False),
    "/chain/end": (200, {"Content-Type": "text/html"}, html('<a href="done">done</a>'), False),
    # endless redirect loop
    "/loop/a": (302, {"Location": "/loop/b"}, "", False),
    "/loop/b": (302, {"Location": "/loop/a"}, "", False),
}


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        status, headers, body, chunked = ROUTES.get(
            path, (404, {"Content-Type": "text/html"}, html("no such fixture"), False))
        port = str(self.server.server_address[1])
        data = body.replace("%PORT%", port).encode("latin-1", "replace") if isinstance(body, str) else body
        self.send_response(status)
        for k, v in headers.items():
            self.send_header(k, v.replace("%PORT%", port))
        self.send_header("Connection", "close")
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 1000):
                part = data[i:i + 1000]
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        self.close_connection = True


def main():
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    srv.daemon_threads = True
    print(srv.server_address[1], flush=True)
    threading.Timer(120, srv.shutdown).start()
    srv.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
