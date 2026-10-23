#include "sema/ContractAnalyzer.h"

#include "ast/ASTPrinter.h"
#include "dataset/DatasetLinker.h"
#include "schema/SchemaProvider.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace {

// Why the compiler cannot read a column's values at this point of the program,
// or "" if it can: the values in the file are the values here, because no rows
// were removed and no values were changed since LOAD.
std::string valuesUnknown(const TableState& table, const ColumnState& column) {
    if (table.schema.released) {
        return "a private compilation sees only the released facts of " + table.schema.file;
    }
    if (table.schema.format == TableFormat::Parquet) {
        return "only the footer of " + table.schema.file + " is read at compile time";
    }
    return column.exact && !column.valuesChanged ? "" : "rows or values changed after LOAD";
}

// Private compilation: the released facts of the column if they still describe it here
// (no rows removed and no values changed since LOAD), else null.
const ColumnInfo* releasedInput(const TableState& table, const ColumnState& column) {
    if (!table.schema.released || column.rowsRemoved || column.valuesChanged) {
        return nullptr;
    }
    for (const ColumnInfo& info : table.schema.columns) {
        if (info.name == column.name) {
            return &info;
        }
    }
    return nullptr;
}

std::string rowText(size_t index) { return "row " + std::to_string(index + 1); }

std::string percent(double value) { return formatNumber(std::round(value * 100) / 100) + "%"; }

}  // namespace

ContractAnalyzer::ContractAnalyzer(Diagnostics& diagnostics, AnalysisResult& result)
    : diagnostics_(diagnostics), result_(result) {}

// ---------- Recording verdicts ----------

void ContractAnalyzer::proven(const ExpectStmt& node, const std::string& contract, const std::string& reason,
                              const Target* runtimeTarget, const std::vector<std::string>& valueTables) {
    result_.contracts.push_back(
        {node.location.line, contract, Verdict::Proven, reason, !valueTables.empty(), runtimeTarget != nullptr});
    result_.log.push_back("line " + std::to_string(node.location.line) + ": EXPECT " + contract + ": PROVEN, " +
                          reason);
    if (runtimeTarget) {
        result_.noEffect[{&node, targetKey(*runtimeTarget)}] = "contract proven at compile time: " + reason;
        if (!valueTables.empty()) {
            result_.valueProofs[{&node, targetKey(*runtimeTarget)}] = valueTables;
        }
    }
}

Diagnostic& ContractAnalyzer::violated(const ExpectStmt& node, const std::string& contract, const std::string& message,
                                       const std::string& hint) {
    result_.contracts.push_back({node.location.line, contract, Verdict::Violated, message});
    return diagnostics_.error(Phase::Contract, node.target.location, "contract violated: " + message, hint);
}

void ContractAnalyzer::deferred(const ExpectStmt& node, const std::string& contract, const std::string& reason) {
    result_.contracts.push_back({node.location.line, contract, Verdict::Deferred, reason});
    result_.log.push_back("line " + std::to_string(node.location.line) + ": EXPECT " + contract +
                          ": checked at run time, " + reason);
}

// A private compilation cannot report a contract as violated: the released answers are
// noisy and name no row. When they say the data has many values that break it, the
// contract stays a run-time check and the analyst gets this warning instead.
void ContractAnalyzer::likelyViolated(const ExpectStmt& node, const std::string& contract,
                                      const std::string& evidence) {
    diagnostics_.warning(Phase::Contract, node.target.location,
                         "EXPECT " + contract + " is likely violated: the released facts report " + evidence,
                         "the data owner's answers are differentially private; the check runs on the real data");
}

// ---------- Column contracts ----------

void ContractAnalyzer::columnContract(const ExpectStmt& node, const std::string& name, const Target& target,
                                      const TableState& table, ColumnState& column) {
    const std::string contract = name + " " + constraintText(node);
    const std::string type = columnTypeName(column.type);

    switch (node.constraint) {
        case ConstraintKind::IsNumber:
        case ConstraintKind::IsString:
        case ConstraintKind::IsBoolean: {
            // Column types come from the file and pandas enforces them when it
            // reads it, so a type contract is always decided at compile time.
            const bool holds = node.constraint == ConstraintKind::IsNumber   ? isNumeric(column.type)
                               : node.constraint == ConstraintKind::IsString ? column.type == ColumnType::String
                                                                             : column.type == ColumnType::Boolean;
            if (holds) {
                proven(node, contract, "the column is " + type, nullptr);
            } else {
                const std::string hint =
                    column.stringReason.empty()
                        ? ""
                        : "'" + column.name + "' is STRING because " + column.stringReason + " in " + table.schema.file;
                violated(node, contract, name + " should be " + constraintText(node) + " but it is " + type, hint);
            }
            return;
        }
        case ConstraintKind::NotNull: {
            if (!column.nullable) {
                proven(node, contract, "no missing values", &target);
                return;
            }
            std::int64_t missing = 0;
            bool missingKnown = true;
            for (const ColumnInfo& info : table.schema.columns) {
                if (info.name == column.name) {
                    missing = info.missingCount;
                    missingKnown = info.missingKnown;
                }
            }
            if (column.exact && missing > 0) {
                violated(node, contract,
                         name + " has " + (missingKnown ? "" : "at least ") + std::to_string(missing) +
                             " missing value" + (missing == 1 ? "" : "s") + " in " + table.schema.file,
                         "use FILL_NULL or DROP_NULL before this EXPECT");
                return;
            }
            if (const ColumnInfo* info = releasedInput(table, column); info && info->seenMissing) {
                likelyViolated(node, contract, "missing values in " + name);
            }
            column.nullable = false;  // rows only get past the run-time check if nothing is missing
            deferred(node, contract,
                     missingKnown            ? "missing values may remain after earlier statements"
                     : table.schema.released ? "the released facts do not say that no value is missing"
                                             : "the file's statistics do not say whether values are missing");
            return;
        }
        case ConstraintKind::Between: between(node, contract, name, target, table, column); return;
        case ConstraintKind::Unique:  unique(node, contract, name, target, table, column); return;
        default:                      return;  // the other contracts have their own functions
    }
}

void ContractAnalyzer::between(const ExpectStmt& node, const std::string& contract, const std::string& name,
                               const Target& target, const TableState& table, ColumnState& column) {
    if (!isNumeric(column.type)) {
        diagnostics_.error(Phase::Type, node.target.location,
                           "BETWEEN needs a numeric column, but " + name + " is " + columnTypeName(column.type));
        return;
    }
    const double low = std::stod(node.low);
    const double high = std::stod(node.high);
    if (low > high) {
        diagnostics_.error(Phase::Semantic, node.target.location,
                           "BETWEEN " + node.low + " AND " + node.high + " is an empty range",
                           "write the smaller number first");
        return;
    }

    if (column.range.min >= low && column.range.max <= high) {
        proven(node, contract, "values are in " + column.range.text(), &target);
    } else if (column.exact && column.range.isKnown()) {
        std::string message =
            name + " has values from " + formatNumber(column.range.min) + " to " + formatNumber(column.range.max);
        if (valuesUnknown(table, column).empty()) {  // name the first bad row
            const std::vector<std::string> values = readColumnValues(table.schema, column.name);
            for (size_t i = 0; i < values.size(); ++i) {
                if (!values[i].empty() && (std::stod(values[i]) < low || std::stod(values[i]) > high)) {
                    message += "; " + rowText(i) + " of " + table.schema.file + " has " + values[i];
                    break;
                }
            }
        } else if (table.schema.format == TableFormat::Parquet && !column.valuesChanged) {
            message += ", according to the statistics in the footer of " + table.schema.file;
        }
        violated(node, contract, message, "every value must be between " + node.low + " and " + node.high);
    } else {
        if (const ColumnInfo* info = releasedInput(table, column)) {
            // values < k (or <= k) were seen: below `low` if k <= low (or k < low); likewise above
            if (!std::isinf(info->seenBelow) &&
                (info->seenBelowOrAt ? info->seenBelow < low : info->seenBelow <= low)) {
                likelyViolated(node, contract,
                               "values of " + name + (info->seenBelowOrAt ? " at or below " : " below ") +
                                   formatNumber(info->seenBelow));
            } else if (!std::isinf(info->seenAbove) &&
                       (info->seenAboveOrAt ? info->seenAbove > high : info->seenAbove >= high)) {
                likelyViolated(node, contract,
                               "values of " + name + (info->seenAboveOrAt ? " at or above " : " above ") +
                                   formatNumber(info->seenAbove));
            }
        }
        column.range.min = std::max(column.range.min, low);  // true after the run-time check passes
        column.range.max = std::min(column.range.max, high);
        column.exact = false;  // but low and high need not occur
        deferred(node, contract, "the exact values are not known here");
    }
}

void ContractAnalyzer::unique(const ExpectStmt& node, const std::string& contract, const std::string& name,
                              const Target& target, const TableState& table, const ColumnState& column) {
    const std::string unknown = valuesUnknown(table, column);
    if (!unknown.empty()) {
        deferred(node, contract, unknown);
        return;
    }
    const std::vector<std::string> values = readColumnValues(table.schema, column.name);
    std::map<std::string, std::vector<size_t>> rows;
    for (size_t i = 0; i < values.size(); ++i) {
        if (!values[i].empty()) {
            rows[values[i]].push_back(i);
        }
    }
    for (const std::string& value : values) {
        const std::vector<size_t>& found = value.empty() ? std::vector<size_t>{} : rows[value];
        if (found.size() > 1) {
            violated(node, contract,
                     name + " has repeated values: \"" + value + "\" appears " + std::to_string(found.size()) +
                         " times (" + rowText(found[0]) + ", " + rowText(found[1]) + (found.size() > 2 ? ", ..." : "") +
                         ")",
                     "");
            return;
        }
    }
    proven(node, contract, "all " + std::to_string(rows.size()) + " values are different", &target,
           {tableKey(target.dataset, target.table)});
}

// ---------- Cross-table contracts ----------

void ContractAnalyzer::references(const ExpectStmt& node, const std::string& name, const Target& target,
                                  const TableState& table, const ColumnState& column, const std::string& otherName,
                                  const TableState& otherTable, const ColumnState& otherColumn) {
    const std::string contract = name + " REFERENCES " + otherName;
    const bool comparable = (isNumeric(column.type) && isNumeric(otherColumn.type)) || column.type == otherColumn.type;
    if (!comparable) {
        diagnostics_.error(Phase::Type, node.target.location,
                           std::string("REFERENCES compares ") + columnTypeName(column.type) + " column " + name +
                               " with " + columnTypeName(otherColumn.type) + " column " + otherName,
                           "both columns must hold the same kind of value");
        return;
    }
    const std::string unknown =
        valuesUnknown(table, column).empty() ? valuesUnknown(otherTable, otherColumn) : valuesUnknown(table, column);
    if (!unknown.empty()) {
        deferred(node, contract, unknown);
        return;
    }

    // Numbers are compared as numbers, so 7 matches 7.0.
    const std::vector<std::string> source = readColumnValues(table.schema, column.name);
    const std::vector<std::string> keys = readColumnValues(otherTable.schema, otherColumn.name);
    const bool numeric = isNumeric(column.type);
    std::set<std::string> textKeys(keys.begin(), keys.end());
    std::set<double> numberKeys;
    for (const std::string& key : keys) {
        if (numeric && !key.empty()) {
            numberKeys.insert(std::stod(key));
        }
    }
    std::vector<std::string> missing;
    int checked = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i].empty()) {
            continue;
        }
        ++checked;
        const bool found = numeric ? numberKeys.count(std::stod(source[i])) != 0 : textKeys.count(source[i]) != 0;
        if (!found) {
            missing.push_back(rowText(i) + ": " + source[i]);
        }
    }
    if (missing.empty()) {
        proven(node, contract, "all " + std::to_string(checked) + " values appear in " + otherName, &target,
               {tableKey(target.dataset, target.table), tableKey(target.args[0], target.args[1])});
        return;
    }
    Diagnostic& error = violated(node, contract,
                                 std::to_string(missing.size()) + " of " + std::to_string(checked) + " values of " +
                                     name + " have no match in " + otherName,
                                 "");
    for (size_t i = 0; i < missing.size() && i < 5; ++i) {
        error.notes.push_back(missing[i]);
    }
}

void ContractAnalyzer::schemaSameAs(const ExpectStmt& node, const std::string& name, const TableState& table,
                                    const std::string& otherName, const TableState& other) {
    const std::string contract = name + " SCHEMA_SAME_AS " + otherName;
    const auto excluded = [&](const std::string& column) {
        return std::find(node.except.begin(), node.except.end(), column) != node.except.end();
    };
    std::vector<std::string> problems;
    for (const ColumnState& column : table.columns) {
        const ColumnState* partner = nullptr;
        for (const ColumnState& candidate : other.columns) {
            partner = candidate.name == column.name ? &candidate : partner;
        }
        if (excluded(column.name)) {
            continue;
        }
        if (!partner) {
            problems.push_back(column.name + " is in " + name + " but not in " + otherName);
        } else if (column.type != partner->type && !(isNumeric(column.type) && isNumeric(partner->type))) {
            problems.push_back(column.name + " is " + columnTypeName(column.type) + " in " + name + " but " +
                               columnTypeName(partner->type) + " in " + otherName);
        }
    }
    for (const ColumnState& column : other.columns) {
        const bool known = std::any_of(table.columns.begin(), table.columns.end(),
                                       [&](const ColumnState& c) { return c.name == column.name; });
        if (!known && !excluded(column.name)) {
            problems.push_back(column.name + " is in " + otherName + " but not in " + name);
        }
    }
    if (problems.empty()) {
        proven(node, contract, "same columns and compatible types", nullptr);
        return;
    }
    Diagnostic& error = violated(node, contract, name + " and " + otherName + " have different schemas",
                                 "list columns that may differ in EXCEPT [...]");
    error.notes = problems;
}

// ---------- Image and dataset contracts ----------

void ContractAnalyzer::imageContract(const ExpectStmt& node, const DatasetSymbol& images) {
    const std::string contract = images.name + " " + constraintText(node);
    const int limit = std::stoi(node.low);
    std::vector<std::string> bad;
    std::map<std::string, int> counts;  // e.g. "3 channels" -> 10 images
    int valid = 0;
    for (const ImageShape& shape : images.images.shapes) {
        if (shape.corrupt) {
            continue;
        }
        ++valid;
        std::string fact;
        bool ok = true;
        if (node.constraint == ConstraintKind::Channels) {
            fact = std::to_string(shape.channels) + " channel" + (shape.channels == 1 ? "" : "s");
            ok = shape.channels == limit;
        } else {
            fact = std::to_string(shape.width) + "x" + std::to_string(shape.height);
            ok = node.constraint == ConstraintKind::MinWidth ? shape.width >= limit : shape.height >= limit;
        }
        ++counts[fact];
        if (!ok) {
            bad.push_back(shape.path + ": " + fact);
        }
    }
    if (bad.empty()) {
        proven(node, contract, "all " + std::to_string(valid) + " images satisfy it (read from the file headers)",
               nullptr);
        return;
    }
    const std::string hint = node.constraint == ConstraintKind::Channels
                                 ? "use GRAYSCALE, or fix the files"
                                 : "RESIZE before this EXPECT, or fix the files";
    Diagnostic& error = violated(node, contract,
                                 std::to_string(bad.size()) + " of " + std::to_string(valid) + " images in " +
                                     images.name + " break " + constraintText(node),
                                 hint);
    if (node.constraint == ConstraintKind::Channels) {
        for (const auto& [fact, count] : counts) {
            error.notes.push_back(fact + ": " + std::to_string(count) + " image" + (count == 1 ? "" : "s"));
        }
    }
    for (size_t i = 0; i < bad.size() && i < 5; ++i) {
        error.notes.push_back(bad[i]);
    }
}

void ContractAnalyzer::matchRate(const ExpectStmt& node, const DatasetSymbol& linked, const Target& target) {
    const std::string contract = linked.name + " MATCH_RATE " + node.low + "%";
    const LinkState& link = linked.link;
    if (!link.exact) {
        deferred(node, contract, "the labels changed before LINK");
        return;
    }
    const double rate = ::matchRate(link);  // the DatasetLinker function, not this member
    if (rate >= std::stod(node.low)) {
        proven(node, contract, "match rate is " + percent(rate), &target, {link.table, link.images});
        return;
    }
    Diagnostic& error =
        violated(node, contract,
                 "match rate of " + linked.name + " is " + percent(rate) + ", expected at least " + node.low + "%",
                 "fix the file names in " + link.table + "." + link.column + " or the image files");
    error.notes.push_back("images: " + std::to_string(link.imageCount) + ", labels: " +
                          std::to_string(link.labelCount) + ", matched: " + std::to_string(link.matched));
    const auto examples = [](const std::vector<std::string>& items) {
        std::string text;
        for (size_t i = 0; i < items.size() && i < 3; ++i) {
            text += (i > 0 ? ", " : "") + items[i];
        }
        return text + (items.size() > 3 ? ", ..." : "");
    };
    if (!link.imagesWithoutLabel.empty()) {
        error.notes.push_back("images without a label: " + std::to_string(link.imagesWithoutLabel.size()) + " (" +
                              examples(link.imagesWithoutLabel) + ")");
    }
    if (!link.labelsWithoutImage.empty()) {
        error.notes.push_back("labels without an image: " + std::to_string(link.labelsWithoutImage.size()) + " (" +
                              examples(link.labelsWithoutImage) + ")");
    }
}
