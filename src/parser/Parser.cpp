#include "parser/Parser.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

// Thrown after a syntax error has been reported. parse() catches it and
// skips to the next statement (panic-mode recovery).
struct ParseError {};

const std::vector<std::string> statementKeywords = {
    "LOAD",     "LOAD_TABLES", "LOAD_IMAGE",     "LOAD_IMAGES", "DROP_NULL", "FILL_NULL", "NORMALIZE",
    "FILTER",   "SELECT",      "DROP",           "PARSE_DATE",  "DERIVE",    "JOIN",      "AGGREGATE",
    "SAVE",     "SAVE_TABLES", "SAVE_IMAGE",     "SAVE_IMAGES", "EXPECT",    "RESIZE",    "GRAYSCALE",
    "COMPRESS", "CONVERT",     "REMOVE_CORRUPT", "LINK"};

// How a token is shown in "found ..." messages.
std::string describe(const Token& token) {
    switch (token.type) {
        case TokenType::END_OF_FILE:    return "end of file";
        case TokenType::STRING_LITERAL: return "\"" + token.lexeme + "\"";
        default:                        return "'" + token.lexeme + "'";
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

// program -> { statement } END_OF_FILE
Program Parser::parse() {
    Program program;
    while (!isAtEnd()) {
        try {
            program.statements.push_back(statement());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    return program;
}

// ---------- Statements ----------

// Every statement starts with a different keyword, so one token of lookahead
// is enough to choose the rule. That makes the grammar LL(1).
std::unique_ptr<Stmt> Parser::statement() {
    switch (peek().type) {
        case TokenType::LOAD:           return loadStatement(DataKind::Table);
        case TokenType::LOAD_TABLES:    return loadStatement(DataKind::TableSet);
        case TokenType::LOAD_IMAGE:     return loadStatement(DataKind::Image);
        case TokenType::LOAD_IMAGES:    return loadStatement(DataKind::ImageSet);
        case TokenType::DROP_NULL:      return dropNullStatement();
        case TokenType::FILL_NULL:      return fillNullStatement();
        case TokenType::NORMALIZE:      return normalizeStatement();
        case TokenType::FILTER:         return filterStatement();
        case TokenType::SELECT:         return selectStatement();
        case TokenType::DROP:           return dropStatement();
        case TokenType::PARSE_DATE:     return parseDateStatement();
        case TokenType::DERIVE:         return deriveStatement();
        case TokenType::JOIN:           return joinStatement();
        case TokenType::AGGREGATE:      return aggregateStatement();
        case TokenType::SAVE:           return saveStatement(DataKind::Table);
        case TokenType::SAVE_TABLES:    return saveStatement(DataKind::TableSet);
        case TokenType::SAVE_IMAGE:     return saveStatement(DataKind::Image);
        case TokenType::SAVE_IMAGES:    return saveStatement(DataKind::ImageSet);
        case TokenType::EXPECT:         return expectStatement();
        case TokenType::RESIZE:         return resizeStatement();
        case TokenType::GRAYSCALE:      return grayscaleStatement();
        case TokenType::COMPRESS:       return compressStatement();
        case TokenType::CONVERT:        return convertStatement();
        case TokenType::REMOVE_CORRUPT: return removeCorruptStatement();
        case TokenType::LINK:           return linkStatement();
        default:                        break;
    }
    const std::string hint = check(TokenType::IDENTIFIER) ? suggestClosest(peek().lexeme, statementKeywords) : "";
    errorAt(peek().location, "expected a statement (LOAD, FILTER, RESIZE, SAVE, ...), found " + describe(peek()), hint);
}

// load -> ( LOAD | LOAD_TABLES | LOAD_IMAGE | LOAD_IMAGES ) STRING_LITERAL AS IDENTIFIER [ RECURSIVE ] ;
// RECURSIVE is only allowed after LOAD_IMAGES.
std::unique_ptr<Stmt> Parser::loadStatement(DataKind kind) {
    auto node = std::make_unique<LoadStmt>();
    node->location = advance().location;
    node->kind = kind;
    node->path = consume(TokenType::STRING_LITERAL, "a file or folder name in double quotes").lexeme;
    consume(TokenType::AS, "'AS'");
    node->dataset = identifier("a dataset name");
    if (kind == DataKind::ImageSet) {
        node->recursive = match(TokenType::RECURSIVE);
    }
    endStatement();
    return node;
}

// drop_null -> DROP_NULL table_selector [ column_list ] ;
std::unique_ptr<Stmt> Parser::dropNullStatement() {
    auto node = std::make_unique<DropNullStmt>();
    node->location = advance().location;
    node->target = tableSelector();
    if (check(TokenType::LEFT_BRACKET)) {
        node->columns = columnList();
    }
    endStatement();
    return node;
}

// fill_null -> FILL_NULL column_selector WITH ( literal | MEAN | MEDIAN | MODE ) [ IF_EXISTS ] ;
std::unique_ptr<Stmt> Parser::fillNullStatement() {
    auto node = std::make_unique<FillNullStmt>();
    node->location = advance().location;
    node->target = columnSelector();
    consume(TokenType::WITH, "'WITH'");
    if (match(TokenType::MEAN)) {
        node->method = FillMethod::Mean;
    } else if (match(TokenType::MEDIAN)) {
        node->method = FillMethod::Median;
    } else if (match(TokenType::MODE)) {
        node->method = FillMethod::Mode;
    } else {
        node->value = literal();
    }
    optionalIfExists(node->target);
    endStatement();
    return node;
}

// normalize -> NORMALIZE column_selector ( RANGE number number | [ USING ( ZSCORE | MINMAX ) ] ) [ IF_EXISTS ] ;
// RANGE is for images, USING for table columns.
std::unique_ptr<Stmt> Parser::normalizeStatement() {
    auto node = std::make_unique<NormalizeStmt>();
    node->location = advance().location;
    node->target = columnSelector();
    if (match(TokenType::RANGE)) {
        node->method = NormalizeMethod::Range;
        node->low = number();
        node->high = number();
    } else if (match(TokenType::USING)) {
        if (match(TokenType::ZSCORE)) {
            node->method = NormalizeMethod::ZScore;
        } else if (match(TokenType::MINMAX)) {
            node->method = NormalizeMethod::MinMax;
        } else {
            errorExpected("ZSCORE or MINMAX after USING");
        }
    }
    optionalIfExists(node->target);
    endStatement();
    return node;
}

// filter -> FILTER table_ref WHERE or_expr ;
std::unique_ptr<Stmt> Parser::filterStatement() {
    auto node = std::make_unique<FilterStmt>();
    node->location = advance().location;
    node->target = tableRef();
    consume(TokenType::WHERE, "'WHERE'");
    node->condition = orExpression();
    endStatement();
    return node;
}

// select -> SELECT table_ref column_list ;
std::unique_ptr<Stmt> Parser::selectStatement() {
    auto node = std::make_unique<SelectStmt>();
    node->location = advance().location;
    node->target = tableRef();
    node->columns = columnList();
    endStatement();
    return node;
}

// drop -> DROP table_selector column_list [ IF_EXISTS ] ;
std::unique_ptr<Stmt> Parser::dropStatement() {
    auto node = std::make_unique<DropStmt>();
    node->location = advance().location;
    node->target = tableSelector();
    node->columns = columnList();
    optionalIfExists(node->target);
    endStatement();
    return node;
}

// parse_date -> PARSE_DATE column_selector ;
std::unique_ptr<Stmt> Parser::parseDateStatement() {
    auto node = std::make_unique<ParseDateStmt>();
    node->location = advance().location;
    node->target = columnSelector();
    endStatement();
    return node;
}

// derive -> DERIVE path = sum ;          path: table.column or tableset.table.column
std::unique_ptr<Stmt> Parser::deriveStatement() {
    auto node = std::make_unique<DeriveStmt>();
    node->location = advance().location;
    node->target = path();
    if (node->target.parts.size() < 2 || node->target.parts.size() > 3) {
        errorAt(node->target.location, "DERIVE needs table.column: the table and the name of the new column");
    }
    consume(TokenType::EQUAL, "'=' after the new column");
    node->value = sum();
    endStatement();
    return node;
}

// join -> JOIN table_ref WITH table_ref ON IDENTIFIER [ MANY ] ;
// MANY is matched as a word, not reserved.
std::unique_ptr<Stmt> Parser::joinStatement() {
    auto node = std::make_unique<JoinStmt>();
    node->location = advance().location;
    node->target = tableRef();
    consume(TokenType::WITH, "'WITH' and the lookup table");
    node->other = tableRef();
    consume(TokenType::ON, "'ON' and the key column");
    node->key = identifier("the key column both tables have");
    node->many = matchWord("MANY");
    endStatement();
    return node;
}

// aggregate      -> AGGREGATE table_ref BY column_list aggregate_item { , aggregate_item } ;
// aggregate_item -> ( COUNT | ( SUM | MEAN | MEDIAN | MIN | MAX | STD | COUNT_DISTINCT ) IDENTIFIER )
//                   AS IDENTIFIER
// BY, COUNT, SUM, MIN, MAX, STD and COUNT_DISTINCT are matched as words, not reserved: columns keep
// these names.
std::unique_ptr<Stmt> Parser::aggregateStatement() {
    auto node = std::make_unique<AggregateStmt>();
    node->location = advance().location;
    node->target = tableRef();
    if (!matchWord("BY")) {
        errorExpected("'BY' and the columns to group by");
    }
    node->by = columnList();
    do {
        AggregateItem item;
        item.location = peek().location;
        if (match(TokenType::MEAN)) {
            item.function = "MEAN";
        } else if (match(TokenType::MEDIAN)) {
            item.function = "MEDIAN";
        } else {
            for (const char* function : {"COUNT_DISTINCT", "COUNT", "SUM", "MIN", "MAX", "STD"}) {
                if (item.function.empty() && matchWord(function)) {
                    item.function = function;
                }
            }
        }
        if (item.function.empty()) {
            errorExpected("COUNT, SUM, MEAN, MEDIAN, MIN, MAX, STD or COUNT_DISTINCT");
        }
        if (item.function != "COUNT") {
            item.column = identifier("the column " + item.function + " reads");
        }
        consume(TokenType::AS, "'AS' and the name of the new column");
        item.name = identifier("the name of the new column");
        node->items.push_back(item);
    } while (match(TokenType::COMMA));
    endStatement();
    return node;
}

// save -> SAVE table_ref AS STRING_LITERAL ;          SAVE_TABLES IDENTIFIER TO STRING_LITERAL ;
//         SAVE_IMAGE IDENTIFIER AS STRING_LITERAL ;   SAVE_IMAGES IDENTIFIER TO STRING_LITERAL ;
std::unique_ptr<Stmt> Parser::saveStatement(DataKind kind) {
    auto node = std::make_unique<SaveStmt>();
    node->location = advance().location;
    node->kind = kind;
    if (kind == DataKind::Table) {
        node->target = tableRef();
    } else {
        node->target.location = peek().location;
        node->target.parts.push_back(identifier("a dataset name"));
    }
    const bool intoFolder = kind == DataKind::TableSet || kind == DataKind::ImageSet;
    consume(intoFolder ? TokenType::TO : TokenType::AS, intoFolder ? "'TO'" : "'AS'");
    node->path = consume(TokenType::STRING_LITERAL, "a file or folder name in double quotes").lexeme;
    endStatement();
    return node;
}

// expect     -> EXPECT column_selector constraint [ IF_EXISTS ] ;
// constraint -> NUMBER | STRING | BOOLEAN | NOT_NULL | BETWEEN number AND number | UNIQUE
//             | REFERENCES path | SCHEMA_SAME_AS table_ref [ EXCEPT column_list ]
//             | CHANNELS integer | MIN_WIDTH integer | MIN_HEIGHT integer | MATCH_RATE number %
std::unique_ptr<Stmt> Parser::expectStatement() {
    auto node = std::make_unique<ExpectStmt>();
    node->location = advance().location;
    node->target = columnSelector();
    if (match(TokenType::NUMBER)) {
        node->constraint = ConstraintKind::IsNumber;
    } else if (match(TokenType::STRING)) {
        node->constraint = ConstraintKind::IsString;
    } else if (match(TokenType::BOOLEAN)) {
        node->constraint = ConstraintKind::IsBoolean;
    } else if (match(TokenType::NOT_NULL)) {
        node->constraint = ConstraintKind::NotNull;
    } else if (match(TokenType::BETWEEN)) {
        node->constraint = ConstraintKind::Between;
        node->low = number();
        consume(TokenType::AND, "'AND'");
        node->high = number();
    } else if (match(TokenType::UNIQUE)) {
        node->constraint = ConstraintKind::Unique;
    } else if (match(TokenType::REFERENCES)) {
        node->constraint = ConstraintKind::References;
        node->other = path();
    } else if (match(TokenType::SCHEMA_SAME_AS)) {
        node->constraint = ConstraintKind::SchemaSameAs;
        node->other = tableRef();
        if (match(TokenType::EXCEPT)) {
            node->except = columnList();
        }
    } else if (match(TokenType::CHANNELS)) {
        node->constraint = ConstraintKind::Channels;
        node->low = std::to_string(integer("the number of channels"));
    } else if (match(TokenType::MIN_WIDTH)) {
        node->constraint = ConstraintKind::MinWidth;
        node->low = std::to_string(integer("a width in pixels"));
    } else if (match(TokenType::MIN_HEIGHT)) {
        node->constraint = ConstraintKind::MinHeight;
        node->low = std::to_string(integer("a height in pixels"));
    } else if (match(TokenType::MATCH_RATE)) {
        node->constraint = ConstraintKind::MatchRate;
        node->low = number();
        consume(TokenType::PERCENT, "'%' after the match rate");
    } else {
        errorExpected("a contract such as NUMBER, NOT_NULL, BETWEEN, UNIQUE, REFERENCES or CHANNELS");
    }
    optionalIfExists(node->target);
    endStatement();
    return node;
}

// resize -> RESIZE IDENTIFIER TO integer integer [ KEEP_ASPECT ] ;
std::unique_ptr<Stmt> Parser::resizeStatement() {
    auto node = std::make_unique<ResizeStmt>();
    node->location = advance().location;
    node->dataset = identifier("an image dataset name");
    consume(TokenType::TO, "'TO'");
    node->width = integer("a width in pixels");
    node->height = integer("a height in pixels");
    node->keepAspect = match(TokenType::KEEP_ASPECT);
    endStatement();
    return node;
}

// grayscale -> GRAYSCALE IDENTIFIER ;
std::unique_ptr<Stmt> Parser::grayscaleStatement() {
    auto node = std::make_unique<GrayscaleStmt>();
    node->location = advance().location;
    node->dataset = identifier("an image dataset name");
    endStatement();
    return node;
}

// compress -> COMPRESS IDENTIFIER QUALITY integer ;
std::unique_ptr<Stmt> Parser::compressStatement() {
    auto node = std::make_unique<CompressStmt>();
    node->location = advance().location;
    node->dataset = identifier("an image dataset name");
    consume(TokenType::QUALITY, "'QUALITY'");
    node->quality = integer("a quality from 1 to 100");
    endStatement();
    return node;
}

// convert -> CONVERT IDENTIFIER TO ( JPG | PNG | NPY ) ;
std::unique_ptr<Stmt> Parser::convertStatement() {
    auto node = std::make_unique<ConvertStmt>();
    node->location = advance().location;
    node->dataset = identifier("an image dataset name");
    consume(TokenType::TO, "'TO'");
    if (match(TokenType::JPG) || match(TokenType::PNG) || match(TokenType::NPY)) {
        node->format = previous().lexeme;
        for (char& c : node->format) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    } else {
        errorExpected("an image format: JPG, PNG or NPY");
    }
    endStatement();
    return node;
}

// remove_corrupt -> REMOVE_CORRUPT IDENTIFIER ;
std::unique_ptr<Stmt> Parser::removeCorruptStatement() {
    auto node = std::make_unique<RemoveCorruptStmt>();
    node->location = advance().location;
    node->dataset = identifier("an image dataset name");
    endStatement();
    return node;
}

// link -> LINK IDENTIFIER WITH IDENTIFIER ON IDENTIFIER AS IDENTIFIER ;
std::unique_ptr<Stmt> Parser::linkStatement() {
    auto node = std::make_unique<LinkStmt>();
    node->location = advance().location;
    node->images = identifier("an image dataset name");
    consume(TokenType::WITH, "'WITH'");
    node->table = identifier("a table name");
    consume(TokenType::ON, "'ON'");
    node->column = identifier("the column with the image file names");
    consume(TokenType::AS, "'AS'");
    node->dataset = identifier("a name for the linked dataset");
    endStatement();
    return node;
}

// ---------- FILTER conditions ----------
// Precedence from lowest to highest: OR, AND, NOT, comparison.
// Each level calls the next one, so higher levels group first.

// or_expr -> and_expr { OR and_expr }
std::unique_ptr<Expr> Parser::orExpression() {
    std::unique_ptr<Expr> left = andExpression();
    while (match(TokenType::OR)) {
        auto node = std::make_unique<LogicalExpr>();
        node->op = LogicalOp::Or;
        node->left = std::move(left);
        node->right = andExpression();
        left = std::move(node);
    }
    return left;
}

// and_expr -> not_expr { AND not_expr }
std::unique_ptr<Expr> Parser::andExpression() {
    std::unique_ptr<Expr> left = notExpression();
    while (match(TokenType::AND)) {
        auto node = std::make_unique<LogicalExpr>();
        node->op = LogicalOp::And;
        node->left = std::move(left);
        node->right = notExpression();
        left = std::move(node);
    }
    return left;
}

// not_expr -> NOT not_expr | primary
std::unique_ptr<Expr> Parser::notExpression() {
    if (match(TokenType::NOT)) {
        auto node = std::make_unique<NotExpr>();
        node->operand = notExpression();
        return node;
    }
    return primary();
}

// primary -> ( or_expr ) | IDENTIFIER comparison_op literal
std::unique_ptr<Expr> Parser::primary() {
    if (match(TokenType::LEFT_PAREN)) {
        std::unique_ptr<Expr> inner = orExpression();
        consume(TokenType::RIGHT_PAREN, "')'");
        return inner;
    }

    auto node = std::make_unique<ComparisonExpr>();
    node->location = peek().location;
    node->column = identifier("a column name or '('");
    switch (peek().type) {
        case TokenType::EQUAL_EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::LESS:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER:
        case TokenType::GREATER_EQUAL: node->op = advance().lexeme; break;
        case TokenType::EQUAL:
            errorAt(peek().location, "expected a comparison operator, found '='", "use '==' to compare values");
        default: errorExpected("a comparison operator (==, !=, <, <=, >, >=)");
    }
    node->value = literal();
    return node;
}

// ---------- DERIVE values ----------
// Precedence from lowest to highest: + and -, * and /, unary -, operands.

// sum -> product { ( + | - ) product }
std::shared_ptr<const ValueExpr> Parser::sum() {
    std::shared_ptr<const ValueExpr> left = product();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        auto node = std::make_shared<ValueExpr>();
        node->kind = ValueKind::Binary;
        node->location = peek().location;
        node->text = advance().lexeme;
        node->left = left;
        node->right = product();
        left = node;
    }
    return left;
}

// product -> unary { ( * | / ) unary }
std::shared_ptr<const ValueExpr> Parser::product() {
    std::shared_ptr<const ValueExpr> left = unary();
    while (check(TokenType::STAR) || check(TokenType::SLASH)) {
        auto node = std::make_shared<ValueExpr>();
        node->kind = ValueKind::Binary;
        node->location = peek().location;
        node->text = advance().lexeme;
        node->left = left;
        node->right = unary();
        left = node;
    }
    return left;
}

// unary -> - unary | operand        (-5 is a number; -x is 0 - x)
std::shared_ptr<const ValueExpr> Parser::unary() {
    if (!check(TokenType::MINUS)) {
        return operand();
    }
    const SourceLocation location = advance().location;
    std::shared_ptr<const ValueExpr> inner = unary();
    if (inner->kind == ValueKind::Number && inner->text.front() != '-') {
        auto negative = std::make_shared<ValueExpr>(*inner);
        negative->text = "-" + inner->text;
        return negative;
    }
    auto zero = std::make_shared<ValueExpr>();
    zero->text = "0";
    zero->location = location;
    auto node = std::make_shared<ValueExpr>();
    node->kind = ValueKind::Binary;
    node->location = location;
    node->text = "-";
    node->left = zero;
    node->right = inner;
    return node;
}

// operand   -> NUMBER_LITERAL | IDENTIFIER | date_part ( IDENTIFIER ) | ( sum )
// date_part -> YEAR | MONTH | DAY | HOUR | WEEKDAY     (names, not keywords: a column may be called hour)
std::shared_ptr<const ValueExpr> Parser::operand() {
    if (match(TokenType::LEFT_PAREN)) {
        std::shared_ptr<const ValueExpr> inner = sum();
        consume(TokenType::RIGHT_PAREN, "')'");
        return inner;
    }
    auto node = std::make_shared<ValueExpr>();
    node->location = peek().location;
    if (check(TokenType::NUMBER_LITERAL)) {
        node->text = advance().lexeme;
        return node;
    }
    node->kind = ValueKind::Column;
    node->text = identifier("a column name, a number or '('");
    if (!match(TokenType::LEFT_PAREN)) {
        return node;
    }
    std::string part;
    for (char c : node->text) {
        part += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (std::find(dateParts().begin(), dateParts().end(), part) == dateParts().end()) {
        errorAt(node->location, "unknown function '" + node->text + "'",
                "DERIVE knows YEAR, MONTH, DAY, HOUR and WEEKDAY of a DATETIME column");
    }
    auto column = std::make_shared<ValueExpr>();
    column->kind = ValueKind::Column;
    column->location = peek().location;
    column->text = identifier("a DATETIME column");
    consume(TokenType::RIGHT_PAREN, "')'");
    node->kind = ValueKind::DatePart;
    node->text = part;
    node->left = column;
    return node;
}

// ---------- Selectors ----------

// table_ref -> IDENTIFIER [ . IDENTIFIER ]            one table: t or ts.name
Selector Parser::tableRef() {
    Selector selector;
    selector.location = peek().location;
    selector.parts.push_back(identifier("a dataset name"));
    if (match(TokenType::DOT)) {
        selector.parts.push_back(identifier("a table name"));
    }
    return selector;
}

// table_selector -> IDENTIFIER [ . ( IDENTIFIER | * ) ]    one or more tables
Selector Parser::tableSelector() {
    Selector selector;
    selector.location = peek().location;
    selector.parts.push_back(identifier("a dataset name"));
    if (match(TokenType::DOT)) {
        selector.parts.push_back(match(TokenType::STAR) ? "*" : identifier("a table name or '*'"));
    }
    return selector;
}

// column_selector -> IDENTIFIER { . ( IDENTIFIER | * ) }
//                    [ WHERE ( TYPE ( NUMERIC | STRING | BOOLEAN ) | NULLABLE ) ] [ EXCEPT column_list ]
Selector Parser::columnSelector() {
    Selector selector;
    selector.location = peek().location;
    selector.parts.push_back(identifier("a dataset name"));
    while (match(TokenType::DOT)) {
        selector.parts.push_back(match(TokenType::STAR) ? "*" : identifier("a table or column name, or '*'"));
    }
    if (selector.parts.size() > 3) {
        errorAt(selector.location, "a selector has at most three parts: dataset.table.column");
    }
    if (match(TokenType::WHERE)) {
        if (match(TokenType::NULLABLE)) {
            selector.filter = SelectorFilter::Nullable;
        } else {
            consume(TokenType::TYPE, "TYPE or NULLABLE after WHERE");
            if (match(TokenType::NUMERIC) || match(TokenType::NUMBER)) {
                selector.filter = SelectorFilter::Numeric;
            } else if (match(TokenType::STRING)) {
                selector.filter = SelectorFilter::String;
            } else if (match(TokenType::BOOLEAN)) {
                selector.filter = SelectorFilter::Boolean;
            } else {
                errorExpected("NUMERIC, STRING or BOOLEAN after TYPE");
            }
        }
    }
    if (match(TokenType::EXCEPT)) {
        selector.except = columnList();
    }
    return selector;
}

// path -> IDENTIFIER { . IDENTIFIER }       a column without wildcards
Selector Parser::path() {
    Selector selector;
    selector.location = peek().location;
    selector.parts.push_back(identifier("a dataset name"));
    while (match(TokenType::DOT)) {
        selector.parts.push_back(identifier("a table or column name"));
    }
    return selector;
}

void Parser::optionalIfExists(Selector& selector) { selector.ifExists = match(TokenType::IF_EXISTS); }

// ---------- Shared pieces ----------

// column_list -> [ IDENTIFIER { , IDENTIFIER } ]
std::vector<std::string> Parser::columnList() {
    consume(TokenType::LEFT_BRACKET, "'[' before the column names");
    std::vector<std::string> columns;
    columns.push_back(identifier("a column name"));
    while (match(TokenType::COMMA)) {
        columns.push_back(identifier("a column name"));
    }
    consume(TokenType::RIGHT_BRACKET, "']' after the column names");
    return columns;
}

// literal -> number | STRING_LITERAL | TRUE | FALSE
Literal Parser::literal() {
    switch (peek().type) {
        case TokenType::NUMBER_LITERAL:
        case TokenType::MINUS:          return {LiteralKind::Number, number()};
        case TokenType::STRING_LITERAL: return {LiteralKind::String, advance().lexeme};
        case TokenType::TRUE:
        case TokenType::FALSE:          return {LiteralKind::Boolean, advance().type == TokenType::TRUE ? "TRUE" : "FALSE"};
        default:                        errorExpected("a value (a number, \"text\", TRUE or FALSE)");
    }
}

// number -> [ - ] NUMBER_LITERAL
std::string Parser::number() {
    const std::string sign = match(TokenType::MINUS) ? "-" : "";
    return sign + consume(TokenType::NUMBER_LITERAL, "a number").lexeme;
}

// A whole number such as a width, a quality or a channel count.
int Parser::integer(const std::string& what) {
    const Token& token = consume(TokenType::NUMBER_LITERAL, what);
    if (token.lexeme.find('.') != std::string::npos || token.lexeme.size() > 9) {
        errorAt(token.location, "expected " + what + " as a whole number, found '" + token.lexeme + "'");
    }
    return std::stoi(token.lexeme);
}

// A dataset, table or column name. A column called e.g. "number" clashes with
// the keyword NUMBER, so the hint shows how to quote it.
// A word that is a keyword only in one place (BY, COUNT, ...): an identifier with this text.
bool Parser::matchWord(const std::string& word) {
    if (!check(TokenType::IDENTIFIER)) {
        return false;
    }
    std::string upper;
    for (char c : peek().lexeme) {
        upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper != word) {
        return false;
    }
    advance();
    return true;
}

std::string Parser::identifier(const std::string& what) {
    if (check(TokenType::IDENTIFIER)) {
        return advance().lexeme;
    }
    std::string hint;
    if (isKeyword(peek().type)) {
        hint = "'" + peek().lexeme + "' is a keyword; write `" + peek().lexeme + "` to use it as a name";
    }
    errorExpected(what, hint);
}

void Parser::endStatement() { consume(TokenType::SEMICOLON, "';' at the end of the statement"); }

// ---------- Reading tokens ----------

const Token& Parser::peek() const { return tokens_[current_]; }

const Token& Parser::previous() const { return tokens_[current_ - 1]; }

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }

bool Parser::check(TokenType type) const { return peek().type == type; }

bool Parser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

const Token& Parser::advance() {
    if (!isAtEnd()) {
        ++current_;
    }
    return previous();
}

const Token& Parser::consume(TokenType type, const std::string& what) {
    if (!check(type)) {
        errorExpected(what);
    }
    return advance();
}

// ---------- Errors and recovery ----------

void Parser::errorAt(SourceLocation location, const std::string& message, const std::string& hint) {
    diagnostics_.error(Phase::Syntax, location, message, hint);
    throw ParseError{};
}

// Reports that `what` was expected but the next token is something else.
// If that token is already on the next line (for example a missing ';'),
// the error points just after the previous token, where the fix belongs.
void Parser::errorExpected(const std::string& what, const std::string& hint) {
    SourceLocation location = peek().location;
    if (current_ > 0 && previous().location.line != peek().location.line) {
        location = previous().location;
        location.column += previous().length;
    }
    errorAt(location, "expected " + what + ", found " + describe(peek()), hint);
}

// Panic mode: throw away tokens until the broken statement ends (';') or the
// next statement starts, so errors later in the file are still reported.
void Parser::synchronize() {
    while (!isAtEnd()) {
        if (isStatementKeyword(peek().type)) {
            return;
        }
        if (advance().type == TokenType::SEMICOLON) {
            return;
        }
    }
}
