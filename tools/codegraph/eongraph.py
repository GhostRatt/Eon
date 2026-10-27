#!/usr/bin/env python3
"""eongraph: a code knowledge graph of the Eon repo, for the coding assistant (and you).

Query the graph instead of reading whole files: an outline of a file, one function's source, who calls what,
what a change would break, and the symbols that matter for a task, all trimmed to a token budget.

  python tools/codegraph/eongraph.py index              # (re)build; incremental by file hash
  python tools/codegraph/eongraph.py map                # whole-repo overview (~1.5k tokens)
  python tools/codegraph/eongraph.py context "walk climb ledge"   # what to look at for a task
  python tools/codegraph/eongraph.py file engine/character/Motor.cpp
  python tools/codegraph/eongraph.py find StartClimb
  python tools/codegraph/eongraph.py show Motor::StartClimb        # just that function's lines
  python tools/codegraph/eongraph.py callers Motor::GroundAt | callees ... | impact ... | path A B
  python tools/codegraph/eongraph.py changed            # what changed since the last index
  python tools/codegraph/eongraph.py note Motor::StartClimb "climb height limits 0.3-1.35 m"   # durable notes
  python tools/codegraph/eongraph.py report | stats | install-hook

Every edge carries provenance like Graphify: "extracted" (seen in the syntax) or "ambiguous"
(several symbols share the called name; all candidates kept, weighted).
"""
from __future__ import annotations

import hashlib
import json
import math
import os
import re
import subprocess
import sys
from collections import defaultdict, deque

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, ".eongraph")
GRAPH = os.path.join(OUT, "graph.json")
NOTES = os.path.join(os.path.dirname(__file__), "notes.json")   # committed: survives rebuilds and sessions
SKIP_DIRS = {".git", ".eongraph", "_deps", "node_modules", "__pycache__"}
CPP_EXT = {".h", ".hpp", ".hh", ".c", ".cc", ".cpp", ".cxx", ".inl"}

ts = tree_sitter_cpp = tree_sitter_python = None
HAVE_TS = None   # decided on first index (queries never pay for importing the parser)


def load_parsers():
    global ts, tree_sitter_cpp, tree_sitter_python, HAVE_TS
    if HAVE_TS is not None:
        return HAVE_TS
    try:
        import tree_sitter as _ts
        import tree_sitter_cpp as _cpp
        ts, tree_sitter_cpp = _ts, _cpp
        try:
            import tree_sitter_python as _py
            tree_sitter_python = _py
        except ImportError:
            tree_sitter_python = None
        HAVE_TS = True
    except ImportError:   # still works (cruder) without tree-sitter
        HAVE_TS = False
    return HAVE_TS


def tok(s: str) -> int:
    return max(1, len(s) // 4)


def rel(p: str) -> str:
    return os.path.relpath(p, ROOT).replace("\\", "/")


def source_files():
    try:
        out = subprocess.run(["git", "ls-files", "-co", "--exclude-standard"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
        files = [f for f in out.splitlines() if f]
    except Exception:
        files = []
        for d, dirs, fs in os.walk(ROOT):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS and not x.startswith("build")]
            files += [rel(os.path.join(d, f)) for f in fs]
    keep = []
    for f in files:
        parts = f.split("/")
        if any(p in SKIP_DIRS or p.startswith("build") for p in parts[:-1]):
            continue
        ext = os.path.splitext(f)[1].lower()
        if ext in CPP_EXT or ext in {".py", ".md", ".json"} or os.path.basename(f) == "CMakeLists.txt":
            if os.path.isfile(os.path.join(ROOT, f)) and os.path.getsize(os.path.join(ROOT, f)) < 2_000_000:
                keep.append(f)
    return sorted(keep)


# ---------------------------------------------------------------- extraction

class Sym(dict):
    """id, name, qual, kind, file, line, end, sig, doc, parent"""


def leading_comment(lines, line_idx):
    """Comment block right above line_idx (0-based) -> first sentence-ish, trimmed."""
    out = []
    i = line_idx - 1
    while i >= 0:
        s = lines[i].strip()
        if s.startswith("//") or s.startswith("#") and not s.startswith("#include") and not s.startswith("#pragma") and not s.startswith("#define") and not s.startswith("#if") and not s.startswith("#end"):
            out.append(s.lstrip("/#").strip())
            i -= 1
        elif s.endswith("*/"):
            while i >= 0 and "/*" not in lines[i]:
                out.append(lines[i].strip(" */"))
                i -= 1
            if i >= 0:
                out.append(lines[i].strip(" /*"))
            i -= 1
        else:
            break
    out.reverse()
    txt = " ".join(x for x in out if x and not set(x) <= set("-=*"))
    return txt[:220]


def trailing_comment(line):
    m = re.search(r"//\s*(.+)$", line)
    return m.group(1).strip()[:160] if m else ""


SMART = ("unique_ptr", "shared_ptr", "weak_ptr", "optional", "reference_wrapper")


def norm_type(ty):
    """'const std::unique_ptr<Physics>&' -> 'Physics'; containers/auto/builtins -> None"""
    if not ty:
        return None
    ty = re.sub(r"\b(const|volatile|struct|class|typename|mutable|static|inline|constexpr)\b", "", ty).replace("&", "").replace("*", "").strip()
    m = re.match(r"(?:\w+::)*(\w+)\s*<\s*(.+)>\s*$", ty)
    if m:
        if m.group(1) in SMART:
            return norm_type(m.group(2))
        return None
    ty = ty.split("::")[-1].strip()
    if not re.match(r"^[A-Z]\w*$", ty):   # project types are CapitalCase; skip int/float/auto/std types
        return None
    return ty


class CppExtractor:
    def __init__(self):
        self.lang = ts.Language(tree_sitter_cpp.language())
        self.parser = ts.Parser(self.lang)

    def run(self, path, text):
        src = text.encode("utf-8", "replace")
        tree = self.parser.parse(src)
        lines = text.split("\n")
        syms, edges = [], []

        def t(n):
            return src[n.start_byte:n.end_byte].decode("utf-8", "replace")

        def decl_name(d):
            # dig through pointer/reference/function declarators to the name
            while d is not None and d.type in ("pointer_declarator", "reference_declarator", "function_declarator", "init_declarator", "attributed_declarator"):
                d = d.child_by_field_name("declarator") or (d.named_children[0] if d.named_children else None)
            return t(d) if d is not None else None

        def add(kind, name, qual, node, parent=None, sig=None):
            line = node.start_point[0]
            s = Sym(id=f"{path}::{qual}", name=name, qual=qual, kind=kind, file=path, line=line + 1, end=node.end_point[0] + 1,
                    sig=(sig or "").strip()[:200], doc=leading_comment(lines, line) or trailing_comment(lines[line]), parent=parent)
            syms.append(s)
            return s

        def locals_of(func, body):
            env = {}
            def bind(type_node, decl):
                ty = norm_type(t(type_node)) if type_node is not None else None
                d = decl
                while d is not None and d.type in ("pointer_declarator", "reference_declarator", "init_declarator", "array_declarator", "optional_parameter_declaration"):
                    d = d.child_by_field_name("declarator") or (d.named_children[-1] if d.named_children else None)
                if ty and d is not None and d.type == "identifier":
                    env[t(d)] = ty
            fd = func.child_by_field_name("declarator")
            while fd is not None and fd.type != "function_declarator":
                fd = fd.child_by_field_name("declarator")
            if fd is not None and fd.child_by_field_name("parameters") is not None:
                for prm in fd.child_by_field_name("parameters").named_children:
                    if prm.type in ("parameter_declaration", "optional_parameter_declaration"):
                        bind(prm.child_by_field_name("type"), prm.child_by_field_name("declarator"))
            st = [body]
            while st:
                n = st.pop()
                if n.type == "declaration":
                    for dcl in n.children_by_field_name("declarator"):
                        bind(n.child_by_field_name("type"), dcl)
                elif n.type == "for_range_loop":
                    bind(n.child_by_field_name("type"), n.child_by_field_name("declarator"))
                st.extend(n.children)
            return env

        def hint_of(expr, env):
            """Type path of the object a method is called on: '=World' (known), '@world_' (member of the owner class),
            '@body|motor' (member chain), or None."""
            if expr is None:
                return None
            if expr.type == "identifier":
                nm = t(expr)
                return "=" + env[nm] if nm in env else "@" + nm
            if expr.type == "this":
                return "@this"
            if expr.type == "field_expression":
                base = hint_of(expr.child_by_field_name("argument"), env)
                fld = expr.child_by_field_name("field")
                if base and fld is not None:
                    return (base[1:] if base == "@this" else base) + "|" + t(fld) if base != "@this" else "@" + t(fld)
            if expr.type in ("parenthesized_expression", "pointer_expression"):
                return hint_of(expr.named_children[-1] if expr.named_children else None, env)
            if expr.type == "subscript_expression":
                return hint_of(expr.child_by_field_name("argument"), env)
            return None

        def calls_in(body, owner, env=None):
            env = env or {}
            seen = set()
            stack = [body]
            while stack:
                n = stack.pop()
                if n.type == "call_expression":
                    f = n.child_by_field_name("function")
                    name = None
                    hint = None
                    if f is not None:
                        if f.type == "field_expression":
                            fld = f.child_by_field_name("field")
                            name = t(fld) if fld is not None else None
                            hint = hint_of(f.child_by_field_name("argument"), env)
                        elif f.type in ("qualified_identifier", "template_function"):
                            name = t(f).split("::")[-1].split("<")[0]
                        elif f.type == "identifier":
                            name = t(f)
                    if name and (name, hint) not in seen:
                        seen.add((name, hint))
                        edges.append([owner, name, "calls?", hint])
                elif n.type in ("type_identifier",):
                    nm = t(n)
                    if ("T:" + nm) not in seen:
                        seen.add("T:" + nm)
                        edges.append([owner, nm, "uses?"])
                stack.extend(n.children)

        def walk(n, ns, cls):
            for c in n.children:
                ty = c.type
                if ty == "preproc_include":
                    p = c.child_by_field_name("path")
                    if p is not None:
                        edges.append([path, t(p).strip('"<>'), "includes?"])
                elif ty == "namespace_definition":
                    nm = c.child_by_field_name("name")
                    body = c.child_by_field_name("body")
                    if body is not None:
                        walk(body, ns + ([t(nm)] if nm is not None else []), cls)
                elif ty in ("class_specifier", "struct_specifier", "union_specifier") or (ty in ("declaration", "type_definition", "field_declaration") and any(k.type in ("class_specifier", "struct_specifier", "enum_specifier") for k in c.children)):
                    spec = c if ty.endswith("_specifier") else next(k for k in c.children if k.type in ("class_specifier", "struct_specifier", "enum_specifier"))
                    if spec.type == "enum_specifier":
                        do_enum(spec, cls)
                        continue
                    nm = spec.child_by_field_name("name")
                    body = spec.child_by_field_name("body")
                    if nm is None or body is None:
                        continue
                    name = t(nm)
                    qual = (cls + "::" if cls else "") + name
                    bases = ""
                    for k in spec.children:
                        if k.type == "base_class_clause":
                            bases = t(k).lstrip(":").strip()
                    s = add("class" if spec.type == "class_specifier" else "struct", name, qual, spec, parent=cls or None,
                            sig=f"{'class' if spec.type == 'class_specifier' else 'struct'} {name}" + (f" : {bases}" if bases else ""))
                    for b in re.findall(r"[A-Za-z_]\w*", bases):
                        if b not in ("public", "private", "protected", "virtual"):
                            edges.append([s["id"], b, "inherits?"])
                    members = []
                    mtypes = {}
                    for m in body.children:
                        if m.type in ("field_declaration", "declaration"):
                            dd = m.child_by_field_name("declarator")
                            if dd is not None and (dd.type == "function_declarator" or any(x.type == "function_declarator" for x in dd.children)):
                                mn = decl_name(dd)
                                if mn:
                                    d = add("method", mn.split("::")[-1], qual + "::" + mn.split("::")[-1], m, parent=qual, sig=t(m).split("{")[0].rstrip(";").strip())
                                    d["decl"] = True
                            elif any(k.type in ("class_specifier", "struct_specifier", "enum_specifier") for k in m.children):
                                walk(m.parent if False else _Wrap([m]), ns, qual)
                            else:
                                mt = norm_type(t(m.child_by_field_name("type"))) if m.child_by_field_name("type") is not None else None
                                for k in m.children:
                                    nm_ = None
                                    if k.type in ("field_identifier", "identifier"):
                                        nm_ = t(k)
                                    elif k.type in ("init_declarator", "array_declarator", "pointer_declarator", "reference_declarator"):
                                        x = k.child_by_field_name("declarator") or (k.named_children[-1] if k.named_children else None)
                                        while x is not None and x.type in ("pointer_declarator", "reference_declarator", "array_declarator", "init_declarator"):
                                            x = x.child_by_field_name("declarator") or (x.named_children[-1] if x.named_children else None)
                                        nm_ = t(x) if x is not None else None
                                    if nm_ and mt:
                                        mtypes[nm_] = mt
                                for k in m.children:
                                    if k.type in ("field_identifier", "identifier"):
                                        members.append(t(k))
                                    elif k.type in ("init_declarator",):
                                        x = k.child_by_field_name("declarator")
                                        if x is not None:
                                            members.append(t(x))
                                    elif k.type in ("array_declarator",):
                                        x = k.child_by_field_name("declarator")
                                        if x is not None:
                                            members.append(t(x))
                        elif m.type == "function_definition":
                            do_func(m, qual)
                        elif m.type in ("class_specifier", "struct_specifier", "enum_specifier"):
                            walk(_Wrap([m]), ns, qual)
                        elif m.type == "template_declaration":
                            walk(_Wrap(m.children), ns, qual)
                    if members:
                        s["members"] = members[:40]
                    if mtypes:
                        s["mtypes"] = mtypes
                elif ty == "enum_specifier":
                    do_enum(c, cls)
                elif ty == "function_definition":
                    do_func(c, cls)
                elif ty == "template_declaration":
                    walk(c, ns, cls)
                elif ty in ("declaration",) and not cls:
                    dd = c.child_by_field_name("declarator")
                    if dd is not None and dd.type == "function_declarator":
                        nm = decl_name(dd)
                        if nm:
                            add("decl", nm.split("::")[-1], nm, c, sig=t(c).rstrip(";"))
                elif ty in ("linkage_specification", "declaration_list", "preproc_if", "preproc_ifdef", "preproc_else"):
                    walk(c, ns, cls)

        def do_enum(spec, cls):
            nm = spec.child_by_field_name("name")
            body = spec.child_by_field_name("body")
            if nm is None:
                return
            name = t(nm)
            vals = [t(e.child_by_field_name("name")) for e in (body.children if body is not None else []) if e.type == "enumerator" and e.child_by_field_name("name") is not None]
            s = add("enum", name, (cls + "::" if cls else "") + name, spec, parent=cls or None, sig=f"enum {name} {{{', '.join(vals[:16])}}}")
            s["members"] = vals

        def do_func(c, cls):
            dd = c.child_by_field_name("declarator")
            nm = decl_name(dd) if dd is not None else None
            if not nm:
                return
            nm = re.sub(r"\s+", "", nm)
            qual = nm if "::" in nm or not cls else cls + "::" + nm
            body = c.child_by_field_name("body")
            header = src[c.start_byte:(body.start_byte if body is not None else c.end_byte)].decode("utf-8", "replace")
            s = add("function" if "::" not in qual else "method", qual.split("::")[-1], qual, c, parent="::".join(qual.split("::")[:-1]) or None,
                    sig=" ".join(header.split()))
            if body is not None:
                calls_in(body, s["id"], locals_of(c, body))

        walk(tree.root_node, [], "")
        return syms, edges


class _Wrap:
    def __init__(self, children):
        self.children = children


def py_extract(path, text):
    syms, edges = [], []
    lines = text.split("\n")
    if HAVE_TS and tree_sitter_python is not None:
        lang = ts.Language(tree_sitter_python.language())
        parser = ts.Parser(lang)
        src = text.encode()
        tree = parser.parse(src)

        def t(n):
            return src[n.start_byte:n.end_byte].decode("utf-8", "replace")

        def walk(n, cls):
            for c in n.children:
                node = c.child_by_field_name("definition") if c.type == "decorated_definition" else c
                if node is None:
                    continue
                if node.type in ("function_definition", "class_definition"):
                    name = t(node.child_by_field_name("name"))
                    qual = (cls + "." if cls else "") + name
                    body = node.child_by_field_name("body")
                    doc = ""
                    if body is not None and body.children and body.children[0].type == "expression_statement" and body.children[0].children and body.children[0].children[0].type == "string":
                        doc = t(body.children[0]).strip("\"' \n").split("\n")[0][:200]
                    sid = f"{path}::{qual}"
                    syms.append(Sym(id=sid, name=name, qual=qual, kind="class" if node.type == "class_definition" else ("method" if cls else "function"),
                                    file=path, line=node.start_point[0] + 1, end=node.end_point[0] + 1,
                                    sig=lines[node.start_point[0]].strip()[:200], doc=doc or leading_comment(lines, node.start_point[0]), parent=cls or None))
                    if node.type == "class_definition" and body is not None:
                        walk(body, qual)
                    elif body is not None:
                        st = [body]
                        seen = set()
                        while st:
                            x = st.pop()
                            if x.type == "call":
                                f = x.child_by_field_name("function")
                                if f is not None:
                                    nm = t(f).split(".")[-1]
                                    if nm not in seen:
                                        seen.add(nm)
                                        edges.append([sid, nm, "calls?"])
                            st.extend(x.children)
        walk(tree.root_node, "")
    else:
        for i, l in enumerate(lines):
            m = re.match(r"(\s*)(def|class)\s+(\w+)", l)
            if m:
                syms.append(Sym(id=f"{path}::{m.group(3)}", name=m.group(3), qual=m.group(3), kind="class" if m.group(2) == "class" else "function",
                                file=path, line=i + 1, end=i + 1, sig=l.strip()[:200], doc=leading_comment(lines, i), parent=None))
    return syms, edges


def regex_cpp(path, text):
    """Fallback without tree-sitter: function definitions and classes by pattern."""
    syms, edges = [], []
    lines = text.split("\n")
    for i, l in enumerate(lines):
        m = re.match(r"^\s*(?:class|struct)\s+(\w+)[^;]*\{?\s*$", l)
        if m:
            syms.append(Sym(id=f"{path}::{m.group(1)}", name=m.group(1), qual=m.group(1), kind="class", file=path, line=i + 1, end=i + 1, sig=l.strip(), doc=leading_comment(lines, i), parent=None))
        m = re.match(r"^[\w:<>,\s\*&]+?\s+([\w:~]+)\s*\([^;]*\)\s*(const)?\s*\{\s*$", l)
        if m and not l.strip().startswith(("if", "for", "while", "switch", "return")):
            q = m.group(1)
            syms.append(Sym(id=f"{path}::{q}", name=q.split("::")[-1], qual=q, kind="method" if "::" in q else "function", file=path, line=i + 1, end=i + 1, sig=l.strip(" {"), doc=leading_comment(lines, i), parent=None))
    return syms, edges


def md_extract(path, text):
    syms = []
    for i, l in enumerate(text.split("\n")):
        m = re.match(r"^(#{1,3})\s+(.+)", l)
        if m:
            syms.append(Sym(id=f"{path}::#{i+1}", name=m.group(2).strip(), qual=m.group(2).strip(), kind="heading", file=path, line=i + 1, end=i + 1, sig=l.strip(), doc="", parent=None))
    return syms, []


def json_extract(path, text):
    """Zones/characters: list entities briefly (name, type) so content is findable without opening it."""
    try:
        j = json.loads(text)
    except Exception:
        return [], []
    syms = []
    if isinstance(j, dict) and isinstance(j.get("entities"), list):
        lines = text.split("\n")
        for e in j["entities"]:
            if not isinstance(e, dict) or "name" not in e:
                continue
            ln = next((i + 1 for i, l in enumerate(lines) if f'"{e["name"]}"' in l), 1)
            syms.append(Sym(id=f"{path}::{e['name']}", name=e["name"], qual=e["name"], kind="entity", file=path, line=ln, end=ln,
                            sig=f"{e.get('type','?')} {e['name']} pos={e.get('pos')} size={e.get('size')}", doc=json.dumps(e.get("params", {}))[:160], parent=None))
    elif isinstance(j, dict):
        syms.append(Sym(id=f"{path}::keys", name=os.path.basename(path), qual=os.path.basename(path), kind="config", file=path, line=1, end=1,
                        sig="keys: " + ", ".join(list(j.keys())[:20]), doc="", parent=None))
    return syms, []


def cmake_extract(path, text):
    syms = []
    for i, l in enumerate(text.split("\n")):
        m = re.match(r"\s*add_(library|executable)\((\w+)", l)
        if m:
            syms.append(Sym(id=f"{path}::{m.group(2)}", name=m.group(2), qual=m.group(2), kind="target", file=path, line=i + 1, end=i + 1, sig=l.strip(), doc=m.group(1), parent=None))
    return syms, []


# ---------------------------------------------------------------- graph build

def load_graph():
    if not os.path.exists(GRAPH):
        return None
    with open(GRAPH, encoding="utf-8") as f:
        return json.load(f)


def file_summary(path, text):
    lines = text.split("\n")
    for i, l in enumerate(lines[:40]):
        s = l.strip()
        if s.startswith("//") and len(s) > 6 and "pragma" not in s:
            return leading_comment(lines, i + 1) or s.lstrip("/ ")
        if s.startswith('"""') or s.startswith("#!"):
            continue
    return ""


def index(force=False, quiet=False):
    os.makedirs(OUT, exist_ok=True)
    old = load_graph() or {}
    old_files = {} if force or old.get("version") != 3 else old.get("files", {})
    cpp = CppExtractor() if load_parsers() else None
    files, all_syms, raw_edges = {}, [], []
    reparsed = 0
    for f in source_files():
        full = os.path.join(ROOT, f)
        with open(full, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        h = hashlib.sha1(text.encode("utf-8", "replace")).hexdigest()
        prev = old_files.get(f)
        if prev and prev.get("hash") == h:
            files[f] = prev
            all_syms += prev["syms"]
            raw_edges += prev["edges"]
            continue
        reparsed += 1
        ext = os.path.splitext(f)[1].lower()
        if ext in CPP_EXT:
            syms, edges = cpp.run(f, text) if cpp else regex_cpp(f, text)
        elif ext == ".py":
            syms, edges = py_extract(f, text)
        elif ext == ".md":
            syms, edges = md_extract(f, text)
        elif ext == ".json":
            syms, edges = json_extract(f, text)
        else:
            syms, edges = cmake_extract(f, text)
        # what each function says about itself inside its body (intent comments): strong search signal
        tl = text.split("\n")
        for sy in syms:
            if sy["end"] > sy["line"] and sy["kind"] in ("function", "method", "class", "struct"):
                bc = []
                for ln in tl[sy["line"]:sy["end"]]:
                    m = re.search(r"(?://|#)\s*(.+)$", ln)
                    if m and not ln.strip().startswith("#include"):
                        bc.append(m.group(1).strip())
                if bc:
                    sy["bc"] = " ".join(bc)[:400]
                if sy["kind"] in ("function", "method"):
                    ids = []
                    seen_w = set()
                    for ident in re.findall(r"[A-Za-z_][A-Za-z0-9_]{2,}", "\n".join(tl[sy["line"]:sy["end"]])):
                        if ident not in seen_w:
                            seen_w.add(ident)
                            ids.append(ident)
                    sy["bw"] = " ".join(ids)[:700]   # what the body works with (identifiers)
        files[f] = {"hash": h, "lines": text.count("\n") + 1, "summary": file_summary(f, text), "syms": syms, "edges": edges}
        all_syms += syms
        raw_edges += edges
    # header comments describe the definitions in the .cpp: pass them on
    decl_doc = {}
    for sy in all_syms:
        if (sy.get("decl") or sy["kind"] == "decl") and sy.get("doc"):
            decl_doc.setdefault(sy["qual"], sy["doc"])
    for sy in all_syms:
        if not sy.get("decl") and sy["kind"] != "decl" and sy["qual"] in decl_doc:
            d = decl_doc[sy["qual"]]
            if d not in sy.get("doc", ""):
                sy["doc"] = (d + (" | " + sy["doc"] if sy.get("doc") else ""))[:300]
    # resolve names -> symbols (prefer same class, then same file, then unique; else keep all as ambiguous)
    by_name = defaultdict(list)
    for s in all_syms:
        if s["kind"] in ("function", "method", "decl", "class", "struct", "enum"):
            by_name[s["name"]].append(s)
    by_id = {s["id"]: s for s in all_syms}
    edges = []
    # member types per class (for resolving obj.method() by the object's type)
    mtypes = {}
    for sy in all_syms:
        if sy.get("mtypes"):
            mtypes[sy["qual"]] = sy["mtypes"]
            mtypes.setdefault(sy["name"], sy["mtypes"])

    def type_of_hint(hint, owner_cls):
        if not hint:
            return None
        parts = hint.split("|")
        head = parts[0]
        if head.startswith("="):
            ty = head[1:]
        else:
            ty = mtypes.get(owner_cls or "", {}).get(head[1:]) if head.startswith("@") else None
        for p in parts[1:]:
            if ty is None:
                return None
            ty = mtypes.get(ty, {}).get(p)
        return ty

    typed = 0
    for e in raw_edges:
        a, name, kind = e[0], e[1], e[2]
        hint = e[3] if len(e) > 3 else None
        k = kind.rstrip("?")
        if k == "includes":
            tgt = next((f for f in files if f.endswith("/" + name) or f == name), None)
            if tgt:
                edges.append([a, tgt, "includes", "extracted", 1.0])
            continue
        cands = by_name.get(name, [])
        if k == "uses" or k == "inherits":
            cands = [c for c in cands if c["kind"] in ("class", "struct", "enum")]
        else:
            cands = [c for c in cands if c["kind"] in ("function", "method", "decl")]
            # a definition beats its declaration
            # a definition beats its declaration (same qualified name)
            def_quals = {c["qual"] for c in cands if not c.get("decl") and c["kind"] != "decl"}
            cands = [c for c in cands if not ((c.get("decl") or c["kind"] == "decl") and c["qual"] in def_quals)]
        if not cands:
            continue
        src = by_id.get(a)
        if hint and len(cands) > 1 and src is not None:
            ty = type_of_hint(hint, src.get("parent"))
            if ty:
                by_type = [c for c in cands if c.get("parent") == ty or c["qual"].startswith(ty + "::")]
                if by_type:
                    cands = by_type
                    typed += 1
        if src and len(cands) > 1:
            same_cls = [c for c in cands if src.get("parent") and c.get("parent") == src.get("parent")]
            same_file = [c for c in cands if c["file"] == src["file"]]
            same_dir = [c for c in cands if os.path.dirname(c["file"]) == os.path.dirname(src["file"])]
            cands = same_cls or same_file or same_dir or cands
        prov = "extracted" if len(cands) == 1 else "ambiguous"
        for c in cands[:6]:
            if c["id"] != a:
                edges.append([a, c["id"], k, prov, round(1.0 / len(cands), 3)])
    # dedupe edges
    seen = set()
    uniq = []
    for e in edges:
        key = (e[0], e[1], e[2])
        if key not in seen:
            seen.add(key)
            uniq.append(e)
    g = {"version": 3, "files": files, "edges": uniq, "parser": "tree-sitter" if HAVE_TS else "regex"}
    with open(GRAPH, "w", encoding="utf-8") as fh:
        json.dump(g, fh)
    if not quiet:
        print(f"indexed {len(files)} files ({reparsed} re-parsed), {len(all_syms)} symbols, {len(uniq)} edges [{g['parser']}]")
    return g


def ensure_graph():
    g = load_graph()
    # stale? re-index changed files quietly (cheap: hashes only re-parse what changed)
    if g is None or any_changed(g):
        g = index(quiet=True)
    return g


def any_changed(g):
    for f in source_files():
        info = g["files"].get(f)
        if info is None:
            return True
        try:
            if os.path.getmtime(os.path.join(ROOT, f)) > os.path.getmtime(GRAPH):
                return True
        except OSError:
            return True
    return len(g["files"]) != len(source_files())


class G:
    def __init__(self, g):
        self.g = g
        self.syms = [s for f in g["files"].values() for s in f["syms"]]
        self.by_id = {s["id"]: s for s in self.syms}
        self.out = defaultdict(list)
        self.inn = defaultdict(list)
        for e in g["edges"]:
            self.out[e[0]].append(e)
            self.inn[e[1]].append(e)
        self.notes = load_notes()
        self.defined = {s["qual"] for s in self.syms if not s.get("decl") and s["kind"] != "decl"}

    def resolve(self, q):
        """'Motor::StartClimb', 'StartClimb', 'Motor.cpp::StartClimb', a file path -> symbols"""
        q = q.strip()
        if q in self.by_id:
            return [self.by_id[q]]
        exact = [s for s in self.syms if s["qual"] == q or s["qual"].endswith("::" + q) and "::" in q]
        if exact:
            return self._prefer_defs(exact)
        named = [s for s in self.syms if s["name"] == q]
        if named:
            return self._prefer_defs(named)
        low = q.lower()
        return [s for s in self.syms if low in s["qual"].lower()][:20]

    @staticmethod
    def _prefer_defs(lst):
        def_quals = {s["qual"] for s in lst if not s.get("decl") and s["kind"] != "decl"}
        return [s for s in lst if not ((s.get("decl") or s["kind"] == "decl") and s["qual"] in def_quals)] or lst

    def loc(self, s):
        return f"{s['file']}:{s['line']}" + (f"-{s['end']}" if s["end"] > s["line"] else "")

    def note_for(self, s):
        return self.notes.get(s["qual"]) or self.notes.get(s["id"]) or []


def load_notes():
    if os.path.exists(NOTES):
        with open(NOTES, encoding="utf-8") as f:
            return json.load(f)
    return {}


# ---------------------------------------------------------------- commands

def emit(text):
    sys.stdout.write(text.rstrip() + f"\n[~{tok(text)} tokens]\n")


def cmd_map(G_, budget=1800):
    by_dir = defaultdict(list)
    for f, info in sorted(G_.g["files"].items()):
        by_dir[os.path.dirname(f) or "."].append((f, info))
    lines = ["# Eon code map (eongraph). `show <sym>` for source, `file <path>` for an outline."]
    for d, fl in by_dir.items():
        lines.append(f"\n{d}/")
        for f, info in fl:
            if f.endswith(".json") and "zones" not in f and "characters" not in f:
                continue
            top = [s for s in info["syms"] if s["kind"] in ("class", "struct", "enum", "target") and not s.get("parent")]
            funcs = [s for s in info["syms"] if s["kind"] in ("function",) and not s.get("parent")]
            names = ", ".join(s["name"] for s in top[:8]) or ", ".join(s["name"] for s in funcs[:8])
            if f.endswith(".md"):
                names = "; ".join(s["name"] for s in info["syms"][:5])
            if f.endswith(".json"):
                names = f"{len(info['syms'])} entities" if info["syms"] and info["syms"][0]["kind"] == "entity" else ""
            summ = info.get("summary", "")
            lines.append(f"  {os.path.basename(f)} ({info['lines']}L)" + (f" — {summ[:110]}" if summ else "") + (f" [{names}]" if names else ""))
    text = "\n".join(lines)
    while tok(text) > budget and len(lines) > 10:
        # trim summaries first, then drop long lists
        lines = [re.sub(r" — .*?( \[|$)", r"\1", l) if " — " in l else l for l in lines]
        text = "\n".join(lines)
        if tok(text) > budget:
            lines = [re.sub(r"\[(.{60}).*\]", r"[\1…]", l) for l in lines]
            text = "\n".join(lines)
            break
    emit(text)


def cmd_file(G_, path):
    path = rel(os.path.join(ROOT, path)) if os.path.exists(os.path.join(ROOT, path)) else path
    info = G_.g["files"].get(path)
    if not info:
        hits = [f for f in G_.g["files"] if f.endswith(path)]
        if not hits:
            print("no such indexed file")
            return
        path = hits[0]
        info = G_.g["files"][path]
    out = [f"{path} ({info['lines']} lines)" + (f" — {info['summary']}" if info.get("summary") else "")]
    for s in sorted(info["syms"], key=lambda s: s["line"]):
        ind = "  " if s.get("parent") and s["kind"] in ("method",) and s["end"] == s["line"] else ""
        extra = f"  // {s['doc'][:100]}" if s.get("doc") else ""
        mem = f"  members: {', '.join(s['members'][:14])}" if s.get("members") and s["kind"] in ("class", "struct") else ""
        out.append(f"{ind}{s['line']}-{s['end']} {s['kind']} {s['sig'][:140]}{extra}{mem}")
        for n in G_.note_for(s):
            out.append(f"{ind}   NOTE: {n}")
    emit("\n".join(out))


def cmd_find(G_, q):
    res = G_.resolve(q)
    if not res:
        print("not found")
        return
    out = [f"{G_.loc(s)} {s['kind']} {s['sig'][:150]}" + (f"  // {s['doc'][:90]}" if s.get("doc") else "") for s in res[:20]]
    emit("\n".join(out))


def cmd_show(G_, q, ctx=0):
    res = []
    for one in q if isinstance(q, list) else [q]:
        r = G_.resolve(one)
        res += r[:3] if len(r) <= 3 else r[:1]
    if not res:
        print("not found")
        return
    out = []
    for s in res[:6]:
        with open(os.path.join(ROOT, s["file"]), encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        a = max(0, s["line"] - 1 - ctx)
        # include the leading comment block
        while a > 0 and lines[a - 1].strip().startswith("//"):
            a -= 1
        b = min(len(lines), s["end"] + ctx)
        out.append(f"── {s['file']}:{a+1}-{b} ({s['qual']})")
        for n in G_.note_for(s):
            out.append(f"NOTE: {n}")
        out += [f"{i+1:5} {lines[i]}" for i in range(a, b)]
    emit("\n".join(out))


def _neighbors(G_, sid, direction, kinds=("calls",)):
    es = G_.inn[sid] if direction == "in" else G_.out[sid]
    res = []
    for e in es:
        if e[2] in kinds:
            other = e[0] if direction == "in" else e[1]
            if other in G_.by_id:
                res.append((G_.by_id[other], e))
    return res


def cmd_rel(G_, q, direction):
    res = G_.resolve(q)
    if not res:
        print("not found")
        return
    out = []
    for s in res[:3]:
        out.append(f"{'callers of' if direction == 'in' else 'called by'} {s['qual']} ({G_.loc(s)}):")
        for o, e in sorted(_neighbors(G_, s["id"], direction), key=lambda x: x[0]["qual"]):
            out.append(f"  {o['qual']}  {G_.loc(o)}" + ("  (ambiguous)" if e[3] == "ambiguous" else ""))
    emit("\n".join(out))


def cmd_impact(G_, q, depth=3):
    res = G_.resolve(q)
    if not res:
        print("not found")
        return
    s = res[0]
    seen = {s["id"]: 0}
    dq = deque([s["id"]])
    while dq:
        u = dq.popleft()
        if seen[u] >= depth:
            continue
        for o, e in _neighbors(G_, u, "in", ("calls", "uses", "inherits")):
            if o["id"] not in seen:
                seen[o["id"]] = seen[u] + 1
                dq.append(o["id"])
    files = defaultdict(list)
    for sid, d in seen.items():
        if d == 0:
            continue
        o = G_.by_id[sid]
        files[o["file"]].append((d, o["qual"]))
    tests = [f for f in files if "test" in f]
    out = [f"changing {s['qual']} ({G_.loc(s)}) can affect {sum(len(v) for v in files.values())} symbols in {len(files)} files"
           + (f"; tests touching it: {', '.join(tests)}" if tests else "") + ":"]
    for f in sorted(files, key=lambda f: min(x[0] for x in files[f])):
        items = sorted(files[f])
        out.append(f"  {f}: " + ", ".join(f"{q}(d{d})" for d, q in items[:10]) + (" …" if len(items) > 10 else ""))
    emit("\n".join(out))


def cmd_path(G_, a, b):
    A, B = G_.resolve(a), G_.resolve(b)
    if not A or not B:
        print("not found")
        return
    src, dst = A[0]["id"], {x["id"] for x in B}
    prev = {src: None}
    dq = deque([src])
    while dq:
        u = dq.popleft()
        if u in dst:
            path = []
            while u:
                path.append(u)
                u = prev[u]
            path.reverse()
            emit(" -> ".join(f"{G_.by_id[p]['qual']} ({G_.loc(G_.by_id[p])})" for p in path))
            return
        for e in G_.out[u]:
            if e[2] == "calls" and e[1] not in prev:
                prev[e[1]] = u
                dq.append(e[1])
    print("no call path")


WORD = re.compile(r"[A-Za-z][a-z]+|[A-Z]+(?![a-z])|\d+")


STOP = {"the", "and", "for", "with", "from", "that", "this", "into", "her", "she", "its", "are", "was", "not", "but", "can",
        "get", "set", "use", "out", "all", "one", "two"}


def stem(w):
    """Tiny suffix stripper: flatten/flattens/flattened -> flatten, visibility/visible -> visib, clips -> clip."""
    for suf in ("ization", "ations", "ation", "ibility", "ility", "ities", "ingly", "ness", "ments", "ment", "ings", "ing", "ions", "ion",
                "ible", "able", "ity", "ies", "ers", "ed", "er", "es", "ly", "s"):
        if len(w) > len(suf) + 2 and w.endswith(suf):
            w = w[: -len(suf)]
            break
    if len(w) > 3 and w[-1] == w[-2]:
        w = w[:-1]
    return w


# how programmers abbreviate (query side): "field of view" finds `fov`, "position" finds `pos`
ABBREV = {"field of view": "fov", "position": "pos", "direction": "dir", "velocity": "vel", "rotation": "rot", "number": "num",
          "count": "num", "index": "idx", "temperature": "temp", "configuration": "cfg", "config": "cfg", "camera": "cam",
          "inverse kinematics": "ik", "delta time": "dt", "argument": "arg", "arguments": "argv", "minimum": "min", "maximum": "max",
          "previous": "prev", "current": "cur", "reference": "ref", "message": "msg", "error": "err", "colour": "color"}


def expand_query(q):
    ql = q.lower()
    extra = [v for k, v in ABBREV.items() if k in ql]
    return q + " " + " ".join(extra)


def words(s):
    return [stem(w.lower()) for w in WORD.findall(s) if w.lower() not in STOP]


def cmd_eval(G_):
    """Benchmark the task search: rank of the expected symbol for each task in eval.json."""
    cases = json.load(open(os.path.join(os.path.dirname(__file__), "eval.json")))
    rr, at1, at5, at10 = 0.0, 0, 0, 0
    for q, want in cases:
        want = want if isinstance(want, list) else [want]
        ranked = [G_.by_id[i]["qual"] for i, _ in rank_context(G_, q)]
        r = next((k + 1 for k, x in enumerate(ranked) if x in want), None)
        rr += 1.0 / r if r else 0.0
        at1 += r == 1; at5 += bool(r and r <= 5); at10 += bool(r and r <= 10)
        print(f"{(str(r) if r else '-'):>3}  {q}  -> {want}" + ("" if r == 1 else f"   (top: {ranked[0] if ranked else '-'})"))
    n = len(cases)
    print(f"MRR {rr / n:.3f}  @1 {at1}/{n}  @5 {at5}/{n}  @10 {at10}/{n}")


def rank_context(G_, query, limit=30):
    """Rank symbols for a task (name/sig/doc/note word overlap, idf-weighted), add their 1-hop neighbors,
    print outline lines with locations, best first, within budget."""
    q = set(words(expand_query(query)))
    wants_tests = "test" in query.lower()
    wants_content = any(k in query.lower() for k in ("zone", "entity", "content", "json", "config"))
    docs = {}
    df = defaultdict(int)
    for s in G_.syms:
        if s["kind"] in ("heading",):
            continue
        ws = {}
        for w in words(s.get("sig", "")):
            ws[w] = max(ws.get(w, 0), 0.5)          # parameter names say little
        for w in words(s["file"]):
            ws[w] = max(ws.get(w, 0), 0.6)
        for w in words(s.get("bw", "")):
            ws[w] = max(ws.get(w, 0), 0.6)          # identifiers the body uses
        for w in words(s.get("bc", "")):
            ws[w] = max(ws.get(w, 0), 1.0)          # intent comments in the body
        for w in words(s.get("doc", "")) + words(" ".join(G_.note_for(s))):
            ws[w] = max(ws.get(w, 0), 1.3)
        for w in words(s["qual"]):
            ws[w] = 2.5                              # it's in the name
        docs[s["id"]] = ws
        for w in ws:
            df[w] += 1
    n = max(1, len(docs))
    score = {}
    for sid, ws in docs.items():
        sc = 0.0
        hits = 0
        for w in q:
            if w in ws:
                sc += math.log(1 + n / (1 + df[w])) * ws[w]
                hits += 1
        if sc > 0:
            sy = G_.by_id[sid]
            sc *= 1 + 0.35 * (hits - 1)                  # matching several task words beats one strong word
            if not wants_tests and ("test" in sy["file"] or sy["name"].startswith("Test")):
                sc *= 0.55                               # tests show how, but the code is usually what's wanted
            if not wants_content and sy["kind"] in ("entity", "config", "target"):
                sc *= 0.5
            if sy["kind"] in ("function", "method") and sy["end"] > sy["line"]:
                sc *= 1 + 0.08 * math.log(1 + sy["end"] - sy["line"])   # real implementations over one-liners
            score[sid] = sc
    # spread a little along calls so the helpers of a relevant function show up too
    spread = dict(score)
    for sid, sc in score.items():
        for e in G_.out[sid] + G_.inn[sid]:
            other = e[1] if e[0] == sid else e[0]
            if other in G_.by_id and e[2] in ("calls", "uses"):
                hub = len(G_.inn[other])            # little helpers used everywhere (Normalized, Length) don't count
                spread[other] = max(spread.get(other, 0), sc * 0.3 * e[4] / math.log(2.7 + hub))
    ranked = sorted(spread.items(), key=lambda x: -x[1])
    top = ranked[0][1] if ranked else 0
    ranked = [r for r in ranked if r[1] >= top * 0.12]   # only the clearly relevant
    ranked = [r for r in ranked if not ((G_.by_id[r[0]].get("decl") or G_.by_id[r[0]]["kind"] == "decl") and G_.by_id[r[0]]["qual"] in G_.defined)]
    return ranked[:limit]


def compact_sig(s):
    """'static void Motor::StartClimb(Character& ch, ...)' -> 'StartClimb(Character& ch, ...)'; types stay, noise goes."""
    sig = s.get("sig", "")
    if s["kind"] in ("function", "method", "decl"):
        k = sig.find(s["name"] + "(")
        if k >= 0:
            sig = sig[k:]
    sig = re.sub(r"\bstd::", "", sig)
    sig = re.sub(r"\b(struct|class) (\w+)$", r"\1 \2", sig)
    sig = re.sub(r"\s+", " ", sig).strip()
    return sig if len(sig) <= 110 else sig[:107] + "..."


def cmd_context(G_, query, budget=900):
    ranked = rank_context(G_, query, limit=40)
    if not ranked:
        emit(f"Task: {query}\nnothing matched; try `map` or other words")
        return
    top = ranked[0][1]
    strong = [r for r in ranked if r[1] >= top * 0.25][:14]
    weak = [r for r in ranked if r not in strong][:10]
    # group the strong ones by file, files in order of their best match
    order, groups = [], defaultdict(list)
    for sid, sc in strong:
        f = G_.by_id[sid]["file"]
        if f not in groups:
            order.append(f)
        groups[f].append(G_.by_id[sid])
    out = [f"Task: {query}  (`show <name>` reads one)"]
    for f in order:
        block = [f]
        for sy in groups[f]:
            ln = f"  {sy['line']}-{sy['end']} {compact_sig(sy)}" if sy["end"] > sy["line"] else f"  {sy['line']} {compact_sig(sy)}"
            if sy.get("parent") and not compact_sig(sy).startswith(sy["parent"]) and sy["kind"] == "method":
                ln = ln.replace(f" {sy['name']}(", f" {sy['parent'].split('::')[-1]}::{sy['name']}(", 1)
            if sy.get("doc"):
                ln += f"  // {sy['doc'][:70]}"
            block.append(ln)
            for nt in G_.note_for(sy):
                block.append(f"    NOTE: {nt}")
        if tok("\n".join(out + block)) > budget:
            break
        out += block
    if weak:
        out.append("also: " + ", ".join(f"{G_.by_id[sid]['qual']} {G_.by_id[sid]['file'].split('/')[-1]}:{G_.by_id[sid]['line']}" for sid, _ in weak))
    emit("\n".join(out))


def cmd_brief(G_, q):
    """Everything about one symbol in one view: where, signature, doc, notes, members, callers, callees."""
    res = G_.resolve(q)
    if not res:
        print("not found")
        return
    out = []
    for sy in res[:2]:
        out.append(f"{sy['qual']}  {G_.loc(sy)}  [{sy['kind']}]")
        out.append(f"  {compact_sig(sy)}")
        if sy.get("doc"):
            out.append(f"  // {sy['doc'][:200]}")
        for nt in G_.note_for(sy):
            out.append(f"  NOTE: {nt}")
        if sy.get("members"):
            out.append("  members: " + ", ".join(sy["members"][:30]))
        meths = [x for x in G_.syms if x.get("parent") == sy["qual"] and x["kind"] == "method" and not x.get("decl")]
        if meths:
            out.append("  methods: " + ", ".join(f"{m['name']}:{m['line']}" for m in sorted(meths, key=lambda m: m['line'])[:30]))
        ins = sorted({o["qual"] for o, e in _neighbors(G_, sy["id"], "in")})
        outs = sorted({o["qual"] for o, e in _neighbors(G_, sy["id"], "out")})
        if ins:
            out.append(f"  called by ({len(ins)}): " + ", ".join(ins[:15]) + (" …" if len(ins) > 15 else ""))
        if outs:
            out.append(f"  calls ({len(outs)}): " + ", ".join(outs[:20]) + (" …" if len(outs) > 20 else ""))
    emit("\n".join(out))


def cmd_refs(G_, ident):
    """Functions whose bodies use an identifier (a member, constant, local name): grep, but scoped to symbols."""
    rx = re.compile(r"\b" + re.escape(ident) + r"\b")
    hits = [sy for sy in G_.syms if sy.get("bw") and rx.search(sy["bw"])]
    if len(hits) < 40:   # bw is truncated for long functions: confirm on disk
        extra = []
        cache = {}
        for sy in G_.syms:
            if sy in hits or sy["kind"] not in ("function", "method") or sy["end"] <= sy["line"] or sy.get("decl"):
                continue
            if sy["file"] not in cache:
                with open(os.path.join(ROOT, sy["file"]), encoding="utf-8", errors="replace") as f:
                    cache[sy["file"]] = f.read().split("\n")
            if any(rx.search(l) for l in cache[sy["file"]][sy["line"] - 1:sy["end"]]):
                extra.append(sy)
        hits += extra
    by_file = defaultdict(list)
    for sy in hits:
        by_file[sy["file"]].append(sy)
    out = [f"`{ident}` used in {len(hits)} symbols:"]
    for f in sorted(by_file):
        out.append(f"  {f}: " + ", ".join(f"{x['qual']}:{x['line']}" for x in sorted(by_file[f], key=lambda x: x['line'])))
    emit("\n".join(out))


def cmd_diff(G_, rev="HEAD"):
    """Symbols touched by changes since `rev` (default: uncommitted), with how many callers each has."""
    try:
        d = subprocess.run(["git", "diff", "-U0", rev, "--"], cwd=ROOT, capture_output=True, text=True).stdout
    except Exception as ex:
        print(f"git diff failed: {ex}")
        return
    touched = defaultdict(set)
    cur = None
    for l in d.split("\n"):
        if l.startswith("+++ b/"):
            cur = l[6:]
        elif l.startswith("@@") and cur:
            m = re.search(r"\+(\d+)(?:,(\d+))?", l)
            a = int(m.group(1)); n = int(m.group(2) or 1)
            for ln in range(a, a + max(n, 1)):
                touched[cur].add(ln)
    out = [f"changes since {rev}:"]
    for f, lines in sorted(touched.items()):
        info = G_.g["files"].get(f)
        syms = []
        if info:
            for sy in info["syms"]:
                if sy["kind"] in ("heading",):
                    continue
                if any(sy["line"] <= ln <= sy["end"] for ln in lines) and not any(o is not sy and o["line"] >= sy["line"] and o["end"] <= sy["end"] and o["end"] > o["line"] and any(o["line"] <= ln <= o["end"] for ln in lines) for o in info["syms"] if o.get("parent") == sy["qual"]):
                    syms.append(sy)
        names = ", ".join(f"{x['qual']}({len(_neighbors(G_, x['id'], 'in'))} callers)" for x in syms[:12]) or "(outside symbols)"
        out.append(f"  {f} [{len(lines)} lines]: {names}")
    emit("\n".join(out) if touched else f"no changes since {rev}")


def cmd_html(G_, out_path):
    """The visual: a layer-ordered file map + symbol neighbourhoods, self-contained (data embedded)."""
    import datetime
    files = sorted(f for f in G_.g["files"] if f.endswith(tuple(CPP_EXT | {".py"})))
    fidx = {f: i for i, f in enumerate(files)}
    keep = [sy for sy in G_.syms if sy["file"] in fidx and sy["kind"] in ("function", "method", "class", "struct", "enum")
            and not sy.get("decl") and not (sy["kind"] == "method" and sy["end"] == sy["line"] and sy["qual"] in G_.defined)]
    sidx = {sy["id"]: i for i, sy in enumerate(keep)}
    syms = [[sy["qual"], sy["name"], sy["kind"], fidx[sy["file"]], sy["line"], sy["end"], compact_sig(sy), sy.get("doc", "")[:220]] for sy in keep]
    calls = [[sidx[e[0]], sidx[e[1]], 1 if e[3] == "ambiguous" else 0] for e in G_.g["edges"] if e[2] == "calls" and e[0] in sidx and e[1] in sidx]
    finfo = [[f, G_.g["files"][f]["lines"], G_.g["files"][f].get("summary", "")[:200]] for f in files]
    deg = defaultdict(int)
    for a, b, _ in calls:
        deg[a] += 1
        deg[b] += 1
    start = max(range(len(syms)), key=lambda i: deg[i]) if syms else 0
    fdeg = defaultdict(int)
    for a, b, _ in calls:
        if syms[a][3] != syms[b][3]:
            fdeg[syms[a][3]] += 1
            fdeg[syms[b][3]] += 1
    data = {"files": finfo, "syms": syms, "calls": calls, "notes": G_.notes, "start": start,
            "startFile": max(fdeg, key=fdeg.get) if fdeg else 0, "built": datetime.date.today().isoformat()}
    tpl = open(os.path.join(os.path.dirname(__file__), "atlas_template.html"), encoding="utf-8").read()
    html = tpl.replace("/*DATA*/null", json.dumps(data, separators=(",", ":")).replace("</", "<\\/"))
    out = out_path if os.path.isabs(out_path) else os.path.join(ROOT, out_path)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    open(out, "w", encoding="utf-8").write(html)
    print(f"{os.path.relpath(out, ROOT)}: {len(files)} files, {len(syms)} symbols, {len(calls)} calls ({len(html) // 1024} KB)")


def cmd_changed(G_):
    out = []
    for f in source_files():
        full = os.path.join(ROOT, f)
        with open(full, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
        h = hashlib.sha1(text.encode("utf-8", "replace")).hexdigest()
        info = G_.g["files"].get(f)
        if info is None:
            out.append(f"new: {f}")
        elif info["hash"] != h:
            out.append(f"changed: {f}")
    removed = [f for f in G_.g["files"] if not os.path.exists(os.path.join(ROOT, f))]
    out += [f"removed: {f}" for f in removed]
    emit("\n".join(out) or "nothing changed since the last index")


def cmd_report(G_):
    called = defaultdict(int)
    for e in G_.g["edges"]:
        if e[2] == "calls":
            called[e[1]] += 1
    hubs = sorted(called.items(), key=lambda x: -x[1])[:15]
    big = sorted([s for s in G_.syms if s["kind"] in ("function", "method")], key=lambda s: -(s["end"] - s["line"]))[:12]
    amb = sum(1 for e in G_.g["edges"] if e[3] == "ambiguous")
    out = ["# Eon code report", f"{len(G_.g['files'])} files, {len(G_.syms)} symbols, {len(G_.g['edges'])} edges ({amb} ambiguous) [{G_.g.get('parser')}]",
           "\n## Hubs (most called)"] + [f"- {G_.by_id[i]['qual']} ×{c}  {G_.loc(G_.by_id[i])}" for i, c in hubs if i in G_.by_id]
    out += ["\n## Biggest functions (split candidates)"] + [f"- {s['qual']} {s['end'] - s['line']}L  {G_.loc(s)}" for s in big]
    todo = []
    for f in G_.g["files"]:
        if not f.endswith(tuple(CPP_EXT | {'.py'})):
            continue
        with open(os.path.join(ROOT, f), encoding="utf-8", errors="replace") as fh:
            for i, l in enumerate(fh):
                if re.search(r"\b(TODO|FIXME|HACK|XXX)\b", l):
                    todo.append(f"- {f}:{i+1} {l.strip()[:110]}")
    out += ["\n## TODO/FIXME/HACK"] + (todo[:30] or ["- none"])
    if G_.notes:
        out += ["\n## Notes"] + [f"- {k}: {v}" for k, vs in G_.notes.items() for v in vs]
    emit("\n".join(out))


def cmd_note(G_, q, text):
    res = G_.resolve(q)
    key = res[0]["qual"] if res else q
    notes = load_notes()
    if text == "--clear":
        notes.pop(key, None)
    else:
        notes.setdefault(key, [])
        if text not in notes[key]:
            notes[key].append(text)
    with open(NOTES, "w", encoding="utf-8") as f:
        json.dump(notes, f, indent=1, sort_keys=True)
    print(f"note on {key}: {len(notes.get(key, []))} total")


def cmd_stats(G_):
    kinds = defaultdict(int)
    for s in G_.syms:
        kinds[s["kind"]] += 1
    total = sum(i["lines"] for i in G_.g["files"].values())
    print(f"{len(G_.g['files'])} files, {total} lines (~{total * 10 // 4} tokens to read everything), {len(G_.syms)} symbols: "
          + ", ".join(f"{k} {v}" for k, v in sorted(kinds.items())) + f"; {len(G_.g['edges'])} edges; parser {G_.g.get('parser')}")


def cmd_install_hook():
    hook = os.path.join(ROOT, ".git", "hooks", "post-commit")
    os.makedirs(os.path.dirname(hook), exist_ok=True)
    with open(hook, "w") as f:
        f.write('#!/bin/sh\npython3 tools/codegraph/eongraph.py index >/dev/null 2>&1 || python tools/codegraph/eongraph.py index >/dev/null 2>&1\n')
    os.chmod(hook, 0o755)
    print("post-commit hook installed: the graph re-indexes after every commit")


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help", "help"):
        print(__doc__)
        return
    cmd, args = argv[1], argv[2:]
    budget = None
    if "--budget" in args:
        i = args.index("--budget")
        budget = int(args[i + 1])
        args = args[:i] + args[i + 2:]
    if cmd == "index":
        index(force="--force" in args)
        return
    if cmd == "install-hook":
        cmd_install_hook()
        return
    G_ = G(ensure_graph())
    if cmd == "q":
        # batch: several queries in one call, e.g. q "brief Motor::Ray" "callers World::RayCast"
        import shlex
        for k, sub in enumerate(args):
            parts = shlex.split(sub)
            if k:
                print("─" * 40)
            dispatch(G_, parts[0], parts[1:], None)
        return
    dispatch(G_, cmd, args, budget)


def dispatch(G_, cmd, args, budget):
    if cmd == "map":
        cmd_map(G_, budget or 1800)
    elif cmd == "file":
        cmd_file(G_, args[0])
    elif cmd == "find":
        cmd_find(G_, args[0])
    elif cmd == "show":
        ctx = int(args[-1]) if len(args) > 1 and args[-1].isdigit() else 0
        cmd_show(G_, [a for a in args if not a.isdigit()], ctx)
    elif cmd == "brief":
        cmd_brief(G_, args[0])
    elif cmd == "refs":
        cmd_refs(G_, args[0])
    elif cmd == "diff":
        cmd_diff(G_, args[0] if args else "HEAD")
    elif cmd == "callers":
        cmd_rel(G_, args[0], "in")
    elif cmd == "callees":
        cmd_rel(G_, args[0], "out")
    elif cmd == "impact":
        cmd_impact(G_, args[0], int(args[1]) if len(args) > 1 else 3)
    elif cmd == "path":
        cmd_path(G_, args[0], args[1])
    elif cmd == "context":
        cmd_context(G_, " ".join(args), budget or 700)
    elif cmd == "html":
        cmd_html(G_, args[0] if args else "out/eon_codegraph.html")
    elif cmd == "eval":
        cmd_eval(G_)
    elif cmd == "changed":
        cmd_changed(G_)
    elif cmd == "report":
        cmd_report(G_)
    elif cmd == "note":
        cmd_note(G_, args[0], " ".join(args[1:]))
    elif cmd == "stats":
        cmd_stats(G_)
    else:
        print(f"unknown command {cmd}\n{__doc__}")


if __name__ == "__main__":
    main(sys.argv)
