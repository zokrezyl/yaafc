#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml>=6"]
# ///
"""Polyglot client generator for the Picomesh msgpack frontend.

Consumes codegen `model.yaml` files and emits a typed client for the chosen
language. The same model that drives the C codegen — no clang needed. The wire
contract is docs/msgpack-rpc.md.

    gen.py --lang python --out bindings/python --module calculator \
           src/picomesh/plugins/calculator/model.yaml
"""
import argparse
import sys
from pathlib import Path

import model as ir
import emit_cpp
import emit_go
import emit_lua
import emit_python
import emit_python_server
import emit_rust

# role -> { lang -> emitter module }. The `client` row calls INTO a picomesh
# msgpack frontend; the `server` row is a foreign service picomesh calls OUT to.
# Server skeletons start with Python (issue #23); other languages follow the
# same model and can be added here.
EMITTERS = {
    "client": {
        "python": emit_python,
        "go": emit_go,
        "rust": emit_rust,
        "cpp": emit_cpp,
        "lua": emit_lua,
    },
    "server": {
        "python": emit_python_server,
    },
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--role", default="client", choices=sorted(EMITTERS),
                    help="client (call into a frontend) or server (foreign service)")
    ap.add_argument("--lang", required=True, help="target language")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--module", required=True, help="logical client name (e.g. calculator)")
    ap.add_argument("models", nargs="+", help="one or more model.yaml paths")
    a = ap.parse_args()

    by_lang = EMITTERS[a.role]
    if a.lang not in by_lang:
        sys.stderr.write("gen: role=%s has no %s emitter (have: %s)\n"
                         % (a.role, a.lang, ", ".join(sorted(by_lang))))
        return 2

    methods = ir.load(a.models)
    if not methods:
        sys.stderr.write("gen: no methods in the given model(s)\n")
        return 1

    text, filename = by_lang[a.lang].emit(a.module, methods)
    outdir = Path(a.out)
    outdir.mkdir(parents=True, exist_ok=True)
    out = outdir / filename
    out.write_text(text)
    print(f"wrote {out} ({len(methods)} method(s), role={a.role}, lang={a.lang})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
