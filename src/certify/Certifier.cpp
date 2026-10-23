#include "certify/Certifier.h"

#include "common/Dates.h"
#include "common/Diagnostics.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <set>
#include <utility>

namespace {

std::string join(const std::vector<std::string>& items) {
    std::string text;
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? "; " : "") + items[i];
    }
    return text;
}

// Adds the items of `more` that `into` does not have yet.
void addAll(std::vector<std::string>& into, const std::vector<std::string>& more) {
    for (const std::string& item : more) {
        if (std::find(into.begin(), into.end(), item) == into.end()) {
            into.push_back(item);
        }
    }
}

std::string listText(const std::vector<int>& values) {
    std::string text;
    for (size_t i = 0; i < values.size(); ++i) {
        text += (i > 0 ? ", " : "") + std::to_string(values[i]);
    }
    return text;
}

int depthOf(const ImageInfo& image) { return image.bitDepth > 8 ? 16 : 8; }

// "students.marks" for the target key "students..marks", "company.staff.age" for "company.staff.age"
std::string targetName(const std::string& key) {
    const size_t first = key.find('.');
    const size_t last = key.rfind('.');
    if (first == std::string::npos || first == last) {
        return key;
    }
    const std::string table = key.substr(first + 1, last - first - 1);
    return key.substr(0, first) + (table.empty() ? "" : "." + table) + key.substr(last);
}

// The value facts of `item` that `admission` does not already require; nullopt if none.
std::optional<ColumnFacts> beyond(const ColumnFacts& item, const ColumnFacts* admission) {
    ColumnFacts facts;
    facts.name = item.name;
    facts.read = item.read;
    facts.type = item.type;
    facts.typeFixed = false;  // type and presence are checked on admission
    facts.required = false;
    bool any = false;
    if (item.notNull && !(admission && admission->notNull)) {
        facts.notNull = true;
        facts.notNullFor = item.notNullFor;
        any = true;
    }
    if (!std::isinf(item.min) && (!admission || item.min > admission->min ||
                                  (item.min == admission->min && item.minStrict && !admission->minStrict))) {
        facts.min = item.min;
        facts.minStrict = item.minStrict;
        facts.minFor = item.minFor;
        any = true;
    }
    if (!std::isinf(item.max) && (!admission || item.max < admission->max ||
                                  (item.max == admission->max && item.maxStrict && !admission->maxStrict))) {
        facts.max = item.max;
        facts.maxStrict = item.maxStrict;
        facts.maxFor = item.maxFor;
        any = true;
    }
    if (!std::isinf(item.dateMin) && (!admission || item.dateMin > admission->dateMin)) {
        facts.dateMin = item.dateMin;
        facts.dateMinFor = item.dateMinFor;
        any = true;
    }
    if (!std::isinf(item.dateMax) && (!admission || item.dateMax < admission->dateMax)) {
        facts.dateMax = item.dateMax;
        facts.dateMaxFor = item.dateMaxFor;
        any = true;
    }
    return any ? std::optional<ColumnFacts>(facts) : std::nullopt;
}

// True if the image certificate `item` admits less than `admission` in some fact.
bool narrower(const ImageCertificate& item, const ImageCertificate& admission) {
    if (item.fixed && !admission.fixed) {
        return true;
    }
    if (item.fixed || admission.fixed) {
        return false;
    }
    if (admission.brokenAllowed && !item.brokenAllowed) {
        return true;
    }
    for (const ImageGroupFacts& group : item.groups) {
        const auto other = std::find_if(admission.groups.begin(), admission.groups.end(),
                                        [&](const ImageGroupFacts& g) { return g.extension == group.extension; });
        if (other == admission.groups.end()) {
            continue;
        }
        for (int channels : other->channels) {
            if (std::find(group.channels.begin(), group.channels.end(), channels) == group.channels.end()) {
                return true;
            }
        }
        if (group.minWidth > other->minWidth || group.maxWidth < other->maxWidth ||
            group.minHeight > other->minHeight || group.maxHeight < other->maxHeight) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string imageExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".png" ? "png" : "jpg";
}

const TableCertificate* Certificate::table(const std::string& key) const {
    for (const TableCertificate& certificate : tables) {
        if (certificate.key == key) {
            return &certificate;
        }
    }
    return nullptr;
}

const ImageCertificate* Certificate::image(const std::string& dataset) const {
    for (const ImageCertificate& certificate : images) {
        if (certificate.dataset == dataset) {
            return &certificate;
        }
    }
    return nullptr;
}

const ItemCertificate* Certificate::item(const std::string& key) const {
    for (const ItemCertificate& certificate : items) {
        if (certificate.key == key) {
            return &certificate;
        }
    }
    return nullptr;
}

Certifier::Certifier(const Program& program, SchemaProvider& schemas, std::filesystem::path baseFolder)
    : program_(program), schemas_(schemas), baseFolder_(std::move(baseFolder)) {}

// One more run of the whole semantic analysis, with facts_ and imageFacts_ in place of the inputs.
Certifier::Proofs Certifier::analyse() {
    Diagnostics diagnostics;
    SymbolTable symbols;
    schemas_.setOverrides(facts_);
    schemas_.setImageOverrides(imageFacts_);
    const AnalysisResult result = SemanticAnalyzer(symbols, schemas_, baseFolder_, diagnostics).analyze(program_);
    ++runs_;
    for (const Diagnostic& diagnostic : diagnostics.all()) {
        if (diagnostic.severity == Severity::Error) {
            return proofsOf(result, "line " + std::to_string(diagnostic.location.line) + ": " + diagnostic.message);
        }
    }
    return proofsOf(result, "");
}

std::vector<SavedTable> Certifier::outputsNow() {
    Diagnostics diagnostics;
    SymbolTable symbols;
    schemas_.setOverrides(facts_);
    schemas_.setImageOverrides(imageFacts_);
    ++runs_;
    return SemanticAnalyzer(symbols, schemas_, baseFolder_, diagnostics).analyze(program_).saved;
}

Certifier::Proofs Certifier::proofsOf(const AnalysisResult& result, const std::string& error) {
    Proofs proofs;
    proofs.error = error;
    // Contracts without a run-time check (column types, image contracts): if one is
    // lost, no code of the generated program can stand in for the proof.
    for (const ContractRecord& contract : result.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues && !contract.runtimeCheck) {
            proofs.items["contract " + std::to_string(contract.line) + " " + contract.contract] =
                "line " + std::to_string(contract.line) + ": EXPECT " + contract.contract;
        }
    }
    // Removed checks and operations, keyed like the IR instructions they remove.
    for (const auto& [key, reason] : result.noEffect) {
        if (result.valueProofs.count(key) != 0) {
            continue;  // proven from the values: checked again whenever an input changed
        }
        const int line = key.first->location.line;
        std::string description = "line " + std::to_string(line) + ": removed because " + reason;
        if (dynamic_cast<const ExpectStmt*>(key.first)) {
            const std::string name = targetName(key.second);
            for (const ContractRecord& contract : result.contracts) {
                if (contract.line == line && contract.contract.rfind(name + " ", 0) == 0) {
                    description = "line " + std::to_string(line) + ": EXPECT " + contract.contract;
                }
            }
        }
        proofs.items["removed " + proofKey(*key.first, key.second)] = description;
    }
    return proofs;
}

// A run that reports an error loses the program itself: the error comes first,
// then the proofs it took with it (e.g. a contract proven before is now violated).
std::vector<std::string> Certifier::lost(const Proofs& base, const Proofs& run) {
    std::vector<std::string> missing;
    for (const auto& [key, description] : base.items) {
        if (run.items.count(key) == 0) {
            missing.push_back(description);
        }
    }
    // The error is worth naming only if it is not the lost proof itself (a contract
    // proven before and violated now is already named by its line).
    if (!run.error.empty()) {
        const std::string line = run.error.substr(0, run.error.find(':') + 1);
        const bool named = std::any_of(missing.begin(), missing.end(),
                                       [&](const std::string& item) { return item.rfind(line, 0) == 0; });
        if (!named) {
            missing.insert(missing.begin(), run.error + " (compile error)");
        }
    }
    return missing;
}

Certificate Certifier::certify(const IRProgram& optimized, const AnalysisResult& analysis,
                               const std::vector<double>& constants) {
    // Start from the facts of the inputs, but as bounds that hold for every
    // admitted input: no value of a compiled file is known to occur in another
    // one, and no image file name is known.
    for (const auto& [key, schema] : optimized.tables) {
        TableSchema facts = schema;
        facts.valuesKnown = false;
        for (ColumnInfo& column : facts.columns) {
            column.boundsExact = false;
        }
        facts_[SchemaProvider::overrideKey(facts.file, facts.name)] = facts;
    }
    for (const auto& [name, schema] : optimized.images) {
        ImageSetSchema files = schema;
        files.namesKnown = false;
        imageFacts_[SchemaProvider::imageKey(schema.root, schema.singleFile, schema.recursive)] = files;
    }
    Proofs base = analyse();

    Certificate certificate;
    // The facts as bounds must give exactly the proofs the compiled program uses
    // (only proofs that read the values may need the exact inputs). If they do
    // not, no other input can be admitted safely.
    Proofs compiled = proofsOf(analysis, "");
    // A dead store is removed whatever the data, so the fact that also made it useless
    // is no obligation: without this, a fill of a column dropped later would make
    // "no missing values" an admission fact of a column the program never reads.
    for (const std::string& key : optimized.deadKeys) {
        base.items.erase("removed " + key);
        compiled.items.erase("removed " + key);
    }
    if (!lost(base, compiled).empty() || !lost(compiled, base).empty()) {
        certificate.exactOnly = true;
        certificate.guarantees = analysis.saved;  // only the compiled files are admitted
        certificate.analysisRuns = runs_;
        schemas_.setOverrides({});
        schemas_.setImageOverrides({});
        return certificate;
    }
    for (const ContractRecord& contract : analysis.contracts) {
        if (contract.fromValues) {
            certificate.recheckedIfChanged.push_back("line " + std::to_string(contract.line) + ": EXPECT " +
                                                     contract.contract);
        }
    }

    // 1. The full certificate: every proof and every removed operation.
    criterion_ = [&base](const Proofs& run) { return lost(base, run); };
    for (const Instruction& in : optimized.code) {
        if (in.op == Opcode::Load) {
            certificate.fullTables.push_back(certifyTable(in, optimized, constants));
        } else if (in.op == Opcode::LoadImages) {
            certificate.fullImages.push_back(certifyImages(in.dataset, optimized.images.at(in.dataset), nullptr));
        }
    }

    // The speculative removals, by proofKey (the condition of a FILTER shares its key).
    std::map<std::string, std::string> speculative;
    for (const Instruction& in : optimized.code) {
        const auto found = base.items.find("removed " + in.proofKey);
        if (in.speculative && in.result == 0 && found != base.items.end()) {
            speculative[in.proofKey] = found->second;
        }
    }
    if (speculative.empty()) {
        certificate.tables = certificate.fullTables;
        certificate.images = certificate.fullImages;
        certificate.guarantees = outputsNow();
    } else {
        const std::map<std::string, TableSchema> fullFacts = facts_;
        const std::map<std::string, ImageSetSchema> fullImageFacts = imageFacts_;

        // 2. Admission: no compile error, and every proof that no code of the generated
        // program can stand in for: contracts without a run-time check, and removals
        // that are not speculative (e.g. a check whose column is not loaded).
        criterion_ = [&base, &speculative](const Proofs& run) {
            std::vector<std::string> problems;
            if (!run.error.empty()) {
                problems.push_back(run.error + " (compile error)");
            }
            for (const auto& [key, description] : base.items) {
                const bool deoptimizable = key.rfind("removed ", 0) == 0 && speculative.count(key.substr(8)) != 0;
                if (!deoptimizable && run.items.count(key) == 0) {
                    problems.push_back(description);
                }
            }
            return problems;
        };
        for (const TableCertificate& full : certificate.fullTables) {
            certificate.tables.push_back(widenTableValues(full, constants));
        }
        for (const ImageCertificate& full : certificate.fullImages) {
            certificate.images.push_back(certifyImages(full.dataset, optimized.images.at(full.dataset), &full));
        }
        // Every admitted input runs the program's own semantics (deoptimization keeps
        // it), so what the analysis says for the admission facts holds for every run.
        certificate.guarantees = outputsNow();

        // 3. One item certificate per removal, each found from the full certificate.
        for (const auto& [key, description] : speculative) {
            facts_ = fullFacts;
            imageFacts_ = fullImageFacts;
            const std::string itemKey = "removed " + key;
            const std::string what = description;
            criterion_ = [itemKey, what](const Proofs& run) {
                std::vector<std::string> problems;
                if (!run.error.empty()) {
                    problems.push_back(run.error + " (compile error)");
                }
                if (run.items.count(itemKey) == 0) {
                    problems.push_back(what);
                }
                return problems;
            };
            ItemCertificate item;
            item.key = key;
            item.description = description;
            for (const TableCertificate& full : certificate.fullTables) {
                const TableCertificate found = widenTableValues(full, constants);
                const TableCertificate* admission = certificate.table(full.key);
                TableCertificate extra;
                extra.key = found.key;
                extra.file = found.file;
                for (const ColumnFacts& column : found.columns) {
                    const ColumnFacts* admitted = nullptr;
                    for (const ColumnFacts& other : admission->columns) {
                        if (other.name == column.name) {
                            admitted = &other;
                        }
                    }
                    if (const std::optional<ColumnFacts> facts = beyond(column, admitted)) {
                        extra.columns.push_back(*facts);
                    }
                }
                if (!extra.columns.empty()) {
                    item.tables.push_back(extra);
                }
            }
            for (const ImageCertificate& full : certificate.fullImages) {
                const ImageCertificate found = certifyImages(full.dataset, optimized.images.at(full.dataset), &full);
                if (narrower(found, *certificate.image(full.dataset))) {
                    item.images.push_back(found);
                }
            }
            certificate.items.push_back(item);
        }
    }
    certificate.analysisRuns = runs_;
    schemas_.setOverrides({});
    schemas_.setImageOverrides({});
    return certificate;
}

// ---------- Tables ----------

// Widens a column's missing-value and bound facts, in facts_, as far as criterion_
// allows. `column` must hold the current facts and gets the widened ones.
void Certifier::widenValues(ColumnInfo& info, ColumnFacts& column, const std::vector<double>& constants) {
    // Missing values: allow them, and keep "no missing values" only if the criterion needs it.
    if (info.missingCount == 0 && info.missingKnown) {
        info.missingKnown = false;
        column.notNullFor = broken();
        if (!column.notNullFor.empty()) {
            info.missingKnown = true;
        }
    }
    column.notNull = info.missingCount == 0 && info.missingKnown;
    if (!column.notNull) {
        column.notNullFor.clear();
    }

    // A bound is widened to the loosest value that keeps the proofs: first no
    // bound at all, then each program constant from the loosest one on, as a
    // closed bound (>= k) and then as an open one (> k, for FILTER c > k). The
    // analysis compares ranges only with these constants, so no other value
    // could do better. An open bound is given to the analysis as the next
    // double past k. `neededBy` gets what dropping the bound breaks.
    struct Candidate {
        double value;  // what the analysis is given
        double shown;  // the constant
        bool strict;
    };
    const auto widen = [&](double& bound, const std::vector<Candidate>& looser, double& shown, bool& strict,
                           std::vector<std::string>& neededBy) {
        const double original = bound;
        for (size_t k = 0; k < looser.size(); ++k) {
            bound = looser[k].value;
            const std::vector<std::string> problems = broken();
            if (k == 0) {
                neededBy = problems;
            }
            if (problems.empty()) {
                shown = looser[k].shown;
                strict = looser[k].strict;
                return;
            }
        }
        bound = original;  // `shown` and `strict` keep the current bound
    };
    if (isNumeric(info.type) && info.hasRange) {
        const double infinity = std::numeric_limits<double>::infinity();
        if (!std::isinf(info.min)) {
            std::vector<Candidate> lower = {{-infinity, -infinity, false}};
            for (double constant : constants) {  // constants are sorted
                if (constant < info.min) {
                    lower.push_back({constant, constant, false});
                    if (std::nextafter(constant, infinity) <= info.min) {
                        lower.push_back({std::nextafter(constant, infinity), constant, true});
                    }
                }
            }
            widen(info.min, lower, column.min, column.minStrict, column.minFor);
        }
        if (!std::isinf(info.max)) {
            std::vector<Candidate> upper = {{infinity, infinity, false}};
            for (auto it = constants.rbegin(); it != constants.rend(); ++it) {
                if (*it > info.max) {
                    upper.push_back({*it, *it, false});
                    if (std::nextafter(*it, -infinity) >= info.max) {
                        upper.push_back({std::nextafter(*it, -infinity), *it, true});
                    }
                }
            }
            widen(info.max, upper, column.max, column.maxStrict, column.maxFor);
        }
    }
    // The first and last date of a column PARSE_DATE reads. Dates are compared only
    // through their parts (YEAR, MONTH, DAY, HOUR, WEEKDAY), whose ranges change only
    // where the dates cross a calendar boundary. So a bound is widened to no bound at
    // all, or else to the start (or end) of its year, month or day, the loosest first.
    if (info.datesKnown && info.allDates && info.hasDates) {
        const double infinity = std::numeric_limits<double>::infinity();
        const auto widenDate = [&](double& bound, const std::vector<double>& looser, double& kept,
                                   std::vector<std::string>& neededBy) {
            const double original = bound;
            for (size_t k = 0; k < looser.size(); ++k) {
                bound = looser[k];
                const std::vector<std::string> problems = broken();
                if (k == 0) {
                    neededBy = problems;
                }
                if (problems.empty()) {
                    kept = looser[k];
                    return;
                }
            }
            bound = original;
            kept = original;
        };
        if (!std::isinf(info.dateMin)) {
            std::vector<double> lower = {-infinity};
            for (const char* unit : {"YEAR", "MONTH", "DAY"}) {
                if (startOf(unit, info.dateMin) < info.dateMin && startOf(unit, info.dateMin) > lower.back()) {
                    lower.push_back(startOf(unit, info.dateMin));
                }
            }
            widenDate(info.dateMin, lower, column.dateMin, column.dateMinFor);
        }
        if (!std::isinf(info.dateMax)) {
            std::vector<double> upper = {infinity};
            for (const char* unit : {"YEAR", "MONTH", "DAY"}) {
                if (endOf(unit, info.dateMax) > info.dateMax && endOf(unit, info.dateMax) < upper.back()) {
                    upper.push_back(endOf(unit, info.dateMax));
                }
            }
            widenDate(info.dateMax, upper, column.dateMax, column.dateMaxFor);
        }
    }
    if (std::isinf(column.dateMin)) {
        column.dateMinFor.clear();
    }
    if (std::isinf(column.dateMax)) {
        column.dateMaxFor.clear();
    }
    if (std::isinf(column.min)) {
        column.minFor.clear();
        column.minStrict = false;
    }
    if (std::isinf(column.max)) {
        column.maxFor.clear();
        column.maxStrict = false;
    }
}

TableCertificate Certifier::certifyTable(const Instruction& load, const IRProgram& optimized,
                                         const std::vector<double>& constants) {
    TableCertificate table;
    table.key = tableKey(load.dataset, load.table);
    const TableSchema& schema = optimized.tables.at(table.key);
    table.file = schema.file;
    overrideOf_[table.key] = SchemaProvider::overrideKey(schema.file, schema.name);
    TableSchema& facts = facts_[overrideOf_[table.key]];
    // Every column of the file, not only the ones the optimized program reads:
    // a proof may depend on a column whose check was removed and which is then
    // not loaded at all (EXPECT t.id NOT_NULL, and id is never used again).
    for (ColumnInfo& info : facts.columns) {
        ColumnFacts column;
        column.name = info.name;
        column.type = info.type;
        column.read = std::find(load.columns.begin(), load.columns.end(), info.name) != load.columns.end();

        // The type of a column the program reads is always required (it is read with
        // that type). Another column's type is required only if some proof needs it.
        if (!column.read) {
            const ColumnInfo original = info;
            for (ColumnType other : {ColumnType::Boolean, ColumnType::Integer, ColumnType::Float, ColumnType::String,
                                     ColumnType::DateTime}) {
                if (other == original.type) {
                    continue;
                }
                info.type = other;
                info.hasRange = isNumeric(other) && original.hasRange;
                addAll(column.typeFor, broken());
                info = original;
            }
            column.typeFixed = !column.typeFor.empty();
        }

        // That every value is a date matters only to a PARSE_DATE of the column, which
        // fails without it: then it is required on admission, like a type.
        if (info.datesKnown && info.allDates) {
            const ColumnInfo original = info;
            info.allDates = false;
            info.hasDates = false;
            column.datesFor = broken();
            column.dates = !column.datesFor.empty();
            info = column.dates ? original : info;
        }
        if (info.datesKnown && info.allDates && info.hasDates) {
            column.dateMin = info.dateMin;
            column.dateMax = info.dateMax;
        }
        if (isNumeric(info.type) && info.hasRange) {
            column.min = info.min;
            column.max = info.max;
        }
        widenValues(info, column, constants);
        const bool needed = column.typeFixed || column.notNull || !std::isinf(column.min) || !std::isinf(column.max) ||
                            column.dates || !std::isinf(column.dateMin) || !std::isinf(column.dateMax);
        if (column.read || needed) {
            table.columns.push_back(column);
        }
    }

    // A column no instruction names, which the program only carries to the output
    // (a target column in a test file, say), may be missing from another input if
    // the analysis without it keeps every proof: alone, and together with the
    // columns already found optional. The output then simply lacks it.
    std::set<std::string> named;
    for (const Instruction& in : optimized.code) {
        if (in.op == Opcode::Load) {
            continue;
        }
        if (tableKey(in.dataset, in.table) == table.key) {
            named.insert(in.column);
            named.insert(in.columns.begin(), in.columns.end());
        }
        if (in.op == Opcode::Check && in.columns.size() >= 3 && tableKey(in.columns[0], in.columns[1]) == table.key) {
            named.insert(in.columns[2]);  // the key column of a REFERENCES
        }
        if (in.op == Opcode::Check && in.text == "MATCH_RATE" && in.columns[1] == table.key) {
            named.insert(in.columns[2]);
        }
    }
    const std::vector<ColumnInfo> all = facts.columns;
    std::set<std::string> absent;
    const auto without = [&](const std::set<std::string>& names) {
        facts.columns.clear();
        for (const ColumnInfo& info : all) {
            if (names.count(info.name) == 0) {
                facts.columns.push_back(info);
            }
        }
        return broken();
    };
    // Every column of the file is tested, also the ones without other facts: a
    // statement the optimizer removed can still name it (DROP t [c] before c is
    // ever used), and a fresh compilation of a file without it would fail.
    for (const ColumnInfo& info : all) {
        auto found = std::find_if(table.columns.begin(), table.columns.end(),
                                  [&](const ColumnFacts& column) { return column.name == info.name; });
        if (named.count(info.name) != 0) {
            continue;  // the generated code uses it: required (and read, so certified)
        }
        std::set<std::string> together = absent;
        together.insert(info.name);
        std::vector<std::string> neededBy = without({info.name});
        if (neededBy.empty()) {
            neededBy = without(together);
        }
        if (neededBy.empty()) {
            absent = together;
            if (found != table.columns.end()) {
                found->required = false;
            }
            continue;
        }
        if (found == table.columns.end()) {  // only its presence is needed
            ColumnFacts column;
            column.name = info.name;
            column.type = info.type;
            column.read = false;
            column.typeFixed = false;
            table.columns.push_back(column);
            found = table.columns.end() - 1;
        }
        found->required = true;
        found->requiredFor = neededBy;
    }
    facts.columns = all;  // later runs see every column again
    return table;
}

TableCertificate Certifier::widenTableValues(const TableCertificate& start, const std::vector<double>& constants) {
    TableCertificate table = start;
    TableSchema& facts = facts_.at(overrideOf_.at(start.key));
    for (ColumnFacts& column : table.columns) {
        for (ColumnInfo& info : facts.columns) {
            if (info.name == column.name) {
                widenValues(info, column, constants);
            }
        }
    }
    return table;
}

// ---------- Image folders ----------

// RESIZE sizes and MIN_WIDTH / MIN_HEIGHT limits: the only numbers image sizes are compared with.
std::vector<int> Certifier::imageConstants() const {
    std::vector<int> sizes;
    for (const auto& statement : program_.statements) {
        if (const auto* resize = dynamic_cast<const ResizeStmt*>(statement.get())) {
            sizes.push_back(resize->width);
            sizes.push_back(resize->height);
        } else if (const auto* expect = dynamic_cast<const ExpectStmt*>(statement.get())) {
            if (expect->constraint == ConstraintKind::MinWidth || expect->constraint == ConstraintKind::MinHeight) {
                sizes.push_back(std::stoi(expect->low));
            }
        }
    }
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    return sizes;
}

ImageCertificate Certifier::certifyImages(const std::string& dataset, const ImageSetSchema& schema,
                                          const ImageCertificate* start) {
    const std::string key = SchemaProvider::imageKey(schema.root, schema.singleFile, schema.recursive);
    if (start && start->fixed) {
        return *start;  // the folder stays exactly as compiled: nothing to widen
    }
    ImageCertificate image;
    image.dataset = dataset;
    image.root = schema.root;
    const auto keepFixed = [&](std::vector<std::string> reason) {
        image.fixed = true;
        image.fixedFor = std::move(reason);
        image.groups.clear();
        imageFacts_[key] = schema;
        imageFacts_[key].namesKnown = false;
        return image;
    };
    const auto run = [&] {
        imageFacts_[key] = cornerImages(image, schema);
        return broken();
    };
    if (start) {
        image = *start;
    } else {
        if (schema.singleFile) {
            return keepFixed({"a single image file (LOAD_IMAGE) is not certified"});
        }
        // The box of each extension, from the files.
        std::map<std::string, ImageGroupFacts> groups;
        std::map<std::string, std::set<int>> channels;
        std::set<int> depths;
        for (const ImageInfo& file : schema.images) {
            if (file.corrupt) {
                image.brokenAllowed = true;
                continue;
            }
            const std::string extension = imageExtension(file.path);
            ImageGroupFacts& group = groups[extension];
            if (channels[extension].empty()) {
                group.extension = extension;
                group.minWidth = group.maxWidth = file.width;
                group.minHeight = group.maxHeight = file.height;
            }
            group.minWidth = std::min(group.minWidth, file.width);
            group.maxWidth = std::max(group.maxWidth, file.width);
            group.minHeight = std::min(group.minHeight, file.height);
            group.maxHeight = std::max(group.maxHeight, file.height);
            channels[extension].insert(file.channels);
            depths.insert(depthOf(file));
        }
        if (groups.empty()) {
            return keepFixed({"no file of the folder can be read"});
        }
        image.depths.assign(depths.begin(), depths.end());
        for (auto& [extension, group] : groups) {
            group.channels.assign(channels[extension].begin(), channels[extension].end());
            image.groups.push_back(group);
        }
        // The box itself must keep the proofs: it also stands for folders that mix
        // the properties of the compiled files differently.
        const std::vector<std::string> boxLost = run();
        if (!boxLost.empty()) {
            return keepFixed(boxLost);
        }
    }
    const std::vector<int> sizes = imageConstants();

    if (!image.brokenAllowed) {
        image.brokenAllowed = true;
        image.brokenFor = run();
        image.brokenAllowed = image.brokenFor.empty();
    }
    for (size_t g = 0; g < image.groups.size(); ++g) {
        for (int count : {1, 3, 4}) {  // channel counts OpenCV decodes PNG and JPEG files to
            std::vector<int>& allowed = image.groups[g].channels;
            if (std::find(allowed.begin(), allowed.end(), count) != allowed.end()) {
                continue;
            }
            allowed.push_back(count);
            const std::vector<std::string> problems = run();
            if (!problems.empty()) {
                image.groups[g].channels.erase(
                    std::find(image.groups[g].channels.begin(), image.groups[g].channels.end(), count));
                addAll(image.groups[g].channelsFor, problems);
            }
        }
        std::sort(image.groups[g].channels.begin(), image.groups[g].channels.end());

        // Size bounds, widened like column bounds: no bound, else the loosest program size.
        const auto widen = [&](int ImageGroupFacts::* bound, int loosest, bool lower,
                               std::vector<std::string> ImageGroupFacts::* neededBy) {
            const int original = image.groups[g].*bound;
            if (original == loosest) {
                return;
            }
            std::vector<int> looser = {loosest};
            for (int size : sizes) {
                if (lower ? size < original : size > original) {
                    looser.push_back(size);
                }
            }
            if (!lower) {
                std::reverse(looser.begin() + 1, looser.end());
            }
            for (size_t k = 0; k < looser.size(); ++k) {
                image.groups[g].*bound = looser[k];
                const std::vector<std::string> problems = run();
                if (k == 0) {
                    image.groups[g].*neededBy = problems;
                }
                if (problems.empty()) {
                    if (k == 0) {
                        (image.groups[g].*neededBy).clear();
                    }
                    return;
                }
            }
            image.groups[g].*bound = original;
        };
        widen(&ImageGroupFacts::minWidth, 1, true, &ImageGroupFacts::minWidthFor);
        widen(&ImageGroupFacts::maxWidth, 65535, false, &ImageGroupFacts::maxWidthFor);
        widen(&ImageGroupFacts::minHeight, 1, true, &ImageGroupFacts::minHeightFor);
        widen(&ImageGroupFacts::maxHeight, 65535, false, &ImageGroupFacts::maxHeightFor);
    }
    imageFacts_[key] = cornerImages(image, schema);
    return image;
}

ImageSetSchema cornerImages(const ImageCertificate& certificate, const ImageSetSchema& original) {
    ImageSetSchema corners;
    corners.root = original.root;
    corners.singleFile = original.singleFile;
    corners.recursive = original.recursive;
    corners.fingerprint = original.fingerprint;
    corners.namesKnown = false;
    for (const ImageGroupFacts& group : certificate.groups) {
        const std::set<int> widths = {group.minWidth, group.maxWidth};
        const std::set<int> heights = {group.minHeight, group.maxHeight};
        for (int width : widths) {
            for (int height : heights) {
                for (int channels : group.channels) {
                    for (int depth : certificate.depths) {
                        ImageInfo file;
                        file.path = "certified/" + group.extension + "/" + std::to_string(width) + "x" +
                                    std::to_string(height) + "-c" + std::to_string(channels) + "-d" +
                                    std::to_string(depth) + "." + group.extension;
                        file.format = group.extension == "png" ? "PNG" : "JPG";
                        file.width = width;
                        file.height = height;
                        file.channels = channels;
                        file.bitDepth = depth;
                        corners.images.push_back(file);
                    }
                }
            }
        }
    }
    if (certificate.brokenAllowed && !certificate.groups.empty()) {
        ImageInfo broken;
        broken.path = "certified/broken." + certificate.groups.front().extension;
        broken.format = certificate.groups.front().extension == "png" ? "PNG" : "JPG";
        broken.corrupt = true;
        broken.problem = "the certificate allows files that cannot be read";
        corners.images.push_back(broken);
    }
    std::sort(corners.images.begin(), corners.images.end(),
              [](const ImageInfo& a, const ImageInfo& b) { return a.path < b.path; });
    return corners;
}

// ---------- Constants, admission and printing ----------

std::vector<double> programConstants(const IRProgram& program) {
    std::vector<double> constants;
    const std::function<void(const ValueExpr&)> numbersOf = [&](const ValueExpr& expr) {
        if (expr.kind == ValueKind::Number) {
            constants.push_back(std::stod(expr.text));
        }
        if (expr.left) {
            numbersOf(*expr.left);
        }
        if (expr.right) {
            numbersOf(*expr.right);
        }
    };
    for (const Instruction& in : program.code) {
        if (in.op == Opcode::Derive) {
            numbersOf(*in.value);
        }
        const bool numbers =
            in.op == Opcode::Compare || in.op == Opcode::FillNull || (in.op == Opcode::Check && in.text == "BETWEEN");
        for (const Literal& value : in.values) {
            if (numbers && value.kind == LiteralKind::Number) {
                constants.push_back(std::stod(value.value));
            }
        }
    }
    std::sort(constants.begin(), constants.end());
    constants.erase(std::unique(constants.begin(), constants.end()), constants.end());
    return constants;
}

std::vector<std::string> admissionProblems(const TableCertificate& certificate, const TableSchema& input) {
    std::vector<std::string> problems;
    for (const ColumnFacts& column : certificate.columns) {
        const auto found = std::find_if(input.columns.begin(), input.columns.end(),
                                        [&](const ColumnInfo& info) { return info.name == column.name; });
        if (found == input.columns.end()) {
            if (column.required) {
                problems.push_back(column.name + " is missing");
            }
            continue;
        }
        const ColumnInfo& info = *found;
        // The type must be the one the compiler infers for the new file: a column
        // of whole numbers is INTEGER, and one without any value is STRING. (Letting
        // INTEGER data into a FLOAT column, or an empty column into any type, can
        // change what the analysis of that file decides.)
        if (column.typeFixed && info.type != column.type) {
            problems.push_back(column.name + " is " + columnTypeName(info.type) + ", not " +
                               columnTypeName(column.type));
            continue;
        }
        if (column.notNull && (info.missingCount > 0 || !info.missingKnown)) {
            problems.push_back(column.name + " has missing values; needed by " + join(column.notNullFor));
        }
        if (info.hasRange && (column.minStrict ? info.min <= column.min : info.min < column.min)) {
            problems.push_back(column.name + " has " + formatNumber(info.min) + ", below " +
                               (column.minStrict ? "or at " : "") + formatNumber(column.min) + "; needed by " +
                               join(column.minFor));
        }
        if (info.hasRange && (column.maxStrict ? info.max >= column.max : info.max > column.max)) {
            problems.push_back(column.name + " has " + formatNumber(info.max) + ", above " +
                               (column.maxStrict ? "or at " : "") + formatNumber(column.max) + "; needed by " +
                               join(column.maxFor));
        }
        const bool dates = info.datesKnown && info.allDates;
        if (column.dates && !dates) {
            problems.push_back(column.name + " has values that are not dates; needed by " + join(column.datesFor));
        }
        if (dates && info.hasDates && info.dateMin < column.dateMin) {
            problems.push_back(column.name + " has " + dateText(info.dateMin) + ", before " + dateText(column.dateMin) +
                               "; needed by " + join(column.dateMinFor));
        }
        if (dates && info.hasDates && info.dateMax > column.dateMax) {
            problems.push_back(column.name + " has " + dateText(info.dateMax) + ", after " + dateText(column.dateMax) +
                               "; needed by " + join(column.dateMaxFor));
        }
    }
    return problems;
}

std::vector<std::string> admissionProblems(const ImageCertificate& certificate, const ImageSetSchema& input) {
    if (certificate.fixed) {
        return {"the folder must stay exactly as compiled"};
    }
    std::vector<std::string> problems;
    std::set<int> depths;
    for (const ImageInfo& file : input.images) {
        if (file.corrupt) {
            if (!certificate.brokenAllowed) {
                problems.push_back(file.path + " cannot be read; needed by " + join(certificate.brokenFor));
            }
            continue;
        }
        depths.insert(depthOf(file));
        const std::string extension = imageExtension(file.path);
        const auto group = std::find_if(certificate.groups.begin(), certificate.groups.end(),
                                        [&](const ImageGroupFacts& g) { return g.extension == extension; });
        if (group == certificate.groups.end()) {
            problems.push_back(file.path + " is a ." + extension + " file, which the compiled folder had none of");
            continue;
        }
        if (std::find(group->channels.begin(), group->channels.end(), file.channels) == group->channels.end()) {
            problems.push_back(file.path + " has " + std::to_string(file.channels) + " channels; needed by " +
                               join(group->channelsFor));
        }
        const auto size = [&](int value, int low, int high, const std::vector<std::string>& lowFor,
                              const std::vector<std::string>& highFor, const char* what) {
            if (value < low || value > high) {
                problems.push_back(file.path + " has " + what + " " + std::to_string(value) + "; needed by " +
                                   join(value < low ? lowFor : highFor));
            }
        };
        size(file.width, group->minWidth, group->maxWidth, group->minWidthFor, group->maxWidthFor, "width");
        size(file.height, group->minHeight, group->maxHeight, group->minHeightFor, group->maxHeightFor, "height");
    }
    if (input.images.empty()) {
        problems.push_back("the folder has no PNG or JPG images");
    } else if (std::vector<int>(depths.begin(), depths.end()) != certificate.depths && !depths.empty()) {
        problems.push_back("its bit depths differ from the compiled folder's (" + listText(certificate.depths) +
                           "), which decide the pixel type");
    }
    return problems;
}

namespace {

void printTables(std::ostream& out, const std::vector<TableCertificate>& tables, bool items) {
    const auto factLines = [&](size_t indent, const std::vector<std::pair<std::string, std::string>>& facts) {
        for (size_t i = 0; i < facts.size(); ++i) {
            if (i > 0) {
                out << std::string(indent, ' ');
            }
            out << std::left << std::setw(20) << facts[i].first << (facts[i].first.size() < 20 ? "<- " : " <- ")
                << facts[i].second << '\n';
        }
    };
    const std::string pad = items ? "    " : "  ";
    for (const TableCertificate& table : tables) {
        out << (items ? "  " : "\n") << table.key << "  (compiled with " << table.file << ")\n";
        size_t width = 8;
        for (const ColumnFacts& column : table.columns) {
            width = std::max(width, column.name.size() + 2);
        }
        for (const ColumnFacts& column : table.columns) {
            std::vector<std::pair<std::string, std::string>> facts;
            if (column.notNull) {
                facts.emplace_back("no missing values", join(column.notNullFor));
            }
            if (!std::isinf(column.min)) {
                facts.emplace_back((column.minStrict ? "> " : ">= ") + formatNumber(column.min), join(column.minFor));
            }
            if (!std::isinf(column.max)) {
                facts.emplace_back((column.maxStrict ? "< " : "<= ") + formatNumber(column.max), join(column.maxFor));
            }
            if (column.dates) {
                facts.emplace_back("dates", join(column.datesFor));
            }
            if (!std::isinf(column.dateMin)) {
                facts.emplace_back("from " + dateText(column.dateMin), join(column.dateMinFor));
            }
            if (!std::isinf(column.dateMax)) {
                facts.emplace_back("until " + dateText(column.dateMax), join(column.dateMaxFor));
            }
            out << pad << std::left << std::setw(static_cast<int>(width)) << column.name;
            if (!items) {
                out << std::setw(9) << (column.typeFixed ? columnTypeName(column.type) : "any type");
            }
            if (!items && !column.read) {
                facts.insert(facts.begin(), {"(not read)", "loaded only to check the facts below"});
            }
            if (!items && !column.required) {
                facts.emplace_back("may be missing", "only carried to the output");
            } else if (!items && !column.read && !column.requiredFor.empty()) {
                facts.emplace_back("must exist", join(column.requiredFor));
            }
            if (!items && !column.read && column.typeFixed && !column.typeFor.empty()) {
                facts.emplace_back("its type", join(column.typeFor));
            }
            if (facts.empty()) {
                out << "(any values)\n";
            }
            factLines(width + pad.size() + (items ? 0 : 9), facts);
        }
    }
}

void printImages(std::ostream& out, const std::vector<ImageCertificate>& images, bool items) {
    const auto factLines = [&](size_t indent, const std::vector<std::pair<std::string, std::string>>& facts) {
        for (size_t i = 0; i < facts.size(); ++i) {
            if (i > 0) {
                out << std::string(indent, ' ');
            }
            out << std::left << std::setw(20) << facts[i].first << "<- " << facts[i].second << '\n';
        }
    };
    for (const ImageCertificate& image : images) {
        out << (items ? "  " : "\n") << image.dataset << "  (compiled with " << image.root << ")\n";
        if (image.fixed) {
            out << "  must stay exactly as compiled <- " << join(image.fixedFor) << '\n';
            continue;
        }
        out << "  bit depth " << listText(image.depths) << " (as compiled); files that cannot be read: "
            << (image.brokenAllowed ? "allowed\n" : "not allowed <- " + join(image.brokenFor) + "\n");
        for (const ImageGroupFacts& group : image.groups) {
            std::vector<std::pair<std::string, std::string>> facts;
            facts.emplace_back("channels " + listText(group.channels),
                               group.channelsFor.empty() ? "(any the decoder gives)" : join(group.channelsFor));
            if (group.minWidth > 1) {
                facts.emplace_back("width >= " + std::to_string(group.minWidth), join(group.minWidthFor));
            }
            if (group.maxWidth < 65535) {
                facts.emplace_back("width <= " + std::to_string(group.maxWidth), join(group.maxWidthFor));
            }
            if (group.minHeight > 1) {
                facts.emplace_back("height >= " + std::to_string(group.minHeight), join(group.minHeightFor));
            }
            if (group.maxHeight < 65535) {
                facts.emplace_back("height <= " + std::to_string(group.maxHeight), join(group.maxHeightFor));
            }
            out << "  ." << std::left << std::setw(6) << group.extension;
            factLines(9, facts);
        }
    }
}

}  // namespace

void printCertificate(std::ostream& out, const Certificate& certificate) {
    if (certificate.exactOnly) {
        out << "The proofs depend on the exact values of the input files, so the generated program\n"
            << "accepts only the files it was compiled with.\n";
        return;
    }
    out << "The generated program admits any input with these facts (found by analysing the\n"
        << "program " << certificate.analysisRuns << " more times with weaker facts); it refuses any other input:\n";
    printTables(out, certificate.tables, false);
    printImages(out, certificate.images, false);
    if (!certificate.items.empty()) {
        out << "\nSpeculatively removed, and run again for an admitted input that breaks these facts:\n";
        for (const ItemCertificate& item : certificate.items) {
            out << "\n" << item.description << "\n";
            if (item.empty()) {
                out << "  holds for every admitted input: removed\n";
            }
            printTables(out, item.tables, true);
            printImages(out, item.images, true);
        }
    }
    if (!certificate.recheckedIfChanged.empty()) {
        out << "\nProven from the values, so checked again when an input changes:\n";
        for (const std::string& contract : certificate.recheckedIfChanged) {
            out << "  " << contract << '\n';
        }
    }
}

namespace {

std::string jsonString(const std::string& text) {
    std::string quoted = "\"";
    for (char c : text) {
        if (c == '\\' || c == '"') {
            quoted += '\\';
            quoted += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            quoted += ' ';
        } else {
            quoted += c;
        }
    }
    return quoted + "\"";
}

std::string jsonNumber(double value) {
    if (std::isinf(value) || std::isnan(value)) {
        return "null";
    }
    char text[32];
    const auto end = std::to_chars(text, text + sizeof text, value).ptr;
    return std::string(text, end);
}

std::string jsonList(const std::vector<std::string>& items) {
    std::string text = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        text += (i > 0 ? ", " : "") + jsonString(items[i]);
    }
    return text + "]";
}

// {"marks": {"type": "INTEGER", "required": true, "not_null": [...], "min": 0, "min_for": [...]}, ...}
std::string columnsJson(const TableCertificate& table, const std::string& indent) {
    std::string text = "{";
    for (size_t i = 0; i < table.columns.size(); ++i) {
        const ColumnFacts& c = table.columns[i];
        text += (i > 0 ? ",\n" + indent : "") + jsonString(c.name) +
                ": {\"type\": " + (c.typeFixed ? jsonString(columnTypeName(c.type)) : "null") +
                ", \"required\": " + (c.required ? "true" : "false");
        if (c.notNull) {
            text += ", \"not_null\": true, \"not_null_for\": " + jsonList(c.notNullFor);
        }
        if (!std::isinf(c.min)) {
            text += ", \"min\": " + jsonNumber(c.min) + ", \"min_strict\": " + (c.minStrict ? "true" : "false") +
                    ", \"min_for\": " + jsonList(c.minFor);
        }
        if (!std::isinf(c.max)) {
            text += ", \"max\": " + jsonNumber(c.max) + ", \"max_strict\": " + (c.maxStrict ? "true" : "false") +
                    ", \"max_for\": " + jsonList(c.maxFor);
        }
        if (c.dates) {
            text += ", \"dates\": true, \"dates_for\": " + jsonList(c.datesFor);
        }
        if (!std::isinf(c.dateMin)) {
            text +=
                ", \"date_min\": " + jsonString(dateText(c.dateMin)) + ", \"date_min_for\": " + jsonList(c.dateMinFor);
        }
        if (!std::isinf(c.dateMax)) {
            text +=
                ", \"date_max\": " + jsonString(dateText(c.dateMax)) + ", \"date_max_for\": " + jsonList(c.dateMaxFor);
        }
        text += "}";
    }
    return text + "}";
}

}  // namespace

void writeContract(std::ostream& out, const Certificate& certificate, const std::string& program) {
    out << "{\n  \"program\": " << jsonString(program)
        << ",\n  \"exact_only\": " << (certificate.exactOnly ? "true" : "false") << ",\n  \"inputs\": {";
    for (size_t i = 0; i < certificate.tables.size(); ++i) {
        const TableCertificate& table = certificate.tables[i];
        out << (i > 0 ? "," : "") << "\n    " << jsonString(table.key) << ": {\"file\": " << jsonString(table.file)
            << ",\n      \"columns\": " << columnsJson(table, "                   ") << "}";
    }
    out << "\n  },\n  \"removals\": [";
    bool first = true;
    for (const ItemCertificate& item : certificate.items) {
        if (item.empty()) {
            continue;
        }
        out << (first ? "" : ",") << "\n    {\"key\": " << jsonString(item.key)
            << ", \"description\": " << jsonString(item.description) << ",\n     \"needs\": {";
        for (size_t i = 0; i < item.tables.size(); ++i) {
            out << (i > 0 ? ", " : "") << jsonString(item.tables[i].key) << ": "
                << columnsJson(item.tables[i], "                ");
        }
        out << "}, \"images\": " << (item.images.empty() ? "false" : "true") << "}";
        first = false;
    }
    out << "\n  ],\n  \"outputs\": {";
    for (size_t i = 0; i < certificate.guarantees.size(); ++i) {
        const SavedTable& saved = certificate.guarantees[i];
        out << (i > 0 ? "," : "") << "\n    " << jsonString(saved.path) << ": {\"table\": " << jsonString(saved.table)
            << ", \"columns\": {";
        for (size_t c = 0; c < saved.columns.size(); ++c) {
            const SavedColumn& column = saved.columns[c];
            out << (c > 0 ? ",\n        " : "\n        ") << jsonString(column.name)
                << ": {\"type\": " << jsonString(column.type)
                << ", \"nullable\": " << (column.nullable ? "true" : "false");
            if (column.hasRange) {
                out << ", \"min\": " << jsonNumber(column.min) << ", \"max\": " << jsonNumber(column.max);
            }
            out << "}";
        }
        out << "}}";
    }
    out << "\n  }\n}\n";
}
