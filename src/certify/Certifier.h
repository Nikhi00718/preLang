#pragma once

#include "ast/AST.h"
#include "ir/IR.h"
#include "schema/SchemaProvider.h"
#include "sema/AnalysisResult.h"

#include <filesystem>
#include <functional>
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
// A certificate is the part of those facts that the proofs really need. It is
// found by *counterfactual re-analysis*: the semantic analysis is run again with
// a weaker fact about one column (missing values allowed, no lower bound, a
// lower bound that is only a constant of the program, ...) or about one image
// folder (another channel count, a wider size range, broken files allowed), and
// the weaker fact is kept if the proofs that matter survive. Because every
// transfer function of the analysis is monotone, any input whose facts are
// inside the certified ones gets at least the same proofs.
//
// Three certificates are found, one after the other, each starting from the
// facts of the one before:
//   * the full certificate keeps every proof and every removed operation. An
//     input inside it runs the optimized program unchanged.
//   * the admission certificate keeps only what no run-time code can replace:
//     the absence of compile errors, and contracts that have no run-time check
//     (column types, image contracts). An input outside it is refused.
//   * one item certificate per speculatively removed check or operation keeps
//     that one removal. For an admitted input that breaks it, the generated
//     program runs that instruction after all (deoptimization); every other
//     removal stays justified, because the analysis of that input, which is
//     below the item certificates it satisfies, still proves it.
// Without speculative removals (--no-deopt) the admission certificate is the
// full one, as in earlier versions.
//
// Contracts that were proven by reading the values themselves (UNIQUE,
// REFERENCES, MATCH_RATE) are checked again whenever an input changed.

struct ColumnFacts {
    std::string name;
    bool read = true;                      // the optimized program loads it; otherwise it is loaded only to
                                           // check it, because a proof depends on it
    ColumnType type = ColumnType::String;
    bool typeFixed = true;                 // the type is required: always for a column the program reads
    std::vector<std::string> typeFor;      // for a column it does not read: the proofs that need the type
    bool required = true;                  // the column must exist; a column no instruction names, which is
    std::vector<std::string> requiredFor;  // only carried to the output, may be missing if no proof needs it
    bool notNull = false;
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
    bool minStrict = false;  // values must be > min (for FILTER c > k), not >= min
    bool maxStrict = false;  // values must be < max
    // The proofs that need each fact, e.g. "line 5: EXPECT students.marks BETWEEN 0 AND 100"
    std::vector<std::string> notNullFor, minFor, maxFor;
};

struct TableCertificate {
    std::string key;   // table key: "students", or "company.staff" in a TABLESET
    std::string file;  // the file it was compiled with
    std::vector<ColumnFacts> columns;  // every column the program reads, and every other one a proof needs
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

// The facts one speculatively removed instruction needs beyond the admission
// certificate. Only value facts appear: missing values and bounds of columns
// (type and presence are part of admission), and channels, sizes and broken files
// of image folders. Columns and groups without such a fact are left out.
struct ItemCertificate {
    std::string key;          // proofKey of the instructions it guards
    std::string description;  // "line 5: EXPECT students.marks BETWEEN 0 AND 100"
    std::vector<TableCertificate> tables;
    std::vector<ImageCertificate> images;
    // Nothing beyond admission: every admitted input keeps the removal, so the
    // instruction is removed outright.
    bool empty() const { return tables.empty() && images.empty(); }
};

struct Certificate {
    std::vector<TableCertificate> tables;  // admission: an input outside is refused
    std::vector<ImageCertificate> images;
    std::vector<ItemCertificate> items;    // one per speculatively removed instruction (proofKey)
    std::vector<TableCertificate> fullTables;  // every proof, i.e. the certificate without deoptimization
    std::vector<ImageCertificate> fullImages;
    std::vector<std::string> recheckedIfChanged;  // contracts proven from the values
    // What every table the program writes is known to satisfy, for any admitted
    // input (the analysis run on the admission certificate): the program's guarantee
    // to the next stage of a pipeline (see tools/contracts.py).
    std::vector<SavedTable> guarantees;
    int analysisRuns = 0;                         // how many times the program was analysed again
    bool exactOnly = false;                       // no certificate: only the compiled files are accepted

    const TableCertificate* table(const std::string& key) const;
    const ImageCertificate* image(const std::string& dataset) const;
    const ItemCertificate* item(const std::string& key) const;
};

class Certifier {
public:
    // `schemas` must be the provider of the first analysis: it keeps the image headers it read.
    Certifier(const Program& program, SchemaProvider& schemas, std::filesystem::path baseFolder);

    // `optimized` gives the columns each LOAD reads and the speculative instructions;
    // `constants` are the numbers in the program (programConstants of the IR before
    // optimization), the only places where widening a range can change a decision.
    Certificate certify(const IRProgram& optimized, const AnalysisResult& analysis, const std::vector<double>& constants);

private:
    // What a run of the analysis proved, each with a description: "contract ..."
    // for contracts without a run-time check, "removed <proofKey>" for no-effect
    // facts. Proofs that read the values are left out: they are checked again at
    // run time whenever an input changed.
    struct Proofs {
        std::string error;  // the first error, e.g. "line 3: 1 of 4 files ... cannot be decoded"; empty if none
        std::map<std::string, std::string> items;  // stable key -> description
    };
    // What a run broke that the current certificate must keep; empty if nothing.
    using Criterion = std::function<std::vector<std::string>(const Proofs&)>;

    const Program& program_;
    SchemaProvider& schemas_;
    std::filesystem::path baseFolder_;
    std::map<std::string, TableSchema> facts_;          // table override key -> the facts the next run uses
    std::map<std::string, ImageSetSchema> imageFacts_;  // image key -> the files the next run uses
    std::map<std::string, std::string> overrideOf_;     // table key -> its key in facts_
    Criterion criterion_;
    int runs_ = 0;

    Proofs analyse();
    std::vector<SavedTable> outputsNow();  // what the analysis with the current facts says the SAVEs write
    std::vector<std::string> broken() { return criterion_(analyse()); }
    static Proofs proofsOf(const AnalysisResult& result, const std::string& error);
    // The proofs of `base` that `run` lost, and the error of `run`; empty if it kept all of them.
    static std::vector<std::string> lost(const Proofs& base, const Proofs& run);

    TableCertificate certifyTable(const Instruction& load, const IRProgram& optimized,
                                  const std::vector<double>& constants);
    // Widens only the value facts of a certificate found before (missing values, bounds).
    TableCertificate widenTableValues(const TableCertificate& start, const std::vector<double>& constants);
    void widenValues(ColumnInfo& info, ColumnFacts& column, const std::vector<double>& constants);
    // From the compiled files if `start` is null, else from a certificate found before.
    ImageCertificate certifyImages(const std::string& dataset, const ImageSetSchema& schema,
                                   const ImageCertificate* start);
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
// For the tables of an item certificate it says why the removal is not justified.
std::vector<std::string> admissionProblems(const TableCertificate& certificate, const TableSchema& input);
std::vector<std::string> admissionProblems(const ImageCertificate& certificate, const ImageSetSchema& input);

void printCertificate(std::ostream& out, const Certificate& certificate);

// The certificates and guarantees as JSON (output/preplang_contract.json): what
// each input must satisfy to be admitted, what each speculative removal needs, and
// what each output satisfies. tools/contracts.py composes these between stages.
void writeContract(std::ostream& out, const Certificate& certificate, const std::string& program);

// "jpg" for .jpg and .jpeg files, "png" for .png files
std::string imageExtension(const std::string& path);
