#pragma once

#include "ast/AST.h"
#include "ir/IR.h"
#include "schema/SchemaProvider.h"
#include "sema/AnalysisResult.h"

#include <filesystem>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

// Data certificates.
//
// The compiler proves contracts and removes operations using facts about the
// input files. Without a certificate the generated program can only trust those
// proofs for the exact files it was compiled with (the fingerprint guard), so a
// new batch of data with the same layout would need a new compilation.
//
// The certificate is the part of those facts that the proofs really need. It is
// found by *counterfactual re-analysis*: the semantic analysis is run again with
// a weaker fact about one column (missing values allowed, no lower bound, a
// lower bound that is only a constant of the program, ...) or about one image
// folder (another channel count, a wider size range, broken files allowed), and
// the weaker fact is kept if every proof and every removed operation of the
// program survives. Because every transfer function of the analysis is
// monotone, any input whose facts are inside the certified ones gets at least
// the same proofs, so the compiled program is correct for it unchanged.
//
// The generated program admits an input that differs from the compiled one if
// it satisfies the certificate; contracts that were proven by reading the
// values themselves (UNIQUE, REFERENCES, MATCH_RATE) are then checked again.

struct ColumnFacts {
    std::string name;
    ColumnType type = ColumnType::String;  // always required: the generated code reads the column with this type
    bool notNull = false;
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
    // The proofs that need each fact, e.g. "line 5: EXPECT students.marks BETWEEN 0 AND 100"
    std::vector<std::string> notNullFor, minFor, maxFor;
};

struct TableCertificate {
    std::string key;   // table key: "students", or "company.staff" in a TABLESET
    std::string file;  // the file it was compiled with
    std::vector<ColumnFacts> columns;  // every column the program reads
};

// What every image with one file extension must be. A folder is described by a
// box per extension rather than by its files, so any number of new files fits.
struct ImageGroupFacts {
    std::string extension;      // "jpg" (also .jpeg) or "png"
    std::vector<int> channels;  // channel counts allowed (1, 3, 4)
    int minWidth = 1, maxWidth = 65535, minHeight = 1, maxHeight = 65535;
    std::vector<std::string> channelsFor, minWidthFor, maxWidthFor, minHeightFor, maxHeightFor;
};

struct ImageCertificate {
    std::string dataset;
    std::string root;           // the folder it was compiled with
    bool fixed = false;         // no box keeps the proofs: the folder must stay exactly as compiled
    std::vector<std::string> fixedFor;
    std::vector<int> depths;    // 8 and/or 16 bits, exactly as compiled (they decide the pixel type)
    bool brokenAllowed = false; // files whose header cannot be read
    std::vector<std::string> brokenFor;
    std::vector<ImageGroupFacts> groups;
};

struct Certificate {
    std::vector<TableCertificate> tables;
    std::vector<ImageCertificate> images;
    std::vector<std::string> recheckedIfChanged;  // contracts proven from the values
    int analysisRuns = 0;                         // how many times the program was analysed again
    bool exactOnly = false;                       // no certificate: only the compiled files are accepted

    const TableCertificate* table(const std::string& key) const;
    const ImageCertificate* image(const std::string& dataset) const;
};

class Certifier {
public:
    // `schemas` must be the provider of the first analysis: it keeps the image headers it read.
    Certifier(const Program& program, SchemaProvider& schemas, std::filesystem::path baseFolder);

    // `optimized` gives the columns each LOAD reads; `constants` are the numbers in
    // the program (programConstants of the IR before optimization), the only places
    // where widening a range can change a decision of the analysis.
    Certificate certify(const IRProgram& optimized, const AnalysisResult& analysis, const std::vector<double>& constants);

private:
    // What a run of the analysis proved: proven contracts and removed operations,
    // each with a description. Proofs that read the values are left out: they are
    // checked again at run time whenever an input changed.
    struct Proofs {
        std::string error;  // the first error, e.g. "line 3: 1 of 4 files ... cannot be decoded"; empty if none
        std::map<std::string, std::string> items;  // stable key -> description
    };

    const Program& program_;
    SchemaProvider& schemas_;
    std::filesystem::path baseFolder_;
    std::map<std::string, TableSchema> facts_;          // table override key -> the facts the next run uses
    std::map<std::string, ImageSetSchema> imageFacts_;  // image key -> the files the next run uses
    int runs_ = 0;

    Proofs analyse();
    static Proofs proofsOf(const AnalysisResult& result, const std::string& error);
    // The proofs of `base` that `run` lost; empty if it kept all of them.
    static std::vector<std::string> lost(const Proofs& base, const Proofs& run);

    void certifyTable(const Instruction& load, const IRProgram& optimized, const Proofs& base,
                      const std::vector<double>& constants, Certificate& certificate);
    void certifyImages(const std::string& dataset, const ImageSetSchema& schema, const Proofs& base,
                       const std::vector<int>& sizes, Certificate& certificate);
    std::vector<int> imageConstants() const;
};

// The files that stand for every folder inside the certificate: for each
// extension, every combination of the extreme widths and heights, the allowed
// channel counts and the bit depths, plus one broken file if broken files are
// allowed. Every image decision of the analysis is "all files ..." or "some
// file ...", over one property or a combination of independent ones, so these
// corner files decide it exactly as any folder inside the box would.
ImageSetSchema cornerImages(const ImageCertificate& certificate, const ImageSetSchema& original);

// Every number in the program: BETWEEN bounds, FILTER constants and FILL_NULL values.
std::vector<double> programConstants(const IRProgram& program);

// Why an input with these facts may not be given to the compiled program; empty if
// the certificate admits it. Used by the tests; the generated program checks the same.
std::vector<std::string> admissionProblems(const TableCertificate& certificate, const TableSchema& input);
std::vector<std::string> admissionProblems(const ImageCertificate& certificate, const ImageSetSchema& input);

void printCertificate(std::ostream& out, const Certificate& certificate);

// "jpg" for .jpg and .jpeg files, "png" for .png files
std::string imageExtension(const std::string& path);
