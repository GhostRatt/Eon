#!/usr/bin/env python3
"""Eon dev loop in as few tokens as possible: every command prints only what matters.

  python3 tools/dev.py check [filter] [--apps]   build (errors only) + tests (failures only) -> one summary line
  python3 tools/dev.py test [filter]              tests only
  python3 tools/dev.py shot NAME [sim args...]    headless render -> out/shots/NAME.png (prints the path)
  python3 tools/dev.py sheet OUT A B C ... [--cols 2] [--full]   contact sheet (HUD cropped unless --full)
  python3 tools/dev.py handoff                    compact state of the work (for a new session / after compaction)
  python3 tools/dev.py viz                        writes the code-graph visual (out/eon_codegraph.html)
  python3 tools/dev.py win                        cross-compiles the Windows .exe files (needs mingw-w64)
  python3 tools/dev.py bench [zone] [--frames N] [--rig]   CPU ms/frame of the whole Jenny pipeline, per stage
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SHOTS = os.path.join(ROOT, "out", "shots")
EXE = ".exe" if os.name == "nt" else ""


def sh(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)


def build(target_dir):
    r = sh(["cmake", "--build", target_dir])
    if r.returncode == 0:
        return True, ""
    lines = (r.stdout + r.stderr).split("\n")
    out, seen = [], set()
    for i, l in enumerate(lines):
        if re.search(r"\berror\b|undefined reference|fatal", l) and l not in seen:
            seen.add(l)
            # path:line: error: message (+ the caret context line) — trimmed to the repo-relative path
            out.append(l.replace(ROOT + "/", "").strip()[:220])
            if i + 1 < len(lines) and lines[i + 1].strip() and not re.search(r"\berror\b", lines[i + 1]):
                out.append("    " + lines[i + 1].strip()[:160])
        if len(out) >= 16:
            out.append("... (more errors)")
            break
    return False, "\n".join(out) or (r.stdout + r.stderr)[-1500:]


def run_tests(flt=None):
    exe = os.path.join(ROOT, "build", "bin", "eon_tests" + EXE)
    args = [exe] + (["--only", flt] if flt else [])
    r = sh(args, timeout=900)
    lines = r.stdout.strip().split("\n")
    fails = [l for l in lines if l.startswith("FAIL")]
    summary = next((l for l in reversed(lines) if "passed" in l), "no summary (crash?) " + (r.stderr[-400:] if r.stderr else ""))
    return r.returncode == 0, fails, summary


def cmd_check(args):
    apps = "--apps" in args
    flt = next((a for a in args if not a.startswith("--")), None)
    ok, err = build("build")
    if not ok:
        print("BUILD FAILED (build)\n" + err)
        return 1
    if apps:
        ok, err = build("build-apps")
        if not ok:
            print("BUILD FAILED (build-apps)\n" + err)
            return 1
    ok, fails, summary = run_tests(flt)
    for f in fails[:25]:
        print(f.replace(ROOT + "/", ""))
    if len(fails) > 25:
        print(f"... {len(fails) - 25} more failures")
    gtest = ""
    if not flt:
        r = sh([sys.executable, "tools/codegraph/test_eongraph.py"])
        gtest = " | " + (r.stdout.strip().split("\n")[-1] if r.stdout else "eongraph test crashed")
    print(("OK  " if ok else "FAIL  ") + summary + gtest)
    return 0 if ok else 1


def cmd_test(args):
    ok, fails, summary = run_tests(args[0] if args else None)
    for f in fails:
        print(f.replace(ROOT + "/", ""))
    print(("OK  " if ok else "FAIL  ") + summary)
    return 0 if ok else 1


def cmd_shot(args):
    if not args:
        print("shot NAME [sim args]")
        return 1
    name, rest = args[0], args[1:]
    os.makedirs(SHOTS, exist_ok=True)
    zone = "content/zones/test_zone_01.json"
    if rest and rest[0].endswith(".json"):
        zone, rest = rest[0], rest[1:]
    exe = os.path.join(ROOT, "build-apps", "bin", "eon_sim" + EXE)
    cmd = [exe, zone, *rest, "--shot", name + ".png"]
    if os.name != "nt" and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a"] + cmd
    r = sh(cmd, timeout=300)
    src = os.path.join(ROOT, name + ".png")
    if not os.path.exists(src):
        err = [l for l in (r.stdout + r.stderr).split("\n") if "WARNING" in l or "ERROR" in l][-5:]
        print("no image written\n" + "\n".join(err))
        return 1
    dst = os.path.join(SHOTS, name + ".png")
    shutil.move(src, dst)
    print(os.path.relpath(dst, ROOT))
    return 0


def cmd_sheet(args):
    from PIL import Image
    cols = 2
    full = "--full" in args
    if "--cols" in args:
        i = args.index("--cols")
        cols = int(args[i + 1])
        args = args[:i] + args[i + 2:]
    args = [a for a in args if a != "--full"]
    out, imgs = args[0], args[1:]
    paths = [p if os.path.exists(p) else os.path.join(SHOTS, p if p.endswith(".png") else p + ".png") for p in imgs]
    ims = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if not full:
            w, h = im.size
            im = im.crop((0, int(h * 0.21), w, h))   # the operator HUD sits at the top
        ims.append(im.resize((800, int(800 * im.size[1] / im.size[0]))))
    h = max(i.size[1] for i in ims)
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (800 * cols, h * rows), (20, 20, 22))
    for k, im in enumerate(ims):
        sheet.paste(im, ((k % cols) * 800, (k // cols) * h))
    if not os.path.isabs(out) and os.sep not in out and "/" not in out:
        os.makedirs(SHOTS, exist_ok=True)
        out = os.path.join(SHOTS, out if out.endswith(".png") else out + ".png")
    sheet.save(out)
    print(os.path.relpath(out, ROOT) if out.startswith(ROOT) else out)
    return 0


def cmd_handoff(args):
    out = []
    log = sh(["git", "log", "--oneline", "-8"]).stdout.strip()
    out.append("## Recent commits\n" + "\n".join(l[:110] for l in log.split("\n")))
    st = sh(["git", "status", "--short"]).stdout.strip()
    out.append("## Uncommitted\n" + (st or "clean"))
    if st:
        d = sh([sys.executable, "tools/codegraph/eongraph.py", "diff"]).stdout.strip()
        out.append(d)
    r = sh([os.path.join(ROOT, "build", "bin", "eon_tests" + EXE), "--list"])
    out.append(f"## Tests: {len(r.stdout.split())} suites (run `dev.py check`)")
    notes = sh([sys.executable, "tools/codegraph/eongraph.py", "report"]).stdout
    m = notes.split("## Notes")
    if len(m) > 1:
        out.append("## Notes" + m[1].split("[~")[0].rstrip())
    todo = notes.split("## TODO/FIXME/HACK")
    if len(todo) > 1:
        out.append("## TODO" + todo[1].split("##")[0].rstrip())
    text = "\n\n".join(out)
    print(text + f"\n[~{len(text) // 4} tokens]")
    return 0


def cmd_viz(args):
    r = sh([sys.executable, "tools/codegraph/eongraph.py", "html", *(args or ["out/eon_codegraph.html"])])
    print(r.stdout.strip() or r.stderr.strip())
    return r.returncode


def cmd_bench(args):
    """Builds and runs eon_bench: CPU ms per frame of Jenny's whole pipeline, stage by stage."""
    b = sh(["cmake", "--build", "build", "--target", "eon_bench"])
    if b.returncode != 0:
        print(b.stdout[-1500:])
        return 1
    exe = os.path.join("build", "bin", "eon_bench" + (".exe" if os.name == "nt" else ""))
    r = sh([exe, *args])
    print(r.stdout.strip() or r.stderr.strip())
    return r.returncode


def cmd_win(args):
    """Cross-compiles the Windows .exe files (MinGW, static, stripped) into build-win/bin."""
    if not os.path.isdir("build-win"):
        c = sh(["cmake", "-S", ".", "-B", "build-win", "-G", "Ninja", "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake", "-DCMAKE_BUILD_TYPE=Release"], timeout=900)
        if c.returncode != 0:
            print(c.stdout[-1500:] + c.stderr[-1500:])
            return 1
    b = sh(["cmake", "--build", "build-win"], timeout=1800)
    if b.returncode != 0:
        print("\n".join(l for l in b.stdout.splitlines() if "error" in l.lower())[:3000])
        return 1
    for exe in ("eon_sim.exe", "eon_editor.exe", "eon_tests.exe"):
        sh(["x86_64-w64-mingw32-strip", os.path.join("build-win", "bin", exe)])
    print("OK  build-win/bin: eon_sim.exe eon_editor.exe eon_tests.exe")
    return 0


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmds = {"check": cmd_check, "test": cmd_test, "shot": cmd_shot, "sheet": cmd_sheet, "handoff": cmd_handoff, "viz": cmd_viz, "bench": cmd_bench, "win": cmd_win}
    if argv[1] not in cmds:
        print(__doc__)
        return 1
    return cmds[argv[1]](argv[2:])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
