#include "lexer.hpp"
#include <unordered_map>
#include <cctype>

namespace shell_lite {

const std::unordered_map<std::string_view, TokenType> KEYWORDS = {
    {"if", TokenType::TOK_IF}, {"else", TokenType::TOK_ELSE}, {"elif", TokenType::TOK_ELIF},
    {"for", TokenType::TOK_FOR}, {"in", TokenType::TOK_IN}, {"range", TokenType::TOK_RANGE},
    {"times", TokenType::TOK_TIMES}, {"while", TokenType::TOK_WHILE},
    {"until", TokenType::TOK_UNTIL}, {"repeat", TokenType::TOK_REPEAT}, {"forever", TokenType::TOK_FOREVER},
    {"stop", TokenType::TOK_STOP}, {"skip", TokenType::TOK_SKIP}, {"pass", TokenType::TOK_PASS},
    {"each", TokenType::TOK_EACH}, {"check", TokenType::TOK_CHECK}, {"unless", TokenType::TOK_UNLESS},
    {"when", TokenType::TOK_WHEN}, {"otherwise", TokenType::TOK_OTHERWISE}, {"do", TokenType::TOK_DO}, {"print", TokenType::TOK_PRINT}, {"say", TokenType::TOK_SAY}, {"show", TokenType::TOK_SAY},
    {"esay", TokenType::TOK_ESAY},
    {"input", TokenType::TOK_INPUT}, {"ask", TokenType::TOK_ASK}, {"to", TokenType::TOK_TO},
    {"can", TokenType::TOK_TO}, {"def", TokenType::TOK_FUNCTION}, {"return", TokenType::TOK_RETURN}, {"give", TokenType::TOK_RETURN},
    {"take", TokenType::TOK_TAKE}, {"structure", TokenType::TOK_STRUCTURE}, {"thing", TokenType::TOK_STRUCTURE},
    {"class", TokenType::TOK_STRUCTURE},
    {"has", TokenType::TOK_HAS}, {"with", TokenType::TOK_WITH},
    {"is", TokenType::TOK_IS}, {"extends", TokenType::TOK_EXTENDS}, {"from", TokenType::TOK_FROM},
    {"make", TokenType::TOK_MAKE}, {"new", TokenType::TOK_MAKE}, {"the", TokenType::TOK_NOISE},
    {"let", TokenType::TOK_NOISE}, {"please", TokenType::TOK_NOISE}, {"yes", TokenType::TOK_YES},
    {"no", TokenType::TOK_NO}, {"true", TokenType::TOK_YES}, {"false", TokenType::TOK_NO},
    {"const", TokenType::TOK_CONST}, {"and", TokenType::TOK_AND}, {"or", TokenType::TOK_OR},
    {"not", TokenType::TOK_NOT}, {"try", TokenType::TOK_TRY}, {"catch", TokenType::TOK_CATCH},
    {"always", TokenType::TOK_ALWAYS}, {"finally", TokenType::TOK_ALWAYS}, {"error", TokenType::TOK_ERROR},
    {"throw", TokenType::TOK_ERROR}, {"use", TokenType::TOK_USE}, {"as", TokenType::TOK_AS},
    {"fn", TokenType::TOK_LAMBDA}, {"lambda", TokenType::TOK_LAMBDA},
    {"import", TokenType::TOK_IMPORT}, {"execute", TokenType::TOK_EXECUTE},
    {"run", TokenType::TOK_EXECUTE}, {"spawn", TokenType::TOK_SPAWN}, {"await", TokenType::TOK_AWAIT},
    {"namespace", TokenType::TOK_NAMESPACE},
    {"matches", TokenType::TOK_MATCHES}, {"on", TokenType::TOK_ON}, 
    {"at", TokenType::TOK_AT}, {"after", TokenType::TOK_AFTER},
    {"list", TokenType::TOK_LIST}, {"set", TokenType::TOK_SET},
    {"unique", TokenType::TOK_UNIQUE}, {"of", TokenType::TOK_OF}, {"listen", TokenType::TOK_LISTEN}, {"port", TokenType::TOK_PORT}, {"model", TokenType::TOK_MODEL},
    {"create", TokenType::TOK_CREATE}, {"insert", TokenType::TOK_INSERT},
    {"find", TokenType::TOK_FIND}, {"update", TokenType::TOK_UPDATE}, {"delete", TokenType::TOK_DELETE},
    {"del", TokenType::TOK_DEL},
    {"where", TokenType::TOK_WHERE}, {"every", TokenType::TOK_EVERY}, {"minute", TokenType::TOK_MINUTE},
    {"minutes", TokenType::TOK_MINUTE}, {"second", TokenType::TOK_SECOND}, {"seconds", TokenType::TOK_SECOND},
    {"serve", TokenType::TOK_SERVE},
    {"write", TokenType::TOK_WRITE}, {"append", TokenType::TOK_APPEND},
    {"read", TokenType::TOK_READ}, {"file", TokenType::TOK_FILE}, {"db", TokenType::TOK_DB},
    {"database", TokenType::TOK_DB}, {"query", TokenType::TOK_QUERY}, {"middleware", TokenType::TOK_MIDDLEWARE},
    {"someone", TokenType::TOK_SOMEONE}, {"visits", TokenType::TOK_VISITS}, {"submits", TokenType::TOK_SUBMITS},
    {"start", TokenType::TOK_START}, {"server", TokenType::TOK_SERVER},
    {"define", TokenType::TOK_DEFINE}, {"page", TokenType::TOK_PAGE},
    {"using", TokenType::TOK_USING}, {"component", TokenType::TOK_PAGE},
    {"add", TokenType::TOK_ADD}, {"put", TokenType::TOK_ADD}, {"into", TokenType::TOK_INTO}, {"push", TokenType::TOK_ADD},
    {"remove", TokenType::TOK_REMOVE},
    {"be", TokenType::TOK_BE},
    {"by", TokenType::TOK_BY}, {"less", TokenType::TOK_LESS},
    {"function", TokenType::TOK_FUNCTION},
    {"contains", TokenType::TOK_CONTAINS}, {"empty", TokenType::TOK_EMPTY}, {"than", TokenType::TOK_THAN},
    {"test", TokenType::TOK_TEST},
    {"expect", TokenType::TOK_EXPECT}, {"ensure", TokenType::TOK_ENSURE}, {"parallel", TokenType::TOK_PARALLEL},
    {"gather", TokenType::TOK_GATHER}, {"lock", TokenType::TOK_LOCK}, {"send", TokenType::TOK_SEND}, {"receive", TokenType::TOK_RECEIVE},
    {"maximum", TokenType::TOK_MAX}, {"minimum", TokenType::TOK_MIN}, {"clamped", TokenType::TOK_CLAMPED},
    {"between", TokenType::TOK_BETWEEN}, {"lerp", TokenType::TOK_LERP},
    {"least", TokenType::TOK_LEAST}, {"exactly", TokenType::TOK_EXACTLY}, {"more", TokenType::TOK_MORE},
    {"all", TokenType::TOK_ALL}, {"any", TokenType::TOK_ANY}
};

bool is_hard_anchor_keyword(std::string_view word) {
    auto it = KEYWORDS.find(word);
    if (it == KEYWORDS.end()) {
        return false;
    }
    TokenType t = it->second;
    if (t == TokenType::TOK_TO || t == TokenType::TOK_FUNCTION || t == TokenType::TOK_DEFINE ||
        t == TokenType::TOK_STRUCTURE || t == TokenType::TOK_NAMESPACE ||
        t == TokenType::TOK_IMPORT || t == TokenType::TOK_USE || t == TokenType::TOK_FROM ||
        t == TokenType::TOK_CONST) {
        return true;
    }
    return false;
}

Lexer::Lexer(std::string_view source) : source_(source) {}

std::vector<Token> Lexer::tokenize_line_only(std::string_view line, int line_num) {
    source_ = line;
    pos_ = 0;
    line_ = line_num;
    col_ = 1;

    std::vector<Token> raw_tokens;
    while (!is_at_end()) {
        skip_whitespace_and_comments();
        if (is_at_end()) break;
        raw_tokens.push_back(next_token());
    }

    if (raw_tokens.empty()) return {};

    return raw_tokens;
}

void Lexer::skip_whitespace_and_comments() {
    while (!is_at_end()) {
        char c = peek();
        if (in_multiline_comment_) {
            if (c == '*' && peek_next() == '/') {
                advance(); advance();
                in_multiline_comment_ = false;
            } else {
                advance();
            }
        } else if (isspace(c)) {
            advance();
        } else if (c == '#') {
            while (!is_at_end() && peek() != '\n') advance();
        } else if (c == '/' && peek_next() == '*') {
            advance(); advance();
            in_multiline_comment_ = true;
            block_comment_start_line_ = line_;
            while (!is_at_end()) {
                if (peek() == '*' && peek_next() == '/') {
                    advance(); advance();
                    in_multiline_comment_ = false;
                    break;
                }
                advance();
            }
        } else {
            break;
        }
    }
}

Token Lexer::next_token() {
    char c = peek();
    int start_col = col_;


    if (c == '/') {
        bool can_be_regex = true;
        if (pos_ > 0) {
            size_t back = pos_ - 1;
            while (back > 0 && isspace(source_[back])) back--;
            char prev = source_[back];
            if (isalnum(prev) || prev == ')' || prev == ']' || prev == '}' || prev == '"' || prev == '\'') {
                can_be_regex = false;
            }
        }

        if (can_be_regex && peek_next() != '*' && peek_next() != '/' && peek_next() != ' ' && peek_next() != '=') {
            size_t start = pos_;
            advance();
            while (!is_at_end() && peek() != '/') {
                if (peek() == '\\') {
                    advance();
                    if (is_at_end()) break;
                }
                advance();
            }
            if (!is_at_end() && peek() == '/') {
                advance();
                while (!is_at_end() && isalpha(peek())) advance();
                return Token(TokenType::TOK_REGEX, source_.substr(start, pos_ - start), line_, start_col);
            }
            pos_ = start;
        }
    }

    if (isalpha(c) || c == '_') return read_identifier_or_keyword();
    if (isdigit(c)) return read_number();
    if (c == '"' || c == '\'') return read_string();

    return read_operator();
}

Token Lexer::read_identifier_or_keyword() {
    size_t start = pos_;
    int start_col = col_;
    while (!is_at_end() && (isalnum(peek()) || peek() == '_')) advance();

    std::string_view value = source_.substr(start, pos_ - start);

    auto it = KEYWORDS.find(value);
    if (it != KEYWORDS.end()) {
        return Token(it->second, value, line_, start_col);
    }

    return Token(TokenType::TOK_ID, value, line_, start_col);
}

Token Lexer::read_number() {
    size_t start = pos_;
    int start_col = col_;
    if (peek() == '0' && pos_ + 1 < source_.size()) {
        char p = source_[pos_ + 1];
        if (p == 'x' || p == 'X' || p == 'b' || p == 'B' || p == 'o' || p == 'O') {
            advance(); advance();
            size_t dstart = pos_;
            while (!is_at_end() && isalnum(peek())) advance();
            bool ok = pos_ > dstart;
            for (size_t i = dstart; ok && i < pos_; i++) {
                char d = source_[i];
                bool valid = (p == 'x' || p == 'X')
                    ? (isdigit(d) || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))
                    : (p == 'b' || p == 'B') ? (d == '0' || d == '1')
                    : (d >= '0' && d <= '7');
                if (!valid) ok = false;
            }
            if (!ok)
                return Token(TokenType::TOK_ILLEGAL, source_.substr(start, pos_ - start), line_, start_col);
            return Token(TokenType::TOK_NUMBER, source_.substr(start, pos_ - start), line_, start_col);
        }
    }
    while (!is_at_end() && isdigit(peek())) advance();
    if (!is_at_end() && peek() == '.' && isdigit(peek_next())) {
        advance();
        while (!is_at_end() && isdigit(peek())) advance();
    }
    if (!is_at_end() && (peek() == 'e' || peek() == 'E')) {
        size_t saved = pos_;
        advance();
        if (!is_at_end() && (peek() == '+' || peek() == '-')) advance();
        if (!is_at_end() && isdigit(peek())) {
            while (!is_at_end() && isdigit(peek())) advance();
        } else {
            pos_ = saved;
        }
    }
    return Token(TokenType::TOK_NUMBER, source_.substr(start, pos_ - start), line_, start_col);
}

Token Lexer::read_string() {
    char quote = advance();
    size_t start = pos_;
    int start_col = col_ - 1;
    while (!is_at_end() && peek() != quote) {
        if (peek() == '\\') {
            advance();
            if (is_at_end()) break;
        }
        advance();
    }
    std::string_view value = source_.substr(start, pos_ - start);
    if (!is_at_end()) advance();
    return Token(TokenType::TOK_STRING, value, line_, start_col);
}

Token Lexer::read_operator() {
    size_t start = pos_;
    int start_col = col_;
    char c = advance();

    switch (c) {
        case '(': return Token(TokenType::TOK_LPAREN, source_.substr(start, 1), line_, start_col);
        case ')': return Token(TokenType::TOK_RPAREN, source_.substr(start, 1), line_, start_col);
        case '[': return Token(TokenType::TOK_LBRACKET, source_.substr(start, 1), line_, start_col);
        case ']': return Token(TokenType::TOK_RBRACKET, source_.substr(start, 1), line_, start_col);
        case '{': return Token(TokenType::TOK_LBRACE, source_.substr(start, 1), line_, start_col);
        case '}': return Token(TokenType::TOK_RBRACE, source_.substr(start, 1), line_, start_col);
        case ':':
            if (peek() == ':') { advance(); return Token(TokenType::TOK_COLON_COLON, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_COLON, source_.substr(start, 1), line_, start_col);
        case ',': return Token(TokenType::TOK_COMMA, source_.substr(start, 1), line_, start_col);
        case '.':
            if (peek() == '.' && peek_next() == '.') {
                advance(); advance();
                return Token(TokenType::TOK_DOTDOTDOT, source_.substr(start, 3), line_, start_col);
            }
            return Token(TokenType::TOK_DOT, source_.substr(start, 1), line_, start_col);
        case '+':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_PLUSEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_PLUS_OP, source_.substr(start, 1), line_, start_col);
        case '-':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_MINUSEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_MINUS_OP, source_.substr(start, 1), line_, start_col);
        case '*':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_MULEQ, source_.substr(start, 2), line_, start_col); }
            if (peek() == '*') { advance(); return Token(TokenType::TOK_POW, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_MUL_OP, source_.substr(start, 1), line_, start_col);
        case '/':
            if (peek() == '/') { advance(); return Token(TokenType::TOK_FLOORDIV, source_.substr(start, 2), line_, start_col); }
            if (peek() == '=') { advance(); return Token(TokenType::TOK_DIVEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_DIV_OP, source_.substr(start, 1), line_, start_col);
        case '%':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_MODEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_MOD_OP, source_.substr(start, 1), line_, start_col);
        case '=':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_EQ, source_.substr(start, 2), line_, start_col); }
            if (peek() == '>') { advance(); return Token(TokenType::TOK_ARROW, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_ASSIGN, source_.substr(start, 1), line_, start_col);
        case '!':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_NEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_NOT, source_.substr(start, 1), line_, start_col);
        case '<':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_LE, source_.substr(start, 2), line_, start_col); }
            if (peek() == '<') { advance(); return Token(TokenType::TOK_LSHIFT, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_OP_LT, source_.substr(start, 1), line_, start_col);
        case '>':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_GE, source_.substr(start, 2), line_, start_col); }
            if (peek() == '>') { advance(); return Token(TokenType::TOK_RSHIFT, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_OP_GT, source_.substr(start, 1), line_, start_col);
        case '&':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_ANDEQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_BIT_AND, source_.substr(start, 1), line_, start_col);
        case '|':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_OREQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_BIT_OR, source_.substr(start, 1), line_, start_col);
        case '^':
            if (peek() == '=') { advance(); return Token(TokenType::TOK_XOREQ, source_.substr(start, 2), line_, start_col); }
            return Token(TokenType::TOK_BIT_XOR, source_.substr(start, 1), line_, start_col);
        case '~': return Token(TokenType::TOK_BIT_NOT, source_.substr(start, 1), line_, start_col);
        case '?': return Token(TokenType::TOK_OP_QUESTION, source_.substr(start, 1), line_, start_col);
    }

    return Token(TokenType::TOK_ILLEGAL, source_.substr(start, 1), line_, start_col);
}
}

