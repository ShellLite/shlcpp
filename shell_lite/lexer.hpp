#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <optional>

namespace shell_lite {

enum class TokenType {
    TOK_ID, TOK_NUMBER, TOK_STRING, TOK_REGEX,
    TOK_IF, TOK_ELSE, TOK_ELIF, TOK_FOR, TOK_IN, TOK_RANGE, TOK_LOOP, TOK_TIMES, TOK_WHILE, TOK_UNTIL, TOK_REPEAT, TOK_FOREVER,
    TOK_STOP, TOK_SKIP, TOK_EXIT, TOK_EACH, TOK_CHECK, TOK_UNLESS, TOK_WHEN, TOK_OTHERWISE, TOK_THEN, TOK_DO, TOK_BEGIN, TOK_END,
    TOK_PRINT, TOK_SAY, TOK_INPUT, TOK_ASK, TOK_TO, TOK_RETURN, TOK_TAKE, TOK_STRUCTURE, TOK_HAS, TOK_WITH, TOK_IS, TOK_EXTENDS,
    TOK_FROM, TOK_MAKE, TOK_YES, TOK_NO, TOK_NULL, TOK_CONST, TOK_AND, TOK_OR, TOK_NOT, TOK_TRY, TOK_CATCH, TOK_ALWAYS, TOK_ERROR, TOK_USE,
    TOK_AS, TOK_SHARE, TOK_IMPORT, TOK_EXECUTE, TOK_ALERT, TOK_PROMPT, TOK_CONFIRM, TOK_SPAWN, TOK_AWAIT, TOK_MATCHES,
    TOK_ON, TOK_DOWNLOAD, TOK_COMPRESS, TOK_EXTRACT, TOK_FOLDER, TOK_LOAD, TOK_SAVE, TOK_CSV, TOK_COPY, TOK_PASTE,
    TOK_CLIPBOARD, TOK_PRESS, TOK_TYPE, TOK_CLICK, TOK_AT, TOK_NOTIFY, TOK_AFTER, TOK_BEFORE,
    TOK_WAIT, TOK_CONVERT, TOK_JSON, TOK_HTTP, TOK_LISTEN, TOK_PORT, TOK_MODEL, TOK_CREATE, TOK_TABLE,
    TOK_INSERT, TOK_FIND, TOK_UPDATE, TOK_DELETE, TOK_WHERE, TOK_EVERY, TOK_MINUTE, TOK_SECOND, TOK_PROGRESS, TOK_BOLD,
    TOK_RED, TOK_GREEN, TOK_BLUE, TOK_YELLOW, TOK_CYAN, TOK_MAGENTA, TOK_SERVE, TOK_STATIC, TOK_WRITE, TOK_APPEND,
    TOK_READ, TOK_FILE, TOK_DB, TOK_QUERY, TOK_OPEN, TOK_CLOSE, TOK_EXEC, TOK_MIDDLEWARE, TOK_SOMEONE, TOK_VISITS,
    TOK_SUBMITS, TOK_START, TOK_SERVER, TOK_FILES, TOK_DEFINE, TOK_PAGE, TOK_CALLED, TOK_USING, TOK_HEADING,
    TOK_PARAGRAPH, TOK_IMAGE, TOK_ADD, TOK_INTO, TOK_PUSH, TOK_MANY, TOK_HOW, TOK_FIELD, TOK_SUBMIT, TOK_NAMED,
    TOK_PLACEHOLDER, TOK_APP, TOK_SIZE, TOK_BUTTON, TOK_UPPER, TOK_LOWER, TOK_INCREMENT, TOK_DECREMENT,
    TOK_MULTIPLY, TOK_DIVIDE, TOK_SUBTRACT, TOK_BE, TOK_BY, TOK_PLUS, TOK_MINUS, TOK_DIV, TOK_GREATER, TOK_LESS,
    TOK_EQUAL, TOK_FUNCTION, TOK_LAMBDA, TOK_CONTAINS, TOK_EMPTY, TOK_THAN, TOK_DOING, TOK_LONG, TOK_TEST, TOK_EXPECT,
    TOK_ENSURE, TOK_PARALLEL, TOK_GATHER, TOK_LOCK, TOK_CHANNEL, TOK_SEND, TOK_RECEIVE, TOK_COUNT, TOK_MAX,
    TOK_MIN, TOK_CLAMPED, TOK_BETWEEN, TOK_LERP, TOK_NOTIN,
    TOK_LIST, TOK_SET, TOK_UNIQUE, TOK_OF,
    TOK_EVEN, TOK_ODD, TOK_PRIME,
    TOK_ALL, TOK_ANY, TOK_POSITIVE, TOK_NEGATIVE, TOK_SUM, TOK_ITEMS, TOK_ITEM,
    TOK_COLUMN, TOK_ROW, TOK_LAYOUT, TOK_WIDGET, TOK_REMOVE,
    TOK_LEAST, TOK_EXACTLY, TOK_MORE,
    TOK_LET, TOK_THE, TOK_PLEASE, TOK_NOISE,
    TOK_MINUS_UNARY,


    TOK_PLUS_OP, TOK_MINUS_OP, TOK_MUL_OP, TOK_DIV_OP, TOK_MOD_OP, TOK_BIT_AND, TOK_BIT_OR, TOK_BIT_XOR, TOK_BIT_NOT,
    TOK_ASSIGN, TOK_ARROW, TOK_EQ, TOK_NEQ, TOK_LE, TOK_GE, TOK_PLUSEQ, TOK_MINUSEQ, TOK_MULEQ, TOK_DIVEQ, TOK_MODEQ,
    TOK_POW, TOK_LSHIFT, TOK_RSHIFT, TOK_DOTDOTDOT, TOK_OP_LT, TOK_OP_GT, TOK_OP_QUESTION, TOK_COLON_COLON, TOK_NAMESPACE,


    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET, TOK_LBRACE, TOK_RBRACE, TOK_COLON, TOK_COMMA, TOK_DOT,


    TOK_INDENT, TOK_DEDENT, TOK_NEWLINE, TOK_EOF_TOK, TOK_COMMENT, TOK_ILLEGAL
    };

struct Token {
    TokenType type;
    std::string_view value;
    int line;
    int col;

    Token(TokenType t, std::string_view v, int l, int c)
        : type(t), value(v), line(l), col(c) {}
};

class Lexer {
public:
    explicit Lexer(std::string_view source);
    std::vector<Token> tokenize();
    std::vector<Token> tokenize_line_only(std::string_view line, int line_num);

private:
    std::string_view source_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    std::vector<int> indent_stack_ = {0};
    int bracket_depth_ = 0;
    bool in_multiline_comment_ = false;

    Token next_token();
    void skip_whitespace_and_comments();
    Token read_identifier_or_keyword();
    Token read_number();
    Token read_string();
    Token read_operator();

    bool is_at_end() const { return pos_ >= source_.size(); }
    char peek() const { return is_at_end() ? '\0' : source_[pos_]; }
    char peek_next() const { return pos_ + 1 >= source_.size() ? '\0' : source_[pos_ + 1]; }
    char advance() {
        char c = source_[pos_++];
        if (c == '\n') { line_++; col_ = 1; }
        else col_++;
        return c;
    }
};

bool is_hard_anchor_keyword(std::string_view word);

}
