#!/usr/bin/env python3
"""Regression checks for eongraph: indexing, typed call resolution, queries, and search quality on eval.json."""
import io
import json
import os
import sys
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(__file__))
import eongraph as eg  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL", what)


def run(*argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        eg.main(["eongraph", *argv])
    return buf.getvalue()


g = eg.index(quiet=True)
G = eg.G(g)
check(g["parser"] == "tree-sitter", "tree-sitter parser available")
check(len(G.syms) > 500, "symbols extracted")

# typed resolution: world.RayCast(...) inside Motor goes to World::RayCast, not Physics::RayCast
ray = G.resolve("Motor::Ray")[0]
callees = {o["qual"]: e[3] for o, e in eg._neighbors(G, ray["id"], "out")}
check(callees.get("World::RayCast") == "extracted", "Motor::Ray -> World::RayCast resolved by type")
check("Physics::RayCast" not in callees, "no false edge to Physics::RayCast")

# definitions win over declarations
check(all(not s.get("decl") for s in G.resolve("Motor::GroundAt")), "resolve prefers the definition")
out = run("callers", "Motor::GroundAt")
check("Motor::StartClimb" in out and "ambiguous" not in out, "callers are precise")

# show prints exactly the function (with its line numbers)
out = run("show", "Motor::BedRoom")
check("bool Motor::BedRoom" in out and "Motor.cpp" in out, "show prints the function")
check(out.count("\n") < 30, "show is just that function")

# batch + brief + refs + impact work
out = run("q", "brief Motor::Ray", "refs held_", "impact Motor::GroundAt")
check("called by" in out and "used in" in out and "can affect" in out, "batch of brief/refs/impact")

# search quality floor (eval.json): keep MRR >= 0.8 and every answer in the top 10
cases = json.load(open(os.path.join(os.path.dirname(__file__), "eval.json")))
rr, top10 = 0.0, 0
for q, want in cases:
    want = want if isinstance(want, list) else [want]
    ranked = [G.by_id[i]["qual"] for i, _ in eg.rank_context(G, q)]
    r = next((k + 1 for k, x in enumerate(ranked) if x in want), None)
    rr += 1 / r if r else 0
    top10 += bool(r and r <= 10)
mrr = rr / len(cases)
check(mrr >= 0.8, f"search MRR {mrr:.3f} >= 0.8")
check(top10 == len(cases), f"all answers in top 10 ({top10}/{len(cases)})")

# context output stays compact
out = run("context", "carry crate two hands steady weight")
check(eg.tok(out) < 800, "context under budget")

print(f"eongraph tests: {'OK' if not fails else str(fails) + ' failed'} (MRR {mrr:.3f})")
sys.exit(1 if fails else 0)
