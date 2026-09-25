// Unit tests for IMAGE and IMAGESET: header profiling, shape tracking, image
// contracts, pixel-type checks, LINK, and the image optimizations.
// Run: build/Debug/test_imageset.exe  (it writes a small folder of image files)
//
// The compiler only reads file headers, so the test images are just headers.

#include "backend/ExecutionPlanner.h"
#include "common/Diagnostics.h"
#include "ir/IRBuilder.h"
#include "lexer/Lexer.h"
#include "optimizer/PassManager.h"
#include "parser/Parser.h"
#include "schema/SchemaProvider.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool passed, const std::string& name) {
    std::cout << (passed ? "  PASS  " : "  FAIL  ") << name << '\n';
    if (!passed) {
        ++failures;
    }
}

std::string bigEndian(unsigned value, int bytes) {
    std::string text;
    for (int i = bytes - 1; i >= 0; --i) {
        text += static_cast<char>((value >> (8 * i)) & 0xFF);
    }
    return text;
}

// PNG signature + IHDR chunk. Color type 0 = gray, 2 = RGB, 6 = RGBA.
void writePng(const std::string& path, unsigned width, unsigned height, char colorType) {
    std::string bytes = "\x89PNG\r\n\x1A";
    bytes += "\n" + bigEndian(13, 4) + "IHDR" + bigEndian(width, 4) + bigEndian(height, 4);
    bytes += std::string{8, colorType, 0, 0, 0} + bigEndian(0, 4);
    std::ofstream(path, std::ios::binary) << bytes;
}

// JPEG start marker + baseline frame header (SOF0) with 3 components.
void writeJpeg(const std::string& path, unsigned width, unsigned height) {
    std::string bytes = "\xFF\xD8\xFF\xC0" + bigEndian(17, 2) + std::string(1, 8) + bigEndian(height, 2) +
                        bigEndian(width, 2) + std::string(1, 3) + std::string(9, '\x01') + "\xFF\xD9";
    std::ofstream(path, std::ios::binary) << bytes;
}

void writeImages() {
    std::filesystem::create_directories("test_imageset_data/a");
    writePng("test_imageset_data/a/one.png", 64, 48, 2);
    writeJpeg("test_imageset_data/a/two.jpg", 100, 80);
    writePng("test_imageset_data/gray.png", 50, 50, 0);
    writePng("test_imageset_data/rgba.png", 40, 40, 6);
    std::ofstream("test_imageset_data/bad.jpg") << "hello";
    std::ofstream("test_imageset_labels.csv") << "filename,label\na/one.png,x\na/two.jpg,y\ngray.png,x\nrgba.png,y\n";
    std::ofstream("test_imageset_partial.csv") << "filename,label\na/one.png,x\ngray.png,x\nmissing.png,y\n";
}

struct Compiled {
    Diagnostics diagnostics;
    SymbolTable symbols;
    Program program;
    AnalysisResult analysis;
    IRProgram ir;
    OptimizationReport report;
};

// Compiles `body`, placed after  LOAD_IMAGES "test_imageset_data/" AS imgs RECURSIVE;
void compile(const std::string& body, Compiled& c, OptimizerOptions options = {true, false}) {
    const std::string source = "LOAD_IMAGES \"test_imageset_data/\" AS imgs RECURSIVE;\n" + body;
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SchemaProvider schemas(c.diagnostics);
    c.analysis = SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    if (!c.diagnostics.hasErrors()) {
        c.ir = IRBuilder(c.symbols, c.analysis).build(c.program);
        c.report = PassManager(options).run(c.ir);
    }
}

const Diagnostic* reported(const Compiled& c, Phase phase, const std::string& text) {
    for (const Diagnostic& d : c.diagnostics.all()) {
        if (d.severity == Severity::Error && d.phase == phase && d.message.find(text) != std::string::npos) {
            return &d;
        }
    }
    return nullptr;
}

int countOf(const IRProgram& ir, Opcode op) {
    return static_cast<int>(std::count_if(ir.code.begin(), ir.code.end(),
                                          [op](const Instruction& instruction) { return instruction.op == op; }));
}

const ImageShape* shape(const Compiled& c, const std::string& path) {
    for (const ImageShape& s : c.symbols.lookup("imgs")->images.shapes) {
        if (s.path == path) {
            return &s;
        }
    }
    return nullptr;
}

const std::string save = "\nSAVE_IMAGES imgs TO \"out/\";";

void testHeaders() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;" + save, c);
    const ImageSetSchema& schema = c.symbols.lookup("imgs")->imageSchema;
    const auto corrupt = std::count_if(schema.images.begin(), schema.images.end(), [](const ImageInfo& i) { return i.corrupt; });
    const ImageShape* one = shape(c, "a/one.png");
    const ImageShape* two = shape(c, "a/two.jpg");
    check(!c.diagnostics.hasErrors() && schema.images.size() == 5 && corrupt == 1,
          "5 files are profiled and bad.jpg is found to be broken");
    check(one && one->width == 64 && one->height == 48 && one->channels == 3 && two && two->width == 100 &&
              two->height == 80 && two->channels == 3 && shape(c, "gray.png")->channels == 1 &&
              shape(c, "rgba.png")->channels == 4,
          "width, height and channels come from the PNG and JPEG headers");
}

void testNotRecursive() {
    Diagnostics diagnostics;
    const Program program =
        Parser(Lexer("LOAD_IMAGES \"test_imageset_data/\" AS imgs;", diagnostics).tokenize(), diagnostics).parse();
    SymbolTable symbols;
    SchemaProvider schemas(diagnostics);
    SemanticAnalyzer(symbols, schemas, ".", diagnostics).analyze(program);
    check(symbols.lookup("imgs")->imageSchema.images.size() == 3, "without RECURSIVE sub-folders are not read");
}

void testBrokenFilesBlockPixelOperations() {
    Compiled c;
    compile("RESIZE imgs TO 32 32;" + save, c);
    const Diagnostic* error = reported(c, Phase::Schema, "cannot be decoded");
    check(error && !error->notes.empty() && error->notes.front().find("bad.jpg") != std::string::npos,
          "a pixel operation on a set with broken files needs REMOVE_CORRUPT first");
}

void testKindErrors() {
    Compiled c;
    compile("DROP_NULL imgs;" + save, c);
    check(reported(c, Phase::Type, "expects a TABLE or TABLESET, but 'imgs' is an IMAGESET") != nullptr,
          "a table operation on images is a type error");
}

void testShapeTracking() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nRESIZE imgs TO 32 32;\nEXPECT imgs MIN_WIDTH 32;\nGRAYSCALE imgs;\n"
            "EXPECT imgs CHANNELS 1;" + save, c);
    check(!c.diagnostics.hasErrors() && c.analysis.contracts.size() == 2 &&
              std::all_of(c.analysis.contracts.begin(), c.analysis.contracts.end(),
                          [](const ContractRecord& r) { return r.verdict == Verdict::Proven; }),
          "contracts after RESIZE and GRAYSCALE are proven from the tracked shapes");
}

void testChannelsViolated() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nEXPECT imgs CHANNELS 3;" + save, c);
    const Diagnostic* error = reported(c, Phase::Contract, "2 of 4 images");
    check(error != nullptr, "CHANNELS 3 fails for the gray and the RGBA image");
}

void testFloatPixelsNeedNpy() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nNORMALIZE imgs RANGE 0 1;\nCONVERT imgs TO PNG;" + save, c);
    check(reported(c, Phase::Type, "NORMALIZE at line 3") != nullptr,
          "saving FLOAT32 pixels as PNG is an error that points at the NORMALIZE");

    Compiled ok;
    compile("REMOVE_CORRUPT imgs;\nNORMALIZE imgs RANGE 0 1;\nCONVERT imgs TO NPY;" + save, ok);
    const auto normalize = std::find_if(ok.ir.code.begin(), ok.ir.code.end(),
                                        [](const Instruction& i) { return i.op == Opcode::NormalizeImages; });
    check(!ok.diagnostics.hasErrors() && normalize != ok.ir.code.end() && normalize->values.size() == 4 &&
              normalize->values[3].value == "255",
          "NPY keeps float pixels; the IR carries the known source range 0..255");
}

void testAlphaNeedsPng() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nCONVERT imgs TO JPG;" + save, c);
    check(reported(c, Phase::Type, "4 channels") != nullptr, "JPG cannot store the RGBA image");

    Compiled ok;
    compile("REMOVE_CORRUPT imgs;\nGRAYSCALE imgs;\nCONVERT imgs TO JPG;" + save, ok);
    check(!ok.diagnostics.hasErrors(), "after GRAYSCALE the images fit in JPG");
}

void testNoEffectOperations() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nGRAYSCALE imgs;\nGRAYSCALE imgs;\nRESIZE imgs TO 16 16;\nRESIZE imgs TO 16 16;\n"
            "CONVERT imgs TO PNG;\nCONVERT imgs TO PNG;" + save, c);
    check(countOf(c.ir, Opcode::Grayscale) == 1 && countOf(c.ir, Opcode::Resize) == 1 &&
              countOf(c.ir, Opcode::Convert) == 1,
          "repeating GRAYSCALE, RESIZE to the same size or CONVERT to the same format is removed");
}

void testDeadAfterLastSave() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;" + save + "\nRESIZE imgs TO 8 8;", c);
    check(countOf(c.ir, Opcode::Resize) == 0, "an image operation with no SAVE_IMAGES after it is removed");
}

void testResizeFusionIsOptIn() {
    const std::string body = "REMOVE_CORRUPT imgs;\nRESIZE imgs TO 64 64;\nRESIZE imgs TO 32 32;" + save;
    Compiled exact;
    compile(body, exact);
    Compiled approximate;
    compile(body, approximate, {true, true});
    check(countOf(exact.ir, Opcode::Resize) == 2 && exact.report.suggestions.size() == 1,
          "by default the resizes are kept and fusion is only suggested");
    check(countOf(approximate.ir, Opcode::Resize) == 1, "with --approx the first resize is merged away");
}

void testLinkMatchRate() {
    Compiled c;
    compile("LOAD \"test_imageset_labels.csv\" AS labels;\nREMOVE_CORRUPT imgs;\n"
            "LINK imgs WITH labels ON filename AS pairs;\nEXPECT pairs MATCH_RATE 100%;" + save +
                "\nSAVE labels AS \"labels.csv\";",
            c);
    check(!c.diagnostics.hasErrors() && c.analysis.contracts.front().verdict == Verdict::Proven,
          "every image has a label: MATCH_RATE 100% is proven");

    Compiled bad;
    compile("LOAD \"test_imageset_partial.csv\" AS labels;\nREMOVE_CORRUPT imgs;\n"
            "LINK imgs WITH labels ON filename AS pairs;\nEXPECT pairs MATCH_RATE 100%;" + save,
            bad);
    const Diagnostic* error = reported(bad, Phase::Contract, "match rate of pairs is 50%");
    check(error && error->notes.size() == 3, "the report counts matches and lists unmatched images and labels");
}

void testStreamingPlan() {
    Compiled c;
    compile("REMOVE_CORRUPT imgs;\nRESIZE imgs TO 32 32;\nGRAYSCALE imgs;" + save, c);
    const std::vector<std::string> passes = ExecutionPlanner::imagePasses(c.ir);
    check(passes.size() == 1 && passes.front().find("1 streaming pass over 5 files") != std::string::npos &&
              passes.front().find("RESIZE 32x32 -> GRAYSCALE") != std::string::npos,
          "all image operations run in one streaming pass per SAVE_IMAGES");
}

// A screenshot tool saved a transparent PNG as "shot.jpg" (github.com/cocktailpeanut/fluxgym/issues/109).
void testFormatFromContent() {
    std::filesystem::create_directories("test_imageset_misnamed");
    writePng("test_imageset_misnamed/shot.jpg", 30, 20, 6);
    Diagnostics diagnostics;
    SchemaProvider schemas(diagnostics);
    const auto set = schemas.readImages("test_imageset_misnamed", false, false, {});
    check(set && !set->images.front().corrupt && set->images.front().format == "PNG" &&
              set->images.front().channels == 4,
          "the format comes from the file's content, not its name");

    Compiled c;
    const std::string source = "LOAD_IMAGES \"test_imageset_misnamed/\" AS shots;\nSAVE_IMAGES shots TO \"out/\";";
    c.program = Parser(Lexer(source, c.diagnostics).tokenize(), c.diagnostics).parse();
    SemanticAnalyzer(c.symbols, schemas, ".", c.diagnostics).analyze(c.program);
    check(reported(c, Phase::Type, "JPG cannot store transparency"),
          "keeping the .jpg name of a transparent PNG is reported before anything runs");
}

}  // namespace

int main() {
    writeImages();

    std::cout << "IMAGESET tests\n";
    testHeaders();
    testNotRecursive();
    testBrokenFilesBlockPixelOperations();
    testKindErrors();
    testShapeTracking();
    testChannelsViolated();
    testFloatPixelsNeedNpy();
    testAlphaNeedsPng();
    testNoEffectOperations();
    testDeadAfterLastSave();
    testResizeFusionIsOptIn();
    testLinkMatchRate();
    testStreamingPlan();
    testFormatFromContent();

    std::cout << (failures == 0 ? "All IMAGESET tests passed.\n" : "Some IMAGESET tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
