// Builds the Review 3 (Phase 3) final report as a Word file.
//   node docs/review3/review3.js <output.docx>
// Numbers come from eval/results/*.json and docs/review3/cases.json, so the report
// always matches the latest runs. Needs the "docx" npm package.
const fs = require("fs");
const path = require("path");
const {
  Document, Packer, Paragraph, TextRun, ImageRun, HeadingLevel, AlignmentType, LevelFormat, PageBreak,
} = require("docx");

const HERE = __dirname;
const ROOT = path.join(HERE, "..", "..");
const json = (file) => JSON.parse(fs.readFileSync(path.join(ROOT, file), "utf8"));

// ---------- numbers ----------
const cert = json("eval/results/certificates.json");
const certAdmitted = cert.filter((r) => r.compiled_once !== "refused");
const agrees = (r) => (r.compiled_once === "finished" ? r.fresh === "finished" && r.same_outputs
  : r.compiled_once === "contract error" && ["contract error", "compile error"].includes(r.fresh));
const factsAdmit = cert.filter((r) => r.file_facts_admit).length;
const pct = (a, b) => Math.round((100 * a) / b);
const falseRefusals = cert.filter((r) => r.compiled_once === "refused" && r.fresh === "finished").length;
const rq3 = json("eval/results/rq3.json");
const million = rq3.compile_cost[2];
const f2 = (x) => x.toFixed(2);
const tab = rq3.table_optimizations;
const img = rq3.image_streaming;
const fusion = rq3.resize_fusion;
const rq1 = json("eval/results/rq1.json");
const mutants = rq1.filter((r) => r.class !== "control");
const caught = mutants.filter((r) => r.preplang.verdict !== "not reported").length;
const bugs = json("eval/results/realbugs.json");
const early = bugs.filter((b) => b.verdict === "compile" || b.verdict === "contract").length;
const scale = json("eval/results/scale.json");
const cases = json("docs/review3/cases.json");
const real = json("eval/results/realbatches.json").filter((r) => r.fresh !== null);
const realAdmitted = real.filter((r) => r.compiled_once !== "refused");
const realAgree = (r) => (r.compiled_once === "finished" ? r.fresh === "finished" && r.same_outputs
  : r.compiled_once === "contract error" && ["contract error", "compile error"].includes(r.fresh));
const realFacts = real.filter((r) => r.file_facts_admit).length;
const corpusAll = json("eval/results/pandas_corpus.json");
const corpus = corpusAll.slice(100);  // the held-out test set
const corpusLifted = corpus.filter((r) => r.outcome === "lifted" || r.outcome === "compile error");
const corpusRuns = corpus.flatMap((r) => r.batches || []);
const corpusSame = corpusRuns.filter((b) => b.prep === "finished" && b.same).length;
const corpusUnsound = corpusRuns.filter((b) => b.prep === "finished" && !b.same).length;
const corpusAdmitted = corpusRuns.filter((b) => b.prep !== "refused").length;
const readme = fs.readFileSync(path.join(ROOT, "README.md"), "utf8").split(String.fromCharCode(13)).join("");
const grammarStart = readme.indexOf("```ebnf\n") + "```ebnf\n".length;
const grammar = readme.slice(grammarStart, readme.indexOf("\n```", grammarStart)).split("\n");

// ---------- formatting (plain black text, Times New Roman, no tables or boxes) ----------
const FONT = "Times New Roman";
const BLACK = "000000";
const BODY = 24;  // 12 pt
const H1 = 28;    // 14 pt
const H2 = 26;    // 13 pt
const CODE = 20;  // 10 pt

const t = (text) => new TextRun({ text, font: FONT, size: BODY, color: BLACK });
const b = (text) => new TextRun({ text, font: FONT, size: BODY, color: BLACK, bold: true });
const p = (...runs) => new Paragraph({ spacing: { after: 160, line: 300 }, alignment: AlignmentType.JUSTIFIED,
  children: runs.map((r) => (typeof r === "string" ? t(r) : r)) });
const h1 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 360, after: 160 },
  children: [new TextRun({ text, font: FONT, size: H1, bold: true, color: BLACK })] });
const h2 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 240, after: 120 },
  children: [new TextRun({ text, font: FONT, size: H2, bold: true, color: BLACK })] });
const bullet = (...runs) => new Paragraph({ numbering: { reference: "bullets", level: 0 }, spacing: { after: 80, line: 280 },
  children: runs.map((r) => (typeof r === "string" ? t(r) : r)) });
const step = (ref) => (...runs) => new Paragraph({ numbering: { reference: ref, level: 0 }, spacing: { after: 80, line: 280 },
  children: runs.map((r) => (typeof r === "string" ? t(r) : r)) });
// Wide listings (the grammar) use a smaller size so no line wraps.
const code = (lines) => {
  const widest = Math.max(...lines.map((line) => line.length));
  const size = widest > 100 ? 14 : widest > 82 ? 15 : CODE;
  return lines.map((line, i) => new Paragraph({
    spacing: { after: i === lines.length - 1 ? 200 : 0, before: i === 0 ? 80 : 0 }, indent: { left: size < CODE ? 0 : 360 },
    children: [new TextRun({ text: line === "" ? " " : line, font: "Consolas", size, color: BLACK })],
  }));
};
const center = (text, size, bold = false, after = 200) => new Paragraph({ alignment: AlignmentType.CENTER,
  spacing: { after }, children: [new TextRun({ text, font: FONT, size, bold, color: BLACK })] });
let figureNumber = 0;
function figure(file, widthPx, caption) {
  const data = fs.readFileSync(path.join(HERE, file));
  const w = data.readUInt32BE(16), h = data.readUInt32BE(20);
  figureNumber += 1;
  return [
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { before: 120, after: 80 },
      children: [new ImageRun({ type: "png", data, transformation: { width: widthPx, height: Math.round(widthPx * h / w) } })] }),
    new Paragraph({ alignment: AlignmentType.CENTER, spacing: { after: 240 },
      children: [new TextRun({ text: `Figure ${figureNumber}: ${caption}`, font: FONT, size: 22, italics: true, color: BLACK })] }),
  ];
}
const pageBreak = () => new Paragraph({ children: [new PageBreak()] });

// ---------- front page ----------
const front = [
  new Paragraph({ spacing: { before: 3000 }, children: [] }),
  center("PrepLang", 44, true, 240),
  center("A Data Aware Compiler for Table and Image Preprocessing", 28, false, 1200),
  center("Compiler Design Laboratory (BCSE307P)", 28, true, 240),
  center("Phase 3: Final Implementation and Testing", 28, false, 2000),
  center("V.Nikhil", 28, true, 120),
  center("24BAI0073", 28, false, 120),
  pageBreak(),
];

// ---------- test cases from cases.json ----------
const kindTitle = { valid: "Valid inputs", invalid: "Invalid inputs", boundary: "Boundary cases",
  "run time": "Error conditions when the generated program runs" };
const caseParagraphs = [];
let caseNumber = 0;
for (const kind of Object.keys(kindTitle)) {
  caseParagraphs.push(h2(`9.${3 + Object.keys(kindTitle).indexOf(kind)} ${kindTitle[kind]}`));
  for (const c of cases.filter((x) => x.kind === kind)) {
    caseNumber += 1;
    caseParagraphs.push(p(b(`Test ${caseNumber}. `), c.what + "."));
    caseParagraphs.push(p(b("Expected: "), c.expected + ". ", b("Actual: "), c.actual.replace(/ {2,}/g, " ")));
  }
}

const body = [
  // 1
  h1("Chapter 1: Introduction"),
  p("Before data can be used to train a machine learning model it has to be cleaned: missing values are filled or removed, numbers are scaled, rows are filtered, and images are resized and converted. This work is usually done with a Python script that uses pandas for tables and OpenCV for images. Such a script only finds out that something is wrong while it is running. A misspelled column name, a word inside a column of numbers, a broken image file, or decimal pixels saved as a PNG file show up halfway through the data, or never, and then the dataset is silently wrong."),
  p("PrepLang is a small language I designed for this job, and this project is its compiler, written in C++17. The compiler does something an ordinary compiler cannot do: it opens the real data files while it is compiling. It reads every cell of a CSV file, only the summary at the end of a Parquet file, and only the header of every image. From this it knows the type of every column, whether values are missing, the smallest and largest value, and the size and channels of every image. It follows these facts through every statement, so it can report data mistakes before anything runs, prove the data checks the programmer wrote, remove work that is not needed, and then write one Python program that does the cleaning."),
  p("In Phase 3 I completed the project, tested it with valid, invalid, boundary and large inputs, measured its performance, and added two things: a much faster analysis of large CSV files, and data certificates, which let a compiled program safely accept new data without being compiled again."),

  // 2
  h1("Chapter 2: Problem Statement"),
  p("Data preparation scripts check their assumptions too late. The assumptions are about the data (this column holds numbers, this column has no missing values, every image has three channels, every image has a label), but a script can only test them by running over the data. The existing tools fall into two groups. Data validation tools such as Great Expectations or TensorFlow Data Validation check a dataset when the pipeline runs, separately from the code that transforms it. Static analysis tools look at the code but not at the data, so they cannot know what range a column has or how big an image is."),
  p("The problem I solve is: build a compiler for data preprocessing programs that uses the real input data as a compile time input, so that data mistakes are found before processing starts, data contracts are proven or rejected at compile time, and the generated program is both safe and fast. A second problem appears as soon as this works: a compiled program is proven only for the files it was compiled with, while real pipelines run again every day on new files. So the compiler should also say which facts about the data its proofs really need, and the generated program should accept any new file that has those facts."),

  // 3
  h1("Chapter 3: Objectives"),
  bullet("Design a small, readable language for table and image preprocessing, with its own tokens and an LL(1) grammar."),
  bullet("Build every phase of a compiler by hand: lexical analysis, syntax analysis with error recovery, an abstract syntax tree, a symbol table, semantic analysis, three address code, optimization and code generation."),
  bullet("Use the input data during semantic analysis to type-check every operation and to decide every EXPECT contract as proven, violated or checked at run time."),
  bullet("Optimize the program using facts about the data, and keep every optimization exact unless the user asks for an approximate one."),
  bullet("Generate a Python program (pandas and OpenCV) that cannot run on data its proofs do not cover."),
  bullet("Find the data certificate of every program, so that the compiled program can be reused on new batches of data."),
  bullet("Keep compile time low even for large inputs."),
  bullet("Test every module and measure correctness, speed and memory."),

  // 4
  h1("Chapter 4: Literature Survey"),
  bullet(b("Data validation. "), "TensorFlow Data Validation (Breck et al., 2019), Deequ (Schelter et al., 2018), Great Expectations and Pandera (Bantilan, 2020) check expectations on a dataset while a pipeline runs. They do not look at the program, so they cannot say at which statement a fact stops being true, and they always check, even when the answer is already known."),
  bullet(b("Static analysis of data science code. "), "Zhuang and Lu (2022) check column types of data frames by abstract interpretation, and Gerstl, Sihler and Tichy (2026) infer the shape of R data frames, becoming more precise when the datasets are available. They infer names, types or shapes, but not missing values or value ranges, and they do not prove contracts or handle images."),
  bullet(b("Types from data. "), "F# type providers (Petricek, Guerra and Syme, 2016) read a sample of a data file at compile time to create types. PrepLang reads the whole file (or its metadata), and keeps facts about values, not only types."),
  bullet(b("Compile-time data contracts. "), "Mirji (2026) checks the structure of Spark pipelines at compile time from declared types. PrepLang proves contracts about values (ranges, missing values, unique keys, foreign keys, image sizes) from the data itself."),
  bullet(b("Hybrid checking. "), "Soft typing (Cartwright and Fagan, 1991) and hybrid type checking (Flanagan, 2006) prove what they can at compile time and insert run-time checks for the rest. PrepLang applies the same idea to data contracts."),
  bullet(b("Reusing proofs on new inputs. "), "Proof-carrying code (Necula, 1997) ships a proof with a program. JIT compilers protect fast code with guards and fall back when a guard fails (Hölzle et al., 1992), and databases attach validity ranges to query plans (Markl et al., 2004). Cousot et al. (2013) infer conditions on the inputs of a program, and DeepInfer (Ahmed et al., 2024) infers data preconditions for neural networks. None of these finds the smallest set of data facts that a compiled data pipeline's correctness proofs depend on, which is what PrepLang's data certificate does."),
  bullet(b("Compiler background. "), "The phases follow Aho, Lam, Sethi and Ullman, Compilers: Principles, Techniques, and Tools. The analysis follows abstract interpretation (Cousot and Cousot, 1977) with intervals (Cousot and Cousot, 1976)."),

  // 5
  h1("Chapter 5: System Design"),
  h2("5.1 Architecture"),
  p("The compiler is a chain of phases. Each phase hands its result to the next one, and if a phase finds an error the compiler stops there, so the same mistake is not reported again by later phases. Figure 1 shows the complete pipeline."),
  ...figure("flow_pipeline.png", 300, "Phases of the PrepLang compiler"),
  h2("5.2 Modules"),
  bullet(b("src/common: "), "source text and diagnostics (line, column, caret, hint, notes)."),
  bullet(b("src/lexer: "), "tokens and the lexer."),
  bullet(b("src/parser: "), "the recursive descent parser with error recovery."),
  bullet(b("src/ast: "), "syntax tree nodes, a tree printer and Graphviz export."),
  bullet(b("src/schema: "), "reading the data: the parallel CSV scanner, Parquet footers, PNG and JPEG headers, CRC-32."),
  bullet(b("src/sema: "), "the symbol table with the abstract state of the data, selectors, contracts and the semantic analyzer."),
  bullet(b("src/dataset: "), "lineage of every column and linking images with labels."),
  bullet(b("src/ir: "), "three address code."),
  bullet(b("src/optimizer: "), "the pass manager and the optimization passes."),
  bullet(b("src/certify: "), "data certificates."),
  bullet(b("src/backend: "), "pandas code, OpenCV code and the planner that joins them."),
  bullet(b("src/explain: "), "the --explain, --dry-run and --lineage outputs and the run manifest."),
  h2("5.3 Data Flow"),
  p("The input is a .prep program. LOAD statements name data files, which the schema module reads during semantic analysis. The analysis keeps an abstract state of every dataset and updates it after every statement. The IR builder turns the checked tree into three address code, the optimizer removes what the state proves unnecessary, the certifier finds the facts the proofs need, and the code generator writes output/generated.py. When generated.py runs it reads the data, checks its fingerprint or certificate, does the cleaning, and writes the output files and a manifest."),

  // 6
  h1("Chapter 6: Methodology"),
  p("I built the compiler phase by phase, testing each phase before starting the next one, as planned in Phase 1. The method rests on four ideas."),
  step("method")(b("Data as a compile time input. "), "LOAD reads the data while compiling and builds an abstract state: for each column a type, whether values can be missing, an interval of values and an exactness flag; for images the size and channels of every file, the pixel type and the output format."),
  step("method")(b("Transfer functions. "), "Every statement changes the state in a known way. For example, FILTER marks >= 50 narrows marks to start at 50, FILL_NULL makes a column non-missing, GRAYSCALE makes every image one channel, and NORMALIZE ... RANGE 0 1 makes pixels decimal numbers between 0 and 1."),
  step("method")(b("Three verdicts for contracts. "), "Each EXPECT is PROVEN when the state shows it always holds, VIOLATED when the data certainly breaks it (the error names the row or file), and otherwise checked when the program runs."),
  step("method")(b("Data certificates. "), "After optimizing, the compiler analyses the program again many times with weaker facts, and keeps only the facts without which some proof would fail. Figure 2 shows the algorithm."),
  ...figure("flow_certificate.png", 470, "How the data certificate is found"),

  // 7
  h1("Chapter 7: Compiler Design Concepts Used"),
  h2("7.1 Lexical Analysis"),
  p("The lexer reads the program one character at a time and groups characters into tokens: keywords (LOAD, FILTER and so on, in any case), identifiers, numbers (with decimals and exponents), strings, operators and punctuation. A # starts a comment. Every token keeps its line and column. An unknown character or a string that is never closed is a lexical error."),
  h2("7.2 Syntax Analysis"),
  p("The parser is hand written recursive descent. Every statement starts with its own keyword, so one token of look-ahead decides the rule, which makes the grammar LL(1). Conditions in FILTER follow the usual precedence: OR is weakest, then AND, then NOT, and comparisons bind tightest. On an error the parser uses panic mode recovery: it skips to the next semicolon and continues, so one run reports several errors, and it suggests the closest keyword for a misspelling."),
  h2("7.3 Abstract Syntax Tree"),
  p("The parser builds a tree with one node type per statement and per condition. Later phases walk it with the visitor pattern. It can be printed with --ast or saved as a Graphviz picture with --dot."),
  h2("7.4 Symbol Table"),
  p("There is one global scope. Each dataset has a name, a kind (TABLE, TABLESET, IMAGE, IMAGESET or DATASET), the line where it was defined, and the current state of its data. Lookups resolve wildcards (company.*.age) and type selectors (WHERE TYPE NUMERIC). A column used after DROP or SELECT removed it is reported, like a use after free."),
  h2("7.5 Semantic Analysis"),
  p("Semantic analysis is name checking, type checking and state tracking together. It is a forward abstract interpretation: programs have no loops, so one pass over the statements is enough and no widening is needed. Contracts are decided here, and image save statements are checked against the pixel type and channels."),
  h2("7.6 Intermediate Code"),
  p("The checked tree becomes three address code stored as quadruples. Wildcards are expanded into one instruction per table, and conditions are broken into temporaries: t1 = marks >= 50, t2 = attendance > 0.5, t3 = t1 AND t2, FILTER students IF t3."),
  h2("7.7 Code Optimization"),
  bullet(b("No-effect elimination: "), "removes operations the state proves useless, such as FILL_NULL on a column without missing values or GRAYSCALE on images that already have one channel."),
  bullet(b("Contract check elimination: "), "removes run-time checks of proven contracts. A contract proven by reading the values keeps a check that runs only when an input changes."),
  bullet(b("Dead code elimination: "), "removes unused temporaries and image work after the last save."),
  bullet(b("Dead store elimination and column pruning: "), "a backward liveness analysis over columns, so LOAD reads only the columns that are used."),
  bullet(b("Image streaming: "), "all image steps become one loop that handles one image at a time."),
  bullet(b("RESIZE fusion: "), "the only optimization that changes pixels, so it runs only with --approx."),
  h2("7.8 Target Code Generation"),
  p("The execution planner sends table instructions to the pandas code generator and image instructions to the OpenCV code generator, in program order, and writes one Python file. Column types become pandas nullable types, so missing values behave exactly as the analysis assumed."),
  h2("7.9 Error Detection and Recovery"),
  p("Every message names the phase, the line and column, draws a caret under the spot and gives a hint. The parser recovers from syntax errors, and one mistake in a wildcard is reported once with a note for every table it affects. The generated program reports run-time errors with the line of the statement and, for refused inputs, the row and the proof."),

  // 8
  h1("Chapter 8: Implementation"),
  h2("8.1 Language, Tools and Libraries"),
  bullet("C++17 with Visual Studio 2022 Build Tools (MSVC) and CMake; no external C++ libraries."),
  bullet("Python 3 with pandas, NumPy, OpenCV and pyarrow for the generated programs and the evaluation."),
  bullet("Visual Studio Code tasks for building, running, explaining and testing; Git and GitHub for version history."),
  bullet(`About 9,200 lines of C++ in src, a 400-line Python front end for pandas scripts, 207 unit tests in 9 test programs, 8 example programs and 5 error examples.`),
  h2("8.2 The Parallel CSV Scanner"),
  p(`In Phase 2 the compiler read a CSV file line by line, copying every cell into a new string, and one million rows took 2.04 seconds, about three times longer than pandas needs to read the file. I rewrote this part as a parallel scanner, shown in Figure 3. The file is read once into memory. Its rows are cut into one piece per processor core, always at a line break, and each piece is analysed by its own thread. Cells are looked at in place with std::string_view instead of being copied, and the type of a cell and the value of a number are found in one pass. For numbers I used Clinger's fast path: when the digits fit exactly in a double and the power of ten is at most 10^22, one multiplication or division gives the correctly rounded result, and every other number is handed to std::from_chars. The CRC-32 fingerprint is computed on its own thread at the same time, eight bytes per step.`),
  p("The pieces are merged in file order. The first problem in the file wins, and the line where a column first turns into text is found from the first text cell and the first boolean and number cells of each piece, so the messages name exactly the same lines as a single pass would."),
  ...figure("flow_scanner.png", 330, "The parallel CSV scanner"),
  p(`Now one million rows take ${f2(million.compile_s)} seconds to analyse, including starting the compiler. That is ${(million.read_all_s / million.compile_s).toFixed(1)} times faster than pandas reads the same file with its default reader (${f2(million.read_all_s)} s), and about as fast as pandas with the multi-threaded pyarrow reader (${f2(million.read_arrow_s)} s, measured inside an already running Python), although the compiler also checks the encoding, infers every type and finds every minimum, maximum and missing value.`),
  ...figure("chart_compile_time.png", 430, "Compile time of PrepLang against just reading the CSV file in Python"),
  h2("8.3 Data Certificates"),
  p("The certifier takes the facts of every input table and first clears the exactness flag, so they describe a set of possible files instead of one file. It checks that these facts still prove everything the compiled program uses. Then, for every column the optimized program reads, it tries to make one fact weaker and runs the whole semantic analysis again: it allows missing values, and it moves the lower and upper bounds to minus or plus infinity, or else to the loosest constant of the program that still works. A weaker fact is kept when no proof and no removed operation is lost. Otherwise the fact stays in the certificate together with the proofs that needed it. The analysis compares ranges only with the program's own constants, so no other bound could do better."),
  p("These re-analyses read no data: the tables are replaced by their facts and image headers are kept in memory. For the students example the whole certificate needs 13 extra analyses and takes a few milliseconds. The result is printed with --certificate and written into the generated program:"),
  ...code([
    "students  (compiled with examples/tabular/students.csv)",
    "  name        STRING   no missing values   <- line 6: EXPECT students.name NOT_NULL",
    "  age         INTEGER  (any values)",
    "  marks       INTEGER  >= 0                <- line 5: EXPECT students.marks BETWEEN 0 AND 100",
    "                       <= 100              <- line 5: EXPECT students.marks BETWEEN 0 AND 100",
    "  attendance  FLOAT    (any values)",
  ]),
  p("The file had marks between 35 and 95, but the only proof about marks needs 0 to 100, so a new file with a mark of 100 is accepted. The run can be given a new file on the command line, for example py -3 output/generated.py students=new_students.csv. Figure 4 shows what the generated program does with each input. Contracts proven by reading the values themselves (UNIQUE, REFERENCES and MATCH_RATE) keep their check, but it runs only when an input is not the compiled file."),
  ...figure("flow_admission.png", 440, "What the generated program does with an input file"),
  p("Why this is safe: every transfer function is monotone once exactness is cleared, and every proof or removal is a test of the form \"the state lies inside a fixed condition\". A file that satisfies the certificate has a state inside the certified state, so the analysis of that file would prove at least everything the compiled program relies on. I wrote this argument as a theorem in the paper and also check it with a test: 400 random files are tried, and every file the certificate accepts, when compiled on its own, keeps every proof."),
  h2("8.4 Certificates for Image Folders, Missing Columns and Columns Not Read"),
  p("A folder of images is certified as one box per file type (.jpg and .png): the channel counts, the width and height ranges and the bit depths its images may have, and whether files that cannot be read are allowed. The analysis works on a list of files, so it is given the corner images of the box: one image for every combination of the smallest and largest width and height, every allowed channel count and every bit depth. Every image decision of the compiler is a test of the form \"all images ...\" or \"some image ...\" over single properties, so the corners decide it exactly as any folder inside the box would. The generated program reads only the header of each image of a new folder and admits the folder if every header fits its box."),
  p("Two cases from real data changed the certificate for tables. First, a proof can depend on a column the optimized program never loads: after EXPECT t.id NOT_NULL is proven, its check is removed and column pruning drops id. Such a column now stays in the certificate and is loaded only to be checked. Second, new batches often lack a column: the Kaggle Titanic test file has no Survived column. A column that no instruction names and that is only carried to the output is made optional when the analysis without it keeps every proof."),
  h2("8.5 Language Additions"),
  bullet(b("FILL_NULL c WITH MEAN, MEDIAN or MODE"), " computes the fill value from the data when the program runs. MEAN and MEDIAN always make an INTEGER column FLOAT, so the type never depends on the data, which keeps the analysis monotone."),
  bullet(b("DROP_NULL t [a, b]"), " removes only the rows that miss one of the listed columns."),
  bullet(b("DATETIME"), " columns come from the dates and timestamps of Parquet files. They can be kept, dropped, saved and checked with NOT_NULL; comparing or filling them is a type error."),
  h2("8.6 A Front End for pandas Scripts"),
  p("frontend/pandas2prep.py reads a pandas script with Python's own parser and translates the part of its cleaning that PrepLang can express: fillna (with constants or mean, median and mode), dropna, drop, column selection, filters, normalization written as arithmetic, assert checks and to_csv. It stops at the first statement it cannot express exactly, such as a new column made with map. This lets certificates protect existing scripts."),
  h2("8.7 Bugs Found by the Experiments"),
  p("While evaluating certificates I found a bug from an earlier phase. The run-time MATCH_RATE check compared the labels with every image file, including a broken image that REMOVE_CORRUPT had removed before the LINK, while the compile-time proof had left it out. Before Phase 3 this check was always removed as proven, so it never ran. Now it runs when the labels change, and it failed where it should pass. The generated check now skips the same broken files as the proof. I added a regression test for it."),
  p("The experiments found two more mistakes in my first certificate. It covered only the columns the optimized program loads, so a proven NOT_NULL on a column that was pruned afterwards was not protected; I noticed this while writing the taxi pipeline. And it let whole numbers into a FLOAT column and an empty column into any type; the random soundness test found files where a fresh compilation then decided differently. Both are fixed, and the random tests now also make files with missing and pruned columns."),

  // 9
  h1("Chapter 9: Testing and Results"),
  h2("9.1 Unit Tests"),
  p("All 207 unit tests in 9 test programs pass with ctest (Figure 5):"),
  bullet(b("test_lexer"), " 15 tests: tokens, keywords, numbers, strings and lexical errors."),
  bullet(b("test_parser"), " 31 tests: every statement, operator precedence, error recovery and suggestions."),
  bullet(b("test_semantic"), " 33 tests: types, names, missing values, ranges, contracts, MEAN/MEDIAN/MODE fills and DROP_NULL with columns."),
  bullet(b("test_tableset"), " 13 tests: folders of tables, wildcards and contracts across tables."),
  bullet(b("test_imageset"), " 21 tests: image headers, sizes, channels, image errors and MATCH_RATE."),
  bullet(b("test_parquet"), " 25 tests: Parquet footers, statistics, contracts and DATETIME columns."),
  bullet(b("test_optimizer"), " 12 tests: every optimization."),
  bullet(b("test_scanner"), " 22 tests: the parallel scanner gives the same facts with 1 to 16 pieces, reports the first problem of the file, and parses 20,000 random numbers exactly like std::from_chars."),
  bullet(b("test_certificate"), " 35 tests: which facts are certified and why, columns not read and columns that may be missing, image folders, admission and refusal, and soundness properties on 1,200 random tables and 600 random image folders."),
  ...figure("evidence_tests.png", 520, "All test programs pass"),
  h2("9.2 Test Cases"),
  p("Each case below was run by docs/review3/cases.py, which records the real output, so the actual results are copied from the program and not typed by hand. All of them behave as expected."),
  ...caseParagraphs,
  h2("9.7 Output Evidence"),
  ...figure("evidence_explain.png", 520, "Execution plan of examples/tabular/students.prep (--explain)"),
  ...figure("evidence_errors.png", 520, "Semantic errors with line, caret and hint"),
  ...figure("evidence_image_error.png", 520, "Decimal pixels saved as PNG are stopped at compile time"),
  ...figure("evidence_certificate.png", 520, "The data certificate of the students program"),
  ...figure("evidence_admission.png", 520, "The compiled program accepts a new batch and refuses a wrong one"),
  h2("9.8 Performance and Evaluation"),
  bullet(b("Injected mistakes: "), `${caught} of ${mutants.length} mistakes injected into the example programs are reported before any data is processed, with no false alarms; a script that does the same steps crashes part-way on most of them and silently writes wrong output on the rest.`),
  bullet(b("Contracts: "), "15 of the 16 contracts in the examples are decided at compile time."),
  bullet(b("Optimizations: "), `on a table of 200,000 rows and 40 columns, the optimized program reads 5 columns and runs no checks, and takes ${f2(tab.optimized_s)} s against ${f2(tab.O0_s)} s without optimization, with the same output. For 1,000 images, streaming needs ${Math.round(img.preplang_peak_mib)} MiB of memory instead of ${Math.round(img.baseline_peak_mib)} MiB. RESIZE fusion saves little and changes pixels, so it is left to --approx.`),
  bullet(b("Compile time: "), `one million CSV rows in ${f2(million.compile_s)} s (Section 8.2); a Parquet file with 16 million rows in ${scale.parquet_compile_cost[2].compile_s.toFixed(2)} s, because only its footer is read.`),
  bullet(b("Large inputs: "), `on 3,000 images, saving decimal pixels as PNG is reported in ${f2(scale.image_cases[2].preplang_compile_s)} s, while the equivalent script runs for ${f2(scale.image_cases[2].script_s)} s and writes 3,000 black images.`),
  bullet(b("Real failures: "), `of ${bugs.length} data preparation failures collected from GitHub issues, PrepLang reports ${early} (${pct(early, bugs.length)}%) before any data is processed.`),
  bullet(b("Real monthly data: "), `three public datasets that come in monthly batches (traffic on a US interstate, bike rentals, and New York green taxi trips as published Parquet files) were cleaned by pipelines compiled on one month and run on the others: ${real.length} runs. Certificates admitted ${realAdmitted.length} (${pct(realAdmitted.length, real.length)}%), the compiled month's own facts only ${realFacts}, and every admitted run gave exactly the result of compiling that month fresh (${realAdmitted.filter(realAgree).length} of ${realAdmitted.length}). Refusals named real problems, for example temperatures of 0 Kelvin from a broken sensor, and the change of passenger_count from decimal to integer in the taxi files of February 2023.`),
  bullet(b("Real pandas scripts: "), `${corpusAll.length} Titanic cleaning scripts were collected from GitHub. The first 100 were used to develop the front end; of the other ${corpus.length}, ${corpusLifted.length} were lifted and compiled against the Kaggle training file, then given ${corpusRuns.length} new batches (the real Kaggle test file, resampled rows and extra missing values). Certificates admitted ${corpusAdmitted}; ${corpusSame} runs gave exactly the result of the script's own pandas code, and ${corpusUnsound} gave a different result.`),
  bullet(b("Data certificates: "), `seven compiled programs were given ${cert.length} new batches of data (other days of a sensor process, resampled rows, values that drift, missing values and values out of range), and every batch was also compiled from scratch as the ground truth. The certificate admitted ${certAdmitted.length} batches (${pct(certAdmitted.length, cert.length)}%); taking all facts of the compiled file, as a schema inferred by TFDV would, admits only ${factsAdmit} (${pct(factsAdmit, cert.length)}%), and the plain fingerprint admits none. ${certAdmitted.filter(agrees).length} of the ${certAdmitted.length} admitted runs ended exactly like the fresh compilation (the same output files, or stopped by the same contract), and ${falseRefusals === 0 ? "every refused batch also failed to compile from scratch, so nothing was refused that could have run" : falseRefusals + " refused batches could have run"}.`),

  // 10
  h1("Chapter 10: Conclusion"),
  p("PrepLang shows that, for batch data preparation, treating the input data as a compile time input is worth it. The compiler has every classic phase, written by hand, and its semantic analysis carries real facts about the data through the program. This turns most data mistakes into compile time errors with clear messages, decides almost every contract before the program runs, and justifies optimizations that keep the output exact. In Phase 3 the analysis of large CSV files became faster than reading them with pandas, and data certificates removed the main limit of the earlier design: a compiled program is no longer tied to one file, but accepts any new batch that has the few facts its proofs need, and explains exactly why when it refuses one."),

  // 11
  h1("Chapter 11: Future Enhancements"),
  bullet("Certificates for image folders, by checking the headers of new images against the certified sizes and channels."),
  bullet("Joins, date columns and per-image label files, which several of the real GitHub failures needed."),
  bullet("A native C++ backend for the table operations, so the generated program does not need Python."),
  bullet("Searching for the smallest certificate instead of a greedy one, and certificates for groups of columns."),
  bullet("A language server for VS Code that shows the state of the data while typing."),

  // references
  h1("References"),
  ...[
    "A. V. Aho, M. S. Lam, R. Sethi and J. D. Ullman, Compilers: Principles, Techniques, and Tools, 2nd ed., Pearson, 2006.",
    "P. Cousot and R. Cousot, \"Abstract interpretation: a unified lattice model for static analysis of programs,\" POPL 1977.",
    "P. Cousot and R. Cousot, \"Static determination of dynamic properties of programs,\" Int. Symp. on Programming, 1976.",
    "E. Breck, N. Polyzotis, S. Roy, S. E. Whang and M. Zinkevich, \"Data validation for machine learning,\" MLSys 2019.",
    "S. Schelter et al., \"Automating large-scale data quality verification,\" PVLDB 11(12), 2018.",
    "N. Bantilan, \"pandera: Statistical data validation of pandas dataframes,\" SciPy 2020.",
    "T. Petricek, G. Guerra and D. Syme, \"Types from data: Making structured data first-class citizens in F#,\" PLDI 2016.",
    "Y. Y. Zhuang and M.-Y. Lu, \"Enabling type checking on columns in data frame libraries by abstract interpretation,\" IEEE Access, 2022.",
    "O. Gerstl, F. Sihler and M. Tichy, \"Inferring the shape of data frames in R programs using abstract interpretation,\" arXiv:2607.03889, 2026.",
    "V. Mirji, \"Shift schema drift left: policy-aware compile-time contracts for typed JVM and Spark pipelines,\" arXiv:2604.16986, 2026.",
    "C. Flanagan, \"Hybrid type checking,\" POPL 2006.",
    "R. Cartwright and M. Fagan, \"Soft typing,\" PLDI 1991.",
    "G. C. Necula, \"Proof-carrying code,\" POPL 1997.",
    "U. Hölzle, C. Chambers and D. Ungar, \"Debugging optimized code with dynamic deoptimization,\" PLDI 1992.",
    "V. Markl et al., \"Robust query processing through progressive optimization,\" SIGMOD 2004.",
    "P. Cousot, R. Cousot, M. Fähndrich and F. Logozzo, \"Automatic inference of necessary preconditions,\" VMCAI 2013.",
    "S. Ahmed, H. Gao and H. Rajan, \"Inferring data preconditions from deep learning models for trustworthy prediction in deployment,\" ICSE 2024.",
    "W. D. Clinger, \"How to read floating point numbers accurately,\" PLDI 1990.",
    "Apache Parquet file format documentation, https://parquet.apache.org/docs/",
    "pandas documentation, https://pandas.pydata.org/docs/ ; OpenCV documentation, https://docs.opencv.org/",
  ].map((ref) => step("refs")(ref)),

  // appendix
  h1("Appendix A: Grammar"),
  p("The grammar in EBNF, copied from the project README when this report is built. Every statement starts with a different keyword, so the grammar is LL(1)."),
  ...code(grammar),
  h1("Appendix B: User Instructions"),
  p("One-time setup: install Visual Studio 2022 Build Tools (Desktop development with C++), CMake 3.16 or newer, and Python 3 with pandas, NumPy, OpenCV and pyarrow (py -3 -m pip install pandas numpy opencv-python pyarrow). Open the PrepLang folder in VS Code and accept the recommended extensions."),
  step("demo")("Build the compiler: press Ctrl + Shift + B, or run the commands below."),
  step("demo")("Open a .prep file and choose Terminal, Run Task, \"PrepLang: compile and run the open .prep file\". Every phase is printed and the cleaned data appears in the output folder."),
  step("demo")("To see the plan without running anything, use the task \"PrepLang: explain the open .prep file\"."),
  step("demo")("To see which facts the program needs, add --certificate. To run the compiled program on a new file, pass table=file, for example students=new_students.csv."),
  step("demo")("To see the error messages, open a file in examples/errors and run the compile task."),
  step("demo")("To run the tests, choose the task \"PrepLang: run all tests\"."),
  ...code([
    'cmake -S . -B build -G "Visual Studio 17 2022"',
    "cmake --build build --config Release",
    "build\\Release\\preplang.exe examples\\tabular\\students.prep --run",
    "build\\Release\\preplang.exe examples\\tabular\\students.prep --certificate",
    "py -3 output\\generated.py students=new_students.csv",
    "ctest --test-dir build -C Release",
  ]),
  p("The full source code, the evaluation scripts and the research paper draft are in the project repository at github.com/Nikhi00718/preLang."),
];

const headingStyle = (id, name, size) => ({
  id, name, basedOn: "Normal", next: "Normal", quickFormat: true,
  run: { font: FONT, size, bold: true, color: BLACK },
  paragraph: { outlineLevel: id === "Heading1" ? 0 : 1 },
});
const numbered = (reference, indent = 360) => ({ reference, levels: [{ level: 0, format: LevelFormat.DECIMAL,
  text: reference === "refs" ? "[%1]" : "%1.", alignment: AlignmentType.LEFT,
  style: { paragraph: { indent: { left: 540, hanging: indent } }, run: { color: BLACK } } }] });

const doc = new Document({
  creator: "V.Nikhil",
  title: "PrepLang Phase 3 Final Report",
  styles: {
    default: { document: { run: { font: FONT, size: BODY, color: BLACK } } },
    paragraphStyles: [headingStyle("Heading1", "Heading 1", H1), headingStyle("Heading2", "Heading 2", H2)],
  },
  numbering: { config: [
    { reference: "bullets", levels: [{ level: 0, format: LevelFormat.BULLET, text: "•", alignment: AlignmentType.LEFT,
      style: { paragraph: { indent: { left: 540, hanging: 270 } }, run: { color: BLACK } } }] },
    numbered("method"), numbered("demo"), numbered("refs", 540),
  ] },
  sections: [{
    properties: { page: { margin: { top: 1440, bottom: 1440, left: 1440, right: 1440 } } },
    children: [...front, ...body],
  }],
});

Packer.toBuffer(doc).then((buf) => {
  fs.writeFileSync(process.argv[2], buf);
  console.log("wrote", process.argv[2]);
});
