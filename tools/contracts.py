"""Composing compiled pipeline stages through their data certificates.

Every compilation writes output/preplang_contract.json: what each input must
satisfy to be admitted (the admission certificate), what each speculatively
removed check or operation needs (its item certificate), and what each output
file is known to satisfy for every admitted input (the guarantee).

    py -3 tools/contracts.py check STAGE1.json STAGE2.json --link OUTPUT=INPUT [--link ...]
    py -3 tools/contracts.py producer CONSUMER.json... --input TABLE [--out FILE.prep]

check     Assume-guarantee composition. For a stage-2 input fed by a stage-1 output,
          every value fact stage 2 relies on (no missing values, bounds) is compared
          with stage 1's guarantee. A fact the guarantee implies holds for every file
          stage 1 can write: stage 2 then never refuses or deoptimizes for it, and its
          check on admission is redundant. For a fact that is not implied, the tool
          names it and writes the EXPECT that stage 1 would need. Column types are
          not composed: they are checked on admission, from the file's metadata.

producer  The facts several consumers of one table rely on, together: the data
          contract its producer must meet so that no consumer ever refuses it or runs
          a removed check again, written as PrepLang EXPECT statements.

Exit code of check: 0 if every fact is guaranteed, 1 otherwise.
"""
import argparse
import json
import math
import sys
from pathlib import Path


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def requirements(contract, table):
    """[(column, kind, value, strict, needed by, level)] stage 2 relies on for one input table;
    level is "admission" (refused if broken) or "removal" (deoptimized if broken)."""
    found = []

    def add(columns, level, owner):
        for column, fact in columns.items():
            for_ = lambda key: fact.get(key + "_for") or [owner]  # noqa: E731
            if fact.get("not_null"):
                found.append((column, "not_null", None, False, for_("not_null"), level))
            if fact.get("min") is not None:
                found.append((column, "min", fact["min"], fact.get("min_strict", False), for_("min"), level))
            if fact.get("max") is not None:
                found.append((column, "max", fact["max"], fact.get("max_strict", False), for_("max"), level))
            if fact.get("required"):
                found.append((column, "present", None, False, ["admission"], level))

    if table in contract["inputs"]:
        add(contract["inputs"][table]["columns"], "admission", "admission")
    for removal in contract["removals"]:
        if table in removal["needs"]:
            add(removal["needs"][table], "removal", removal["description"])
    return found


def implied(requirement, guarantee):
    """True if every file with the guaranteed facts satisfies the requirement."""
    column, kind, value, strict, _, _ = requirement
    fact = guarantee["columns"].get(column)
    if fact is None:
        return False
    if kind == "present":
        return True
    if kind == "not_null":
        return not fact["nullable"]
    low, high = fact.get("min"), fact.get("max")
    if kind == "min":
        return low is not None and (low > value if strict else low >= value)
    return high is not None and (high < value if strict else high <= value)


def number(value):
    """A PrepLang number literal: no exponent."""
    return str(int(value)) if float(value).is_integer() else repr(float(value))


def suggestions(table, open_facts, guarantee):
    """PrepLang statements that, placed just before the SAVE in stage 1, would guarantee the open facts:
    one EXPECT per column (BETWEEN takes the other bound from the guarantee if it has one)."""
    lines, columns = [], {}
    for column, kind, value, strict, _, _ in open_facts:
        columns.setdefault(column, {})[kind] = (value, strict)
    for column, kinds in columns.items():
        if "not_null" in kinds:
            lines.append(f"EXPECT {table}.{column} NOT_NULL;")
        if "min" in kinds or "max" in kinds:
            known = guarantee["columns"].get(column, {})
            low = kinds["min"][0] if "min" in kinds else known.get("min")
            high = kinds["max"][0] if "max" in kinds else known.get("max")
            low = -1e15 if low is None else low
            high = 1e15 if high is None else high
            strict = any(kinds.get(k, (0, False))[1] for k in ("min", "max"))
            lines.append(f"EXPECT {table}.{column} BETWEEN {number(low)} AND {number(high)};" +
                         ("  # the removal needs a strict bound: use a FILTER instead" if strict else ""))
        if "present" in kinds:
            lines.append(f"# keep column {column} in what stage 1 saves")
    return lines


def check(stage1, stage2, links):
    first, second = load(stage1), load(stage2)
    missing = 0
    report = {"links": []}
    for output, table in links:
        guarantee = first["outputs"].get(output)
        if guarantee is None:
            print(f"error: {stage1} has no output {output!r}; it writes {', '.join(first['outputs'])}")
            return 2
        needs = requirements(second, table)
        held = [r for r in needs if implied(r, guarantee)]
        open_ = [r for r in needs if not implied(r, guarantee)]
        missing += len(open_)
        print(f"{output} -> {table}: {len(held)} of {len(needs)} facts guaranteed by {Path(stage1).name}")
        for r in held:
            print(f"  guaranteed   {r[0]} {r[1]}{'' if r[2] is None else ' ' + format(r[2], 'g')}"
                  f"  ({r[5]}: {'; '.join(r[4])})")
        for r in open_:
            print(f"  NOT implied  {r[0]} {r[1]}{'' if r[2] is None else ' ' + format(r[2], 'g')}"
                  f"  ({r[5]}: {'; '.join(r[4])})")
        fixes = suggestions(guarantee["table"], open_, guarantee)
        if fixes:
            print("  stage 1 would guarantee them with, before its SAVE:")
            for line in fixes:
                print("    " + line)
        report["links"].append({"output": output, "input": table, "facts": len(needs), "guaranteed": len(held),
                                "open": [{"column": r[0], "kind": r[1], "value": r[2], "level": r[5],
                                          "for": r[4]} for r in open_], "fixes": fixes})
    print("composes: stage 2 never refuses or deoptimizes a stage-1 output for its values" if missing == 0
          else f"{missing} fact(s) not guaranteed: stage 2 may refuse or deoptimize")
    return report if missing == 0 else report | {"missing": missing}


def producer(consumers, table, out):
    """The meet of every consumer's requirements on `table`: per column, the strongest bound."""
    columns = {}
    for path in consumers:
        for column, kind, value, strict, needed, level in requirements(load(path), table):
            facts = columns.setdefault(column, {"for": []})
            facts["for"] += [f"{Path(path).parent.parent.name}: {n}" for n in needed if n != "admission"]
            if kind == "not_null":
                facts["not_null"] = True
            elif kind == "present":
                facts["present"] = True
            elif kind == "min":
                old = facts.get("min")
                if old is None or value > old[0] or (value == old[0] and strict):
                    facts["min"] = (value, strict)
            elif kind == "max":
                old = facts.get("max")
                if old is None or value < old[0] or (value == old[0] and strict):
                    facts["max"] = (value, strict)
    lines = [f"# Data contract for {table}: the facts {len(consumers)} consumer(s) rely on (tools/contracts.py)."]
    count = 0
    for column, facts in columns.items():
        if facts.get("not_null"):
            lines.append(f"EXPECT {table}.{column} NOT_NULL;")
            count += 1
        low, high = facts.get("min"), facts.get("max")
        if low or high:
            a = low[0] if low else -math.inf
            b = high[0] if high else math.inf
            strict = (low and low[1]) or (high and high[1])
            lines.append(f"EXPECT {table}.{column} BETWEEN {a:g} AND {b:g};" +
                         ("  # strict bound" if strict else ""))
            count += 1
    text = "\n".join(lines) + "\n"
    if out:
        Path(out).write_text(text, encoding="utf-8")
    print(text, end="")
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    one = sub.add_parser("check")
    one.add_argument("stage1")
    one.add_argument("stage2")
    one.add_argument("--link", action="append", required=True, help="OUTPUT=INPUT, e.g. trips_clean.parquet=trips")
    one.add_argument("--json")
    two = sub.add_parser("producer")
    two.add_argument("consumers", nargs="+")
    two.add_argument("--input", required=True)
    two.add_argument("--out")
    args = parser.parse_args()
    if args.command == "check":
        result = check(args.stage1, args.stage2, [tuple(link.split("=", 1)) for link in args.link])
        if args.json and isinstance(result, dict):
            Path(args.json).write_text(json.dumps(result, indent=1), encoding="utf-8")
        sys.exit(0 if isinstance(result, dict) and "missing" not in result else 1)
    producer(args.consumers, args.input, args.out)


if __name__ == "__main__":
    main()
