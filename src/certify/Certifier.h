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
// lower bound that is only a constant of the program, ...), and the weaker fact
// is kept if every proof and every removed operation of the program survives.
// Because every transfer function of the analysis is monotone, any input whose
// facts are inside the certified ones gets at least the same proofs, so the
// compiled program is correct for it unchanged.
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

struct Certificate {
    std::vector<TableCertificate> tables;
    std::vector<std::string> recheckedIfChanged;  // contracts proven from the values
    std::vector<std::string> fixedImageFolders;   // image inputs must stay exactly as compiled
    int analysisRuns = 0;                         // how many times the program was analysed again
    bool exactOnly = false;                       // no certificate: only the compiled files are accepted

    const TableCertificate* table(const std::string& key) const;
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
        bool failed = false;
        std::map<std::string, std::string> items;  // stable key -> description
    };

    const Program& program_;
    SchemaProvider& schemas_;
    std::filesystem::path baseFolder_;
    std::map<std::string, TableSchema> facts_;  // override key -> the facts the next run uses
    int runs_ = 0;

    Proofs analyse();
    static Proofs proofsOf(const AnalysisResult& result, bool failed);
    // The proofs of `base` that `run` lost; empty if it kept all of them.
    static std::vector<std::string> lost(const Proofs& base, const Proofs& run);
};

// Every number in the program: BETWEEN bounds, FILTER constants and FILL_NULL values.
std::vector<double> programConstants(const IRProgram& program);

// Why an input with these facts may not be given to the compiled program; empty if
// the certificate admits it. Used by the tests; the generated program checks the same.
std::vector<std::string> admissionProblems(const TableCertificate& certificate, const TableSchema& input);

void printCertificate(std::ostream& out, const Certificate& certificate);
