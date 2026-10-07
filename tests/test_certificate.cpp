// Unit tests for data certificates: which facts are certified, why, and the
// soundness property: every input the certificate admits, analysed on its own,
// gives at least the proofs and removed operations of the compiled program.
// Run: build/Debug/test_certificate.exe  (it writes small CSV files in the current folder)

#include "certify/Certifier.h"
#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
    Certificate certificate;
};

// With `images`, image folders are replaced by these files (by SchemaProvider::imageKey)
// and only the analysis runs.
void compile(const std::string& source, Compiled& c, const std::map<std::string, ImageSetSchema>& images = {}) {
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    schemas.setImageOverrides(images);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (c.diagnostics.hasErrors() || !images.empty()) {
        return;
    }
    c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
    const std::vector<double> constants = programConstants(c.ir);
    PassManager({true, false}).run(c.ir);
    c.certificate = Certifier(c.program, schemas, ".").certify(c.ir, c.analysis, constants);
}

// What a compilation proved, comparable between two compilations of the same
// program text: proven contracts, and removed operations by line and target.
std::set<std::string> proofs(const Compiled& c) {
    std::set<std::string> items;
    for (const ContractRecord& contract : c.analysis.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues) {
            items.insert(std::to_string(contract.line) + " " + contract.contract);
        }
    }
    for (const auto& [key, reason] : c.analysis.noEffect) {
        items.insert(std::to_string(key.first->location.line) + " removed " + key.second);
    }
    return items;
}

void writeFile(const std::string& name, const std::string& text) {
    std::ofstream(name, std::ios::binary) << text;
}

const ColumnFacts* fact(const Certificate& certificate, const std::string& table, const std::string& column) {
    const TableCertificate* found = certificate.table(table);
    if (!found) {
        return nullptr;
    }
    for (const ColumnFacts& facts : found->columns) {
        if (facts.name == column) {
            return &facts;
        }
    }
    return nullptr;
}

// marks: INTEGER in [35, 95], no missing values; age: one missing value; city: text.
const char* const students = "name,age,marks,city\n"
                             "Asha,20,85,Chennai\n"
                             "Ravi,,35,Pune\n"
                             "Mina,22,95,Delhi\n"
                             "Joe,21,60,Pune\n";

const std::string program = "LOAD \"test_certificate_students.csv\" AS s;\n"   // line 1
                            "EXPECT s.marks BETWEEN 0 AND 100;\n"  // line 2: proven, needs 0 <= marks <= 100
                            "EXPECT s.name NOT_NULL;\n"            // line 3: proven, needs name without missing values
                            "FILL_NULL s.marks WITH 0;\n"          // line 4: removed, needs marks without missing values
                            "FILTER s WHERE marks >= 10;\n"        // line 5: removed (always true), needs marks >= 10
                            "SAVE s AS \"out.csv\";\n";

void testFacts() {
    std::cout << "Certified facts\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile(program, c);
    check(!c.diagnostics.hasErrors() && c.certificate.tables.size() == 1, "compiles, one table certificate");
    const ColumnFacts* marks = fact(c.certificate, "s", "marks");
    check(marks && marks->min == 10 && marks->max == 100,
          "marks is widened from the data's [35, 95] to [10, 100], the program's constants");
    const auto mentions = [](const std::vector<std::string>& proofs, const std::string& text) {
        return std::any_of(proofs.begin(), proofs.end(),
                           [&](const std::string& proof) { return proof.find(text) != std::string::npos; });
    };
    check(marks && marks->notNull && marks->notNullFor.size() == 2 && mentions(marks->notNullFor, "line 4") &&
              mentions(marks->notNullFor, "line 5"),
          "marks must have no missing values: the removed FILL_NULL (line 4) and FILTER (line 5) need it");
    check(marks && mentions(marks->minFor, "line 5") && mentions(marks->maxFor, "line 2") &&
              !mentions(marks->maxFor, "line 5"),
          "marks >= 10 is needed by the FILTER at line 5, marks <= 100 only by the contract at line 2");
    const ColumnFacts* name = fact(c.certificate, "s", "name");
    check(name && name->notNull && name->notNullFor == std::vector<std::string>{"line 3: EXPECT s.name NOT_NULL"},
          "name must have no missing values, for the contract at line 3");
    const ColumnFacts* city = fact(c.certificate, "s", "city");
    check(city && !city->notNull && city->type == ColumnType::String, "city: any values, only its type");
    const ColumnFacts* age = fact(c.certificate, "s", "age");
    check(age && !age->notNull && std::isinf(age->min) && std::isinf(age->max),
          "age: no proof depends on its values, so no bounds");
}

void testAdmission() {
    std::cout << "Admission of other inputs\n";
    writeFile("test_certificate_students.csv", students);
    Compiled c;
    compile(program, c);
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    const auto problems = [&](const std::string& text) {
        writeFile("test_certificate_batch.csv", text);
        return admissionProblems(*c.certificate.table("s"), *schemas.readTable("test_certificate_batch.csv", "s", {}));
    };
    check(problems("name,age,marks,city\nZoe,,100,\nLi,,10,Goa\n").empty(),
          "new rows with marks 10 and 100 and missing ages and cities are admitted");
    const std::vector<std::string> high = problems("name,age,marks,city\nZoe,30,101,Goa\n");
    check(high.size() == 1 && high[0].find("line 2") != std::string::npos, "marks 101 is refused, naming line 2");
    check(problems("name,age,marks,city\n,30,50,Goa\n").size() == 1, "a missing name is refused");
    check(problems("name,age,marks,city\nZoe,30,50.5,Goa\n").size() == 1, "FLOAT marks are refused (INTEGER certified)");
    check(problems("name,age,city\nZoe,30,Goa\n").size() == 1, "a missing column is refused");
}

void testValueProofs() {
    std::cout << "Proofs that read the values\n";
    writeFile("test_certificate_keys.csv", "id,city\n1,Pune\n2,Goa\n3,Pune\n");
    Compiled c;
    compile("LOAD \"test_certificate_keys.csv\" AS k;\nEXPECT k.id UNIQUE;\nSAVE k AS \"out.csv\";\n", c);
    const auto check = std::find_if(c.ir.code.begin(), c.ir.code.end(),
                                    [](const Instruction& in) { return in.op == Opcode::Check; });
    ::check(check != c.ir.code.end() && check->onlyIfInputChanged && check->provenFromValuesOf ==
                                                                          std::vector<std::string>{"k"},
            "UNIQUE proven from the values keeps a check that runs only if k changes");
    ::check(c.certificate.recheckedIfChanged.size() == 1, "the certificate lists it as checked again");
}

// The soundness property, on random inputs: if the certificate admits an input,
// compiling the same program for that input proves at least as much.
void testSoundness() {
    std::cout << "Soundness on 400 random inputs\n";
    writeFile("test_certificate_students.csv", students);
    Compiled base;
    compile(program, base);
    const std::set<std::string> baseProofs = proofs(base);
    const std::string batchProgram = [] {
        std::string text = program;
        const std::string from = "test_certificate_students.csv";
        text.replace(text.find(from), from.size(), "test_certificate_random.csv");
        return text;
    }();

    std::mt19937 random(2026);
    int admitted = 0, unsound = 0, outsideData = 0;
    for (int round = 0; round < 400; ++round) {
        std::string text = "name,age,marks,city\n";
        const int rows = 1 + static_cast<int>(random() % 6);
        for (int i = 0; i < rows; ++i) {
            const bool noName = random() % 25 == 0;
            const bool noMarks = random() % 25 == 0;
            text += (noName ? "" : "N" + std::to_string(i)) + "," + (random() % 3 ? std::to_string(18 + random() % 10) : "") +
                    "," + (noMarks ? "" : std::to_string(static_cast<int>(random() % 115) - 5)) + ",C\n";
        }
        writeFile("test_certificate_random.csv", text);
        Diagnostics diagnostics;
        SchemaProvider schemas(diagnostics);
        const TableSchema facts = *schemas.readTable("test_certificate_random.csv", "s", {});
        if (!admissionProblems(*base.certificate.table("s"), facts).empty()) {
            continue;
        }
        ++admitted;
        const ColumnInfo& marks = facts.columns[2];
        outsideData += marks.min < 35 || marks.max > 95;  // the compiled file's facts would refuse it
        Compiled again;
        compile(batchProgram, again);
        const std::set<std::string> found = proofs(again);
        const bool keepsAll = !again.diagnostics.hasErrors() &&
                              std::includes(found.begin(), found.end(), baseProofs.begin(), baseProofs.end());
        unsound += !keepsAll;
    }
    check(admitted >= 40, std::to_string(admitted) + " of 400 random inputs admitted");
    check(outsideData > 0, std::to_string(outsideData) + " admitted inputs lie outside the compiled file's own ranges");
    check(unsound == 0, "every admitted input keeps every proof (" + std::to_string(unsound) + " did not)");
}

// ---------- Image folders ----------

std::string bigEndian(unsigned value, int bytes) {
    std::string text;
    for (int i = bytes - 1; i >= 0; --i) {
        text += static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return text;
}

// Only the header is read at compile time: PNG signature + IHDR (color type 0 gray, 2 RGB).
void writePng(const std::string& path, unsigned width, unsigned height, char colorType) {
    std::string bytes = "\x89PNG\r\n\x1A";
    bytes += "\n" + bigEndian(13, 4) + "IHDR" + bigEndian(width, 4) + bigEndian(height, 4);
    bytes += std::string{8, colorType, 0, 0, 0} + bigEndian(0, 4);
    writeFile(path, bytes);
}

void writeJpeg(const std::string& path, unsigned width, unsigned height) {
    writeFile(path, "\xFF\xD8\xFF\xC0" + bigEndian(17, 2) + std::string(1, 8) + bigEndian(height, 2) +
                        bigEndian(width, 2) + std::string(1, 3) + std::string(9, '\x01') + "\xFF\xD9");
}

// Color images; the contract needs width >= 100, GRAYSCALE needs readable files.
const std::string colorProgram = "LOAD_IMAGES \"test_certificate_color/\" AS imgs;\n"  // line 1
                                 "EXPECT imgs MIN_WIDTH 100;\n"                         // line 2
                                 "GRAYSCALE imgs;\n"                                    // line 3
                                 "EXPECT imgs CHANNELS 1;\n"                            // line 4
                                 "RESIZE imgs TO 64 64;\n"                              // line 5
                                 "RESIZE imgs TO 64 64;\n"                              // line 6: removed
                                 "CONVERT imgs TO JPG;\n"
                                 "SAVE_IMAGES imgs TO \"out/\";\n";

// Gray 64x64 images: GRAYSCALE and RESIZE are removed, so the shapes matter.
const std::string grayProgram = "LOAD_IMAGES \"test_certificate_gray/\" AS g;\n"  // line 1
                                "GRAYSCALE g;\n"                                   // line 2: removed
                                "RESIZE g TO 64 64;\n"                             // line 3: removed
                                "CONVERT g TO PNG;\n"
                                "SAVE_IMAGES g TO \"out2/\";\n";

void writeImageFolders() {
    std::filesystem::create_directories("test_certificate_color");
    writePng("test_certificate_color/a.png", 120, 90, 2);
    writePng("test_certificate_color/b.png", 200, 150, 2);
    writeJpeg("test_certificate_color/c.jpg", 160, 120);
    std::filesystem::create_directories("test_certificate_gray");
    writePng("test_certificate_gray/a.png", 64, 64, 0);
    writePng("test_certificate_gray/b.png", 64, 64, 0);
}

bool mentions(const std::vector<std::string>& proofs, const std::string& text) {
    return std::any_of(proofs.begin(), proofs.end(),
                       [&](const std::string& proof) { return proof.find(text) != std::string::npos; });
}

void testImageFacts() {
    std::cout << "Certified facts of image folders\n";
    writeImageFolders();
    Compiled color;
    compile(colorProgram, color);
    const ImageCertificate* imgs = color.certificate.image("imgs");
    check(!color.diagnostics.hasErrors() && imgs && !imgs->fixed && imgs->groups.size() == 2,
          "the color folder is certified with one box per extension (jpg, png)");
    check(imgs && !imgs->brokenAllowed && mentions(imgs->brokenFor, "line 3"),
          "broken files are not allowed: GRAYSCALE at line 3 must decode every file");
    check(imgs && std::all_of(imgs->groups.begin(), imgs->groups.end(), [](const ImageGroupFacts& g) {
              return g.channels == std::vector<int>{1, 3, 4} && g.minWidth == 100 && mentions(g.minWidthFor, "line 2") &&
                     g.maxWidth == 65535 && g.minHeight == 1 && g.maxHeight == 65535;
          }),
          "any channels and heights; width >= 100 for MIN_WIDTH 100 at line 2 (the data had 120 to 200)");

    Compiled gray;
    compile(grayProgram, gray);
    const ImageCertificate* g = gray.certificate.image("g");
    check(g && g->groups.size() == 1 && g->groups[0].channels == std::vector<int>{1} &&
              mentions(g->groups[0].channelsFor, "line 2"),
          "gray folder: only 1-channel images, because the removed GRAYSCALE at line 2 needs it");
    check(g && g->groups[0].minWidth == 64 && g->groups[0].maxWidth == 64 && g->groups[0].minHeight == 64 &&
              g->groups[0].maxHeight == 64 && mentions(g->groups[0].maxWidthFor, "line 3"),
          "gray folder: exactly 64x64, because the removed RESIZE at line 3 needs it");

    std::vector<ImageInfo> files(2);
    files[0] = {"x.png", "PNG", 64, 64, 1, 8, false, "", 0};
    files[1] = {"y.png", "PNG", 64, 64, 3, 8, false, "", 0};
    ImageSetSchema input;
    input.images = files;
    check(g && admissionProblems(*g, input).size() == 1, "a 3-channel image is refused for the gray program");
    input.images.pop_back();
    check(g && admissionProblems(*g, input).empty(), "another 1-channel 64x64 folder is admitted");
}

// The image soundness property: any folder the box admits keeps every proof.
// This also checks that the corner images stand for every folder in the box.
void testImageSoundness() {
    std::cout << "Soundness on 600 random image folders\n";
    writeImageFolders();
    std::mt19937 random(7);
    for (const std::string& source : {colorProgram, grayProgram}) {
        const bool gray = source == grayProgram;  // gray folders need 1-channel 64x64 PNG files
        Compiled base;
        compile(source, base);
        const std::set<std::string> baseProofs = proofs(base);
        const ImageCertificate& certificate = base.certificate.images.front();
        const ImageSetSchema& compiled = base.ir.images.begin()->second;
        const std::string key = SchemaProvider::imageKey(compiled.root, false, false);
        const int sizes[] = {16, 63, 64, 65, 99, 100, 150, 400};
        int admitted = 0, unsound = 0;
        for (int round = 0; round < 300; ++round) {
            ImageSetSchema folder = compiled;
            folder.namesKnown = false;
            folder.images.clear();
            const int count = 1 + static_cast<int>(random() % 5);
            for (int i = 0; i < count; ++i) {
                ImageInfo file;
                const bool png = gray || random() % 2 == 0;
                file.path = "f" + std::to_string(i) + (png ? ".png" : ".jpg");
                file.format = png ? "PNG" : "JPG";
                file.width = gray && random() % 4 ? 64 : sizes[random() % 8];
                file.height = gray && random() % 4 ? 64 : sizes[random() % 8];
                file.channels = gray && random() % 4 ? 1 : std::vector<int>{1, 3, 4}[random() % 3];
                file.corrupt = random() % 12 == 0;
                folder.images.push_back(file);
            }
            if (!admissionProblems(certificate, folder).empty()) {
                continue;
            }
            ++admitted;
            Compiled again;
            compile(source, again, {{key, folder}});
            const std::set<std::string> found = proofs(again);
            unsound += again.diagnostics.hasErrors() ||
                       !std::includes(found.begin(), found.end(), baseProofs.begin(), baseProofs.end());
        }
        check(admitted >= 20 && unsound == 0, std::to_string(admitted) + " of 300 admitted folders keep every proof (" +
                                                  std::to_string(unsound) + " did not)");
    }
}

}  // namespace

int main() {
    std::cout << std::unitbuf;  // keep the output if a test crashes
    testFacts();
    testAdmission();
    testValueProofs();
    testSoundness();
    testImageFacts();
    testImageSoundness();
    std::cout << (failures == 0 ? "All certificate tests passed\n"
                                : std::to_string(failures) + " certificate test(s) FAILED\n");
    return failures == 0 ? 0 : 1;
}
