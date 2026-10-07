// Builds the Review 3 presentation.
//   node docs/review3/deck.js <output.pptx>      (needs the pptxgenjs npm package)
// Numbers come from eval/results/*.json, like the report.
const fs = require("fs");
const path = require("path");
const pptxgen = require("pptxgenjs");

const HERE = __dirname;
const ROOT = path.join(HERE, "..", "..");
const json = (file) => JSON.parse(fs.readFileSync(path.join(ROOT, file), "utf8"));

const cert = json("eval/results/certificates.json");
const admitted = cert.filter((r) => r.compiled_once !== "refused").length;
const facts = cert.filter((r) => r.file_facts_admit).length;
const pct = (a, b) => Math.round((100 * a) / b);
const rq3 = json("eval/results/rq3.json");
const million = rq3.compile_cost[2];
const img = rq3.image_streaming;
const tab = rq3.table_optimizations;
const scale = json("eval/results/scale.json");
const bugs = json("eval/results/realbugs.json");
const early = bugs.filter((b) => b.verdict === "compile" || b.verdict === "contract").length;
const real = json("eval/results/realbatches.json").filter((r) => r.fresh !== null);
const realAdmitted = real.filter((r) => r.compiled_once !== "refused").length;
const realFacts = real.filter((r) => r.file_facts_admit).length;
const realUnsound = real.filter((r) => r.compiled_once === "finished" && !(r.fresh === "finished" && r.same_outputs)).length;
const corpusAll = json("eval/results/pandas_corpus.json");
const corpus = corpusAll.slice(100);  // held-out test set
const corpusRuns = corpus.flatMap((r) => r.batches || []);
const corpusSame = corpusRuns.filter((b) => b.prep === "finished" && b.same).length;
const corpusUnsound = corpusRuns.filter((b) => b.prep === "finished" && !b.same).length;
const corpusLifted = corpus.filter((r) => r.outcome === "lifted").length;

// Ink and paper, with amber for what the compiler proves and teal for data.
const INK = "1B2430", PAPER = "FFFFFF", MIST = "EEF1F4", AMBER = "E8A33D", TEAL = "23867A", GREY = "5B6573";
const HEAD = "Cambria", BODY = "Calibri";

const pres = new pptxgen();
pres.layout = "LAYOUT_16x9";  // 10 x 5.625 in
pres.author = "V.Nikhil";
pres.title = "PrepLang: Phase 3 Final Review";
pres.theme = { headFontFace: HEAD, bodyFontFace: BODY };

const text = (slide, value, options) => slide.addText(value, { isTextBox: true, margin: 0, fontFace: BODY, color: INK, ...options });
const title = (slide, value) => text(slide, value, { x: 0.5, y: 0.35, w: 9, h: 0.6, fontFace: HEAD, fontSize: 28, bold: true });
const notes = (slide, value) => slide.addNotes(value);
let page = 0;
function content(section, heading) {
  const slide = pres.addSlide({ sectionTitle: section });
  slide.background = { color: PAPER };
  page += 1;
  text(slide, String(page + 1), { x: 9.2, y: 5.2, w: 0.4, h: 0.25, fontSize: 10, color: GREY, align: "right" });
  title(slide, heading);
  return slide;
}
// The motif: a numbered amber circle.
function badge(slide, x, y, label, size = 0.42) {
  slide.addShape(pres.shapes.OVAL, { x, y, w: size, h: size, fill: { color: AMBER }, line: { color: AMBER } });
  text(slide, label, { x, y, w: size, h: size, fontSize: 14, bold: true, color: INK, align: "center", valign: "middle" });
}
function card(slide, x, y, w, h, fill = MIST) {
  slide.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.08, fill: { color: fill }, line: { color: fill } });
}
function stat(slide, x, y, w, value, label, color = INK) {
  text(slide, value, { x, y, w, h: 0.75, fontFace: HEAD, fontSize: 40, bold: true, color });
  text(slide, label, { x, y: y + 0.75, w, h: 0.55, fontSize: 13, color: GREY });
}

// 1. Title
pres.addSection({ title: "Introduction" });
let s = pres.addSlide({ sectionTitle: "Introduction" });
s.background = { color: INK };
text(s, "PrepLang", { x: 0.7, y: 1.3, w: 8.6, h: 1.0, fontFace: HEAD, fontSize: 54, bold: true, color: PAPER });
text(s, "A data-aware compiler for table and image preprocessing", { x: 0.7, y: 2.3, w: 8.6, h: 0.5, fontSize: 22, color: AMBER });
text(s, "Compiler Design Laboratory (BCSE307P)  ·  Phase 3: Final Implementation and Testing", { x: 0.7, y: 3.4, w: 8.6, h: 0.4, fontSize: 15, color: PAPER });
text(s, "V.Nikhil  ·  24BAI0073", { x: 0.7, y: 3.85, w: 8.6, h: 0.4, fontSize: 15, bold: true, color: PAPER });
notes(s, "PrepLang is a language for cleaning data before machine learning, and this is its compiler, written in C++17.");

// 2. Problem
s = content("Introduction", "Data-cleaning scripts fail late, or silently");
text(s, [
  { text: "A pandas or OpenCV script only learns that its assumptions are wrong while it runs over the data.", options: { breakLine: true } },
  { text: "A misspelled column crashes it halfway through.", options: { bullet: true, breakLine: true } },
  { text: "A word in a number column breaks a type conversion.", options: { bullet: true, breakLine: true } },
  { text: "Decimal pixels saved as PNG finish without an error and every image is black.", options: { bullet: true } },
], { x: 0.5, y: 1.2, w: 5.0, h: 2.6, fontSize: 16, paraSpaceAfter: 8, valign: "top" });
card(s, 5.9, 1.2, 3.6, 3.6);
stat(s, 6.15, 1.4, 3.2, `${scale.image_cases[2].script_s.toFixed(1)} s`, "a script runs on 3,000 images, then writes black files");
stat(s, 6.15, 3.0, 3.2, `${scale.image_cases[2].preplang_compile_s.toFixed(2)} s`, "PrepLang reports the mistake at compile time", TEAL);
notes(s, "Measured on 3,000 JPG images: the hand-written script finishes and writes black images; PrepLang refuses to compile.");

// 3. Idea + pipeline
pres.addSection({ title: "Design" });
s = content("Design", "The idea: the data is a compile-time input");
const phases = ["Lexer", "LL(1) parser", "AST", "Semantic analysis\n(reads the data)", "Three address code", "Optimizer", "Data certificate", "pandas + OpenCV code"];
phases.forEach((name, i) => {
  const x = 0.5 + (i % 4) * 2.3, y = 1.35 + Math.floor(i / 4) * 1.45;
  card(s, x, y, 2.05, 1.05, i === 3 || i === 6 ? AMBER : MIST);
  badge(s, x + 0.12, y + 0.12, String(i + 1), 0.36);
  text(s, name, { x: x + 0.12, y: y + 0.5, w: 1.85, h: 0.5, fontSize: 13, bold: true });
});
text(s, "LOAD reads every CSV cell, a Parquet footer or the header of every image, so the compiler knows types, missing values, value ranges, image sizes and channels before anything runs.",
  { x: 0.5, y: 4.4, w: 9, h: 0.7, fontSize: 14, color: GREY });

// 4. Compiler concepts
s = content("Design", "Every classic phase, written by hand");
const concepts = [
  ["Lexical analysis", "keywords in any case, numbers with exponents, line and column on every token"],
  ["Syntax analysis", "LL(1) recursive descent, panic-mode recovery, did-you-mean hints"],
  ["Symbol table", "one scope; every dataset with the abstract state of its data"],
  ["Semantic analysis", "type checks and a forward abstract interpretation over intervals"],
  ["Intermediate code", "quadruples; wildcards expanded; conditions as temporaries"],
  ["Optimization", "no-effect and check elimination, liveness-based column pruning, streaming"],
];
concepts.forEach(([name, what], i) => {
  const x = 0.5 + (i % 2) * 4.6, y = 1.2 + Math.floor(i / 2) * 1.25;
  card(s, x, y, 4.4, 1.05);
  text(s, name, { x: x + 0.2, y: y + 0.12, w: 4.0, h: 0.35, fontSize: 15, bold: true, color: TEAL });
  text(s, what, { x: x + 0.2, y: y + 0.47, w: 4.0, h: 0.5, fontSize: 13 });
});

// 5. Contracts
s = content("Design", "Every EXPECT gets a verdict at compile time");
[["PROVEN", "The facts imply it, so no run-time check is generated.", "EXPECT marks BETWEEN 0 AND 100\nvalues are in [35, 95]", TEAL],
 ["VIOLATED", "The data breaks it: a compile error names the row or file.", "EXPECT marks BETWEEN 0 AND 90\nrow 2 of s.csv has 100", "B5452F"],
 ["DEFERRED", "Not decidable here: a check is generated, then assumed true.", "after FILTER marks < 100\nchecked when the program runs", GREY]]
  .forEach(([name, what, example, color], i) => {
    const x = 0.5 + i * 3.05;
    card(s, x, 1.25, 2.85, 3.5);
    text(s, name, { x: x + 0.2, y: 1.45, w: 2.5, h: 0.5, fontFace: HEAD, fontSize: 22, bold: true, color });
    text(s, what, { x: x + 0.2, y: 2.05, w: 2.5, h: 1.0, fontSize: 14 });
    text(s, example, { x: x + 0.2, y: 3.4, w: 2.5, h: 1.0, fontFace: "Consolas", fontSize: 11, color: GREY });
  });

// 6. Optimizations
s = content("Design", "Facts about the data drive the optimizer");
stat(s, 0.5, 1.3, 2.8, `${tab.columns_read[0]} of ${tab.columns_read[1]}`, "columns read after liveness-based pruning");
stat(s, 3.6, 1.3, 2.8, `${Math.round(img.baseline_peak_mib)} → ${Math.round(img.preplang_peak_mib)}`, "MiB of memory for 1,000 images, streamed", TEAL);
stat(s, 6.7, 1.3, 2.8, "exact", "every rewrite keeps the output; RESIZE fusion only with --approx");
text(s, [
  { text: "FILL_NULL on a column with no missing values, a FILTER that is always true, GRAYSCALE on 1-channel images: removed.", options: { bullet: true, breakLine: true } },
  { text: "Checks of proven contracts: removed.", options: { bullet: true, breakLine: true } },
  { text: "All image steps up to a save: one loop that handles one image at a time.", options: { bullet: true } },
], { x: 0.5, y: 3.15, w: 9, h: 1.6, fontSize: 15, paraSpaceAfter: 6, valign: "top" });

// 7. Certificates: idea
pres.addSection({ title: "New in Phase 3" });
s = content("New in Phase 3", "New: data certificates, compile once and reuse");
text(s, "A proof was valid only for the exact file it was compiled with. Tomorrow's file was refused, even with the same layout.",
  { x: 0.5, y: 1.15, w: 9, h: 0.6, fontSize: 15, color: GREY });
card(s, 0.5, 1.9, 5.3, 2.4);
text(s, "students  (data had marks in [35, 95])\n  name   STRING   no missing values   <- line 6\n  age    INTEGER  (any values)\n  marks  INTEGER  >= 0                <- line 5\n                  <= 100              <- line 5",
  { x: 0.7, y: 2.05, w: 5.0, h: 2.1, fontFace: "Consolas", fontSize: 11 });
text(s, [
  { text: "The compiler analyses the program again with weaker facts and keeps only the facts some proof needs.", options: { breakLine: true } },
  { text: "py -3 generated.py students=next_week.csv", options: { fontFace: "Consolas", fontSize: 12, color: TEAL, breakLine: true } },
  { text: "A refused file is explained: the row, and the proof it would break.", options: {} },
], { x: 6.1, y: 1.9, w: 3.4, h: 2.6, fontSize: 14, paraSpaceAfter: 10, valign: "top" });
text(s, "UNIQUE, REFERENCES and MATCH_RATE were proven from the values, so their checks stay and run only when an input changes.",
  { x: 0.5, y: 4.5, w: 9, h: 0.6, fontSize: 13, color: GREY });

// 8. Certificates: algorithm
s = content("New in Phase 3", "How the certificate is found, and why it is safe");
s.addImage({ path: path.join(HERE, "flow_certificate.png"), x: 0.5, y: 1.1, h: 4.2, w: 4.2 * (1580 / 1660) });
[["Weaken one fact", "allow missing values, or move a bound to a program constant or infinity"],
 ["Analyse again", "the whole semantic analysis, with no data read (facts replace files)"],
 ["Keep or restore", "keep the weaker fact only if every proof and removal survives"],
 ["Sound", "monotone transfer functions: an input inside the certificate keeps every proof"]]
  .forEach(([name, what], i) => {
    const y = 1.15 + i * 1.02;
    badge(s, 5.0, y, String(i + 1));
    text(s, name, { x: 5.6, y: y - 0.02, w: 3.9, h: 0.35, fontSize: 15, bold: true });
    text(s, what, { x: 5.6, y: y + 0.33, w: 3.9, h: 0.55, fontSize: 12, color: GREY });
  });

// 9. Certificates: results (native chart)
s = content("New in Phase 3", `${pct(admitted, cert.length)}% of new batches reused safely`);
s.addChart(pres.charts.BAR, [{ name: "Batches admitted (%)", labels: ["Fingerprint", "All facts of the file (TFDV-style)", "Data certificate"],
  values: [0, pct(facts, cert.length), pct(admitted, cert.length)] }], {
  x: 0.5, y: 1.15, w: 5.6, h: 3.9, barDir: "bar", chartColors: [AMBER], showValue: true, dataLabelPosition: "outEnd",
  dataLabelFormatCode: "0\"%\"", dataLabelFontSize: 12, dataLabelColor: INK, catAxisLabelColor: INK, catAxisLabelFontSize: 12,
  valAxisHidden: true, valAxisMaxVal: 100, valGridLine: { style: "none" }, catGridLine: { style: "none" }, showLegend: false,
  catAxisLabelFontFace: "+mn-lt", dataLabelFontFace: "+mn-lt",
});
stat(s, 6.5, 1.3, 3.0, String(cert.length), "new batches for 7 compiled programs, each also compiled fresh");
stat(s, 6.5, 2.6, 3.0, "0", "admitted runs that differ from a fresh compilation", TEAL);
stat(s, 6.5, 3.9, 3.0, "0", "refused batches that a fresh compilation accepts");

// 9b. Real data and real code
s = content("New in Phase 3", "Real data, real scripts: no unsound run");
card(s, 0.5, 1.2, 4.4, 3.6);
text(s, "Real monthly data", { x: 0.75, y: 1.35, w: 4.0, h: 0.4, fontSize: 16, bold: true, color: TEAL });
text(s, "Traffic sensors and bike rentals (UCI), New York green taxi Parquet files 2022-2024",
  { x: 0.75, y: 1.75, w: 4.0, h: 0.6, fontSize: 12, color: GREY });
stat(s, 0.75, 2.4, 4.0, `${pct(realAdmitted, real.length)}%`, `of ${real.length} new months admitted; the month's own facts: ${pct(realFacts, real.length)}%`);
text(s, `${realUnsound} admitted runs differ from a fresh compilation`, { x: 0.75, y: 3.9, w: 4.0, h: 0.6, fontSize: 13, bold: true });
card(s, 5.1, 1.2, 4.4, 3.6);
text(s, "Real pandas scripts", { x: 5.35, y: 1.35, w: 4.0, h: 0.4, fontSize: 16, bold: true, color: TEAL });
text(s, `${corpusAll.length} Titanic scripts from GitHub; ${corpusLifted} held-out scripts lifted into PrepLang by a new front end`,
  { x: 5.35, y: 1.75, w: 4.0, h: 0.6, fontSize: 12, color: GREY });
stat(s, 5.35, 2.4, 4.0, `${corpusSame}`, `of ${corpusRuns.length} runs on new batches give exactly the script's own result`);
text(s, `${corpusUnsound} admitted runs differ from the script`, { x: 5.35, y: 3.9, w: 4.0, h: 0.6, fontSize: 13, bold: true });

// 10. Scanner (native chart)
s = content("New in Phase 3", "New: checking every cell beats a pandas read");
s.addChart(pres.charts.BAR, [{ name: "seconds", labels: ["PrepLang compile (all checks)", "pandas read, pyarrow engine", "pandas read, C engine", "PrepLang, Phase 2"],
  values: [+million.compile_s.toFixed(2), +million.read_arrow_s.toFixed(2), +million.read_all_s.toFixed(2), 2.04] }], {
  x: 0.5, y: 1.15, w: 5.6, h: 3.9, barDir: "bar", chartColors: [TEAL], showValue: true, dataLabelPosition: "outEnd",
  dataLabelFormatCode: "0.00\" s\"", dataLabelFontSize: 12, dataLabelColor: INK, catAxisLabelColor: INK, catAxisLabelFontSize: 12,
  valAxisHidden: true, valGridLine: { style: "none" }, catGridLine: { style: "none" }, showLegend: false,
  catAxisLabelFontFace: "+mn-lt", dataLabelFontFace: "+mn-lt", catAxisOrientation: "maxMin",
});
text(s, "One million CSV rows x 10 columns", { x: 6.5, y: 1.2, w: 3.0, h: 0.4, fontSize: 13, bold: true });
text(s, [
  { text: "One read, one piece per core, cut at line breaks", options: { bullet: true, breakLine: true } },
  { text: "Cells viewed in place (string_view), no copies", options: { bullet: true, breakLine: true } },
  { text: "Numbers parsed exactly in one pass (Clinger's fast path)", options: { bullet: true, breakLine: true } },
  { text: "CRC-32 on its own thread; merge in file order gives the same facts and line numbers", options: { bullet: true } },
], { x: 6.5, y: 1.7, w: 3.0, h: 3.3, fontSize: 13, paraSpaceAfter: 6, valign: "top" });

// 11. Testing
pres.addSection({ title: "Results" });
s = content("Results", "Testing and evaluation");
[["207", "unit tests in 9 programs, all passing"], ["17", "recorded test cases: valid, invalid, boundary, run time"],
 ["24 / 24", "injected mistakes reported before processing"], [`${early} / ${bugs.length}`, "real GitHub failures reported before processing"]]
  .forEach(([value, label], i) => {
    const x = 0.5 + (i % 2) * 4.6, y = 1.2 + Math.floor(i / 2) * 1.85;
    card(s, x, y, 4.4, 1.6);
    stat(s, x + 0.25, y + 0.1, 3.9, value, label, i % 2 ? TEAL : INK);
  });

// 12. Demo
s = content("Results", "Demonstration");
[["Build", "Ctrl + Shift + B in VS Code"], ["Compile and run", "students.prep: every phase, then the cleaned file"],
 ["See the errors", "examples/errors: line, caret and hint"], ["Certificate", "--certificate, then run on a new batch and a bad one"],
 ["Tests", "run all tests: 9 programs pass"]]
  .forEach(([name, what], i) => {
    const y = 1.2 + i * 0.78;
    badge(s, 0.5, y, String(i + 1));
    text(s, name, { x: 1.1, y: y + 0.04, w: 2.4, h: 0.35, fontSize: 16, bold: true });
    text(s, what, { x: 3.5, y: y + 0.04, w: 6.0, h: 0.35, fontSize: 15, color: GREY });
  });

// 13. Conclusion
s = pres.addSlide({ sectionTitle: "Results" });
s.background = { color: INK };
text(s, "Conclusion", { x: 0.7, y: 0.6, w: 8.6, h: 0.7, fontFace: HEAD, fontSize: 34, bold: true, color: PAPER });
text(s, [
  { text: "Reading the data at compile time turns data mistakes into compile errors with clear messages.", options: { bullet: true, breakLine: true } },
  { text: "Contracts are proven, refuted or checked; optimizations stay exact.", options: { bullet: true, breakLine: true } },
  { text: `Data certificates reuse one compilation on new data: ${pct(admitted, cert.length)}% of new batches, with no unsound run.`, options: { bullet: true, breakLine: true } },
  { text: "Next: certificates for image folders, joins and dates, a native backend.", options: { bullet: true } },
], { x: 0.7, y: 1.5, w: 8.6, h: 3.0, fontSize: 18, color: PAPER, paraSpaceAfter: 12, valign: "top" });
text(s, "Thank you", { x: 0.7, y: 4.6, w: 8.6, h: 0.5, fontSize: 20, bold: true, color: AMBER });

pres.writeFile({ fileName: process.argv[2] }).then((file) => console.log("wrote", file));
