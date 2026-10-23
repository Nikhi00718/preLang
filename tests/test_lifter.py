"""Tests of frontend/pandas2prep.py, the lifter of pandas scripts.

    py -3 tests/test_lifter.py

Each script reads its data in one of the ways the lifter follows (a path built from strings,
read options that change nothing, parse_dates and index_col, a reading function, main()). The
lifted program must be what the script does, and the Python statements the lifter recorded
(Lifted.source_lines) must give the same table as the lifted PrepLang statements would.
"""
import sys
import tempfile
from pathlib import Path

import pandas as pd

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "frontend"))
from pandas2prep import lift  # noqa: E402

failures = 0


def check(passed, name):
    global failures
    print(("  PASS  " if passed else "  FAIL  ") + name)
    failures += not passed


def lifted(source):
    result = lift(source)
    return [line for line in result.prep if not line.startswith("SAVE")], result


def recorded_table(result, variable, folder):
    """Runs the recorded statements in `folder`; the DataFrame `variable` they leave."""
    namespace = {"pd": pd}
    import os
    here = os.getcwd()
    os.chdir(folder)
    try:
        exec("\n".join(result.source_lines), namespace)
    finally:
        os.chdir(here)
    return namespace[variable]


def main():
    with tempfile.TemporaryDirectory() as folder:
        Path(folder, "data").mkdir()
        Path(folder, "data", "hour.csv").write_text(
            "instant,dteday,cnt\n1,2011-01-01,16\n2,2011-01-01,\n3,2011-01-02,0\n4,2011-01-02,13\n", encoding="utf-8")

        prep, _ = lifted('import os\nimport pandas as pd\nDATA = "data"\n'
                         'df = pd.read_csv(os.path.join(DATA, "hour.csv"), sep=",", low_memory=False)\n'
                         'df = df.dropna(subset=["cnt"])\n')
        check(prep == ['LOAD "data/hour.csv" AS df;', "DROP_NULL df [cnt];"],
              "os.path.join of a module-level string, and options that change nothing")

        prep, _ = lifted('import pandas as pd\nfrom pathlib import Path\nROOT = Path("data")\n'
                         'df = pd.read_csv(ROOT / "hour.csv")\ndf = df[df.cnt > 0]\n')
        check(prep[0] == 'LOAD "data/hour.csv" AS df;', "pathlib's / on a known path")

        prep, _ = lifted('import pandas as pd\nname = "hour"\ndf = pd.read_csv(f"data/{name}.csv")\n'
                         'df = df.dropna()\n')
        check(prep[0] == 'LOAD "data/hour.csv" AS df;', "an f-string of known strings")

        prep, _ = lifted('import pandas as pd\nimport sys\ndf = pd.read_csv(sys.argv[1])\ndf = df.dropna()\n')
        check(prep == [], "a path known only when the script runs is not followed")

        prep, _ = lifted('import pandas as pd\ndf = pd.read_csv("data/hour.csv", sep=";")\ndf = df.dropna()\n')
        check(prep == [], "a read option that changes the table is not followed")

        source = ('import pandas as pd\nDATA = "data/"\n'
                  'def load_data(path):\n'
                  '    df = pd.read_csv(path, index_col="instant", parse_dates=["dteday"])\n'
                  '    df = df.dropna(subset=["cnt"])\n'
                  '    return df\n'
                  'bikes = load_data(DATA + "hour.csv")\n'
                  'bikes = bikes[bikes.cnt > 0]\n')
        prep, result = lifted(source)
        check(prep == ['LOAD "data/hour.csv" AS df;', "PARSE_DATE df.dteday;", "DROP df [instant];",
                       "DROP_NULL df [cnt];", "FILTER df WHERE cnt > 0;"],
              "a function that reads and cleans, with index_col and parse_dates")
        table = recorded_table(result, "bikes", folder)
        check(list(table.columns) == ["dteday", "cnt"] and len(table) == 2,
              "the recorded statements give the table the lifted program writes")

        prep, _ = lifted('import pandas as pd\n'
                         'def main():\n'
                         '    try:\n'
                         '        df = pd.read_csv("data/hour.csv")\n'
                         '    except FileNotFoundError:\n'
                         '        raise\n'
                         '    with open("log.txt", "w") as log:\n'
                         '        df = df.dropna()\n'
                         '    df.to_csv("clean.csv", index=False)\n'
                         'if __name__ == "__main__":\n'
                         '    main()\n')
        check(prep == ['LOAD "data/hour.csv" AS df;', "DROP_NULL df;"], "main(), try and with")

        first = lift('import pandas as pd\nDATA = "data/"\ndf = pd.read_csv(DATA + "hour.csv")\n'
                     'df = df.dropna()\n', derived=False)
        check(not first.frames, "the first lifter version does not follow built paths")
    print("All lifter tests passed" if failures == 0 else f"{failures} lifter test(s) FAILED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
