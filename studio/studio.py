"""PrepLang Studio: a local app for editing programs and data and watching the compiler react.

    py -3 studio/studio.py                 # makes ../demo (once) and opens http://127.0.0.1:8765
    py -3 studio/studio.py --reset         # puts the demo folder back to how it started
    py -3 studio/studio.py --no-browser    # does not open a browser

The workspace (by default comp_v1/demo, next to this repository) has three folders that you can
change in the app or in Explorer:
    csv/        tables: the students file, next week's batches, Titanic
    images/     image folders: train/ (with one broken file), new_photos/, small_photos/
    programs/   PrepLang programs (.prep) and a pandas script (.py) to lift

Everything runs the real compiler (build/Release/preplang.exe) and the real generated programs.
"""
import json
import mimetypes
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
NEW = ROOT / "build" / "Release" / "preplang.exe"
OLD = ROOT.parent / "phase2-src" / "build" / "Release" / "preplang.exe"
WORK = (ROOT.parent / "demo").resolve()
PORT = 8765
PYTHON = sys.executable
FLAGS = ["--tokens", "--ast", "--schema", "--semantic", "--lineage", "--ir", "--certificate", "--explain"]
IMAGE_TYPES = {".jpg", ".jpeg", ".png", ".bmp", ".tif", ".tiff", ".webp"}


# ---------------------------------------------------------------- the workspace

def setup(reset=False):
    """Makes the demo folder from the examples in the repository."""
    if WORK.exists() and not reset:
        return
    if WORK.exists():
        shutil.rmtree(WORK)
    csv, images, programs = WORK / "csv", WORK / "images", WORK / "programs"
    for folder in (csv, images, programs):
        folder.mkdir(parents=True)
    shutil.copyfile(ROOT / "examples" / "tabular" / "students.csv", csv / "students.csv")
    for name in ("next_week.csv", "typo_week.csv", "missing_name.csv"):
        shutil.copyfile(ROOT / "demo" / "data" / name, csv / name)
    shutil.copyfile(ROOT / "examples" / "images" / "labels.csv", csv / "labels.csv")
    titanic = ROOT / "eval" / "data" / "titanic"
    if (titanic / "train.csv").exists():
        shutil.copyfile(titanic / "train.csv", csv / "titanic_train.csv")
        shutil.copyfile(titanic / "test.csv", csv / "titanic_test.csv")

    shutil.copytree(ROOT / "examples" / "images" / "dataset", images / "train")
    sys.path.insert(0, str(ROOT / "demo"))
    from run_demo import make_photos  # the same photos as the scripted demo
    make_photos(images)

    tabular = (ROOT / "examples" / "tabular" / "students.prep").read_text(encoding="utf-8")
    (programs / "students.prep").write_text(tabular.replace('"students.csv"', '"../csv/students.csv"'), encoding="utf-8")
    imageset = (ROOT / "examples" / "images" / "imageset.prep").read_text(encoding="utf-8")
    (programs / "imageset.prep").write_text(imageset.replace('"dataset/"', '"../images/train/"'), encoding="utf-8")
    linked = (ROOT / "examples" / "images" / "linked.prep").read_text(encoding="utf-8")
    (programs / "linked.prep").write_text(linked.replace('"dataset/"', '"../images/train/"')
                                          .replace('"labels.csv"', '"../csv/labels.csv"'), encoding="utf-8")
    errors = (ROOT / "examples" / "errors" / "semantic_error.prep").read_text(encoding="utf-8")
    (programs / "errors.prep").write_text(errors.replace('"../tabular/students.csv"', '"../csv/students.csv"'),
                                          encoding="utf-8")
    if (csv / "titanic_train.csv").exists():
        script = (ROOT / "demo" / "titanic_clean.py").read_text(encoding="utf-8")
        (programs / "titanic_clean.py").write_text(script.replace('"train.csv"', '"../csv/titanic_train.csv"')
                                                   .replace('"train_clean.csv"', '"../csv/titanic_clean_out.csv"'),
                                                   encoding="utf-8")
    (WORK / "README.txt").write_text(
        "PrepLang demo workspace.\n\n"
        "  csv/        tables. Edit them here or in the Studio, or copy one to make a new batch.\n"
        "  images/     image folders. Add, remove or replace images freely.\n"
        "  programs/   PrepLang programs (.prep). Paths in a program are relative to programs/.\n\n"
        "Start the Studio:  py -3 ..\\PrepLang\\studio\\studio.py   (or double-click Start Studio.cmd)\n"
        "Start over:        py -3 ..\\PrepLang\\studio\\studio.py --reset\n", encoding="utf-8")
    (WORK / "Start Studio.cmd").write_text(f'@"{PYTHON}" "{HERE / "studio.py"}" %*\n', encoding="utf-8")


def inside(relative):
    """A path in the workspace; anything outside it is refused."""
    path = (WORK / relative).resolve()
    if path != WORK and WORK not in path.parents:
        raise ValueError(f"{relative} is outside the workspace")
    return path


def rel(path):
    return Path(path).resolve().relative_to(WORK).as_posix()


def image_info(path):
    import cv2
    import numpy as np
    data = np.fromfile(str(path), dtype=np.uint8)
    pixels = cv2.imdecode(data, cv2.IMREAD_UNCHANGED) if data.size else None
    if pixels is None:
        return {"path": rel(path), "name": path.name, "broken": True, "bytes": path.stat().st_size}
    channels = 1 if pixels.ndim == 2 else pixels.shape[2]
    return {"path": rel(path), "name": path.name, "broken": False, "bytes": path.stat().st_size,
            "width": int(pixels.shape[1]), "height": int(pixels.shape[0]), "channels": int(channels),
            "depth": str(pixels.dtype)}


def tree():
    programs = sorted(rel(p) for p in (WORK / "programs").glob("*") if p.suffix in (".prep", ".py"))
    tables = sorted(rel(p) for p in (WORK / "csv").rglob("*.csv"))
    folders = []
    for folder in sorted(p for p in (WORK / "images").iterdir() if p.is_dir()):
        files = [p for p in folder.rglob("*") if p.is_file()]
        folders.append({"path": rel(folder), "name": folder.name, "count": len(files)})
    return {"workspace": str(WORK), "programs": programs, "tables": tables, "folders": folders,
            "phase2": OLD.exists(), "compiler": NEW.exists()}


# ---------------------------------------------------------------- the compiler

SECTION = re.compile(r"^=+ (.+?) =+$")
DIAGNOSTIC = re.compile(r"^(error|warning)\[(\w+)\]: (.*)$")
WHERE = re.compile(r"-->\s+\S+?:(\d+):(\d+)")


def sections(text):
    found, name = {}, None
    for line in text.splitlines():
        match = SECTION.match(line)
        if match:
            name = re.sub(r"\s*\(.*?\)|\s*->.*", "", match.group(1)).strip()
            found[name] = []
        elif name:
            found[name].append(line)
    return {key: "\n".join(lines).strip("\n") for key, lines in found.items()}


def diagnostics(text):
    lines, found = text.splitlines(), []
    for i, line in enumerate(lines):
        match = DIAGNOSTIC.match(line)
        if not match:
            continue
        where = WHERE.search(lines[i + 1]) if i + 1 < len(lines) else None
        block = [line]
        for follow in lines[i + 1:]:
            if not follow.strip() or DIAGNOSTIC.match(follow):
                break
            block.append(follow)
        found.append({"level": match.group(1), "kind": match.group(2), "message": match.group(3),
                      "line": int(where.group(1)) if where else None, "column": int(where.group(2)) if where else None,
                      "text": "\n".join(block)})
    return found


def compile_program(relative, source=None, flags=FLAGS, compiler=NEW):
    """Compiles a program. With `source`, the unsaved text is compiled from a hidden copy next to it,
    so relative paths in it still work."""
    path = inside(relative)
    target = path
    if source is not None:
        target = path.with_name(f".~{path.name}")
        target.write_text(source, encoding="utf-8")
    start = time.perf_counter()
    done = subprocess.run([str(compiler), target.name, *flags], cwd=path.parent, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    seconds = time.perf_counter() - start
    if source is not None:
        target.unlink(missing_ok=True)
    text = (done.stdout + done.stderr).replace(target.name, path.name)
    summary = next((l for l in text.splitlines() if l.startswith(("Compiled successfully", "Compilation failed"))), "")
    return {"ok": done.returncode == 0, "seconds": seconds, "summary": summary, "sections": sections(text),
            "diagnostics": diagnostics(text), "raw": text, "inputs": inputs_of(source or path.read_text("utf-8"))}


def inputs_of(source):
    return [{"kind": "images" if kind else "table", "file": file, "name": name}
            for kind, file, name in re.findall(r'^\s*LOAD(_IMAGES)?\s+"([^"]+)"\s+AS\s+(\w+)', source, re.M)]


def run_program(relative, choices):
    """Compiles the saved program, then runs its generated Python with other inputs if chosen."""
    compiled = compile_program(relative, flags=["--certificate"])
    result = {"compile": {k: compiled[k] for k in ("ok", "seconds", "summary", "diagnostics")}}
    if not compiled["ok"]:
        result.update(verdict="not compiled", output="")
        return result
    path = inside(relative)
    generated = path.parent / "output" / "generated.py"
    args = [f"{name}={inside(choice)}" for name, choice in choices.items() if choice]
    start = time.perf_counter()
    done = subprocess.run([PYTHON, str(generated), *args], cwd=path.parent, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")
    output = "\n".join(l for l in (done.stdout + done.stderr).splitlines() if "WARN" not in l)
    output = output.replace(str(WORK) + os.sep, "")
    verdict = ("refused" if "ContractError" in output else "admitted" if "Admitted" in output
               else "finished" if done.returncode == 0 else "failed")
    result.update(verdict=verdict, output=output, seconds=time.perf_counter() - start, previews=[])
    manifest = path.parent / "output" / "preplang_manifest.json"
    if done.returncode == 0 and manifest.exists():
        for out in json.loads(manifest.read_text(encoding="utf-8")).get("outputs", []):
            file = path.parent / "output" / out.get("file", "")
            if file.suffix == ".csv" and file.exists():
                rows = file.read_text(encoding="utf-8", errors="replace").splitlines()
                result["previews"].append({"file": rel(file), "rows": out.get("rows"), "head": rows[:13]})
            elif out.get("folder") or out.get("images"):
                result["previews"].append({"file": str(out.get("folder", "")), "rows": out.get("images")})
    return result


def plain_script(relative):
    """The same steps as a plain pandas/OpenCV script, without any checks (eval/compilation/baseline.py)."""
    path = inside(relative)
    out = path.parent / "output" / "plain_script"
    if out.exists():
        shutil.rmtree(out)
    start = time.perf_counter()
    done = subprocess.run([PYTHON, str(ROOT / "eval" / "baseline.py"), path.name, str(out)], cwd=path.parent,
                          capture_output=True, text=True, encoding="utf-8", errors="replace")
    lines = [l for l in (done.stdout + done.stderr).splitlines() if "WARN" not in l and l.strip()]
    result = {"ok": done.returncode == 0, "seconds": time.perf_counter() - start, "output": "\n".join(lines[-12:])}
    written = sorted(p for p in out.rglob("*") if p.is_file()) if out.exists() else []
    if written:
        result["files"] = {"count": len(written), "names": [p.relative_to(out).as_posix() for p in written[:6]]}
    pngs = list(out.rglob("*.png")) if out.exists() else []
    if pngs:
        import cv2
        means = [float(img.mean()) for img in (cv2.imread(str(p), cv2.IMREAD_UNCHANGED) for p in pngs) if img is not None]
        result["images"] = {"count": len(pngs), "brightest_mean": max(means) if means else None,
                            "samples": [rel(p) for p in pngs[:4]]}
    return result


def lift(relative):
    path = inside(relative)
    target = path.with_name(path.stem + "_lifted.prep")
    done = subprocess.run([PYTHON, str(ROOT / "frontend" / "pandas2prep.py"), path.name, "-o", target.name],
                          cwd=path.parent, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return {"ok": done.returncode == 0 and target.exists(), "program": rel(target) if target.exists() else None,
            "notes": done.stderr.strip()}


def make_image(folder, width, height, channels, kind, broken, name=None):
    import cv2
    import numpy as np
    target = inside(folder)
    target.mkdir(parents=True, exist_ok=True)
    kind = kind.lower().lstrip(".")
    stem = name or f"test_{width}x{height}"
    path, n = target / f"{stem}.{kind}", 1
    while path.exists():
        n += 1
        path = target / f"{stem}_{n}.{kind}"
    if broken:
        path.write_bytes(b"this is not an image " * 20)
        return rel(path)
    y, x = np.mgrid[0:height, 0:width]
    base = ((x * 255 // max(width - 1, 1) + y * 255 // max(height - 1, 1)) // 2).astype(np.uint8)
    pixels = base if channels == 1 else np.dstack([base, 255 - base, base // 2, np.full_like(base, 255)][:channels])
    ok, data = cv2.imencode("." + kind, pixels)
    if not ok:
        raise ValueError(f"cannot write a {channels}-channel {kind.upper()} image")
    path.write_bytes(data.tobytes())
    return rel(path)


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, body, kind="application/json", status=200):
        data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlparse(self.path)
        query = {k: v[0] for k, v in parse_qs(url.query).items()}
        try:
            if url.path == "/":
                self.send((HERE / "index.html").read_bytes(), "text/html; charset=utf-8")
            elif url.path == "/api/tree":
                self.send(tree())
            elif url.path == "/api/file":
                self.send({"path": query["path"], "text": inside(query["path"]).read_text(encoding="utf-8", errors="replace")})
            elif url.path == "/api/images":
                folder = inside(query["path"])
                files = sorted(p for p in folder.rglob("*") if p.is_file())
                self.send({"path": query["path"], "images": [image_info(p) for p in files]})
            elif url.path == "/api/raw":
                path = inside(query["path"])
                self.send(path.read_bytes(), mimetypes.guess_type(path.name)[0] or "application/octet-stream")
            else:
                self.send({"error": "not found"}, status=404)
        except Exception as error:  # shown in the app
            self.send({"error": str(error)}, status=400)

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        try:
            path = urlparse(self.path).path
            if path == "/api/save":
                inside(body["path"]).write_text(body["text"], encoding="utf-8", newline="")
                self.send({"ok": True})
            elif path == "/api/copy":
                source, target = inside(body["path"]), inside(body["to"])
                if target.exists():
                    raise ValueError(f"{body['to']} already exists")
                (shutil.copytree if source.is_dir() else shutil.copyfile)(source, target)
                self.send({"ok": True, "path": rel(target)})
            elif path == "/api/compile":
                compiler = OLD if body.get("compiler") == "phase2" else NEW
                flags = ["--explain"] if compiler == OLD else FLAGS
                self.send(compile_program(body["path"], body.get("source"), flags, compiler))
            elif path == "/api/run":
                self.send(run_program(body["path"], body.get("inputs", {})))
            elif path == "/api/plain":
                self.send(plain_script(body["path"]))
            elif path == "/api/lift":
                self.send(lift(body["path"]))
            elif path == "/api/make_image":
                self.send({"ok": True, "path": make_image(body["folder"], int(body["width"]), int(body["height"]),
                                                          int(body["channels"]), body.get("format", "jpg"),
                                                          bool(body.get("broken")), body.get("name") or None)})
            elif path == "/api/open":
                os.startfile(inside(body.get("path", ".")))
                self.send({"ok": True})
            else:
                self.send({"error": "not found"}, status=404)
        except Exception as error:
            self.send({"error": str(error)}, status=400)


if __name__ == "__main__":
    if not NEW.exists():
        sys.exit(f"Build the compiler first: cmake --build {ROOT / 'build'} --config Release")
    setup(reset="--reset" in sys.argv)
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    url = f"http://127.0.0.1:{PORT}/"
    print(f"PrepLang Studio: {url}\nWorkspace: {WORK}\nStop with Ctrl+C.")
    if "--no-browser" not in sys.argv:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
