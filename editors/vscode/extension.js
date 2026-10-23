// PrepLang for VS Code: compiles a .prep file with `preplang --json` when it is opened or saved,
// and shows the diagnostics in the editor. The compiler reads the program's data files, so the
// file on disk is compiled (unsaved changes are not seen until the file is saved).
"use strict";

const childProcess = require("child_process");
const path = require("path");
const vscode = require("vscode");
const { toDiagnostics, parseReport } = require("./diagnostics");

function activate(context) {
    const collection = vscode.languages.createDiagnosticCollection("preplang");
    const output = vscode.window.createOutputChannel("PrepLang");
    context.subscriptions.push(collection, output);

    const check = (document) => {
        if (document.languageId !== "preplang" || document.uri.scheme !== "file") {
            return;
        }
        const compiler = vscode.workspace.getConfiguration("preplang").get("compilerPath", "preplang");
        const file = document.uri.fsPath;
        childProcess.execFile(compiler, [path.basename(file), "--json"], { cwd: path.dirname(file) },
            (error, stdout, stderr) => {
                const report = parseReport(stdout);
                if (!report) {
                    collection.delete(document.uri);
                    output.appendLine(`preplang could not check ${file}: ${stderr || (error && error.message)}`);
                    return;
                }
                collection.set(document.uri, toDiagnostics(report).map((d) => {
                    const start = new vscode.Position(d.line, d.column);
                    const range = document.getWordRangeAtPosition(start) || new vscode.Range(start, start.translate(0, 1));
                    return new vscode.Diagnostic(range, d.message, d.severity === "error"
                        ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning);
                }));
            });
    };

    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument(check),
        vscode.workspace.onDidSaveTextDocument(check),
        vscode.workspace.onDidCloseTextDocument((document) => collection.delete(document.uri)));
    vscode.workspace.textDocuments.forEach(check);
}

function deactivate() {}

module.exports = { activate, deactivate };
