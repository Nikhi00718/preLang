"""The inputs of the evaluation that come from elsewhere, frozen so that the paper's runs can be repeated.

    py -3 eval/corpora.py freeze           # once, by the author: write eval/corpora/ from eval/data/
    py -3 eval/corpora.py fetch [--limit N]  # anyone: rebuild the script corpora in eval/data/ from eval/corpora/
    py -3 eval/corpora.py verify           # anyone: check the downloaded data against eval/corpora/inputs.sha256

The script corpora (pandas_corpus.py, drift_corpus.py) were collected with GitHub code search, whose
results change over time, so `collect` cannot give the same corpus again. `freeze` keeps each corpus's
index (every file's URL at a fixed commit and the start of its SHA-256) in eval/corpora/, which is
committed; `fetch` downloads those files from raw.githubusercontent.com and keeps only the ones whose
hash matches, so a file whose repository was deleted or rewritten is reported, never replaced.

The data files are not redistributed: realbatches.py download fetches the UCI and NYC TLC files, and
the Kaggle Titanic and House Prices files are downloaded by hand (eval/README.md). `verify` reports any
file that differs from the one the paper's results were computed on.
"""
import argparse
import gzip
import hashlib
import json
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "eval" / "data"
FROZEN = ROOT / "eval" / "corpora"
CORPORA = ["pandas_corpus", "pandas_corpus_houses", "drift_corpus_traffic", "drift_corpus_bikes", "drift_corpus_hours"]
# Input files from outside the repository, relative to eval/data/ (folders: every file in them)
INPUTS = ["titanic/train.csv", "titanic/test.csv", "houses/kaggle_train.csv", "houses/kaggle_test.csv",
          "real/metro.zip", "real/bike.zip", "real/green"]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def input_files():
    for name in INPUTS:
        path = DATA / name
        yield from sorted(p for p in path.iterdir() if p.is_file()) if path.is_dir() else [path]


def freeze():
    FROZEN.mkdir(exist_ok=True)
    for corpus in CORPORA:
        index = json.loads((DATA / corpus / "index.json").read_text(encoding="utf-8"))
        with gzip.open(FROZEN / f"{corpus}.json.gz", "wt", encoding="utf-8") as out:
            json.dump(index, out, indent=0)
        print(f"{corpus}: {len(index)} files")
    lines = [f"{sha256(path)}  {path.relative_to(DATA).as_posix()}" for path in input_files()]
    (FROZEN / "inputs.sha256").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"inputs.sha256: {len(lines)} files")


def raw_url(url):
    """https://github.com/o/r/blob/<sha>/path -> https://raw.githubusercontent.com/o/r/<sha>/path"""
    owner, repo, blob, sha, path = urllib.parse.urlsplit(url).path.lstrip("/").split("/", 4)
    assert blob == "blob", url
    return f"https://raw.githubusercontent.com/{owner}/{repo}/{sha}/" + urllib.parse.quote(urllib.parse.unquote(path))


def download(url):
    for attempt in range(3):
        try:
            return urllib.request.urlopen(url, timeout=30).read()
        except (OSError, ValueError):  # ValueError: a URL urllib cannot send
            time.sleep(2 * (attempt + 1))
    return None


def fetch(limit):
    missing = 0
    for corpus in CORPORA:
        with gzip.open(FROZEN / f"{corpus}.json.gz", "rt", encoding="utf-8") as frozen:
            index = json.load(frozen)[:limit]
        folder = DATA / corpus
        folder.mkdir(parents=True, exist_ok=True)

        def one(item):
            target = folder / f"{item['id']}.py"
            if target.exists() and sha256(target)[:16] == item["id"]:
                return True
            text = download(raw_url(item["url"]))
            if text is None or hashlib.sha256(text).hexdigest()[:16] != item["id"]:
                return False
            target.write_bytes(text)
            return True

        with ThreadPoolExecutor(max_workers=16) as pool:
            found = list(pool.map(one, index))
        kept = [item for item, ok in zip(index, found) if ok]
        for item, ok in zip(index, found):
            if not ok:
                print(f"  not available or changed: {item['url']}")
        (folder / "index.json").write_text(json.dumps(kept, indent=1), encoding="utf-8")
        missing += len(index) - len(kept)
        print(f"{corpus}: {len(kept)} of {len(index)} files")
    return missing


def verify():
    wrong = 0
    for line in (FROZEN / "inputs.sha256").read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        path = DATA / name
        if not path.exists():
            print(f"  missing: eval/data/{name}")
            wrong += 1
        elif sha256(path) != digest:
            print(f"  differs: eval/data/{name}")
            wrong += 1
    print(f"{wrong} input file(s) missing or different")
    return wrong


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("freeze", "fetch", "verify"))
    parser.add_argument("--limit", type=int, help="fetch: only the first N files of each corpus (a quick check)")
    args = parser.parse_args()
    if args.command == "freeze":
        freeze()
    elif args.command == "fetch":
        sys.exit(1 if fetch(args.limit) else 0)
    else:
        sys.exit(1 if verify() else 0)


if __name__ == "__main__":
    main()
