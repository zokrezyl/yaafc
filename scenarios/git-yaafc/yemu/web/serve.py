#!/usr/bin/env python3
# Dev server for yaafc-yemu webasm build.
#
# Usage:
#   python3 serve.py 8000 .              # serve current dir
#   python3 serve.py 8000 ./build        # serve cmake build output
#
# Sets the COOP/COEP headers wasm/SharedArrayBuffer requires (not
# strictly needed for our single-threaded build, but harmless), and
# the correct application/wasm MIME for .wasm files. Otherwise pure
# `python3 -m http.server` works too.
import http.server
import sys
import os

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
ROOT = sys.argv[2] if len(sys.argv) > 2 else "."

class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".cfg":  "text/plain",
        ".bin":  "application/octet-stream",
        ".img":  "application/octet-stream",
    }
    def end_headers(self):
        # Cross-origin isolation — required for SharedArrayBuffer on
        # threaded wasm. Single-thread runtime works without it but
        # it doesn't hurt.
        self.send_header("Cross-Origin-Opener-Policy",   "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # Disable caching during development.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

os.chdir(ROOT)
print(f"serving {os.getcwd()} on http://127.0.0.1:{PORT}/")
with http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler) as srv:
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
