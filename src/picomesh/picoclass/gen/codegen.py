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
    """Map an impl's return type to the PICOMESH_RESULT_DECLARE identifier."""
    r = ret.strip()
    m = re.match(r"^struct\s+(\w+)_result\s*$", r)
    if m:
        return m.group(1)
    if r == "void":
        return "picomesh_void"
    if r == "int":
        return "picomesh_int"
    if r == "size_t":
        return "picomesh_size"
    if r == "int64_t":
        return "picomesh_int64"
    if r == "uint32_t":
        return "picomesh_uint32"
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
    if rid == "picomesh_void":
        return None
    if rid == "picomesh_int":
        return "int"
    if rid == "picomesh_size":
        return "size_t"
    if rid == "picomesh_int64":
        return "int64_t"
    if rid == "picomesh_uint32":
        return "uint32_t"
    if rid == "picomesh_string":
        return "char *"
    if rid == "picomesh_json":
        # JSON text on the wire is just a heap string; the JSON frontend
        # emits it raw (see emit_jinvoke), every other path treats it as a
        # string (is_string_ret keys off this "char *").
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
    # Honour PICOMESH_CODEGEN_EXTRA_INCLUDES — caller (the cmake custom-command,
    # or the developer driving codegen.py manually) hands us the same -I
    # search path the eventual compilation uses, so #include directives in
    # the annotated source resolve at AST-dump time. Without this we'd error
    # before any model parsing starts.
    extra = os.environ.get("PICOMESH_CODEGEN_EXTRA_INCLUDES", "")
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
    # just the macro identifier (e.g. begin=end=offset-of-`PICOMESH_CLASS_
    # ANNOTATE`, tokLen=len("PICOMESH_CLASS_ANNOTATE")). The annotation
    # string sits AFTER that token in `("foo")`. Read a generous window
    # past the token-end so the regex catches the quoted string. 512
    # bytes is plenty — annotation strings are short and the macro call
    # fits on one source line in practice.
    stop = end_off + end_tok + 512
    blob = src[beg_off:stop].decode("utf-8", errors="replace")
    # Match either the raw `[[clang::annotate("x")]]` form or the
    # PICOMESH_CLASS_ANNOTATE("x") macro form. The macro keeps plugin
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
# String RETURN buffer (client _wbuf). Sized to the yrpc frame max so a
# large string response (e.g. the trace collector's whole-trace JSON) is
# not truncated by an artificially small client buffer — the only cap is
# then the transport frame itself. Lives on the worker coroutine's 256 KiB
# stack, so a 64 KiB buffer is free (no heap alloc on the call hot path).
# Kept separate from WIRE_STRING_MAX (the per-arg unpack buffer) so raising
# the response size does not bloat every string-arg stack slot.
WIRE_STRING_RESP_MAX = 65531  # 1 (status) + 4 (len) + this == 65536 frame max
ERROR_TEXT_MAX = 8192


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
    for known in ("ctx", "object", "class", "picomesh_str"):
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
    guard = f"PICOMESH_PLUGIN_{module.upper()}_H"
    parts = [HEADER,
             f"/* Public interface for plugin `{module}` — GENERATED.\n"
             f" * Edit the annotated sources under src/picomesh/plugins/{module}/. */\n",
             f"#ifndef {guard}\n#define {guard}\n\n",
             '#include <picomesh/picoclass/class.h>\n',
             '#include <picomesh/picoclass/rpc.h>\n\n']

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

    # Registration entry point — the driver calls this ONLY for plugins the
    # running instance activated via config (registration is activation; an
    # un-activated plugin's classes never enter the registry). Replaces the
    # old __attribute__((constructor)) auto-registration.
    parts.append("\n/* ---- activation ---- */\n")
    parts.append(f"struct picomesh_void_result picomesh_plugin_{module}_register(void);\n")

    parts.append("\n#endif\n")
    out_path.write_text("".join(parts))


def emit_internal_header(model: dict, module: str, out_path: Path):
    """Codegen-private header — lives in the module's src dir, never
    included by external consumers. Holds the function-pointer typedefs
    the generated dispatch tables need.
    """
    guard = f"PICOMESH_{module.upper()}_INTERNAL_H"
    parts = [HEADER,
             f"/* Internal codegen-only header for plugin `{module}`.\n"
             f" * NEVER include this from outside src/picomesh/plugins/{module}/. */\n",
             f"#ifndef {guard}\n#define {guard}\n\n",
             f'#include <picomesh/plugin/{module}/{module}.h>\n\n']

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
    # `_a` is a pooled heap buffer of `_acap` bytes (not a stack array), so the
    # capacity check uses `_acap`, not sizeof(_a). On a pack overflow the client
    # span was already begun; end it (ok=0) and bail through the single cleanup
    # exit so the pooled buffers are returned.
    fail = (f"{{ ytelemetry_span_end(&_tsp, 0, \"{slot}: pack overflow\");"
            f" _ret = PICOMESH_ERR({rid}, \"{slot}: pack overflow\"); goto _rpc_done; }}")
    if is_string_arg(t):
        return (
            f"{indent}{{\n"
            f"{indent}    uint32_t _slen = (uint32_t)({name} ? strlen({name}) : 0);\n"
            f"{indent}    if (_off + 4 + _slen > _acap)\n"
            f"{indent}        {fail}\n"
            f"{indent}    memcpy(_a + _off, &_slen, 4); _off += 4;\n"
            f"{indent}    if (_slen) {{ memcpy(_a + _off, {name}, _slen); _off += _slen; }}\n"
            f"{indent}}}\n"
        )
    if is_struct_ptr(t):
        # obj handle
        return (
            f"{indent}{{\n"
            f"{indent}    uint64_t _h = *(uint64_t *)((char *){name} + sizeof(*{name}));\n"
            f"{indent}    if (_off + 8 > _acap)\n"
            f"{indent}        {fail}\n"
            f"{indent}    memcpy(_a + _off, &_h, 8); _off += 8;\n"
            f"{indent}}}\n"
        )
    return (
        f"{indent}if (_off + sizeof({name}) > _acap)\n"
        f"{indent}    {fail}\n"
        f"{indent}memcpy(_a + _off, &{name}, sizeof({name})); _off += sizeof({name});\n"
    )


def dotted_path(m: dict) -> str:
    """The dotted "service.class.method" path a foreign msgpack service
    receives. Inverse of the inbound resolver's dotted→underscore mapping:
    service=domain, class=owning_class, method=slot minus the class prefix
    (the codegen slot is `<class>_<method>`)."""
    domain = m["domain"]
    cls = m.get("owning_class") or ""
    slot = m["slot"]
    if not cls:
        return f"{domain}.{slot}"
    prefix = cls + "_"
    method = slot[len(prefix):] if slot.startswith(prefix) else slot
    return f"{domain}.{cls}.{method}"


def emit_mpack_write_arg(a: dict) -> str:
    """Encode one positional arg into the msgpack args array (`_maw`),
    preserving width/signedness. Same type mapping as the inbound unpack."""
    t = a["type"].strip()
    name = a["name"]
    if is_string_arg(t):
        return (f"            cmp_write_str(&_maw, {name} ? {name} : \"\", "
                f"(uint32_t)({name} ? strlen({name}) : 0));\n")
    if t in ("uint32_t", "uint16_t", "uint8_t", "size_t", "uint64_t"):
        return f"            cmp_write_uinteger(&_maw, (uint64_t){name});\n"
    if t in ("double", "float"):
        return f"            cmp_write_decimal(&_maw, (double){name});\n"
    return f"            cmp_write_integer(&_maw, (int64_t){name});\n"


def emit_mpack_read_result(rid: str, vt, slot: str) -> str:
    """Decode the response `result` value (reader `_mrr` over `_mresp`/`_mrlen`)
    into the local `_mret` result. Mirrors emit_minvoke_write_result."""
    if vt is None:
        return "            (void)_mrr; (void)_mrb;\n            _mret = PICOMESH_OK_VOID();\n"
    if vt == "char *":
        return (
            "            uint32_t _mssz = 0;\n"
            "            if (!cmp_read_str_size(&_mrr, &_mssz)) {\n"
            f"                _mret = PICOMESH_ERR({rid}, \"{slot}: bad msgpack string result\");\n"
            "            } else if (_mrb.offset + _mssz > _mrlen) {\n"
            f"                _mret = PICOMESH_ERR({rid}, \"{slot}: truncated msgpack string\");\n"
            "            } else {\n"
            "                char *_msv = malloc((size_t)_mssz + 1);\n"
            f"                if (!_msv) _mret = PICOMESH_ERR({rid}, \"{slot}: out of memory\");\n"
            "                else {\n"
            "                    if (_mssz) memcpy(_msv, _mresp + _mrb.offset, _mssz);\n"
            "                    _msv[_mssz] = 0;\n"
            f"                    _mret = PICOMESH_OK({rid}, _msv);\n"
            "                }\n"
            "            }\n"
        )
    if vt in ("uint32_t", "size_t", "uint64_t", "uint16_t", "uint8_t"):
        return (
            "            uint64_t _mv = 0;\n"
            "            if (!cmp_read_uinteger(&_mrr, &_mv))\n"
            f"                _mret = PICOMESH_ERR({rid}, \"{slot}: bad msgpack uint result\");\n"
            f"            else _mret = PICOMESH_OK({rid}, ({vt})_mv);\n"
        )
    if vt in ("int", "int64_t", "int32_t", "int16_t", "int8_t", "short", "long"):
        return (
            "            int64_t _mv = 0;\n"
            "            if (!cmp_read_integer(&_mrr, &_mv))\n"
            f"                _mret = PICOMESH_ERR({rid}, \"{slot}: bad msgpack int result\");\n"
            f"            else _mret = PICOMESH_OK({rid}, ({vt})_mv);\n"
        )
    if vt in ("double", "float"):
        return (
            "            double _mv = 0;\n"
            "            if (!cmp_read_decimal(&_mrr, &_mv))\n"
            f"                _mret = PICOMESH_ERR({rid}, \"{slot}: bad msgpack float result\");\n"
            f"            else _mret = PICOMESH_OK({rid}, ({vt})_mv);\n"
        )
    return f"            _mret = PICOMESH_ERR({rid}, \"{slot}: msgpack return type unsupported\");\n"


def emit_msgpack_remote_branch(m: dict) -> str:
    """Outbound msgpack client path, taken when the peer is a msgpack channel.
    Encodes the positional args, makes one envelope round-trip, decodes the
    result. Heap buffers (off the hot path) keep the stub's stack small."""
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    slot = qualified_slot(m)
    user_args = m["args"][3:]
    n = len(user_args)
    hdrs_name = m["args"][2]["name"]
    path = dotted_path(m)
    resp_sz = (8 + WIRE_STRING_RESP_MAX) if is_string_ret(vt) else 256
    writes = "".join(emit_mpack_write_arg(a) for a in user_args)
    read = emit_mpack_read_result(rid, vt, slot)
    return f"""\
        if (peer_channel_is_msgpack(_s->peer)) {{
            struct {rid}_result _mret;
            uint8_t *_margs = malloc({WIRE_ARG_BUFFER_BYTES});
            uint8_t *_mresp = malloc({resp_sz});
            if (!_margs || !_mresp) {{
                free(_margs); free(_mresp);
                return PICOMESH_ERR({rid}, "{slot}: out of memory");
            }}
            struct picomesh_msgpack_buffer _mab;
            cmp_ctx_t _maw;
            picomesh_msgpack_writer_init(&_maw, &_mab, _margs, {WIRE_ARG_BUFFER_BYTES});
            cmp_write_array(&_maw, {n}u);
{writes}\
            size_t _mrlen = 0;
            char _merr[{ERROR_TEXT_MAX}] = {{0}};
            if (!peer_channel_msgpack_call(_s->peer, "{path}", {hdrs_name},
                                           _margs, _mab.offset, _mresp, {resp_sz},
                                           &_mrlen, _merr, sizeof(_merr))) {{
                _mret = _merr[0]
                            ? PICOMESH_ERR_OWNED({rid}, strdup(_merr))
                            : PICOMESH_ERR({rid}, "{slot}: msgpack call failed");
            }} else {{
                struct picomesh_msgpack_buffer _mrb;
                cmp_ctx_t _mrr;
                picomesh_msgpack_reader_init(&_mrr, &_mrb, _mresp, _mrlen);
{read}\
            }}
            free(_margs); free(_mresp);
            return _mret;
        }}
"""


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
    msgpack_branch = emit_msgpack_remote_branch(m)

    # Wire response: u8 status; status==0 → value_bytes; status==1 →
    # u32 msg_len + msg_bytes (gh#2: preserve structured remote errors).
    # The buffer is sized to hold the bigger of value or error payload.
    # The outbound call is wrapped in a CLIENT span (begun above, before the
    # header bag was serialized so the remote peer parents its server span to
    # it). ytelemetry_span_end records duration + status, ships the span to the
    # collector (best-effort, non-blocking) and feeds the local /_perf
    # aggregate. The remote peer emits the matching `skel.<slot>` server span.
    # String returns can be long, so the response buffer must hold
    # u8 status + u32 len + up to WIRE_STRING_MAX value bytes. Scalar /
    # error responses fit comfortably in the small buffer.
    wbuf_sz = (1 + 4 + WIRE_STRING_RESP_MAX) if is_string_ret(vt) else (1 + 4 + ERROR_TEXT_MAX)
    # `_wbuf` is a pooled heap buffer (allocated in the body), so the rpc_call
    # capacity is the literal size, not sizeof(_wbuf). Every exit sets `_ret`
    # and `goto _rpc_done` so the pooled _a/_wbuf are returned exactly once.
    timed_rpc = f"""\
        size_t _wn = rpc_call(_s->peer, RPC_OP_CALL, _rid, _a, _off,
                              _wbuf, {wbuf_sz});
        ytelemetry_span_end(&_tsp, _wn >= 1 && _wbuf[0] == 0, NULL);
"""
    err_parse = f"""\
        if (_wn < 1) {{ _ret = PICOMESH_ERR({rid}, "{slot_qname}: short RPC response"); goto _rpc_done; }}
        if (_wbuf[0] != 0) {{
            uint32_t _msg_len = 0;
            if (_wn >= 5) memcpy(&_msg_len, _wbuf + 1, 4);
            char _msg[{ERROR_TEXT_MAX + 1}];
            size_t _copy = _msg_len < sizeof(_msg) - 1 ? _msg_len : sizeof(_msg) - 1;
            if (_wn >= 5 + _copy) memcpy(_msg, _wbuf + 5, _copy);
            _msg[_copy] = 0;
            _ret = _msg[0]
                       ? PICOMESH_ERR_OWNED({rid}, strdup(_msg))
                       : PICOMESH_ERR({rid}, "{slot_qname}: remote error (no msg)");
            goto _rpc_done;
        }}
"""
    if vt is None:
        remote_call = timed_rpc + err_parse + "        _ret = PICOMESH_OK_VOID(); goto _rpc_done;\n"
    elif is_string_ret(vt):
        # Unpack u32 len + bytes into an owned heap string the caller frees.
        remote_call = timed_rpc + err_parse + f"""\
        if (_wn < 5) {{ _ret = PICOMESH_ERR({rid}, "{slot_qname}: truncated string response"); goto _rpc_done; }}
        uint32_t _slen;
        memcpy(&_slen, _wbuf + 1, 4);
        if (_wn < (size_t)5 + _slen) {{ _ret = PICOMESH_ERR({rid}, "{slot_qname}: truncated string payload"); goto _rpc_done; }}
        char *_sv = malloc((size_t)_slen + 1);
        if (!_sv) {{ _ret = PICOMESH_ERR({rid}, "{slot_qname}: out of memory"); goto _rpc_done; }}
        if (_slen) memcpy(_sv, _wbuf + 5, _slen);
        _sv[_slen] = 0;
        _ret = PICOMESH_OK({rid}, _sv); goto _rpc_done;
"""
    else:
        remote_call = timed_rpc + err_parse + f"""\
        if (_wn != 1 + sizeof({vt})) {{ _ret = PICOMESH_ERR({rid}, "{slot_qname}: truncated RPC payload"); goto _rpc_done; }}
        {vt} _v;
        memcpy(&_v, _wbuf + 1, sizeof(_v));
        _ret = PICOMESH_OK({rid}, _v); goto _rpc_done;
"""

    return f"""\
    static method_slot _slot = METHOD_SLOT_UNDEFINED;
    if (_slot == METHOD_SLOT_UNDEFINED) {{
        struct method_slot_result _sr =
            method_slot_get("{m['domain']}", (method_id_t){slot_qname});
        if (PICOMESH_IS_ERR(_sr))
            return PICOMESH_ERR({rid}, "{slot_qname}: method_slot_get failed", _sr);
        _slot = _sr.value;
    }}

    if (!{obj_name}) return PICOMESH_ERR({rid}, "{slot_qname}: NULL object");

    struct ctx *_s = {ctx_name};
    if (_s && _s->peer) {{
{msgpack_branch}\
        struct picomesh_uint32_result _rid_res = peer_channel_ensure_remote_id(_s->peer, _slot);
        if (PICOMESH_IS_ERR(_rid_res))
            return PICOMESH_ERR({rid}, "{slot_qname}: ensure remote id failed", _rid_res);
        uint32_t _rid = _rid_res.value;
        if (_rid == RPC_REMOTE_ID_UNRESOLVED)
            return PICOMESH_ERR({rid}, "{slot_qname}: remote id unresolved");
        /* Wire scratch comes from THIS THREAD's pool, not the stack: the arg
         * buffer (_a, _acap bytes) and response buffer (_wbuf) are large
         * (~16 KiB + up to 64 KiB), and a nested chain of in-process hops would
         * overflow the fixed-size coroutine stack if these were locals. Every
         * exit below routes through _rpc_done, which returns both to the pool
         * exactly once (free(NULL) is a no-op). */
        struct picomesh_allocator *_pool = picomesh_allocator_thread();
        size_t _acap = {WIRE_ARG_BUFFER_BYTES};
        struct {rid}_result _ret;
        uint8_t *_a = (uint8_t *)picomesh_allocator_alloc(_pool, _acap);
        uint8_t *_wbuf = (uint8_t *)picomesh_allocator_alloc(_pool, {wbuf_sz});
        if (!_a || !_wbuf) {{
            picomesh_allocator_free(_pool, _a);
            picomesh_allocator_free(_pool, _wbuf);
            return PICOMESH_ERR({rid}, "{slot_qname}: wire scratch alloc failed");
        }}
        size_t _off = 0;
        /* Client span for this downstream call. Minted BEFORE the header
         * bag is serialized so the wire carries this span as the remote
         * peer's parent. */
        struct ytelemetry_span _tsp;
        ytelemetry_client_span_begin(&_tsp, {hdrs_name}, "rpc.{slot_qname}");
        /* Headers section: the FRAMEWORK serializes the request-header
         * bag (uid, trace context, or anything a caller injected) ahead
         * of the packed business args. The skel parses it straight back
         * into the `hdrs` argument. ytelemetry_client_serialize_headers swaps in
         * this client span's id as parent_span_id across the serialize. */
        {{
            size_t _hn = ytelemetry_client_serialize_headers(&_tsp, {hdrs_name}, _a, _acap);
            if (_hn == 0) {{
                ytelemetry_span_end(&_tsp, 0, "{slot_qname}: header serialize overflow");
                _ret = PICOMESH_ERR({rid}, "{slot_qname}: header serialize overflow");
                goto _rpc_done;
            }}
            _off = _hn;
        }}
{pack_block}\
{remote_call}\
    _rpc_done:
        picomesh_allocator_free(_pool, _a);
        picomesh_allocator_free(_pool, _wbuf);
        return _ret;
    }} else {{
        impl_t fn = class_dispatch_lookup(object_class({obj_name}), _slot);
        if (!fn) return PICOMESH_ERR({rid}, "{slot_qname}: no impl on this class");
        return (({slot_fn})fn)({call_args});
    }}
"""


def emit_methods_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             f'#include "{module}.internal.h"\n',
             '#include <picomesh/core/result.h>\n',
             '#include <picomesh/core/ytrace.h>\n',
             '#include <picomesh/core/yspan.h>\n',
             '#include <picomesh/core/ytelemetry.h>\n',
             '#include <picomesh/picoclass/rpc.h>\n',
             '#include <picomesh/picoclass/yheaders.h>\n',
             '#include <picomesh/msgpack/msgpack.h>\n',
             '#include <picomesh/allocator/allocator.h>\n',
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
            f"    if (PICOMESH_IS_ERR(_parent_r))\n"
            f"        return PICOMESH_ERR(class_ptr, \"{qname}_class_get: parent accessor failed\", _parent_r);\n"
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
                f"    if (PICOMESH_IS_ERR(_mixin{i}_r))\n"
                f"        return PICOMESH_ERR(class_ptr, \"{qname}_class_get: mixin{i} accessor failed\", _mixin{i}_r);\n"
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
    if (cls) return PICOMESH_OK(class_ptr, cls);
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
    if (PICOMESH_IS_ERR(_r))
        return PICOMESH_ERR(class_ptr, "{qname}_class_get: class_register failed", _r);
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
                needed.add(f"<picomesh/plugin/{p['domain']}/{p['domain']}.h>")
            for mx in c.get("mixins", []):
                if mx["domain"] != module:
                    needed.add(f"<picomesh/plugin/{mx['domain']}/{mx['domain']}.h>")
            for op in c.get("ops", []):
                sd = op.get("slot_domain", c["domain"])
                if sd != module:
                    needed.add(f"<picomesh/plugin/{sd}/{sd}.h>")
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
    return f"picomesh/plugin/{module}/{module}.h"


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
    # client can rebuild a picomesh_error with a real message instead of a
    # generic "remote impl returned error".
    err_pack = """\
        char _errbuf[""" + str(ERROR_TEXT_MAX) + """] = {0};
        picomesh_error_snprint(_errbuf, sizeof(_errbuf), _r.error);
        const char *_msg = _errbuf[0] ? _errbuf : (_r.error.msg ? _r.error.msg : "(no msg)");
        uint32_t _ml = (uint32_t)strlen(_msg);
        if (_resp_max <= 5) _ml = 0;
        else if (_ml > _resp_max - 5) _ml = (uint32_t)(_resp_max - 5);
        if (_resp_max < 1 + 4 + _ml) {
            picomesh_error_destroy(_r.error);
            ((uint8_t *)_resp)[0] = 1;
            return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
        }
        ((uint8_t *)_resp)[0] = 1;
        memcpy((uint8_t *)_resp + 1, &_ml, 4);
        memcpy((uint8_t *)_resp + 5, _msg, _ml);
        picomesh_error_destroy(_r.error);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + _ml));
"""
    # Server-half tracing span: ytelemetry_server_span_begin reads the inbound
    # trace context (trace_id + parent_span_id) from the header bag, mints
    # this hop's span_id and rewrites the bag's parent_span_id to it so the
    # impl's downstream calls nest beneath this span. ytelemetry_span_end records
    # duration + status, ships the span to the collector and feeds the local
    # /_perf aggregate. Paired with the caller's `rpc.<slot>` client span,
    # the difference is transport + queueing time.
    invoke_and_span = f"""\
    struct ytelemetry_span _tsp;
    ytelemetry_server_span_begin(&_tsp, _hdrs, "skel.{slot}");
    {rt} _r = {slot}({call});
    ytelemetry_span_end(&_tsp, !PICOMESH_IS_ERR(_r), PICOMESH_IS_ERR(_r) ? _r.error.msg : NULL);
    yheaders_free(_hdrs); _hdrs = NULL;
"""
    if vt is None:
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "{slot}_skel: response buffer too small");
    if (PICOMESH_IS_ERR(_r)) {{
{err_pack}    }}
    ((uint8_t *)_resp)[0] = 0;
    return PICOMESH_OK(picomesh_size, 1);
"""
    elif is_string_ret(vt):
        # Pack u8 status=0, u32 len, len bytes. The impl returns an owned
        # heap string in `_r.value`; free it once it is on the wire.
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "{slot}_skel: response buffer too small");
    if (PICOMESH_IS_ERR(_r)) {{
{err_pack}    }}
    {{
        const char *_sv = _r.value ? _r.value : "";
        uint32_t _svlen = (uint32_t)strlen(_sv);
        if (_resp_max < 1 + 4 + (size_t)_svlen) {{ free(_r.value); return PICOMESH_ERR(picomesh_size, "{slot}_skel: response buffer too small"); }}
        ((uint8_t *)_resp)[0] = 0;
        memcpy((uint8_t *)_resp + 1, &_svlen, 4);
        if (_svlen) memcpy((uint8_t *)_resp + 5, _sv, _svlen);
        free(_r.value);
        return PICOMESH_OK(picomesh_size, (size_t)(1 + 4 + (size_t)_svlen));
    }}
"""
    else:
        body = invoke_and_span + f"""\
    if (_resp_max < 1) return PICOMESH_ERR(picomesh_size, "{slot}_skel: response buffer too small");
    if (PICOMESH_IS_ERR(_r)) {{
{err_pack}    }}
    if (_resp_max < 1 + sizeof(_r.value)) return PICOMESH_ERR(picomesh_size, "{slot}_skel: response buffer too small");
    ((uint8_t *)_resp)[0] = 0;
    memcpy((uint8_t *)_resp + 1, &_r.value, sizeof(_r.value));
    return PICOMESH_OK(picomesh_size, (size_t)(1 + sizeof(_r.value)));
"""

    return f"""\
static struct picomesh_size_result {slot}_skel(const void *_body, size_t _body_len,
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
    return PICOMESH_OK(picomesh_size, _resp_max >= 1 ? 1u : 0u);
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
    if (PICOMESH_IS_ERR(_kr))
        return PICOMESH_ERR(object_ptr, "{qname}_create: class accessor failed", _kr);
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
    return PICOMESH_OK(class_ptr, NULL);
}}
"""

    if not methods:
        skel_install = ""
        skel_section = ""
        jinvoke_install = ""
        minvoke_install = ""
        params_install = ""
    else:
        jinvoke_install = f"    jinvoke_add_lookup({module}_jinvoke_lookup);\n"
        minvoke_install = f"    minvoke_add_lookup({module}_minvoke_lookup);\n"
        params_install = f"    jinvoke_params_add_lookup({module}_params_lookup);\n"
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

static rpc_skel_fn {module}_skel_lookup(const char *name)
{{
    /* rpc_skel_for has already resolved the slot to its qname (the only
     * Result-returning step), so this hook is a pure name→fn lookup that
     * never has to swallow an error. */
    for (size_t i = 0; i < sizeof({module}_skel_rows) / sizeof({module}_skel_rows[0]); ++i)
        if (strcmp({module}_skel_rows[i].name, name) == 0)
            return {module}_skel_rows[i].fn;
    return NULL;
}}
"""
        skel_install = f"    rpc_add_skel_lookup({module}_skel_lookup);\n"

    # Eagerly register every class right here in the register entry point —
    # called on the single main thread at startup, before any worker thread
    # is spawned. Each accessor lazily populates the shared class and
    # method-slot registries on first call; doing it now makes those
    # registries fully populated and strictly read-only by the time N
    # worker threads start serving, so first-touch registration can never
    # race across threads. Best-effort: a registration failure (OOM) is
    # surfaced immediately: a prewarm failure (e.g. OOM) is propagated out of
    # register() rather than swallowed, so the driver fails activation loudly.
    prewarm_calls = "\n".join(
        f"    {{ struct class_ptr_result reg = {c['accessor']}();\n"
        f'      PICOMESH_RETURN_IF_ERR(picomesh_void, reg, "{module} register: prewarm {c["accessor"]}"); }}'
        for c in classes
    )
    prewarm_section = (prewarm_calls + "\n") if prewarm_calls else ""

    # Registration is ACTIVATION: this is an explicit, exported entry point
    # the driver calls ONLY for plugins the running instance activated via
    # config (mesh.services.<self>.plugins / top-level plugins: / --plugins).
    # A plugin that is compiled in but NOT activated is never registered, so
    # its classes never enter the registry and no frontend can reach them —
    # "installed != exposed". This deliberately replaces the old
    # __attribute__((constructor)) auto-registration, which exposed every
    # linked plugin regardless of config.
    return f"""\
{accessor_section}\
{skel_section}\

/* ---- {module}: registration entry point (called from the driver for
 *      config-ACTIVATED plugins only — registration is activation) ---- */

struct picomesh_void_result picomesh_plugin_{module}_register(void)
{{
    struct picomesh_void_result _ar = class_add_accessor_lookup({module}_accessor_lookup);
    PICOMESH_RETURN_IF_ERR(picomesh_void, _ar,
                           "picomesh_plugin_{module}_register: add accessor lookup");
{skel_install}\
{jinvoke_install}\
{minvoke_install}\
{params_install}\
{prewarm_section}\
    return PICOMESH_OK_VOID();
}}
"""


# emit_rpc_h removed; its content (the `*_create` prototypes) now
# lives in the consolidated public header emit_public_header writes.


def emit_jinvoke(m: dict) -> str:
    """One per method. Reads positional JSON args and calls the public
    stub, writing the return through the json_writer. The caller's
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
                f"    uint64_t {var} = (uint64_t)json_as_int(json_array_at(args, {i}), 0);\n"
                f"    (void){var}; /* struct-ptr args from JSON not supported yet */\n"
            )
            call_parts.append("NULL")
        elif "int64_t" in t or "uint64_t" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})json_as_int(json_array_at(args, {i}), 0);\n"
            )
            call_parts.append(var)
        elif "int" in t or "uint" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})json_as_int(json_array_at(args, {i}), 0);\n"
            )
            call_parts.append(var)
        elif "double" in t or "float" in t:
            arg_reads.append(
                f"    {t} {var} = ({t})json_as_float(json_array_at(args, {i}), 0.0);\n"
            )
            call_parts.append(var)
        else:
            arg_reads.append(
                f"    const char *{var} = json_as_string(json_array_at(args, {i}), \"\");\n"
            )
            call_parts.append(var)

    call = ", ".join(call_parts)
    rt = f"struct {rid}_result"

    if rid == "picomesh_json":
        # Owned heap string that already IS serialized JSON — emit verbatim
        # (no quoting) so a list method returns a real JSON array, then free.
        write_result = (
            "    json_writer_raw(result, call_result.value ? call_result.value : \"null\");\n"
            "    free(call_result.value);\n")
    elif vt is None:
        write_result = "    json_writer_null(result);\n"
    elif vt in ("int", "int64_t", "uint32_t", "size_t"):
        write_result = "    json_writer_int(result, (int64_t)call_result.value);\n"
    elif vt in ("double", "float"):
        write_result = "    json_writer_float(result, (double)call_result.value);\n"
    elif vt == "char *":
        # picomesh_string: owned heap return — write it, then free it.
        write_result = (
            "    json_writer_string(result, call_result.value ? call_result.value : \"\");\n"
            "    free(call_result.value);\n")
    elif vt.startswith("const char"):
        # Borrowed string (not owned) — write, do not free.
        write_result = "    json_writer_string(result, call_result.value ? call_result.value : \"\");\n"
    else:
        write_result = ("    json_writer_null(result);  "
                        "/* return type not yet supported in JSON */\n")

    return f"""\
static struct picomesh_void_result {slot}_jinvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, const struct json_value *args,
                          struct json_writer *result, char *err, size_t err_cap)
{{
    yinfo("[rpc] {slot}");
{''.join(arg_reads)}\
    struct ctx local_ctx = {{0}};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    {rt} call_result = {slot}({call});
    if (PICOMESH_IS_ERR(call_result)) {{
        char chain[{ERROR_TEXT_MAX}] = {{0}};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(err, err_cap, "%s: %s", "{slot}",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "{slot}", call_result);
    }}
{write_result}\
    return PICOMESH_OK_VOID();
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


def emit_params_table(model: dict, module: str) -> str:
    """Per-method USER parameter signatures (args after ctx/obj/hdrs),
    baked into the binary so /_describe can reflect the call shape at
    runtime — the deployment image carries no model.yaml. Names+types are
    emitted in declared (positional) order."""
    methods = model.get("methods", [])
    if not methods:
        return ""
    blocks = []
    rows = []
    for m in methods:
        slot = qualified_slot(m)
        user = m["args"][3:]
        if user:
            items = ",\n".join(
                f'    {{"{a["name"]}", "{a["type"]}"}}' for a in user
            )
            blocks.append(
                f"static const struct jinvoke_param {slot}_params[] = {{\n{items}\n}};\n"
            )
            rows.append(f'    {{"{slot}", {{{slot}_params, {len(user)}}}}}')
        else:
            rows.append(f'    {{"{slot}", {{NULL, 0}}}}')
    rows_s = ",\n".join(rows)
    blocks_s = "".join(blocks)
    return f"""\

/* ---- {module}: per-method parameter signatures (runtime reflection) -- */

{blocks_s}\
struct {module}_params_row {{ const char *name; struct jinvoke_params params; }};

static const struct {module}_params_row {module}_params_rows[] = {{
{rows_s}
}};

static const struct jinvoke_params *{module}_params_lookup(const char *qname)
{{
    for (size_t i = 0;
         i < sizeof({module}_params_rows) / sizeof({module}_params_rows[0]); ++i)
        if (strcmp({module}_params_rows[i].name, qname) == 0)
            return &{module}_params_rows[i].params;
    return NULL;
}}
"""


def emit_munpack_arg(a: dict, idx: int) -> tuple:
    """Emit the decl + msgpack read for user arg #idx, read from the cmp
    reader `_mr` with per-type width/signedness/range validation. On a read,
    type or range failure it sets `_err` and `return -1;`. Returns
    (decl_block, call_expr)."""
    t = a["type"].strip()
    name = a["name"]
    var = f"_v{idx}"
    if is_string_arg(t):
        return (
            f"    char {var}[{WIRE_STRING_MAX}];\n"
            f"    {{\n"
            f"        uint32_t _sz = (uint32_t)sizeof({var});\n"
            f"        if (!cmp_read_str(_mr, {var}, &_sz)) {{\n"
            f'            snprintf(_err, _err_cap, "{name}: expected str arg (%s)", cmp_strerror(_mr));\n'
            f'            return PICOMESH_ERR(picomesh_void, "minvoke: bad argument");\n'
            f"        }}\n"
            f"    }}\n",
            var,
        )
    if t in ("uint32_t", "uint16_t", "uint8_t"):
        cap = {"uint32_t": "UINT32_MAX", "uint16_t": "UINT16_MAX", "uint8_t": "UINT8_MAX"}[t]
        return (
            f"    {t} {var};\n"
            f"    {{\n"
            f"        uint64_t _u;\n"
            f'        if (!cmp_read_uinteger(_mr, &_u)) {{ snprintf(_err, _err_cap, "{name}: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
            f'        if (_u > {cap}) {{ snprintf(_err, _err_cap, "{name}: value %llu out of range for {t}", (unsigned long long)_u); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
            f"        {var} = ({t})_u;\n"
            f"    }}\n",
            var,
        )
    if t in ("size_t", "uint64_t"):
        return (
            f"    {t} {var};\n"
            f"    {{\n"
            f"        uint64_t _u;\n"
            f'        if (!cmp_read_uinteger(_mr, &_u)) {{ snprintf(_err, _err_cap, "{name}: expected unsigned int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
            f"        {var} = ({t})_u;\n"
            f"    }}\n",
            var,
        )
    if t == "int64_t":
        return (
            f"    int64_t {var};\n"
            f'    if (!cmp_read_integer(_mr, &{var})) {{ snprintf(_err, _err_cap, "{name}: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n',
            var,
        )
    if t == "float":
        return (
            f"    float {var};\n"
            f"    {{\n"
            f"        double _d;\n"
            f'        if (!cmp_read_decimal(_mr, &_d)) {{ snprintf(_err, _err_cap, "{name}: expected float (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
            f"        {var} = (float)_d;\n"
            f"    }}\n",
            var,
        )
    if t == "double":
        return (
            f"    double {var};\n"
            f'    if (!cmp_read_decimal(_mr, &{var})) {{ snprintf(_err, _err_cap, "{name}: expected float (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n',
            var,
        )
    # Remaining signed integer types (int, int32_t, int16_t, int8_t, short,
    # …): read as int64, range-check against the target's limits.
    limits = {
        "int": ("INT_MIN", "INT_MAX"),
        "int32_t": ("INT32_MIN", "INT32_MAX"),
        "int16_t": ("INT16_MIN", "INT16_MAX"),
        "int8_t": ("INT8_MIN", "INT8_MAX"),
        "short": ("SHRT_MIN", "SHRT_MAX"),
        "long": ("LONG_MIN", "LONG_MAX"),
    }
    lo, hi = limits.get(t, ("INT_MIN", "INT_MAX"))
    return (
        f"    {t} {var};\n"
        f"    {{\n"
        f"        int64_t _i;\n"
        f'        if (!cmp_read_integer(_mr, &_i)) {{ snprintf(_err, _err_cap, "{name}: expected int (%s)", cmp_strerror(_mr)); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
        f'        if (_i < ({lo}) || _i > ({hi})) {{ snprintf(_err, _err_cap, "{name}: value %lld out of range for {t}", (long long)_i); return PICOMESH_ERR(picomesh_void, "minvoke: bad argument"); }}\n'
        f"        {var} = ({t})_i;\n"
        f"    }}\n",
        var,
    )


def emit_minvoke_write_result(vt) -> str:
    """Write the method's return value as ONE msgpack value into `_mw`,
    preserving width/signedness. Owned string returns are freed after."""
    if vt is None:
        return "    cmp_write_nil(_mw);\n"
    if vt in ("uint32_t", "size_t", "uint64_t", "uint16_t", "uint8_t"):
        return "    cmp_write_uinteger(_mw, (uint64_t)call_result.value);\n"
    if vt in ("int", "int64_t", "int32_t", "int16_t", "int8_t", "short", "long"):
        return "    cmp_write_integer(_mw, (int64_t)call_result.value);\n"
    if vt in ("double", "float"):
        return "    cmp_write_decimal(_mw, (double)call_result.value);\n"
    if vt == "char *":
        # picomesh_string / picomesh_json: owned heap return — write then free.
        return (
            "    {\n"
            "        const char *_sv = call_result.value ? call_result.value : \"\";\n"
            "        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));\n"
            "        free(call_result.value);\n"
            "    }\n"
        )
    if vt.startswith("const char"):
        return (
            "    {\n"
            "        const char *_sv = call_result.value ? call_result.value : \"\";\n"
            "        cmp_write_str(_mw, _sv, (uint32_t)strlen(_sv));\n"
            "    }\n"
        )
    return "    cmp_write_nil(_mw);  /* return type not yet supported in msgpack */\n"


def emit_minvoke(m: dict) -> str:
    """One per method. Reads positional msgpack args from `_mr` and calls the
    public stub, writing the return through `_mw`. Mirrors emit_jinvoke: the
    caller's `ctx` forwards straight into the stub (NULL/zeroed → local;
    live session → forward to the owning backend). Scalar / string args only,
    same restriction as the binary skel and jinvoke."""
    slot = qualified_slot(m)
    rid = result_type_id(m["return_type"])
    vt = ret_value_type(rid)
    args = m["args"][3:]
    n = len(args)

    arg_decls = []
    call_parts = ["call_ctx", "obj", "hdrs"]
    for i, a in enumerate(args):
        decl, expr = emit_munpack_arg(a, i)
        arg_decls.append(decl)
        call_parts.append(expr)
    call = ", ".join(call_parts)
    rt = f"struct {rid}_result"
    write_result = emit_minvoke_write_result(vt)

    return f"""\
static struct picomesh_void_result {slot}_minvoke(struct ctx *ctx, struct object *obj,
                          struct yheaders *hdrs, cmp_ctx_t *_mr, uint32_t _argc,
                          cmp_ctx_t *_mw, char *_err, size_t _err_cap)
{{
    (void)_mr;
    if (_argc != {n}u) {{
        snprintf(_err, _err_cap, "{slot}: expected {n} arg(s), got %u", _argc);
        return PICOMESH_ERR(picomesh_void, "{slot}: wrong argument count");
    }}
{''.join(arg_decls)}\
    struct ctx local_ctx = {{0}};
    struct ctx *call_ctx = ctx ? ctx : &local_ctx;
    {rt} call_result = {slot}({call});
    if (PICOMESH_IS_ERR(call_result)) {{
        char chain[{ERROR_TEXT_MAX}] = {{0}};
        picomesh_error_snprint(chain, sizeof(chain), call_result.error);
        snprintf(_err, _err_cap, "%s: %s", "{slot}",
                 chain[0] ? chain : (call_result.error.msg ? call_result.error.msg : "<no message>"));
        return PICOMESH_ERR(picomesh_void, "{slot}", call_result);
    }}
{write_result}\
    return PICOMESH_OK_VOID();
}}
"""


def emit_minvoke_table(model: dict, module: str) -> str:
    methods = model.get("methods", [])
    if not methods:
        return ""
    rows = ",\n".join(
        f'    {{"{qualified_slot(m)}", {qualified_slot(m)}_minvoke}}'
        for m in methods
    )
    return f"""\

/* ---- {module}: minvoke table ------------------------------------ */

struct {module}_minvoke_row {{ const char *name; minvoke_fn fn; }};

static const struct {module}_minvoke_row {module}_minvoke_rows[] = {{
{rows}
}};

static minvoke_fn {module}_minvoke_lookup(const char *qname)
{{
    for (size_t i = 0;
         i < sizeof({module}_minvoke_rows) / sizeof({module}_minvoke_rows[0]); ++i)
        if (strcmp({module}_minvoke_rows[i].name, qname) == 0)
            return {module}_minvoke_rows[i].fn;
    return NULL;
}}
"""


def emit_rpc_c(model: dict, module: str, out_path: Path):
    parts = [HEADER,
             '#include <picomesh/picoclass/rpc.h>\n',
             '#include <picomesh/picoclass/jinvoke.h>\n',
             '#include <picomesh/picoclass/minvoke.h>\n',
             '#include <picomesh/picoclass/yheaders.h>\n',
             '#include <picomesh/json/json.h>\n',
             '#include <picomesh/core/result.h>\n',
             '#include <picomesh/core/ytrace.h>\n',
             '#include <picomesh/core/yspan.h>\n',
             '#include <picomesh/core/ytelemetry.h>\n',
             '#include <picomesh/picoclass/class.h>\n',
             f'#include "{module}.internal.h"\n',
             '#include <limits.h>\n#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n'
             '#include <string.h>\n\n']
    for m in model["methods"]:
        parts.append(emit_skel(m))
        parts.append("\n")
    for m in model["methods"]:
        parts.append(emit_jinvoke(m))
        parts.append("\n")
    for m in model["methods"]:
        parts.append(emit_minvoke(m))
        parts.append("\n")
    for c in regular_classes(model):
        parts.append(emit_create_fn(c))
        parts.append("\n")
    # jinvoke + params tables must come BEFORE the register entry point so
    # its references to <module>_jinvoke_lookup / <module>_params_lookup
    # resolve.
    parts.append(emit_jinvoke_table(model, module))
    parts.append(emit_minvoke_table(model, module))
    parts.append(emit_params_table(model, module))
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

    # Plugin headers live under include/picomesh/plugin/<module>/ so the
    # plugin namespace is visually distinct from core headers
    # (picoclass/, engine/, etc.) — see CLAUDE.md.
    include_module = include_base / "picomesh" / "plugin" / module
    include_module.mkdir(parents=True, exist_ok=True)
    module_src.mkdir(parents=True, exist_ok=True)

    # Stub-create the .gen.c siblings + the combined public header
    # and the codegen-private internal header before the AST dump
    # runs, so the annotated TU's preprocess cleanly even on a cold
    # build (chicken-and-egg between AST input and codegen output).
    placeholder = '#include <picomesh/picoclass/class.h>\n'
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
