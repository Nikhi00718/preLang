// The compiler's --json report (LANGUAGE.md section 11) as editor diagnostics. Kept apart from
// extension.js so it can be tested without VS Code (test/diagnostics.test.js).
"use strict";

// One entry per diagnostic: 0-based line and column, "error" or "warning", and the text shown,
// with the notes and the hint on lines of their own.
function toDiagnostics(report) {
    return report.diagnostics.map((d) => {
        const lines = [`${d.message} [${d.phase}]`];
        for (const note of d.notes) {
            lines.push(`note: ${note}`);
        }
        if (d.hint) {
            lines.push(`hint: ${d.hint}`);
        }
        return {
            line: Math.max(d.line - 1, 0),
            column: Math.max(d.column - 1, 0),
            severity: d.severity === "error" ? "error" : "warning",
            message: lines.join("\n"),
        };
    });
}

// The report in the compiler's output, or null if it printed none (it could not start, or the
// file could not be opened); `stderr` then says why.
function parseReport(stdout) {
    try {
        const report = JSON.parse(stdout);
        return Array.isArray(report.diagnostics) ? report : null;
    } catch {
        return null;
    }
}

module.exports = { toDiagnostics, parseReport };
