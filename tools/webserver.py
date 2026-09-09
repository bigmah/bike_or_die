#!/usr/bin/env python3
"""Serve the WebAssembly build.

SharedArrayBuffer -- which the threads PumpkinOS runs on need -- is only
available to a cross-origin isolated page, so every response carries the two
headers that ask for it. coi-serviceworker.js does the same thing on a host
that will not (GitHub Pages), but a plain server doing it directly is one
fewer moving part while testing.

    tools/webserver.py [port] [dir]
"""
import functools
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    root = sys.argv[2] if len(sys.argv) > 2 else "."
    handler = functools.partial(Handler, directory=os.path.abspath(root))
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
    sys.stderr.write("serving %s at http://127.0.0.1:%d/pumpkin.html\n"
                     % (os.path.abspath(root), port))
    server.serve_forever()


if __name__ == "__main__":
    main()
