#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["msgpack>=1.0"]
# ///
"""Concrete foreign calculator service built on the GENERATED server skeleton.

This replaces the hand-written `echo_server.py` in the outbound msgpack smoke:
the framing, envelope validation, path dispatch, describe and error handling
all come from `bindings/python/calculator_server.py` (emitted by
`tools/msgpack-codegen/gen.py --role server`). The only hand-written part is the
four arithmetic bodies — exactly the developer-fills-in surface the skeleton is
designed for (issue #23)."""
import sys
from pathlib import Path

# The generated skeleton lives in the repo's bindings/ tree.
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "bindings" / "python"))

from calculator_server import CalculatorServer  # noqa: E402


class Calculator(CalculatorServer):
    def calc_add(self, x: int, y: int) -> int:
        return x + y

    def calc_sub(self, x: int, y: int) -> int:
        return x - y

    def calc_mul(self, x: int, y: int) -> int:
        return x * y

    def calc_div(self, x: int, y: int) -> int:
        return (x // y) if y else 0


if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 7912
    sys.stderr.write(f"calculator_server_impl: listening on {host}:{port}\n")
    sys.stderr.flush()
    Calculator().serve(host, port)
