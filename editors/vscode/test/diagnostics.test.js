// node --test test/diagnostics.test.js   (no VS Code needed). With PREPLANG set to a compiler, it also checks the
// mapping on the compiler's real --json output for examples/errors/semantic_error.prep.
"use strict";

const assert = require("node:assert");
const childProcess = require("node:child_process");
const path = require("node:path");
const test = require("node:test");
const { toDiagnostics, parseReport } = require("../diagnostics");

test("diagnostics are 0-based, with notes and the hint on their own lines", () => {
    const report = {
        file: "a.prep", errors: 1, warnings: 1, diagnostics: [
            { severity: "error", phase: "semantic", line: 8, column: 11, message: "column 'mark' does not exist",
              hint: "did you mean 'marks'?", notes: [] },
            { severity: "warning", phase: "semantic", line: 3, column: 1, message: "loaded but never saved",
              hint: "", notes: ["t.a", "t.b"] },
        ],
    };
    assert.deepStrictEqual(toDiagnostics(report), [
        { line: 7, column: 10, severity: "error",
          message: "column 'mark' does not exist [semantic]\nhint: did you mean 'marks'?" },
        { line: 2, column: 0, severity: "warning", message: "loaded but never saved [semantic]\nnote: t.a\nnote: t.b" },
    ]);
});

test("output that is not a report gives null", () => {
    assert.strictEqual(parseReport(""), null);
    assert.strictEqual(parseReport("error: cannot open 'x.prep'"), null);
    assert.strictEqual(parseReport("{\"file\": \"a.prep\"}"), null);
    assert.notStrictEqual(parseReport("{\"file\": \"a.prep\", \"errors\": 0, \"warnings\": 0, \"diagnostics\": []}"), null);
});

test("the compiler's real report maps line by line", { skip: !process.env.PREPLANG && "set PREPLANG to a compiler" }, () => {
    const examples = path.resolve(__dirname, "../../../examples/errors");
    const run = childProcess.spawnSync(process.env.PREPLANG, ["semantic_error.prep", "--json"], { cwd: examples, encoding: "utf-8" });
    const report = parseReport(run.stdout);
    assert.ok(report, run.stderr);
    assert.strictEqual(run.status, 1);
    const diagnostics = toDiagnostics(report);
    assert.strictEqual(diagnostics.length, report.errors + report.warnings);
    assert.ok(diagnostics.some((d) => d.line === 7 && d.severity === "error" && d.message.includes("did you mean 'marks'?")));
});
