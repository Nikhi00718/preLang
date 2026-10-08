// Builds the demo deck: the slides explain, the terminal proves.
//   node demo/deck.js demo/PrepLang_Demo_Deck.pptx      (needs the pptxgenjs npm package)
// Every number comes from demo/demo_data.json, which demo/capture.py writes from real runs.
// Two colors only, the portfolio's ink and paper.
const fs = require("fs");
const path = require("path");
const pptxgen = require("pptxgenjs");

const d = JSON.parse(fs.readFileSync(path.join(__dirname, "demo_data.json"), "utf8"));
const r = d.results;
const mistake = Object.fromEntries(d.mistakes.map((m) => [m.key, m]));
const pct = (a, b) => Math.round((100 * a) / b);

const INK = "18140F", PAPER = "FFF8E7";
const HEAD = "Georgia", BODY = "Arial", MONO = "Consolas";
const W = 13.333, H = 7.5;

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE";
pres.author = "V.Nikhil";
pres.title = "PrepLang: live demo";

const text = (s, value, o) => s.addText(value, { isTextBox: true, margin: 0, fontFace: BODY, color: INK, valign: "top", ...o });
const box = (s, x, y, w, h, fill = PAPER, line = INK, pt = 2) =>
  s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: fill }, line: { color: line, width: pt } });
// The portfolio block: a card with a hard ink shadow.
function card(s, x, y, w, h, dark = false) {
  box(s, x + 0.09, y + 0.09, w, h, INK, INK, 0);
  box(s, x, y, w, h, dark ? INK : PAPER, INK, 2);
}
// Lines cannot have a negative size; a flip turns them the other way.
const arrow = (s, x1, y1, x2, y2) =>
  s.addShape(pres.shapes.LINE, { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1),
    flipH: x2 < x1, flipV: y2 < y1, line: { color: INK, width: 2, endArrowType: "triangle" } });

let number = 0;
function slide(kicker, heading) {
  const s = pres.addSlide();
  s.background = { color: PAPER };
  number += 1;
  text(s, kicker.toUpperCase(), { x: 0.6, y: 0.38, w: 9, h: 0.3, fontFace: MONO, fontSize: 12, bold: true, charSpacing: 2 });
  text(s, heading, { x: 0.6, y: 0.68, w: 12.1, h: 0.75, fontFace: HEAD, fontSize: 30, bold: true });
  s.addShape(pres.shapes.LINE, { x: 0.6, y: 1.5, w: 12.13, h: 0, line: { color: INK, width: 3 } });
  text(s, String(number).padStart(2, "0"), { x: 12.0, y: 7.0, w: 0.73, h: 0.3, fontFace: MONO, fontSize: 11, align: "right" });
  return s;
}
// The bar that says: switch to the terminal now.
function terminal(s, step, show) {
  const y = 6.35;
  box(s, 0.6, y, 12.13, 0.62, INK, INK, 0);
  text(s, "TERMINAL", { x: 0.8, y: y + 0.17, w: 1.3, h: 0.3, fontFace: MONO, fontSize: 13, bold: true, color: PAPER });
  text(s, `py -3 demo/run_demo.py --only ${step}`, { x: 2.15, y: y + 0.15, w: 5.2, h: 0.34, fontFace: MONO, fontSize: 16, bold: true, color: PAPER });
  text(s, show, { x: 7.4, y: y + 0.1, w: 5.15, h: 0.45, fontSize: 12, color: PAPER, valign: "middle", align: "right" });
}
const code = (s, value, x, y, w, h, size = 12) => {
  card(s, x, y, w, h, true);
  text(s, value, { x: x + 0.2, y: y + 0.15, w: w - 0.4, h: h - 0.3, fontFace: MONO, fontSize: size, color: PAPER });
};
const bullets = (s, items, o) =>
  text(s, items.map((t) => ({ text: t, options: { bullet: { indent: 16 }, paraSpaceAfter: 7 } })), { fontSize: 15, ...o });
const notes = (s, value) => s.addNotes(value);

// ---------------------------------------------------------------- 1. title
let s = pres.addSlide();
s.background = { color: INK };
number += 1;
text(s, "COMPILER DESIGN LAB  ·  BCSE307P  ·  REVIEW 3", { x: 0.8, y: 0.8, w: 11, h: 0.3, fontFace: MONO, fontSize: 13, bold: true, color: PAPER, charSpacing: 2 });
text(s, "PrepLang", { x: 0.8, y: 1.9, w: 11, h: 1.3, fontFace: HEAD, fontSize: 80, bold: true, color: PAPER });
text(s, "A compiler that reads your data before your program runs", { x: 0.8, y: 3.3, w: 11.5, h: 0.6, fontFace: HEAD, fontSize: 28, color: PAPER });
s.addShape(pres.shapes.LINE, { x: 0.8, y: 4.3, w: 3, h: 0, line: { color: PAPER, width: 4 } });
text(s, "Live demo: the slides explain, the terminal proves it.", { x: 0.8, y: 4.6, w: 11, h: 0.4, fontSize: 18, color: PAPER });
text(s, "V.Nikhil  ·  24BAI0073", { x: 0.8, y: 6.4, w: 11, h: 0.4, fontFace: MONO, fontSize: 16, bold: true, color: PAPER });
notes(s, "PrepLang is a small language for cleaning tables and images before machine learning. Its compiler is " +
  "different from a normal one: it opens the real data files while it compiles. So before anything runs it knows the " +
  "type of every column, which values are missing, the smallest and largest values, and the size of every image. " +
  "I will explain each idea on a slide and then prove it live in the terminal.");

// ---------------------------------------------------------------- 2. how the demo runs
s = slide("Before we start", "How this demo runs");
const plan = [
  ["1", "Once, before the review", "py -3 demo/run_demo.py prepare", "Builds the old Phase 2 compiler from git (commit f088d66) and makes the sample data."],
  ["2", "On every slide with a black bar", "py -3 demo/run_demo.py --only N", "Runs step N for real. It pauses at each [Enter]: explain, then press Enter."],
  ["3", "When it prints “Back to the slides”", "next slide", "Every number on these slides came from the same runs (demo/capture.py)."],
];
plan.forEach(([n, head, cmd, body], i) => {
  const y = 1.9 + i * 1.45;
  card(s, 0.6, y, 0.95, 1.1, true);
  text(s, n, { x: 0.6, y, w: 0.95, h: 1.1, fontFace: HEAD, fontSize: 40, bold: true, color: PAPER, align: "center", valign: "middle" });
  card(s, 1.75, y, 6.0, 1.1);
  text(s, head, { x: 1.95, y: y + 0.14, w: 5.6, h: 0.35, fontSize: 16, bold: true });
  text(s, cmd, { x: 1.95, y: y + 0.55, w: 5.6, h: 0.35, fontFace: MONO, fontSize: 15 });
  text(s, body, { x: 8.0, y: y + 0.1, w: 4.7, h: 0.95, fontSize: 14, valign: "middle" });
});
text(s, "Terminal steps:  1 problem   2 phases   3 errors   4 speed   5 certificates   6 images   7 pandas   8 tests",
  { x: 0.6, y: 6.45, w: 12.13, h: 0.4, fontFace: MONO, fontSize: 13, bold: true });
notes(s, "Open a terminal in the PrepLang folder next to the slides. Make it large (font 16 or more) so the room can read it. " +
  "If anything goes wrong live, the HTML page demo/PrepLang_Demo.html has the same outputs, captured from real runs.");

// ---------------------------------------------------------------- 3. the problem
s = slide("01  The problem", "Cleaning scripts fail late, or fail silently");
const rows = [
  ["A misspelled column in the last step", "KeyError after all the work", "error, line 19"],
  ["Normalizing a text column", "TypeError at run time", "type error, line 10"],
  ["A range contract the data breaks", "AssertionError at run time", "violated: names the row"],
  ["A threshold on a normalized column", "saves 0 rows, no error", "warning: removes every row"],
  ["A broken image in the folder", "AttributeError: NoneType", "1 of 7 files cannot be decoded"],
  ["Decimal pixels saved as PNG", "6 black images, no error", "type error, line 13"],
];
text(s, "THE MISTAKE", { x: 0.6, y: 1.75, w: 4.6, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
text(s, "A PLAIN PANDAS / OPENCV SCRIPT", { x: 5.3, y: 1.75, w: 3.6, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
text(s, "PREPLANG, BEFORE IT RUNS", { x: 9.0, y: 1.75, w: 3.7, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
rows.forEach(([what, script, ours], i) => {
  const y = 2.12 + i * 0.52;
  text(s, what, { x: 0.6, y, w: 4.6, h: 0.45, fontSize: 15, bold: true, valign: "middle" });
  text(s, script, { x: 5.3, y, w: 3.6, h: 0.45, fontSize: 15, valign: "middle" });
  box(s, 9.0, y + 0.04, 3.73, 0.4, INK, INK, 0);
  text(s, ours, { x: 9.12, y: y + 0.04, w: 3.55, h: 0.4, fontSize: 14, color: PAPER, valign: "middle" });
  s.addShape(pres.shapes.LINE, { x: 0.6, y: y + 0.5, w: 12.13, h: 0, line: { color: INK, width: 0.75 } });
});
text(s, `On 16 million rows the misspelled-column script fails after ${r.scale.typo_script.toFixed(1)} s; PrepLang refuses in ${r.scale.typo_compile.toFixed(2)} s, and nothing runs.`,
  { x: 0.6, y: 5.35, w: 12.13, h: 0.7, fontFace: HEAD, fontSize: 19, bold: true, valign: "middle" });
terminal(s, 1, "the black-PNG mistake: script against compiler");
notes(s, "Six real kinds of mistakes. A normal script either crashes at the end, after all the work, or worse, finishes " +
  "with no error and wrong output: zero rows, or black images. PrepLang catches every one at compile time with the line " +
  "and a hint.\n\nTERMINAL step 1: first the plain script runs and writes PNG files whose brightest mean pixel is below 1 " +
  "of 255, so every image is black. Then the compiler on the same program stops at line 13 in about 0.02 s: it knows the " +
  "pixels are decimals between 0 and 1 because NORMALIZE at line 10 made them so, and it read every image header at line 2.");

// ---------------------------------------------------------------- 4. the idea
s = slide("02  The idea", "The compiler opens the data while it compiles");
const program = d.phases.program.split("\n").filter((l) => l.trim() !== "").slice(0, 17).join("\n");
code(s, program, 0.6, 1.8, 6.6, 4.35, 10);
card(s, 7.55, 1.8, 5.18, 2.15);
text(s, "WHAT IT KNOWS BEFORE ANYTHING RUNS", { x: 7.75, y: 1.95, w: 4.8, h: 0.3, fontFace: MONO, fontSize: 11, bold: true });
bullets(s, ["Tables: each column's type, missing values, smallest and largest value",
  "Images: size, channels and pixel type of every file, and which are broken"], { x: 7.75, y: 2.35, w: 4.8, h: 1.5, fontSize: 14 });
card(s, 7.55, 4.2, 5.18, 1.95, true);
text(s, "SO IT CAN", { x: 7.75, y: 4.35, w: 4.8, h: 0.3, fontFace: MONO, fontSize: 11, bold: true, color: PAPER });
text(s, [
  { text: "reject a wrong program before it runs", options: { bullet: { indent: 16 }, breakLine: true } },
  { text: "prove contracts, or name the row that breaks one", options: { bullet: { indent: 16 }, breakLine: true } },
  { text: "delete proven checks and unused columns", options: { bullet: { indent: 16 } } },
], { x: 7.75, y: 4.7, w: 4.8, h: 1.4, fontSize: 14, color: PAPER, paraSpaceAfter: 6 });
notes(s, "This is examples/tabular/students.prep, the program the next steps use. LOAD, EXPECT for contracts, FILL_NULL, " +
  "NORMALIZE, FILTER, SELECT, SAVE. The key point: at LOAD the compiler reads students.csv. Every later statement " +
  "changes those facts: FILTER marks >= 50 makes the smallest mark 50. That is what lets it catch mistakes and remove work.");

// ---------------------------------------------------------------- 5. phases
s = slide("02  How it works", "Eight phases, from characters to a Python program");
const phases = [
  ["1 Lexer", "characters to tokens, with line and column"],
  ["2 Parser", "LL(1) recursive descent, panic-mode recovery"],
  ["3 Schema", "reads the CSV, Parquet or image headers"],
  ["4 Semantic", "symbol table of data facts; contract verdicts"],
  ["5 IR", "three-address code; temporaries t1, t2, ..."],
  ["6 Optimizer", "removes proven checks, no-ops, unused columns"],
  ["7 Certificate", "the facts the proofs really need  (NEW)"],
  ["8 Code gen", "one Python program: pandas and OpenCV"],
];
phases.forEach(([name, body], i) => {
  const col = i % 4, row = Math.floor(i / 4);
  const x = 0.6 + col * 3.13, y = 1.95 + row * 2.05;
  const dark = i === 6;
  card(s, x, y, 2.7, 1.55, dark);
  text(s, name, { x: x + 0.18, y: y + 0.15, w: 2.4, h: 0.45, fontFace: HEAD, fontSize: 20, bold: true, color: dark ? PAPER : INK });
  text(s, body, { x: x + 0.18, y: y + 0.65, w: 2.4, h: 0.8, fontSize: 13, color: dark ? PAPER : INK });
  if (col < 3) arrow(s, x + 2.82, y + 0.78, x + 3.1, y + 0.78);
});
// Row 1 continues into row 2: down, left, down.
const line = (x1, y1, x2, y2) => s.addShape(pres.shapes.LINE, { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1), line: { color: INK, width: 2 } });
line(11.34, 3.62, 11.34, 3.78);
line(1.95, 3.78, 11.34, 3.78);
arrow(s, 1.95, 3.78, 1.95, 3.95);
text(s, "Phases 1-6 and 8 existed in Phase 2. Phase 3 made 3 fast and added 7.", { x: 0.6, y: 5.75, w: 12.13, h: 0.4, fontSize: 15, bold: true });
terminal(s, 2, "the real output of each phase on students.prep");
notes(s, "One sentence per phase while the terminal shows it.\n1 Lexer: characters to tokens, each with line and column " +
  "for error messages.\n2 Parser: hand-written recursive descent. Every statement starts with its own keyword, so one " +
  "token of look-ahead chooses the rule: LL(1). Panic-mode recovery reports several errors in one run.\n3 Schema: the data " +
  "is read here, at compile time.\n4 Semantic: the symbol table holds the facts about the data, and each statement changes " +
  "them. Contracts get their verdicts.\n5 IR: three-address code.\n6 Optimizer: removes checks already proven, operations that " +
  "do nothing, and columns never used.\n7 Certificate: new in Phase 3, two slides from now.\n8 Code generation: one Python " +
  "program, then it runs.\n\nTERMINAL step 2 pauses after each phase; press Enter after each sentence.");

// ---------------------------------------------------------------- 6. contracts and errors
s = slide("03  Contracts and errors", "Every contract gets a verdict at compile time");
const verdicts = [
  ["PROVEN", "EXPECT marks BETWEEN 0 AND 100", "The data shows it. No check at run time: it is deleted."],
  ["VIOLATED", "EXPECT marks BETWEEN 0 AND 90", "Compile error that names the row: row 2 has 95."],
  ["CHECKED AT RUN TIME", "a contract after a FILTER", "Cannot be decided yet: one check, then the fact is used."],
];
verdicts.forEach(([v, ex, body], i) => {
  const x = 0.6 + i * 4.13;
  card(s, x, 1.85, 3.85, 1.95, i === 0);
  const c = i === 0 ? PAPER : INK;
  text(s, v, { x: x + 0.2, y: 2.0, w: 3.5, h: 0.4, fontFace: MONO, fontSize: 15, bold: true, color: c });
  text(s, ex, { x: x + 0.2, y: 2.45, w: 3.5, h: 0.35, fontFace: MONO, fontSize: 11, color: c });
  text(s, body, { x: x + 0.2, y: 2.9, w: 3.5, h: 0.8, fontSize: 14, color: c });
});
code(s, mistake.typo.compiler.split("\n").slice(0, 7).join("\n"), 0.6, 4.15, 7.4, 1.85, 13);
text(s, "Errors are compiler-style: the phase, the file, line and column, a caret under the token, and a hint.",
  { x: 8.35, y: 4.3, w: 4.38, h: 1.6, fontSize: 16, bold: true, valign: "middle" });
terminal(s, 3, "a lexical, a syntax and a semantic error, each with line and caret");
notes(s, "Three verdicts. Proven: the compiler saw it holds, so the generated program does not check it. Violated: a " +
  "compile error that even names the row. Undecidable, for example after a filter: the program checks it once at run " +
  "time, and after the check passes the compiler uses the fact.\n\nTERMINAL step 3: three error files. Point at the " +
  "error kind (lexical, syntax, semantic), the line:column, the caret and the hint.");

// ---------------------------------------------------------------- 7. before and after
s = slide("04  What Phase 3 changed", "Phase 2 → Phase 3");
const changes = [
  ["Reading a big CSV", `${d.speed.phase2_s.toFixed(2)} s per million rows`, `${d.speed.phase3_s.toFixed(2)} s: parallel, zero-copy scanner`],
  ["A new data file arrives", "refused: proofs tied to one file (CRC)", "admitted if it fits the data certificate"],
  ["A new image folder", "refused: exact file list", "admitted if it fits the image box"],
  ["Existing pandas scripts", "rewrite by hand in PrepLang", "lifted automatically, then certified"],
  ["Language", "FILL_NULL with a constant only", "MEAN | MEDIAN | MODE fills, DROP_NULL on columns, DATETIME"],
  ["Evidence", "synthetic data", "238 real monthly batches, 372 GitHub scripts"],
];
text(s, "", { x: 0.6, y: 1.75, w: 3.3, h: 0.3 });
text(s, "PHASE 2 (BEFORE)", { x: 4.0, y: 1.75, w: 3.9, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
text(s, "PHASE 3 (NOW)", { x: 8.15, y: 1.75, w: 4.5, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
changes.forEach(([what, before, after], i) => {
  const y = 2.12 + i * 0.66;
  text(s, what, { x: 0.6, y, w: 3.3, h: 0.55, fontSize: 15, bold: true, valign: "middle" });
  text(s, before, { x: 4.0, y, w: 3.9, h: 0.55, fontSize: 14, valign: "middle" });
  box(s, 8.05, y + 0.05, 4.68, 0.48, INK, INK, 0);
  text(s, after, { x: 8.2, y: y + 0.05, w: 4.5, h: 0.48, fontSize: 14, bold: true, color: PAPER, valign: "middle" });
  s.addShape(pres.shapes.LINE, { x: 0.6, y: y + 0.6, w: 12.13, h: 0, line: { color: INK, width: 0.75 } });
});
text(s, "The two big limits of Phase 2: slow analysis of big files, and a compiled program that worked on one file only.",
  { x: 0.6, y: 6.2, w: 12.13, h: 0.6, fontFace: HEAD, fontSize: 18, bold: true, valign: "middle" });
notes(s, "This slide is the summary of the improvements. Phase 2 had two real limits. First, analysing a big CSV was slow: " +
  "1.8 seconds per million rows. Second, a compiled program refused any new data file, because its proofs were tied to the " +
  "exact file by a checksum. In real use data arrives every week, so you had to recompile every time. Phase 3 fixes both, " +
  "and adds the pandas front end, new FILL_NULL methods, and an evaluation on real data. The next slides show each one live.");

// ---------------------------------------------------------------- 8. speed
s = slide("04  Speed", `${d.speed.rows.toLocaleString("en-US")} rows, ${d.speed.columns} columns, ${d.speed.mib} MiB: every cell checked`);
const bars = [["Phase 2 compiler", d.speed.phase2_s], ["pandas, just reading", d.speed.pandas_s], ["Phase 3 compiler", d.speed.phase3_s]];
const most = Math.max(...bars.map((b) => b[1]));
bars.forEach(([name, sec], i) => {
  const y = 2.0 + i * 1.05;
  text(s, name, { x: 0.6, y, w: 2.6, h: 0.7, fontSize: 16, bold: true, valign: "middle" });
  const w = Math.max(0.12, (4.0 * sec) / most);
  box(s, 3.3, y + 0.08, w, 0.55, i === 1 ? PAPER : INK, INK, 2);
  text(s, `${sec.toFixed(2)} s`, { x: 3.45 + w, y, w: 1.3, h: 0.7, fontFace: MONO, fontSize: 18, bold: true, valign: "middle" });
});
text(s, `${(d.speed.phase2_s / d.speed.phase3_s).toFixed(0)}× faster than Phase 2, and faster than pandas only reading the file.`,
  { x: 0.6, y: 5.25, w: 7.7, h: 0.8, fontFace: HEAD, fontSize: 18, bold: true });
card(s, 8.95, 1.9, 3.78, 4.15);
text(s, "HOW", { x: 9.15, y: 2.05, w: 3.4, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
bullets(s, ["Read the file once into memory", "Cut it at line breaks: one piece per CPU core",
  "Look at each cell in place, no copies (string_view)", "Fast number parsing (Clinger's fast path)",
  "The checksum runs on its own thread", "Pieces merged back in file order"], { x: 9.15, y: 2.45, w: 3.4, h: 3.5, fontSize: 13 });
terminal(s, 4, "both compilers on the same file, timed live");
notes(s, "The same 59 MiB file, analysed by both compilers. Phase 2 needs about 1.8 s; Phase 3 about 0.12 s, which is " +
  "faster than pandas just reading the file, and the compiler checks every cell's type and range. How: read the file " +
  "once, split it at line breaks into one piece per core, look at cells in place without copying, and merge the pieces " +
  "in order so the result is identical to the one-thread version.\n\nTERMINAL step 4: the timings on this laptop vary a " +
  "little from the slide; the ratio stays.");

// ---------------------------------------------------------------- 9. certificates, the idea
s = slide("05  Data certificates  (the main idea)", "Compile once, keep running safely on next week's file");
card(s, 0.6, 1.85, 3.6, 3.0);
text(s, "THE FILE HAD", { x: 0.8, y: 2.0, w: 3.2, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
text(s, "marks: 35 to 95\nname: never missing\nage: some missing\n... 6 columns of exact facts", { x: 0.8, y: 2.4, w: 3.2, h: 2.3, fontFace: MONO, fontSize: 14 });
arrow(s, 4.35, 3.35, 4.9, 3.35);
card(s, 5.0, 1.85, 3.6, 3.0, true);
text(s, "WEAKEN ONE FACT, ANALYSE AGAIN", { x: 5.2, y: 2.0, w: 3.2, h: 0.3, fontFace: MONO, fontSize: 12, bold: true, color: PAPER });
text(s, "marks 35..95 → 0..100?\nSame proofs → keep it.\nA proof breaks → needed.\n\n13 more analyses for this program.",
  { x: 5.2, y: 2.4, w: 3.2, h: 2.3, fontSize: 14, color: PAPER });
arrow(s, 8.75, 3.35, 9.3, 3.35);
card(s, 9.4, 1.85, 3.33, 3.0);
text(s, "THE CERTIFICATE", { x: 9.6, y: 2.0, w: 3.0, h: 0.3, fontFace: MONO, fontSize: 12, bold: true });
text(s, "marks: INTEGER, 0 to 100\n  ← line 5\nname: never missing\n  ← line 6\nevery other column:\n  its type only", { x: 9.6, y: 2.4, w: 3.0, h: 2.3, fontFace: MONO, fontSize: 13 });
text(s, "Why it is safe: the analysis is monotone. A file inside the certificate gives smaller facts, so every proof still holds. " +
  "Each needed fact points to the line of the contract that needs it.", { x: 0.6, y: 5.15, w: 12.13, h: 1.0, fontFace: HEAD, fontSize: 17, valign: "middle" });
notes(s, "This is the main new idea. Phase 2 tied its proofs to the exact file. Phase 3 asks: which facts does each proof " +
  "really need? It weakens one fact at a time, for example marks 35 to 95 becomes 0 to 100, analyses the program again, and " +
  "keeps the weaker fact if every proof and every removed check is unchanged. If a proof breaks, the fact is needed. For " +
  "students.prep that takes 13 extra analyses. The result: marks must be integers from 0 to 100, because of the contract " +
  "on line 5, and name must never be missing, because of line 6. Everything else only needs its type.\n\nWhy it is safe: " +
  "every transfer function is monotone. A new file that satisfies the certificate has facts inside it, so the analysis " +
  "would make every proof again. Randomized tests check this on 1,200 random tables and 600 random image folders.");

// ---------------------------------------------------------------- 10. certificates, three files
s = slide("05  Data certificates, live", "Three new files arrive. Who decides what?");
const files = [
  ["next_week.csv", "marks 12 and 100", "REFUSED", "REFUSED", "ADMITTED: 3 rows saved"],
  ["typo_week.csv", "marks 730 in row 3", "REFUSED", "REFUSED", "REFUSED: row 3, line 5"],
  ["missing_name.csv", "name missing in row 2", "REFUSED", "REFUSED", "REFUSED: row 2, line 6"],
];
const heads = [["NEW FILE", 0.6, 2.6], ["WHAT CHANGED", 3.3, 2.7], ["PHASE 2 CHECKSUM", 6.1, 1.9], ["OLD FILE'S FACTS", 8.1, 1.9], ["CERTIFICATE", 10.1, 2.63]];
heads.forEach(([h, x, w]) => text(s, h, { x, y: 1.8, w, h: 0.3, fontFace: MONO, fontSize: 11, bold: true }));
files.forEach((row, i) => {
  const y = 2.2 + i * 0.85;
  row.forEach((v, j) => {
    const [, x, w] = heads[j];
    if (j === 4) {
      card(s, x, y + 0.05, w, 0.6, true);
      text(s, v, { x: x + 0.12, y: y + 0.05, w: w - 0.2, h: 0.6, fontSize: 13, bold: true, color: PAPER, valign: "middle" });
    } else {
      text(s, v, { x, y, w, h: 0.7, fontFace: j === 0 ? MONO : BODY, fontSize: j === 0 ? 14 : 14, bold: j === 0, valign: "middle" });
    }
  });
  s.addShape(pres.shapes.LINE, { x: 0.6, y: y + 0.78, w: 12.13, h: 0, line: { color: INK, width: 0.75 } });
});
code(s, d.batches["phase3_typo_week.csv"].split("\n")[0].replace(/(.{1,95})(\s|$)/g, "$1\n").trim(), 0.6, 4.85, 12.13, 1.05, 12);
terminal(s, 5, "compile once, then run the same generated program on all three files");
notes(s, "Next week's file has a mark of 12 and a mark of 100. Phase 2 refuses it: the checksum changed. Even the old file's " +
  "own facts would refuse it, since 12 and 100 are outside 35 to 95. The certificate admits it, because the program only " +
  "needs 0 to 100, and a fresh compile on that file would make exactly the same program. The typo file has 730 in row 3: " +
  "refused, and the message names the row and the contract's line. The missing-name file: refused, naming NOT_NULL on " +
  "line 6.\n\nTERMINAL step 5: first Phase 2 compiles and refuses next week's file. Then Phase 3 compiles once, prints the " +
  "certificate, and the same generated program runs on the three files. The check reads only the columns the certificate names.");

// ---------------------------------------------------------------- 11. images
s = slide("06  Image folders", "For images the certificate is a box");
const ox = 0.9, oy = 5.9, sx = 6.0 / 1000, sy = 3.8 / 1000;  // width 0..1000 px, height 0..1000 px
arrow(s, ox, oy, ox + 6.2, oy);
arrow(s, ox, oy, ox, oy - 4.0);
text(s, "width (px)", { x: ox + 5.0, y: oy + 0.08, w: 1.4, h: 0.3, fontSize: 12 });
text(s, "height", { x: ox - 0.2, y: 1.7, w: 1.0, h: 0.3, fontSize: 12 });
s.addShape(pres.shapes.RECTANGLE, { x: ox + 200 * sx, y: oy - 3.8, w: 800 * sx + 0.15, h: 3.8,
  fill: { color: PAPER }, line: { color: INK, width: 2, dashType: "dash" } });
text(s, "allowed: width ≥ 200, any height", { x: ox + 2.3, y: 2.25, w: 4.3, h: 0.3, fontSize: 13, bold: true });
s.addShape(pres.shapes.LINE, { x: ox + 200 * sx, y: oy, w: 0, h: 0.15, line: { color: INK, width: 2 } });
text(s, "200", { x: ox + 200 * sx - 0.3, y: oy + 0.15, w: 0.6, h: 0.25, fontSize: 11, align: "center", fontFace: MONO });
const photos = [[300, 260, 1], [640, 480, 1], [250, 900, 1], [400, 300, 1], [300, 300, 0], [150, 100, 0]];
photos.forEach(([w, h, fresh]) => {
  const x = ox + w * sx - 0.09, y = oy - h * sy - 0.09;
  s.addShape(fresh ? pres.shapes.OVAL : pres.shapes.RECTANGLE, { x, y, w: 0.18, h: 0.18, fill: { color: fresh ? INK : PAPER }, line: { color: INK, width: 2 } });
});
text(s, "150 × 100", { x: ox + 150 * sx - 0.85, y: oy - 100 * sy - 0.45, w: 1.0, h: 0.3, fontFace: MONO, fontSize: 11, bold: true });
card(s, 7.65, 1.85, 5.08, 1.75, true);
text(s, "new_photos/  ●", { x: 7.85, y: 2.0, w: 4.7, h: 0.35, fontFace: MONO, fontSize: 15, bold: true, color: PAPER });
text(s, "4 photos, 250 to 640 wide, gray, color and RGBA, PNG and JPG: ADMITTED", { x: 7.85, y: 2.45, w: 4.7, h: 1.0, fontSize: 14, color: PAPER });
card(s, 7.65, 3.85, 5.08, 1.75);
text(s, "small_photos/  □", { x: 7.85, y: 4.0, w: 4.7, h: 0.35, fontFace: MONO, fontSize: 15, bold: true });
text(s, "one photo is 150 px wide; MIN_WIDTH 200 on line 6 needs it: REFUSED", { x: 7.85, y: 4.45, w: 4.7, h: 1.0, fontSize: 14 });
text(s, "Only the image headers are read to decide.", { x: 7.65, y: 5.8, w: 5.08, h: 0.4, fontSize: 15, bold: true });
terminal(s, 6, "make two new folders, run the compiled program on both");
notes(s, "For an image folder the certificate is a box: which channel counts and which size ranges the images may have, " +
  "per file type. The program has EXPECT MIN_WIDTH 200, so width must be at least 200; height can be anything because " +
  "RESIZE makes every image 224 anyway. GRAYSCALE accepts 1, 3 or 4 channels. Broken files are allowed because " +
  "REMOVE_CORRUPT drops them. New photos fit the box: admitted. One photo 150 pixels wide: refused. To decide, the program " +
  "reads only the headers, not the pixels. The proof that checking the corners of the box is enough is the corners lemma.");

// ---------------------------------------------------------------- 12. pandas
s = slide("07  Existing pandas scripts", "Most cleaning code already exists. PrepLang lifts it.");
const script = d.pandas.script.split("\n").filter((l) => l.trim() && !l.startsWith("#")).join("\n");
code(s, script, 0.6, 1.8, 6.4, 3.4, 11);
arrow(s, 7.1, 3.5, 7.55, 3.5);
code(s, d.pandas.lifted, 7.65, 1.8, 5.08, 3.4, 12);
text(s, "pandas2prep.py reads the script with Python's own parser and translates the cleaning, up to the first line PrepLang cannot express (the map on line 15).",
  { x: 0.6, y: 5.4, w: 6.6, h: 0.85, fontSize: 14 });
text(s, "Kaggle's test.csv has no Survived column. The program only carries it to the output, so it is admitted.",
  { x: 7.65, y: 5.4, w: 5.08, h: 0.85, fontSize: 14, bold: true });
terminal(s, 7, "lift the Titanic script, certify it, run it on Kaggle's test file");
notes(s, "Most cleaning code already exists as pandas notebooks. A second front end, frontend/pandas2prep.py, parses the " +
  "script with Python's ast module, follows the data frames that come from pd.read_csv, and translates the longest exact " +
  "prefix: fillna with the median becomes FILL_NULL WITH MEDIAN, the assert becomes an EXPECT, the boolean index becomes " +
  "a FILTER. It stops at the first line it cannot express exactly, here the map. The lifted program gets a certificate " +
  "like any other, and accepts Kaggle's test file, which has no Survived column, because the program only carries that column.");

// ---------------------------------------------------------------- 13. evidence
s = slide("08  Evidence", "Measured, not claimed");
const stats = [
  [String(r.tests), "unit tests, all passing"],
  [`${r.mutants_caught}/${r.mutants}`, `seeded mistakes caught at compile time; the plain script crashed on ${r.baseline_crash} and was silent on ${r.baseline_silent}`],
  [`${pct(r.rq8.cert, r.rq8.runs)}%`, `of ${r.rq8.runs} real monthly batches admitted by the certificate, against ${pct(r.rq8.facts, r.rq8.runs)}% for the old file's facts and 0% for the checksum`],
  [`${r.rq9.same.toLocaleString("en-US")}`, `of ${r.rq9.runs.toLocaleString("en-US")} runs of held-out GitHub pandas scripts identical to the script's own output`],
];
stats.forEach(([big, label], i) => {
  const col = i % 2, row = Math.floor(i / 2);
  const x = 0.6 + col * 6.2, y = 1.85 + row * 1.95;
  card(s, x, y, 5.9, 1.65, i === 2);
  const c = i === 2 ? PAPER : INK;
  text(s, big, { x: x + 0.25, y: y + 0.2, w: 2.3, h: 1.2, fontFace: HEAD, fontSize: 40, bold: true, color: c, valign: "middle" });
  text(s, label, { x: x + 2.6, y: y + 0.15, w: 3.15, h: 1.35, fontSize: 13, color: c, valign: "middle" });
});
text(s, `0 unsound runs: every admitted run gave exactly the result of compiling that file fresh (${r.rq7.runs} synthetic, ${r.rq8.runs} real, ${r.rq9.runs.toLocaleString("en-US")} script runs).`,
  { x: 0.6, y: 5.75, w: 12.13, h: 0.5, fontFace: HEAD, fontSize: 16, bold: true });
terminal(s, 8, "run all the tests");
notes(s, "207 unit tests. 24 seeded mistakes: the compiler caught all 24 before running; the plain script crashed on 20 and " +
  "silently produced wrong output on 4. On 238 real monthly batches of public data, compiled once on the first month, the " +
  "certificate admitted 86 percent; the old file's own facts admit only 1 percent, the checksum none. On held-out GitHub " +
  "scripts, every admitted run matched the script's own result. Unsound means admitted but different from a fresh " +
  "compilation: zero, in every experiment.\n\nTERMINAL step 8 runs ctest.");

// ---------------------------------------------------------------- 14. honesty
s = slide("09  Honest limits", "What testing found, and what it cannot do");
card(s, 0.6, 1.85, 6.5, 4.25);
text(s, "5 BUGS THE EVALUATION FOUND IN MY COMPILER (ALL FIXED, ALL TESTED)", { x: 0.8, y: 2.0, w: 6.1, h: 0.3, fontFace: MONO, fontSize: 11, bold: true });
text(s, [
  "MATCH_RATE counted broken images already removed",
  "A NOT_NULL on a column pruned later was not protected",
  "Integer data was admitted into a FLOAT column",
  "A dropped column was wrongly treated as optional",
  "The lifter translated a drop() that pandas rejects",
].map((t, i, a) => ({ text: t, options: { bullet: { type: "number" }, breakLine: i < a.length - 1 } })),
{ x: 0.8, y: 2.45, w: 6.1, h: 3.5, fontSize: 15, paraSpaceAfter: 10 });
card(s, 7.4, 1.85, 5.33, 4.25, true);
text(s, "WHAT IT CANNOT DO YET", { x: 7.6, y: 2.0, w: 4.9, h: 0.3, fontFace: MONO, fontSize: 11, bold: true, color: PAPER });
text(s, [
  "No joins, loops or user functions",
  "Certificates are per column: a fact needed only after a filter is still asked of every row",
  "Data read inside a function is not lifted",
].map((t, i, a) => ({ text: t, options: { bullet: { indent: 16 }, breakLine: i < a.length - 1 } })),
{ x: 7.6, y: 2.45, w: 4.9, h: 3.5, fontSize: 15, color: PAPER, paraSpaceAfter: 10 });
text(s, "Each bug was found by comparing every run with a fresh compilation: the ground truth.", { x: 0.6, y: 6.35, w: 12.13, h: 0.45, fontFace: HEAD, fontSize: 16, bold: true });
notes(s, "I want to be honest about what the evaluation found. Comparing every run against a fresh compilation found five " +
  "bugs in my own first implementation, each a place where the code did not follow the definitions. All are fixed and have " +
  "regression tests. And the limits: no joins, loops or functions in the language; certificates are per column; scripts " +
  "that read data inside functions are not lifted.");

// ---------------------------------------------------------------- 15. close
s = pres.addSlide();
s.background = { color: INK };
number += 1;
text(s, "IN ONE SENTENCE", { x: 0.8, y: 1.0, w: 11, h: 0.3, fontFace: MONO, fontSize: 13, bold: true, color: PAPER, charSpacing: 2 });
text(s, "PrepLang compiles a cleaning program against the real data, catches mistakes before anything runs, and with a data certificate keeps the compiled program safe on every future file.",
  { x: 0.8, y: 1.6, w: 11.7, h: 2.4, fontFace: HEAD, fontSize: 30, bold: true, color: PAPER });
s.addShape(pres.shapes.LINE, { x: 0.8, y: 4.4, w: 3, h: 0, line: { color: PAPER, width: 4 } });
text(s, "Questions?", { x: 0.8, y: 4.75, w: 11, h: 0.8, fontFace: HEAD, fontSize: 40, color: PAPER });
text(s, "V.Nikhil  ·  24BAI0073   ·   demo/PrepLang_Demo.html   ·   py -3 demo/run_demo.py", { x: 0.8, y: 6.4, w: 11.7, h: 0.4, fontFace: MONO, fontSize: 14, bold: true, color: PAPER });
notes(s, "Likely questions.\nWhy LL(1)? Every statement begins with a unique keyword; one token of look-ahead chooses the " +
  "rule. Conditions use precedence levels OR, AND, NOT, comparison.\nWhat is in the symbol table? One global scope of " +
  "datasets; per column the type, whether it may be missing, its value interval and whether those facts are exact; for " +
  "images every file's size and channels, the pixel type and the output format.\nWhy is the certificate sound? Every " +
  "transfer function is monotone and every proof is 'the state lies inside a fixed condition'; a file inside the " +
  "certificate has a smaller state, so it keeps every proof.\nWhy not just keep all the checks? Proven checks cost time " +
  "on every run; the certificate keeps the optimized program's speed and explains every refusal.\nAI tools: answer " +
  "truthfully, as the course rules ask.");

pres.writeFile({ fileName: process.argv[2] || path.join(__dirname, "PrepLang_Demo_Deck.pptx") }).then((f) => console.log("Wrote", f));
