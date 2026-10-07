"""Lists the number macros preplang.tex (and the tables it inputs) uses but numbers.tex does not define.

    py -3 paper/check_macros.py
"""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
text = "".join((HERE / name).read_text(encoding="utf-8")
               for name in ("preplang.tex", "certtable.tex", "realtable.tex") if (HERE / name).exists())
defined = set(re.findall(r"\\newcommand\{\\(\w+)\}", (HERE / "numbers.tex").read_text(encoding="utf-8")))
used = set(re.findall(r"\\([A-Z][A-Za-z]+)", text))
latex = {"IEEEauthorblockN", "IEEEauthorblockA", "IEEEproof", "IEEEkeywords", "TeX", "LaTeX"}
missing = sorted(used - defined - latex)
print("undefined:", ", ".join(missing) if missing else "none")
