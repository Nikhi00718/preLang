# PrepLang for VS Code

Syntax highlighting for `.prep` files, and the PrepLang compiler's errors and warnings in the editor:
each time a `.prep` file is opened or saved, the extension runs `preplang <file> --json` in the
file's folder (so `LOAD` paths resolve as they do for the compiler) and shows every diagnostic, with
its hint, at its line and column. Nothing is written: `--json` only analyses.

The compiler reads the program's data files at compile time, so the file on disk is checked: save
to see the result of a change.

## Setup

1. Install the compiler: `pip install "preplang @ git+https://github.com/Nikhi00718/preLang"`, or a
   build of this repository.
2. Install the extension: `npx @vscode/vsce package` in this folder, then
   `code --install-extension preplang-2.0.0.vsix`.
3. If `preplang` is not on PATH, set **PrepLang: Compiler Path** (`preplang.compilerPath`) to the
   executable, for example `build/Release/preplang.exe`.

## Development

`diagnostics.js` turns the `--json` report into diagnostics and is tested without VS Code:

```bash
npm test
```

With `PREPLANG` set to a compiler, the test also checks the compiler's real report for
`examples/errors/semantic_error.prep`.
