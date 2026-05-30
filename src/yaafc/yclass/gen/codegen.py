#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Annotated C -> generated artefacts for one MODULE.

Each module has its own annotated sources, public-stub header, RPC
plumbing, and skel table. Modules are independent.

Reads annotated .c sources, builds an in-memory model, emits:

  Public (under <include_base>/<module>/):
    <class>.h          — accessor decl, includes sibling methods.gen.h
    methods.gen.h      — every public stub in this module
    rpc.gen.h          — every <class>_create() in this module

  Internal (under <module_src_dir>/):
    <class>.gen.c      — accessor body, #include'd at the foot of <class>.c
    methods.gen.c      — public stub bodies
    rpc.gen.c          — skels + accessor/skel lookup hooks + constructor
    model.yaml         — informational dump

Method signature contract:
  RetT slot(struct ctx *ctx, struct object *obj, <rest...>);

  ctx is *not* on the wire. Public stub branches on ctx->peer:
    NULL → local: vtable dispatch via obj->klass.
    set  → remote: look up remote_id via xlat, then rpc_call(RPC_OP_CALL).

Usage:
  ./codegen.py <module> <include_base> <module_src_dir> <source.c>...

  <include_base>      shared include root (typically `include`).
                      Public headers land in <include_base>/<module>/.
  <module_src_dir>    where annotated .c files live. Internal artefacts
                      (.gen.c, methods.gen.c, rpc.gen.c, model.yaml)
                      land here.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path


HEADER = "/* GENERATED — do not edit. */\n"


def qualified_class(c: dict) -> str:
    return f"{c['domain']}_{c['name']}"

def qualified_slot(m: dict) -> str:
    return f"{m['domain']}_{m['slot']}"

def qualified(domain: str, name: str) -> str:
    return f"{domain}_{name}"

def op_c_name(op: dict) -> str:
    return f"{op['slot_domain']}_{op['slot']}"


def result_type_id(ret: str) -> str:
    """Map an impl's return type to the YAAFC_RESULT_DECLARE identifier."""
    r = ret.strip()
    m = re.match(r"^struct\s+(\w+)_result\s*$", r)
    if m:
        return m.group(1)
    if r == "void":
        return "yaafc_void"
    if r == "int":
        return "yaafc_int"
    if r == "size_t":
        return "yaafc_size"
    if r == "int64_t":
        return "yaafc_int64"
    if r == "uint32_t":
        return "yaafc_uint32"
    m = re.match(r"^struct\s+(\w+)\s*\*\s*$", r)
    if m:
        return f"{m.group(1)}_ptr"
    m = re.match(r"^struct\s+(\w+)\s*$", r)
    if m:
        return m.group(1)
    return r


def result_type(ret: str) -> str:
    return f"struct {result_type_id(ret)}_result"


def ret_value_type(rid: str):
    if rid == "yaafc_void":
        return None
    if rid == "yaafc_int":
        return "int"
    if rid == "yaafc_size":
        return "size_t"
    if rid == "yaafc_int64":
        return "int64_t"
    if rid == "yaafc_uint32":
        return "uint32_t"
    if rid == "yaafc_string":
        return "char *"
    if rid.endswith("_ptr"):
        return f"struct {rid[:-4]} *"
    return f"struct {rid}"


def is_string_ret(vt) -> bool:
    """A heap-owned string return value (`char *`). Packed on the wire as
    u32 len + bytes; the caller owns and frees it. Distinct from the
    fixed-width scalar returns, which are raw memcpy'd."""
    return vt == "char *"


# ============== model — annotated C → in-memory dict ====================
#
# Annotation schema: `<verb>@<domain>:<path...>`.
#
#   class@<DOMAIN>:<CLASS>                            on data struct
#   mixin@<DOMAIN>:<CLASS>                            on data struct
#   parent@<DOMAIN>:<CLASS>                           on data struct
#   uses@<DOMAIN>:<MIXIN>                             on data struct
#   override@<DOMAIN>:<CLASS>:<SLOT>                  on impl fn
#   override@<DOMAIN>:<CLASS>:<SLOT_DOMAIN>:<SLOT>    on impl fn (cross-domain)

def ast_dump(path: Path, include_dirs: list) -> dict:
    clang = os.environ.get("CLANG", "clang")
    # The annotated source #includes its own (possibly STALE) *.gen.c —
    # the very file this pass is about to regenerate. When an impl's
    # signature changes, the stale gen's function-pointer initialisers no
    # longer match, which C23 reports as a hard error and would abort the
    # AST dump before we can emit the fresh gen (a bootstrap deadlock).
    # This pass only needs to PARSE the impls to extract the model, so
    # downgrade exactly those stale-gen mismatch categories to warnings;
    # the real compile step (separate, with the freshly written gen) still
    # enforces them fully.
    cmd = [clang, "-Xclang", "-ast-dump=json", "-fsyntax-only", "-std=c2x",
           "-Wno-error=incompatible-function-pointer-types",
           "-Wno-error=incompatible-pointer-types",
           "-Wno-error=int-conversion"]
    for d in include_dirs:
        cmd.append(f"-I{d}")
    # Honour YAAFC_CODEGEN_EXTRA_INCLUDES — caller (the cmake custom-command,
    # or the developer driving codegen.py manually) hands us the same -I
    # search path the eventual compilation uses, so #include directives in
    # the annotated source resolve at AST-dump time. Without this we'd error
    # before any model parsing starts.
    extra = os.environ.get("YAAFC_CODEGEN_EXTRA_INCLUDES", "")
    if extra:
        for d in extra.split(os.pathsep):
            if d:
                cmd.append(f"-I{d}")
    cmd.append(str(path))
    r = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if r.returncode != 0:
        sys.stderr.write(r.stderr)
        sys.exit(1)
    return json.loads(r.stdout)


def _loc_offset(loc: dict) -> tuple:
    """Get (offset, tokLen) from a clang AST location.

    For tokens that come from a macro expansion the location nests
    `expansionLoc` (where the macro is used in the .c file) and
    `spellingLoc` (the macro definition). We want the EXPANSION offset
    so substring reads land in the .c source, not in the header
    holding the macro. For tokens that aren't macro-expanded the
    offset sits at the top level.
    """
    if "offset" in loc:
        return loc["offset"], loc.get("tokLen", 1)
    exp = loc.get("expansionLoc")
    if exp and "offset" in exp:
        return exp["offset"], exp.get("tokLen", 1)
    return None, 0


def _annotate_value(attr_node: dict, src: bytes):
    rng = attr_node.get("range", {})
    beg_off, _      = _loc_offset(rng.get("begin", {}))
    end_off, end_tok = _loc_offset(rng.get("end", {}))
    if beg_off is None or end_off is None:
        return None
    # Clang collapses the source range of a macro-expanded attribute to
    # just the macro identifier (e.g. begin=end=offset-of-`YAAFC_CLASS_
    # ANNOTATE`, tokLen=len("YAAFC_CLASS_ANNOTATE")). The annotation
    # string sits AFTER that token in `("foo")`. Read a generous window
    # past the token-end so the regex catches the quoted string. 512
    # bytes is plenty — annotation strings are short and the macro call
    # fits on one source line in practice.
    stop = end_off + end_tok + 512
    blob = src[beg_off:stop].decode("utf-8", errors="replace")
    # Match either the raw `[[clang::annotate("x")]]` form or the
    # YAAFC_CLASS_ANNOTATE("x") macro form. The macro keeps plugin
    # sources compilable under gcc (cross-compiles), while clang's
    # AST dump still produces an AnnotateAttr from the expansion.
    m = re.search(r'(?:annotate|ANNOTATE)\s*\(\s*"([^"]*)"', blob)
    return m.group(1) if m else None


def _collect_annotations(decl: dict, src: bytes) -> list:
    out = []
    for child in decl.get("inner", []):
        if child.get("kind") == "AnnotateAttr":
            v = _annotate_value(child, src)
            if v: out.append(v)
    return out


def _fn_args(decl: dict) -> list:
    args = []
    for child in decl.get("inner", []):
        if child.get("kind") == "ParmVarDecl":
            args.append({
                "name": child.get("name") or "",
                "type": child.get("type", {}).get("qualType", ""),
            })
    return args


def _parse_return_type(qual_type: str) -> str:
    m = re.match(r"^(.*?)\s*\((.*)\)$", qual_type.strip())
    return m.group(1).strip() if m else qual_type


def _walk_decls(node: dict):
    if node.get("kind") in ("FunctionDecl", "RecordDecl"):
        yield node
    for child in node.get("inner", []):
        yield from _walk_decls(child)


def _split_ann(ann: str):
    if "@" not in ann:
        sys.stderr.write(
            f"error: annotation '{ann}' lacks '@' separator; "
            f"expected `<verb>@<domain>:<path...>`\n")
        sys.exit(1)
    verb, rest = ann.split("@", 1)
    return verb.strip(), [p.strip() for p in rest.split(":") if p.strip()]


def parse_sources(include_dirs: list, sources: list, module: str) -> dict:
    methods: dict = {}
    classes: dict = {}

    def bucket(name: str) -> dict:
        if name not in classes:
            classes[name] = {
                "name": name, "domain": None, "accessor": None, "type": None,
                "source_file": None, "parent": None, "mixins": [],
                "data": None, "ops": [],
            }
        return classes[name]

    def require_segments(role: str, args: list, n: int, shape: str):
        if len(args) != n:
            sys.stderr.write(
                f"error: '{role}@{':'.join(args)}' — expected {shape}\n")
            sys.exit(1)

    def require_local_domain(role: str, dom: str):
        if dom != module:
            sys.stderr.write(
                f"error: '{role}' domain '{dom}' != current module '{module}'.\n")
            sys.exit(1)

    for path in sources:
        src = path.read_bytes()
        tu = ast_dump(path, include_dirs)
        for decl in _walk_decls(tu):
            anns = _collect_annotations(decl, src)
            if not anns: continue
            kind = decl.get("kind")

            if kind == "FunctionDecl":
                for ann in anns:
                    role, args = _split_ann(ann)
                    if role == "override":
                        if len(args) == 3:
                            impl_dom, cls, slot = args
                            slot_dom = impl_dom
                        elif len(args) == 4:
                            impl_dom, cls, slot_dom, slot = args
                        else:
                            sys.stderr.write(
                                f"error: 'override@{':'.join(args)}' — expected "
                                "override@<DOMAIN>:<CLASS>:<SLOT> or "
                                "override@<DOMAIN>:<CLASS>:<SLOT_DOMAIN>:<SLOT>\n")
                            sys.exit(1)
                        require_local_domain(role, impl_dom)
                        b = bucket(cls)
                        b["ops"].append({
                            "slot": slot,
                            "slot_domain": slot_dom,
                            "impl": decl["name"],
                        })
                        if slot_dom == module and slot not in methods:
                            qt = decl.get("type", {}).get("qualType", "")
                            methods[slot] = {
                                "slot": slot, "domain": slot_dom,
                                "owning_class": cls,
                                "return_type": _parse_return_type(qt),
                                "args": _fn_args(decl),
                            }
            elif kind == "RecordDecl":
                primary, kind2, primary_dom = None, None, None
                for ann in anns:
                    role, args = _split_ann(ann)
                    if role == "class":
                        require_segments(role, args, 2, "class@<DOMAIN>:<CLASS>")
                        primary_dom, primary, kind2 = args[0], args[1], "regular"
                        break
                    if role == "mixin":
                        require_segments(role, args, 2, "mixin@<DOMAIN>:<CLASS>")
                        primary_dom, primary, kind2 = args[0], args[1], "mixin"
                        break
                if primary:
                    require_local_domain(kind2, primary_dom)
                    b = bucket(primary)
                    b["domain"] = primary_dom
                    b["type"] = kind2
                    b["source_file"] = str(path)
                    suffix = "_mixin_get" if kind2 == "mixin" else "_class_get"
                    b["accessor"] = f"{primary_dom}_{primary}{suffix}"
                    tag = decl.get("name")
                    if tag:
                        b["data"] = f"struct {tag}"
                    for ann in anns:
                        role, args = _split_ann(ann)
                        if role == "parent":
                            require_segments(role, args, 2, "parent@<DOMAIN>:<CLASS>")
                            b["parent"] = {"domain": args[0], "name": args[1]}
                        elif role == "uses":
                            require_segments(role, args, 2, "uses@<DOMAIN>:<MIXIN>")
                            b["mixins"].append({"domain": args[0], "name": args[1]})

    return {
        "methods": list(methods.values()),
        "classes": [c for c in classes.values() if c["accessor"]],
    }


# ============== yaml writer (informational dump) ========================

def _yaml_scalar(v) -> str:
    if v is None: return "null"
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, (int, float)): return str(v)
    s = str(v)
    if any(ch in s for ch in ":#[]{}&*!|>'\"%@`") or s != s.strip():
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def yaml_dump(obj, indent=0) -> str:
    pad = "  " * indent
    if isinstance(obj, dict):
        if not obj: return "{}"
        out = []
        for k, v in obj.items():
            if isinstance(v, dict) and v:
                out.append(f"{pad}{k}:\n{yaml_dump(v, indent + 1)}")
            elif isinstance(v, list) and v:
                out.append(f"{pad}{k}:\n{yaml_dump(v, indent + 1)}")
            elif isinstance(v, list):
                out.append(f"{pad}{k}: []")
            elif isinstance(v, dict):
                out.append(f"{pad}{k}: {{}}")
            else:
                out.append(f"{pad}{k}: {_yaml_scalar(v)}")
        return "\n".join(out)
    if isinstance(obj, list):
        if not obj: return f"{pad}[]"
        out = []
        for item in obj:
            if isinstance(item, dict):
                lines = yaml_dump(item, indent + 1).splitlines()
                if lines: lines[0] = pad + "- " + lines[0].lstrip()
                out.append("\n".join(lines))
            else:
                out.append(f"{pad}- {_yaml_scalar(item)}")
        return "\n".join(out)
    return f"{pad}{_yaml_scalar(obj)}"


# ---------------- helpers -------------------------------------------------

def is_struct_ptr(t: str) -> bool:
    return bool(re.match(r"^\s*(const\s+)?struct\s+\w+\s*\*\s*$", t))


def is_specific_struct_ptr(t: str, name: str) -> bool:
    return bool(re.match(rf"^\s*(const\s+)?struct\s+{name}\s*\*\s*$", t))


def is_string_arg(t: str) -> bool:
    """`const char *` — variable-length wire encoding (u32 len + bytes)."""
    return bool(re.match(r"^\s*const\s+char\s*\*\s*$", t.strip()))


# Max bytes the codegen reserves for the serialized arg buffer on the
# client side, and the matching skel-side string-arg copy. Keep them
# in sync; pick generously rather than tightly because the body lives
# on the stack inside the public stub.
WIRE_ARG_BUFFER_BYTES = 16384
WIRE_STRING_MAX = 4096


def wire_type(t: str) -> str:
    return "uint64_t" if is_struct_ptr(t) else t.strip()


def wire_name(arg: dict) -> str:
    return f"{arg['name']}_handle" if is_struct_ptr(arg["type"]) else arg["name"]


def struct_names_in(*types: str) -> set:
    out = set()
    for t in types:
        for m in re.finditer(r"\bstruct\s+(\w+)", t):
            out.add(m.group(1))
    return out


def default_return_for(ret: str) -> str:
    ret = ret.strip()
    if ret == "void":
        return ""
    return f"({ret}){{0}}"


def validate_method(m: dict):
    args = m["args"]
    if len(args) < 3:
        sys.stderr.write(
            f"error: method {m['slot']} needs (ctx*, obj*, yheaders*, ...). "
            f"got {len(args)}\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[0]["type"], "ctx"):
        sys.stderr.write(
            f"error: method {m['slot']}: 1st arg must be 'struct ctx *' "
            f"(got '{args[0]['type']}')\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[1]["type"], "object"):
        sys.stderr.write(
            f"error: method {m['slot']}: 2nd arg must be 'struct object *' "
            f"(got '{args[1]['type']}')\n")
        sys.exit(1)
    if not is_specific_struct_ptr(args[2]["type"], "yheaders"):
        sys.stderr.write(
            f"error: method {m['slot']}: 3rd arg must be 'struct yheaders *' "
            f"(got '{args[2]['type']}')\n")
        sys.exit(1)
    for a in args[3:]:
        t = a["type"]
        if is_string_arg(t):
            continue
        if is_struct_ptr(t):
            sys.stderr.write(
                f"error: method {m['slot']}: arg '{a['name']}' is a struct "
                f"pointer ({a['type']}). Only the obj at arg[1], the headers "
                f"at arg[2], and `const char *` strings are supported.\n")
            sys.exit(1)


# Args packed onto the wire as business args: the object handle (arg[1])
# plus the trailing scalar/string args (arg[3:]). The framework ctx
# (arg[0]) is never on the wire; the headers (arg[2]) ride a separate
# framework-serialized section, not the packed-args region.
def wire_args(m: dict) -> list:
    return [m["args"][1]] + m["args"][3:]


def args_struct_body(args: list, indent: str) -> str:
    if not args:
        return f"{indent}char _empty;\n"
    return "".join(f"{indent}{wire_type(a['type'])} {wire_name(a)};\n" for a in args)


# ---------------- public + internal headers ------------------------------

def collect_result_struct_decls(model: dict) -> list:
    """Return forward `struct X;` declarations for every result type used
    in the model's method signatures. These let consumers include the
    module's public header without separately including the result-type
    macros."""
    structs = set()
    for m in model["methods"]:
        structs |= struct_names_in(m["return_type"])
        for a in m["args"]:
            structs |= struct_names_in(a["type"])
    for known in ("ctx", "object", "class", "yaafc_str"):
        structs.discard(known)
    return [f"struct {s};\n" for s in sorted(structs)]


def emit_public_header(model: dict, module: str, out_path: Path):
    """Single combined public header for the plugin module.

    Replaces the old (methods.gen.h + rpc.gen.h + per-class .h) trio.
    Exposes only what an EXTERNAL consumer needs:
      - forward-decls of result types,
      - class accessors  <qname>_class_get() / <qname>_mixin_get(),
      - constructors     <qname>_create(),
      - method prototypes for the local/remote-aware public stubs.

    Function-pointer typedefs (`*_fn`) used by the codegen-emitted
    dispatch tables live in `<module>.internal.h` instead — that's
    private kitchen, not user-facing API.
    """
    guard = f"YAAFC_PLUGIN_{module.upper()}_H"
    parts = [HEADER,
             f"/* Public interface for plugin `{module}` — GENERATED.\n"
             f" * Edit the annotated sources under src/yaafc/plugins/{module}/. */\n",
             f"#ifndef {guard}\n#define {guard}\n\n",
             '#include <yaafc/yclass/class.h>\n',
             '#include <yaafc/yclass/rpc.h>\n\n']

    parts.extend(collect_result_struct_decls(model))
    parts.append("struct object_ptr_result;\n")
    parts.append("struct class_ptr_result;\n\n")

    # Class accessors (one per declared class, regular or mixin).
    parts.append("/* ---- class accessors ---- */\n")
    for cls in model.get("classes", []):
        is_mixin = cls["type"] == "mixin"
        suffix = "_mixin_get" if is_mixin else "_class_get"
        parts.append(f"struct class_ptr_result {qualified_class(cls)}{suffix}(void);\n")
    parts.append("\n")

    # Constructors (regular classes only — mixins aren't instantiated).
    if any(c.get("type") == "regular" for c in model.get("classes", [])):
        parts.append("/* ---- constructors ---- */\n")
        for c in regular_classes(model):
            parts.append(
                f"struct object_ptr_result {qualified_class(c)}_create(struct ctx *ctx);\n")
        parts.append("\n")

    # Method prototypes (no typedefs here).
    parts.append("/* ---- methods ---- */\n")
    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        rt = result_type(m["return_type"])
        parts.append(f"{rt} {qualified_slot(m)}({params});\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


def emit_internal_header(model: dict, module: str, out_path: Path):
    """Codegen-private header — lives in the module's src dir, never
    included by external consumers. Holds the function-pointer typedefs
    the generated dispatch tables need.
    """
    guard = f"YAAFC_{module.upper()}_INTERNAL_H"
    parts = [HEADER,
             f"/* Internal codegen-only header for plugin `{module}`.\n"
             f" * NEVER include this from outside src/yaafc/plugins/{module}/. */\n",
             f"#ifndef {guard}\n#define {guard}\n\n",
             f'#include <yaafc/plugin/{module}/{module}.h>\n\n']

    for m in model["methods"]:
        type_only = ", ".join(a["type"] for a in m["args"])
        rt = result_type(m["return_type"])
        parts.append(f"typedef {rt} (*{qualified_slot(m)}_fn)({type_only});\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


# ---------------- methods.gen.c ------------------------------------------

def emit_pack_arg(a: dict, slot: str, rid: str, indent: str = "        ") -> str:
    """Emit code that appends one arg to `_a[_off..]`.

    Wire layout, in order: scalars are raw bytes; strings are
    `u32 len | bytes` (no terminator); the obj-handle is a u64.
    Identical to the previous packed-struct format for scalar-only
    signatures, so existing methods stay byte-compatible. */"""
    t = a["type"].strip()
    name = a["name"]
    if is_string_arg(t):
        return (
            f"{indent}{{\n"
            f"{indent}    uint32_t _slen = (uint32_t)({name} ? strlen({name}) : 0);\n"
            f"{indent}    if (_off + 4 + _slen > sizeof(_a))\n"
            f"{indent}        return YAAFC_ERR({rid}, \"{slot}: pack overflow\");\n"
            f"{indent}    memcpy(_a + _off, &_slen, 4); _off += 4;\n"
            f"{indent}    if (_slen) {{ memcpy(_a + _off, {name}, _slen); _off += _slen; }}\n"
            f"{indent}}}\n"
        )
    if is_struct_ptr(t):
        # obj handle
        return (
            f"{indent}{{\n"
            f"{indent}    uint64_t _h = *(uint64_t *)((char *){name} + sizeof(*{name}));\n"
            f"{indent}    if (_off + 8 > sizeof(_a))\n"
            f"{indent}        return YAAFC_ERR({rid}, \"{slot}: pack overflow\");\n"
            f"{indent}    memcpy(_a + _off, &_h, 8); _off += 8;\n"
            f"{indent}}}\n"
        )
    return (
        f"{indent}if (_off + sizeof({name}) > sizeof(_a))\n"
        f"{indent}    return YAAFC_ERR({rid}, \"{slot}: pack overflow\");\n"
        f"{indent}memcpy(_a + _off, &{name}, sizeof({name})); _off += sizeof({name});\n"
    )


def emit_dispatch_body(m: dict) -> str:
    args = m["args"]
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    slot_fn = f"{qualified_slot(m)}_fn"
    ctx_name = args[0]["name"]
    obj_name = args[1]["name"]
    hdrs_name = args[2]["name"]
    call_args = ", ".join(a["name"] for a in args)
    slot_qname = qualified_slot(m)

    pack_block = "".join(emit_pack_arg(a, slot_qname, rid, indent="        ")
                         for a in wire_args(m))

    # Wire response: u8 status; status==0 → value_bytes; status==1 →
    # u32 msg_len + msg_bytes (gh#2: preserve structured remote errors).
    # The buffer is sized to hold the bigger of value or error payload.
    # The outbound call, wrapped in a tracing span: the trace id comes
    # from the header bag, so `ydebug` (gated — zero IO when tracing is
    # off) emits `trace=<id> op=rpc.<slot> dur_us=<n>`, and grepping a
    # trace id reconstructs the end-to-end latency timeline. This span
    # measures the full round trip (transport + remote execution); the
    # skel emits a matching `op=skel.<slot>` span for the remote half.
    # String returns can be long, so the response buffer must hold
    # u8 status + u32 len + up to WIRE_STRING_MAX value bytes. Scalar /
    # error responses fit comfortably in the small buffer.
    wbuf_sz = (1 + 4 + WIRE_STRING_MAX) if is_string_ret(vt) else (1 + 4 + 256)
    timed_rpc = f"""\
        const char *span_trace = {hdrs_name} ? yheaders_get({hdrs_name}, "trace_id") : "-";
        if (!span_trace) span_trace = "-";
        double span_start = yaafc_ytime_monotonic_sec();
        uint8_t _wbuf[{wbuf_sz}];
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, sizeof(_wbuf));
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        ydebug("span trace=%s op=rpc.{slot_qname} dur_us=%.0f", span_trace, span_us);
        yspan_record("rpc.{slot_qname}", span_us);
"""
    err_parse = f"""\
        if (_wn < 1) return YAAFC_ERR({rid}, "{slot_qname}: short RPC response");
        if (_wbuf[0] != 0) {{
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[260];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            return YAAFC_ERR({rid}, _msg[0] ? strdup(_msg) : "{slot_qname}: remote error (no msg)");
        }}
"""
    if vt is None:
        remote_call = timed_rpc + err_parse + "        return YAAFC_OK_VOID();\n"
    elif is_string_ret(vt):
        # Unpack u32 len + bytes into an owned heap string the caller frees.
        remote_call = timed_rpc + err_parse + f"""\
        if (_wn < 5) return YAAFC_ERR({rid}, "{slot_qname}: truncated string response");
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) return YAAFC_ERR({rid}, "{slot_qname}: truncated string payload");
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) return YAAFC_ERR({rid}, "{slot_qname}: out of memory");
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        return YAAFC_OK({rid}, _sv);
"""
    else:
        remote_call = timed_rpc + err_parse + f"""\
        if (_wn != 1 + sizeof({vt})) return YAAFC_ERR({rid}, "{slot_qname}: truncated RPC payload");
        {vt} _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        return YAAFC_OK({rid}, _v);
"""

    return f"""\
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {{
        struct method_slot_result _sr =
            method_slot_get("{m['domain']}", (method_id_t){slot_qname});
        if (YAAFC_IS_ERR(_sr))
            return YAAFC_ERR({rid}, "{slot_qname}: method_slot_get failed", _sr);
        _slot = _sr.value;
    }}

    if (!{obj_name}) return YAAFC_ERR({rid}, "{slot_qname}: NULL object");

    struct ctx *_s = {ctx_name};
    if (_s && _s->peer) {{
        uint32_t _rid = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return YAAFC_ERR({rid}, "{slot_qname}: remote id unresolved");
        uint8_t _a[{WIRE_ARG_BUFFER_BYTES}];
        size_t _off = 0;
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, sid, trace_id, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. The codegen never inspects the
         * contents — it just lets the framework (de)serialize the bag. */
        {{
            size_t _hn = yheaders_serialize({hdrs_name}, _a, sizeof(_a));
            if (_hn == 0)
                return YAAFC_ERR({rid}, "{slot_qname}: header serialize overflow");
            _off = _hn;
        }}
{pack_block}\
{remote_call}\
    }} else {{
        impl_t fn = class_dispatch_lookup(object_class({obj_name}), _slot);
        if (!fn) return YAAFC_ERR({rid}, "{slot_qname}: no impl on this class");
        return (({slot_fn})fn)({call_args});
    }}
"""


def emit_methods_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             f'#include "{module}.internal.h"\n',
             '#include <yaafc/ycore/result.h>\n',
             '#include <yaafc/ycore/ytrace.h>\n',
             '#include <yaafc/ycore/yspan.h>\n',
             '#include <yaafc/yclass/rpc.h>\n',
             '#include <yaafc/yclass/yheaders.h>\n',
             '#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n\n']
    for m in model["methods"]:
        params = ", ".join(f"{a['type']} {a['name']}" for a in m["args"])
        rt = result_type(m["return_type"])
        parts.append(f"{rt} {qualified_slot(m)}({params})\n{{\n"
                     f"{emit_dispatch_body(m)}}}\n\n")
    out_path.write_text("".join(parts))


# ---------------- <class>.gen.c ------------------------------------------

def emit_class_accessor(cls: dict) -> str:
    accessor = cls["accessor"]
    is_mixin = cls["type"] == "mixin"
    data = cls["data"] or "char"
    type_const = "CLASS_TYPE_MIXIN" if is_mixin else "CLASS_TYPE_REGULAR"

    qcls = qualified_class(cls)
    typecheck_lines = [
        f"__attribute__((unused))\n"
        f"static {op_c_name(op)}_fn _{qcls}_{op_c_name(op)}_check = {op['impl']};"
        for op in cls["ops"]
    ]
    typecheck_block = "\n".join(typecheck_lines)
    if typecheck_block:
        typecheck_block += "\n\n"

    op_lines = [
        f'        {{"{op["slot_domain"]}", "{op["slot"]}", '
        f"(method_id_t){op_c_name(op)}, (impl_t){op['impl']}}},"
        for op in cls["ops"]
    ]
    ops_block = "\n".join(op_lines)

    qname = qualified_class(cls)

    parent = cls.get("parent")
    if parent:
        parent_accessor = f"{parent['domain']}_{parent['name']}_class_get"
        parent_block = (
            f"    struct class_ptr_result _parent_r = {parent_accessor}();\n"
            f"    if (YAAFC_IS_ERR(_parent_r))\n"
            f"        return YAAFC_ERR(class_ptr, \"{qname}_class_get: parent accessor failed\", _parent_r);\n"
        )
        parent_expr = "_parent_r.value"
    else:
        parent_block = ""
        parent_expr = "NULL"

    mixins = cls.get("mixins") or []
    if mixins:
        mixin_lines = []
        mixin_values = []
        for i, m in enumerate(mixins):
            mixin_accessor = f"{m['domain']}_{m['name']}_mixin_get"
            mixin_lines.append(
                f"    struct class_ptr_result _mixin{i}_r = {mixin_accessor}();\n"
                f"    if (YAAFC_IS_ERR(_mixin{i}_r))\n"
                f"        return YAAFC_ERR(class_ptr, \"{qname}_class_get: mixin{i} accessor failed\", _mixin{i}_r);\n"
            )
            mixin_values.append(f"_mixin{i}_r.value")
        mixin_block = "".join(mixin_lines)
        mixin_block += (
            f"    const struct class *mixins[] = {{ {', '.join(mixin_values)} }};\n"
        )
        mixin_arg = "mixins"
        mixin_count = str(len(mixins))
    else:
        mixin_block = ""
        mixin_arg = "NULL"
        mixin_count = "0"

    return f"""\
{typecheck_block}\
struct class_ptr_result {accessor}(void)
{{
    static const struct class *cls = NULL;
    if (cls) return YAAFC_OK(class_ptr, cls);
    ydebug("registering class={qname}");

    static const struct class_descriptor desc = {{
        .name = "{qname}",
        .type = {type_const},
        .data_size = sizeof({data}),
    }};
    static const struct op ops[] = {{
{ops_block}
    }};
{parent_block}\
{mixin_block}\
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       {parent_expr}, {mixin_arg}, {mixin_count});
    if (YAAFC_IS_ERR(_r))
        return YAAFC_ERR(class_ptr, "{qname}_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}}
"""


def emit_class_public_headers(model: dict, module: str, include_module_dir: Path):
    """Stub — per-class public headers no longer exist; all of that
    moved into the single combined `<module>.h`. Kept as a no-op for
    grep-find compatibility; remove once the call site stops invoking
    it. """
    (void := model, void := module, void := include_module_dir)


def emit_class_gen_c(model: dict, module: str, module_dir: Path):
    groups: dict = {}
    for c in model["classes"]:
        groups.setdefault(c["source_file"], []).append(c)
    for src_path, classes in groups.items():
        inc_path = module_dir / (Path(src_path).stem + ".gen.c")
        needed = set()
        # Pull in our own internal codegen header (typedefs etc.)
        # plus the public header of any sibling module whose classes
        # appear as parents / mixins / slot-domain providers.
        needed.add(f"\"{module}.internal.h\"")
        for c in classes:
            p = c.get("parent")
            if p and p["domain"] != module:
                needed.add(f"<yaafc/plugin/{p['domain']}/{p['domain']}.h>")
            for mx in c.get("mixins", []):
                if mx["domain"] != module:
                    needed.add(f"<yaafc/plugin/{mx['domain']}/{mx['domain']}.h>")
            for op in c.get("ops", []):
                sd = op.get("slot_domain", c["domain"])
                if sd != module:
                    needed.add(f"<yaafc/plugin/{sd}/{sd}.h>")
        include_block = "".join(f'#include {h}\n' for h in sorted(needed))

        body = HEADER + include_block + "\n" \
             + "\n".join(emit_class_accessor(c) for c in classes)
        inc_path.write_text(body)


# ---------------- rpc.gen.{h,c} ------------------------------------------

def regular_classes(model: dict) -> list:
    return [c for c in model.get("classes", []) if c.get("type") == "regular"]


def class_header_for(cls: dict, module: str) -> str:
    """Now a single public header per plugin module. Kept as a helper
    so any leftover caller still resolves to the right path."""
    (void := cls)
    return f"yaafc/plugin/{module}/{module}.h"


def emit_unpack_arg(a: dict, idx: int) -> tuple:
    """Emit the local-variable declaration and unpack code for arg #idx.

    Returns (declaration_block, call_expr) where call_expr names the
    local variable to pass into the impl. The declaration ends with a
    sentinel `goto _short_body;` jump on truncated input."""
    t = a["type"].strip()
    name = a["name"]
    if is_string_arg(t):
        var = f"_s{idx}"
        decl = (
            f"    char {var}[{WIRE_STRING_MAX}];\n"
            f"    {{\n"
            f"        if (_off + 4 > _body_len) goto _short_body;\n"
            f"        uint32_t _slen;\n"
            f"        memcpy(&_slen, (const uint8_t *)_body + _off, 4); _off += 4;\n"
            f"        if (_off + _slen > _body_len) goto _short_body;\n"
            f"        if (_slen >= sizeof({var})) goto _short_body;\n"
            f"        if (_slen) memcpy({var}, (const uint8_t *)_body + _off, _slen);\n"
            f"        {var}[_slen] = 0; _off += _slen;\n"
            f"    }}\n"
        )
        return (decl, var)
    if is_struct_ptr(t):
        # obj handle
        var = "_obj"
        decl = (
            f"    {t.strip()}{var} = NULL;\n"
            f"    {{\n"
            f"        if (_off + 8 > _body_len) goto _short_body;\n"
            f"        uint64_t _h;\n"
            f"        memcpy(&_h, (const uint8_t *)_body + _off, 8); _off += 8;\n"
            f"        {var} = ({t.strip()})rpc_handle_resolve(_h);\n"
            f"    }}\n"
        )
        return (decl, var)
    # scalar
    var = f"_v{idx}"
    decl = (
        f"    {t} {var} = 0;\n"
        f"    if (_off + sizeof({var}) > _body_len) goto _short_body;\n"
        f"    memcpy(&{var}, (const uint8_t *)_body + _off, sizeof({var}));\n"
        f"    _off += sizeof({var});\n"
    )
    return (decl, var)


def emit_skel(m: dict) -> str:
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    args = wire_args(m)

    decl_parts = []
    call_parts = ["&_local"]
    for i, a in enumerate(args):
        decl, expr = emit_unpack_arg(a, i)
        decl_parts.append(decl)
        call_parts.append(expr)
    decls = "".join(decl_parts)
    # Impl signature is (ctx, obj, hdrs, business...). call_parts holds
    # [&_local, obj, business...]; splice the parsed headers after obj.
    call_parts.insert(2, "_hdrs")
    call = ", ".join(call_parts)

    rt = f"struct {rid}_result"
    # gh#2: on error, write `u8 status=1, u32 msg_len, msg_bytes` so the
    # client can rebuild a yaafc_error with a real message instead of a
    # generic "remote impl returned error".
    err_pack = """\
        yaafc_error_print(stderr, "[skel] """ + slot + """", _r.error);
        const char *_msg = _r.error.msg ? _r.error.msg : "(no msg)";
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_ml > 256) _ml = 256;
        if (_resp_max < 1 + 4 + _ml) {
            yaafc_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return _resp_max >= 1 ? 1 : 0;
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        yaafc_error_destroy(_r.error);
        return 1 + 4 + _ml;
"""
    # Server-half tracing span: time the impl execution and emit
    # `trace=<id> op=skel.<slot> dur_us=<n>` (gated ydebug). Paired with
    # the caller's `op=rpc.<slot>` span, the difference is transport +
    # queueing time. The trace id is read after the call (still before
    # the bag is freed) so it reflects whatever the impl saw/injected.
    invoke_and_span = f"""\
    double span_start = yaafc_ytime_monotonic_sec();
    {rt} _r = {slot}({call});
    {{
        double span_us = (yaafc_ytime_monotonic_sec() - span_start) * 1e6;
        const char *span_trace = _hdrs ? yheaders_get(_hdrs, "trace_id") : "-";
        ydebug("span trace=%s op=skel.{slot} dur_us=%.0f", span_trace ? span_trace : "-", span_us);
        yspan_record("skel.{slot}", span_us);
    }}
    yheaders_free(_hdrs); _hdrs = NULL;
"""
    if vt is None:
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {{
{err_pack}    }}
    ((uint8_t *)_resp)[0] = 0;
    return 1;
"""
    elif is_string_ret(vt):
        # Pack u8 status=0, u32 len, len bytes. The impl returns an owned
        # heap string in `_r.value`; free it once it is on the wire.
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {{
{err_pack}    }}
    {{
        const char *_sv = _r.value ? _r.value : "";
        uint32_t _svlen = (uint32_t)strlen(_sv);
        if (_resp_max < 1 + 4 + (size_t)_svlen) {{ free(_r.value); return 0; }}
        ((uint8_t *)_resp)[0] = 0;
        memcpy((uint8_t *)_resp + 1, &_svlen, 4);
        if (_svlen) memcpy((uint8_t *)_resp + 5, _sv, _svlen);
        free(_r.value);
        return 1 + 4 + (size_t)_svlen;
    }}
"""
    else:
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return 0;
    if (YAAFC_IS_ERR(_r)) {{
{err_pack}    }}
    if (_resp_max < 1 + sizeof(_r.value)) return 0;
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return 1 + sizeof(_r.value);
"""

    return f"""\
static size_t {slot}_skel(const void *_body, size_t _body_len,
                          void *_resp, size_t _resp_max)
{{
    size_t _off = 0;
    struct ctx _local = {{0}};
    /* The framework header section is first on every CALL body — parse
     * it back into the `hdrs` argument before the packed business args. */
    struct yheaders *_hdrs = NULL;
    {{
        size_t _hconsumed = 0;
        _hdrs = yheaders_parse(_body, _body_len, &_hconsumed);
        if (!_hdrs) goto _short_body;
        _off = _hconsumed;
    }}
{decls}\
{body}\
_short_body:
    yheaders_free(_hdrs);
    if (_resp_max >= 1) ((uint8_t *)_resp)[0] = 1;
    return _resp_max >= 1 ? 1 : 0;
}}
"""


def emit_create_fn(cls: dict) -> str:
    accessor = cls["accessor"]
    qname = qualified_class(cls)
    return f"""\
struct object_ptr_result {qname}_create(struct ctx *ctx)
{{
    ydebug("class={qname}");
    struct class_ptr_result _kr = {accessor}();
    if (YAAFC_IS_ERR(_kr))
        return YAAFC_ERR(object_ptr, "{qname}_create: class accessor failed", _kr);
    /* A service dependency is acquired once and cached for the connection
     * (remote) / process (in-process) lifetime — no per-call create. */
    return rpc_object_acquire(ctx, _kr.value, "{qname}");
}}
"""


def emit_lookup_tables(model: dict, module: str) -> str:
    classes = model.get("classes", [])
    methods = model.get("methods", [])

    class_branches = "\n".join(
        f'    if (strcmp(name, "{qualified_class(c)}") == 0) return {c["accessor"]}();'
        for c in classes
    )

    accessor_section = f"""\
/* ---- {module}: class name → accessor (lazy) ---------------------- */

static struct class_ptr_result {module}_accessor_lookup(const char *name)
{{
{class_branches}
    return YAAFC_OK(class_ptr, NULL);
}}
"""

    if not methods:
        skel_install = ""
        skel_section = ""
        jinvoke_install = ""
    else:
        jinvoke_install = f"    jinvoke_add_lookup({module}_jinvoke_lookup);\n"
        skel_rows = ",\n".join(
            f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_skel}}'
            for m in methods
        )
        skel_section = f"""\

/* ---- {module}: slot → skel, name-keyed static data --------------- */

struct {module}_skel_row {{ const char *name; rpc_skel_fn fn; }};

static const struct {module}_skel_row {module}_skel_rows[] = {{
{skel_rows}
}};

static rpc_skel_fn {module}_skel_lookup(method_slot slot)
{{
    struct const_char_ptr_result nr = method_slot_name(slot);
    if (YAAFC_IS_ERR(nr)) {{ yaafc_error_destroy(nr.error); return NULL; }}
    const char *name = nr.value;
    for (size_t i = 0; i < sizeof({module}_skel_rows) / sizeof({module}_skel_rows[0]); ++i)
        if (strcmp({module}_skel_rows[i].name, name) == 0)
            return {module}_skel_rows[i].fn;
    return NULL;
}}
"""
        skel_install = f"    rpc_add_skel_lookup({module}_skel_lookup);\n"

    # Eagerly register every class right here in the constructor — on the
    # single main thread, before main() and therefore before any worker
    # thread is spawned. Each accessor lazily populates the shared class
    # and method-slot registries on first call; doing it now makes those
    # registries fully populated and strictly read-only by the time N
    # worker threads start serving, so first-touch registration can never
    # race across threads. Best-effort: a registration failure (OOM) is
    # left for the accessor to resurface at call time.
    prewarm_calls = "\n".join(
        f"    {{ struct class_ptr_result reg = {c['accessor']}();\n"
        f"      if (YAAFC_IS_ERR(reg)) yaafc_error_destroy(reg.error); }}"
        for c in classes
    )
    prewarm_section = (prewarm_calls + "\n") if prewarm_calls else ""

    return f"""\
{accessor_section}\
{skel_section}\

/* ---- {module}: install hooks before main ------------------------- */

__attribute__((constructor))
static void {module}_install_hooks(void)
{{
    struct yaafc_void_result _ar = class_add_accessor_lookup({module}_accessor_lookup);
    if (YAAFC_IS_ERR(_ar)) {{
        yaafc_error_print(stderr, "{module}_install_hooks", _ar.error);
        yaafc_error_destroy(_ar.error);
        abort();
    }}
{skel_install}\
{jinvoke_install}\
{prewarm_section}\
}}
"""


# emit_rpc_h removed; its content (the `*_create` prototypes) now
# lives in the consolidated public header emit_public_header writes.


def emit_jinvoke(m: dict) -> str:
    """One per method. Reads positional JSON args and calls the public
    stub, writing the return through the yjson_writer. The caller's
    `ctx` is forwarded straight into the stub: a zeroed/NULL ctx
    dispatches locally (yttp / cli own the object in-process), while a
    ctx carrying a live `session` forwards to the owning backend (the
    gateway, which has no local plugin objects). Scalar / string args
    only — the same restriction as the binary skel."""
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    # ctx, obj, hdrs (args[0:3]) are supplied by the dispatcher — only
    # the business tail (args[3:]) goes into the JSON args array.
    args = m["args"][3:]

    arg_reads = []
    call_parts = ["call_ctx", "obj", "hdrs"]
    for i, a in enumerate(args):
        t = a["type"].strip()
        var = f"arg{i}"
        if is_struct_ptr(a["type"]):
            arg_reads.append(
                f"    uint64_t {var} = (uint64_t)yjson_as_int(yjson_array_at(args, {i}), 0);\n"
                f"    (void){var}; /* struct-ptr args from JSON not supported yet */\n"
            )
            call_parts.append("NULL")
        elif "int64_t" in t or "uint64_t" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})yjson_as_int(yjson_array_at(args, {i}), 0);\n"
            )
            call_parts.append(var)
        elif "int" in t or "uint" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})yjson_as_int(yjson_array_at(args, {i}), 0);\n"
            )
            call_parts.append(var)
        elif "double" in t or "float" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})yjson_as_float(yjson_array_at(args, {i}), 0.0);\n"
            )
            call_parts.append(var)
        else:
            arg_reads.append(
                f"    const char *{var} = yjson_as_string(yjson_array_at(args, {i}), \"\");\n"
            )
            call_parts.append(var)

    call = ", ".join(call_parts)
    rt = f"struct {rid}_result"

    if vt is None:
        write_result = "    yjson_w_null(result);\n"
    elif vt in ("int", "int64_t", "uint32_t", "size_t"):
        write_result = "    yjson_w_int(result, (int64_t)call_result.value);\n"
    elif vt in ("double", "float"):
        write_result = "    yjson_w_float(result, (double)call_result.value);\n"
    elif vt == "char *":
        # yaafc_string: owned heap return — write it, then free it.
        write_result = (
            "    yjson_w_string(result, call_result.value ? call_result.value : \"\");\n"
            "    free(call_result.value);\n")
    elif vt.startswith("const char"):
        # Borrowed string (not owned) — write, do not free.
        write_result = "    yjson_w_string(result, call_result.value ? call_result.value : \"\");\n"
    else:
        write_result = ("    yjson_w_null(result);  "
                        "/* return type not yet supported in JSON */\n")

    return f"""\
static int {slot}_jinvoke(struct ctx *ctx, struct object *obj, struct yheaders *hdrs,
                          const struct yjson_value *args,
                          struct yjson_writer *result, char *err, size_t err_cap)
{{
{''.join(arg_reads)}\
    struct ctx local_ctx = {{0}};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    {rt} call_result = {slot}({call});
    if (YAAFC_IS_ERR(call_result)) {{
        snprintf(err, err_cap, "%s: %s", "{slot}",
                 call_result.error.msg ? call_result.error.msg : "<no message>");
        yaafc_error_destroy(call_result.error);
        return -1;
    }}
{write_result}\
    return 0;
}}
"""


def emit_jinvoke_table(model: dict, module: str) -> str:
    methods = model.get("methods", [])
    if not methods:
        return ""
    rows = ",\n".join(
        f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_jinvoke}}'
        for m in methods
    )
    return f"""\

/* ---- {module}: jinvoke table ------------------------------------ */

struct {module}_jinvoke_row {{ const char *name; jinvoke_fn fn; }};

static const struct {module}_jinvoke_row {module}_jinvoke_rows[] = {{
{rows}
}};

static jinvoke_fn {module}_jinvoke_lookup(const char *qname)
{{
    for (size_t i = 0;
         i < sizeof({module}_jinvoke_rows) / sizeof({module}_jinvoke_rows[0]); ++i)
        if (strcmp({module}_jinvoke_rows[i].name, qname) == 0)
            return {module}_jinvoke_rows[i].fn;
    return NULL;
}}
"""


def emit_rpc_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             '#include <yaafc/yclass/rpc.h>\n',
             '#include <yaafc/yclass/jinvoke.h>\n',
             '#include <yaafc/yclass/yheaders.h>\n',
             '#include <yaafc/yjson/yjson.h>\n',
             '#include <yaafc/ycore/result.h>\n',
             '#include <yaafc/ycore/ytrace.h>\n',
             '#include <yaafc/ycore/yspan.h>\n',
             '#include <yaafc/yclass/class.h>\n',
             f'#include "{module}.internal.h"\n',
             '#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n'
             '#include <string.h>\n\n']
    for m in model["methods"]:
        parts.append(emit_skel(m))
        parts.append("\n")
    for m in model["methods"]:
        parts.append(emit_jinvoke(m))
        parts.append("\n")
    for c in regular_classes(model):
        parts.append(emit_create_fn(c))
        parts.append("\n")
    # jinvoke table must come BEFORE the install-hooks constructor so
    # the constructor's reference to <module>_jinvoke_lookup resolves.
    parts.append(emit_jinvoke_table(model, module))
    parts.append(emit_lookup_tables(model, module))
    out_path.write_text("".join(parts))


def main():
    if len(sys.argv) < 5:
        sys.stderr.write(__doc__)
        sys.exit(2)
    module = sys.argv[1]
    include_base = Path(sys.argv[2])
    module_src = Path(sys.argv[3])
    sources = [Path(p) for p in sys.argv[4:]]

    # Plugin headers live under include/yaafc/plugin/<module>/ so the
    # plugin namespace is visually distinct from core headers
    # (yclass/, yengine/, etc.) — see CLAUDE.md.
    include_module = include_base / "yaafc" / "plugin" / module
    include_module.mkdir(parents=True, exist_ok=True)
    module_src.mkdir(parents=True, exist_ok=True)

    # Stub-create the .gen.c siblings + the combined public header
    # and the codegen-private internal header before the AST dump
    # runs, so the annotated TU's preprocess cleanly even on a cold
    # build (chicken-and-egg between AST input and codegen output).
    placeholder = '#include <yaafc/yclass/class.h>\n'
    pub_header = include_module / f"{module}.h"
    int_header = module_src / f"{module}.internal.h"
    if not pub_header.exists():
        pub_header.write_text(placeholder)
    if not int_header.exists():
        int_header.write_text(placeholder)
    for s in sources:
        if s.suffix == ".c":
            inc = module_src / (s.stem + ".gen.c")
            if not inc.exists():
                inc.write_text("")

    model = parse_sources([include_base, include_module, module_src], sources, module)
    for m in model["methods"]:
        validate_method(m)

    emit_public_header(model, module, pub_header)
    emit_internal_header(model, module, int_header)
    emit_methods_c(model, module, module_src / "methods.gen.c")
    emit_class_gen_c(model, module, module_src)
    emit_rpc_c(model, module, module_src / "rpc.gen.c")

    (module_src / "model.yaml").write_text(yaml_dump(model) + "\n")


if __name__ == "__main__":
    main()
