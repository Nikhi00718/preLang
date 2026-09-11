"""
executor.py
===========

The execution engine.

Runs the generated Python/Pandas program and reports what happened. The
compiler has already proved that the program is well formed, so anything that
goes wrong here is a genuine *runtime* error -- a missing file, a permission
problem, a Pandas failure -- and is reported as such
(:class:`~preplang.errors.RuntimeError_`).

Execution notes
---------------
* The program runs in a fresh namespace containing only ``pandas``, so it
  cannot see or disturb the compiler's own state.
* The working directory is switched to the directory of the PrepLang program,
  so relative paths in LOAD/SAVE mean what the programmer wrote.
* Standard output is captured, so the contract messages printed by the
  generated code can be shown in the IDE.
* The resulting dataframes are kept so the IDE can preview the output.

This is deliberately *not* a sandbox against hostile code: the code being run
was produced by this compiler from a checked program, and PrepLang has no
statement that can emit arbitrary Python.
"""

from __future__ import annotations

import contextlib
import io
import os
import time
from dataclasses import dataclass, field
from pathlib import Path

from .errors import RuntimeError_


@dataclass
class ExecutionResult:
    """The outcome of running a generated program."""

    ok: bool
    stdout: str = ""
    error: RuntimeError_ | None = None
    seconds: float = 0.0
    output_files: list[str] = field(default_factory=list)
    #: dataset name -> (rows, columns) of the final dataframe
    frames: dict[str, tuple[int, int]] = field(default_factory=dict)
    #: dataset name -> a short text preview of the final dataframe
    previews: dict[str, str] = field(default_factory=dict)
    #: output path -> whether the file now exists on disk
    written: dict[str, bool] = field(default_factory=dict)

    def format(self) -> str:
        lines: list[str] = []
        if self.stdout.strip():
            lines.append(self.stdout.rstrip())
        if self.ok:
            lines.append("")
            lines.append(f"Execution finished in {self.seconds * 1000:.1f} ms")
            for name, (rows, columns) in self.frames.items():
                lines.append(f"  {name}: {rows} rows x {columns} columns")
            for path in self.output_files:
                state = "written" if self.written.get(path) else "NOT FOUND"
                lines.append(f"  output file {path}: {state}")
        elif self.error is not None:
            lines.append("")
            lines.append(self.error.format())
        return "\n".join(lines)


def execute(
    code: str,
    base_dir: str | Path | None = None,
    preview_rows: int = 8,
    output_files: list[str] | None = None,
) -> ExecutionResult:
    """Run *code* with ``base_dir`` as the working directory.

    *output_files* are the paths the generated program is expected to write;
    they are checked afterwards so the caller can report what was produced.
    """
    try:
        import pandas as pd  # noqa: F401
    except ImportError as exc:  # pragma: no cover - environment dependent
        return ExecutionResult(
            ok=False,
            error=RuntimeError_(
                problem="Pandas is required to execute the generated code.",
                found=str(exc),
                hint="Install it with:  pip install pandas",
            ),
        )

    directory = Path(base_dir) if base_dir else Path.cwd()
    namespace: dict[str, object] = {"__name__": "__preplang_generated__"}
    buffer = io.StringIO()
    previous_cwd = Path.cwd()

    start = time.perf_counter()
    try:
        os.chdir(directory)
        with contextlib.redirect_stdout(buffer):
            exec(compile(code, "<preplang generated>", "exec"), namespace)  # noqa: S102
    except FileNotFoundError as exc:
        return _failure(
            buffer,
            start,
            RuntimeError_(
                problem="The generated program could not open a file.",
                found=str(exc),
                hint="Check the paths used in the LOAD and SAVE statements.",
            ),
        )
    except Exception as exc:  # noqa: BLE001 - any runtime failure is reported
        return _failure(
            buffer,
            start,
            RuntimeError_(
                problem=f"The generated program raised {type(exc).__name__}.",
                found=str(exc),
                hint="This is a runtime failure, not a compile-time one; the "
                "program itself was accepted by every compiler phase.",
            ),
        )
    finally:
        os.chdir(previous_cwd)

    seconds = time.perf_counter() - start
    result = ExecutionResult(ok=True, stdout=buffer.getvalue(), seconds=seconds)

    import pandas as pd

    for name, value in namespace.items():
        if name.startswith("_") or not isinstance(value, pd.DataFrame):
            continue
        result.frames[name] = (len(value), len(value.columns))
        result.previews[name] = value.head(preview_rows).to_string(index=False)

    result.output_files = list(output_files or [])
    result.written = {
        path: (directory / path).exists() for path in result.output_files
    }
    return result


def _failure(buffer: io.StringIO, start: float, error: RuntimeError_) -> ExecutionResult:
    return ExecutionResult(
        ok=False,
        stdout=buffer.getvalue(),
        error=error,
        seconds=time.perf_counter() - start,
    )
