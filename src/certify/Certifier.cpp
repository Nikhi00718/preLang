#include "certify/Certifier.h"

#include "common/Diagnostics.h"
#include "sema/SemanticAnalyzer.h"
#include "sema/SymbolTable.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <iomanip>
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

int depthOf(const ImageInfo& image) {
    return image.bitDepth > 8 ? 16 : 8;
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

Certifier::Proofs Certifier::proofsOf(const AnalysisResult& result, const std::string& error) {
    Proofs proofs;
    proofs.error = error;
    for (const ContractRecord& contract : result.contracts) {
        if (contract.verdict == Verdict::Proven && !contract.fromValues) {
            proofs.items["contract " + std::to_string(contract.line) + " " + contract.contract] =
                "line " + std::to_string(contract.line) + ": EXPECT " + contract.contract;
        }
    }
    // Removed operations. The removed check of a proven contract is already
    // covered by the contract itself.
    for (const auto& [key, reason] : result.noEffect) {
        if (!dynamic_cast<const ExpectStmt*>(key.first)) {
            const std::string where = std::to_string(reinterpret_cast<std::uintptr_t>(key.first));
            proofs.items["removed " + where + " " + key.second] =
                "line " + std::to_string(key.first->location.line) + ": removed because " + reason;
        }
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
    const Proofs base = analyse();

    Certificate certificate;
    // The facts as bounds must give exactly the proofs the compiled program uses
    // (only proofs that read the values may need the exact inputs). If they do
    // not, no other input can be admitted safely.
    const Proofs compiled = proofsOf(analysis, "");
    if (!lost(base, compiled).empty() || !lost(compiled, base).empty()) {
        certificate.exactOnly = true;
    } else {
        for (const Instruction& in : optimized.code) {
            if (in.op == Opcode::Load) {
                certifyTable(in, optimized, base, constants, certificate);
            } else if (in.op == Opcode::LoadImages) {
                certifyImages(in.dataset, optimized.images.at(in.dataset), base, imageConstants(), certificate);
            }
        }
        for (const ContractRecord& contract : analysis.contracts) {
            if (contract.fromValues) {
                certificate.recheckedIfChanged.push_back("line " + std::to_string(contract.line) + ": EXPECT " +
                                                         contract.contract);
            }
        }
    }
    certificate.analysisRuns = runs_;
    schemas_.setOverrides({});
    schemas_.setImageOverrides({});
    return certificate;
}

// ---------- Tables ----------

void Certifier::certifyTable(const Instruction& load, const IRProgram& optimized, const Proofs& base,
                             const std::vector<double>& constants, Certificate& certificate) {
    TableCertificate table;
    table.key = tableKey(load.dataset, load.table);
    const TableSchema& schema = optimized.tables.at(table.key);
    table.file = schema.file;
    TableSchema& facts = facts_[SchemaProvider::overrideKey(schema.file, schema.name)];
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
                addAll(column.typeFor, lost(base, analyse()));
                info = original;
            }
            column.typeFixed = !column.typeFor.empty();
        }

        // Missing values: allow them, and keep "no missing values" only if a proof needs it.
        if (info.missingCount == 0 && info.missingKnown) {
            info.missingKnown = false;
            column.notNullFor = lost(base, analyse());
            if (!column.notNullFor.empty()) {
                info.missingKnown = true;
            }
        }
        column.notNull = info.missingCount == 0 && info.missingKnown;

        // A bound is widened to the loosest value that keeps every proof: first
        // no bound at all, then each program constant from the loosest one on,
        // as a closed bound (>= k) and then as an open one (> k, for FILTER c > k).
        // The analysis compares ranges only with these constants, so no other
        // value could do better. An open bound is given to the analysis as the
        // next double past k. `neededBy` gets what dropping the bound breaks.
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
                const std::vector<std::string> broken = lost(base, analyse());
                if (k == 0) {
                    neededBy = broken;
                }
                if (broken.empty()) {
                    shown = looser[k].shown;
                    strict = looser[k].strict;
                    return;
                }
            }
            bound = shown = original;
        };
        if (isNumeric(info.type) && info.hasRange) {
            const double infinity = std::numeric_limits<double>::infinity();
            std::vector<Candidate> lower = {{-infinity, -infinity, false}};
            std::vector<Candidate> upper = {{infinity, infinity, false}};
            for (double constant : constants) {  // constants are sorted
                if (constant < info.min) {
                    lower.push_back({constant, constant, false});
                    if (std::nextafter(constant, infinity) <= info.min) {
                        lower.push_back({std::nextafter(constant, infinity), constant, true});
                    }
                }
            }
            for (auto it = constants.rbegin(); it != constants.rend(); ++it) {
                if (*it > info.max) {
                    upper.push_back({*it, *it, false});
                    if (std::nextafter(*it, -infinity) >= info.max) {
                        upper.push_back({std::nextafter(*it, -infinity), *it, true});
                    }
                }
            }
            widen(info.min, lower, column.min, column.minStrict, column.minFor);
            widen(info.max, upper, column.max, column.maxStrict, column.maxFor);
            if (std::isinf(column.min)) {
                column.minFor.clear();
            }
            if (std::isinf(column.max)) {
                column.maxFor.clear();
            }
        }
        if (!column.notNull) {
            column.notNullFor.clear();
        }
        const bool needed = column.typeFixed || column.notNull || !std::isinf(column.min) || !std::isinf(column.max);
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
        return lost(base, analyse());
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
    certificate.tables.push_back(table);
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

void Certifier::certifyImages(const std::string& dataset, const ImageSetSchema& schema, const Proofs& base,
                              const std::vector<int>& sizes, Certificate& certificate) {
    ImageCertificate image;
    image.dataset = dataset;
    image.root = schema.root;
    const std::string key = SchemaProvider::imageKey(schema.root, schema.singleFile, schema.recursive);
    const auto keepFixed = [&](std::vector<std::string> reason) {
        image.fixed = true;
        image.fixedFor = std::move(reason);
        image.groups.clear();
        imageFacts_[key] = schema;
        imageFacts_[key].namesKnown = false;
        certificate.images.push_back(image);
    };
    if (schema.singleFile) {
        keepFixed({"a single image file (LOAD_IMAGE) is not certified"});
        return;
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
        keepFixed({"no file of the folder can be read"});
        return;
    }
    image.depths.assign(depths.begin(), depths.end());
    for (auto& [extension, group] : groups) {
        group.channels.assign(channels[extension].begin(), channels[extension].end());
        image.groups.push_back(group);
    }

    const auto run = [&] {
        imageFacts_[key] = cornerImages(image, schema);
        return lost(base, analyse());
    };
    // The box itself must keep the proofs: it also stands for folders that mix
    // the properties of the compiled files differently.
    const std::vector<std::string> boxLost = run();
    if (!boxLost.empty()) {
        keepFixed(boxLost);
        return;
    }

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
            const std::vector<std::string> broken = run();
            if (!broken.empty()) {
                image.groups[g].channels.erase(std::find(image.groups[g].channels.begin(),
                                                         image.groups[g].channels.end(), count));
                addAll(image.groups[g].channelsFor, broken);
            }
        }
        std::sort(image.groups[g].channels.begin(), image.groups[g].channels.end());

        // Size bounds, widened like column bounds: no bound, else the loosest program size.
        const auto widen = [&](int ImageGroupFacts::*bound, int loosest, bool lower,
                               std::vector<std::string> ImageGroupFacts::*neededBy) {
            const int original = image.groups[g].*bound;
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
                const std::vector<std::string> broken = run();
                if (k == 0) {
                    image.groups[g].*neededBy = broken;
                }
                if (broken.empty()) {
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
    certificate.images.push_back(image);
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
    for (const Instruction& in : program.code) {
        const bool numbers = in.op == Opcode::Compare || in.op == Opcode::FillNull ||
                             (in.op == Opcode::Check && in.text == "BETWEEN");
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

void printCertificate(std::ostream& out, const Certificate& certificate) {
    if (certificate.exactOnly) {
        out << "The proofs depend on the exact values of the input files, so the generated program\n"
            << "accepts only the files it was compiled with.\n";
        return;
    }
    out << "The compiled program stays correct for any input with these facts\n"
        << "(found by analysing the program " << certificate.analysisRuns << " more times with weaker facts):\n";
    const auto factLines = [&](size_t indent, const std::vector<std::pair<std::string, std::string>>& facts) {
        for (size_t i = 0; i < facts.size(); ++i) {
            if (i > 0) {
                out << std::string(indent, ' ');
            }
            out << std::left << std::setw(20) << facts[i].first << "<- " << facts[i].second << '\n';
        }
    };
    for (const TableCertificate& table : certificate.tables) {
        out << '\n' << table.key << "  (compiled with " << table.file << ")\n";
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
            if (!column.typeFixed) {
                out << "  " << std::left << std::setw(static_cast<int>(width)) << column.name << std::setw(9)
                    << "any type";
            } else {
                out << "  " << std::left << std::setw(static_cast<int>(width)) << column.name << std::setw(9)
                    << columnTypeName(column.type);
            }
            if (!column.read) {
                facts.insert(facts.begin(), {"(not read)", "loaded only to check the facts below"});
            }
            if (!column.required) {
                facts.emplace_back("may be missing", "only carried to the output");
            } else if (!column.read && !column.requiredFor.empty()) {
                facts.emplace_back("must exist", join(column.requiredFor));
            }
            if (!column.read && column.typeFixed && !column.typeFor.empty()) {
                facts.emplace_back("its type", join(column.typeFor));
            }
            if (facts.empty()) {
                out << "(any values)\n";
            }
            factLines(width + 11, facts);
        }
    }
    for (const ImageCertificate& image : certificate.images) {
        out << '\n' << image.dataset << "  (compiled with " << image.root << ")\n";
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
    if (!certificate.recheckedIfChanged.empty()) {
        out << "\nProven from the values, so checked again when an input changes:\n";
        for (const std::string& contract : certificate.recheckedIfChanged) {
            out << "  " << contract << '\n';
        }
    }
}
