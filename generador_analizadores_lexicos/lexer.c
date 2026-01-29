#include "lexer.h"

static const char* source;
static int current = 0;
static int line = 1;

void init_lexer(const char* src) {
    source = src;
    current = 0;
    line = 1;
}

static char peek() {
    if (current >= strlen(source)) return '\0';
    return source[current];
}

static char advance() {
    return source[current++];
}

static void skip_whitespace() {
    while (isspace(peek())) {
        if (peek() == '\n') line++;
        advance();
    }
}

static Token make_token(TokenType type, const char* lexeme) {
    Token token;
    token.type = type;
    token.lexeme = malloc(strlen(lexeme) + 1);
    strcpy(token.lexeme, lexeme);
    token.line = line;
    return token;
}

static Token make_error_token(const char* message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.lexeme = malloc(strlen(message) + 1);
    strcpy(token.lexeme, message);
    token.line = line;
    return token;
}

static Token scan_identifier() {
    int start = current;
    while (isalnum(peek()) || peek() == '_') advance();
    int length = current - start;
    char* lexeme = malloc(length + 1);
    memcpy(lexeme, source + start, length);
    lexeme[length] = '\0';

    // Verificar palabras clave
    if (strcmp(lexeme, "function") == 0) return make_token(TOKEN_FUNCTION, lexeme);
    if (strcmp(lexeme, "type") == 0) return make_token(TOKEN_TYPE, lexeme);
    if (strcmp(lexeme, "inherits") == 0) return make_token(TOKEN_INHERITS, lexeme);
    if (strcmp(lexeme, "while") == 0) return make_token(TOKEN_WHILE, lexeme);
    if (strcmp(lexeme, "for") == 0) return make_token(TOKEN_FOR, lexeme);
    if (strcmp(lexeme, "in") == 0) return make_token(TOKEN_IN, lexeme);
    if (strcmp(lexeme, "if") == 0) return make_token(TOKEN_IF, lexeme);
    if (strcmp(lexeme, "elif") == 0) return make_token(TOKEN_ELIF, lexeme);
    if (strcmp(lexeme, "else") == 0) return make_token(TOKEN_ELSE, lexeme);
    if (strcmp(lexeme, "let") == 0) return make_token(TOKEN_LET, lexeme);
    if (strcmp(lexeme, "true") == 0) return make_token(TOKEN_TRUE, lexeme);
    if (strcmp(lexeme, "false") == 0) return make_token(TOKEN_FALSE, lexeme);
    if (strcmp(lexeme, "new") == 0) return make_token(TOKEN_NEW, lexeme);
    if (strcmp(lexeme, "self") == 0) return make_token(TOKEN_SELF, lexeme);
    if (strcmp(lexeme, "base") == 0) return make_token(TOKEN_BASE, lexeme);
    if (strcmp(lexeme, "as") == 0) return make_token(TOKEN_AS, lexeme);
    if (strcmp(lexeme, "is") == 0) return make_token(TOKEN_IS, lexeme);

    return make_token(TOKEN_IDENT, lexeme);
}

static Token scan_number() {
    int start = current;
    while (isdigit(peek())) advance();
    if (peek() == '.' && isdigit(source[current + 1])) {
        advance();
        while (isdigit(peek())) advance();
    }
    int length = current - start;
    char* lexeme = malloc(length + 1);
    memcpy(lexeme, source + start, length);
    lexeme[length] = '\0';
    return make_token(TOKEN_NUMBER, lexeme);
}

static Token scan_string() {
    advance(); // skip opening quote
    int start = current;
    while (peek() != '"' && peek() != '\0') {
        if (peek() == '\n') line++;
        advance();
    }
    if (peek() == '\0') return make_error_token("Unterminated string");
    int length = current - start;
    char* lexeme = malloc(length + 1);
    memcpy(lexeme, source + start, length);
    lexeme[length] = '\0';
    advance(); // skip closing quote
    return make_token(TOKEN_STRING, lexeme);
}

Token get_next_token() {
    skip_whitespace();

    char c = peek();
    if (c == '\0') return make_token(TOKEN_EOF, "");

    advance();

    switch (c) {
        case ';': return make_token(TOKEN_SEMICOLON, ";");
        case '(': return make_token(TOKEN_LPAREN, "(");
        case ')': return make_token(TOKEN_RPAREN, ")");
        case '{': return make_token(TOKEN_LBRACE, "{");
        case '}': return make_token(TOKEN_RBRACE, "}");
        case ',': return make_token(TOKEN_COMMA, ",");
        case ':': return make_token(TOKEN_COLON, ":");
        case '.': return make_token(TOKEN_DOT, ".");
        case '+': return make_token(TOKEN_PLUS, "+");
        case '*': return make_token(TOKEN_MULT, "*");
        case '/': return make_token(TOKEN_DIV, "/");
        case '%': return make_token(TOKEN_MOD, "%");
        case '^': return make_token(TOKEN_POW, "^");
        case '<':
            if (peek() == '=') { advance(); return make_token(TOKEN_LE, "<="); }
            return make_token(TOKEN_LT, "<");
        case '>':
            if (peek() == '=') { advance(); return make_token(TOKEN_GE, ">="); }
            return make_token(TOKEN_GT, ">");
        case '=':
            if (peek() == '=') { advance(); return make_token(TOKEN_EQ, "=="); }
            return make_token(TOKEN_ASSIGN, "=");
        case '!':
            if (peek() == '=') { advance(); return make_token(TOKEN_NEQ, "!="); }
            return make_error_token("Unexpected '!'");
        case '|':
            if (peek() == '|') { advance(); return make_token(TOKEN_OR, "||"); }
            return make_error_token("Unexpected '|'");
        case '&':
            if (peek() == '&') { advance(); return make_token(TOKEN_AND, "&&"); }
            return make_error_token("Unexpected '&'");
        case '@':
            if (peek() == '@') { advance(); return make_token(TOKEN_CONCAT_WS, "@@"); }
            return make_token(TOKEN_CONCAT, "@");
        case '-':
            if (peek() == '>') { advance(); return make_token(TOKEN_ARROW, "->"); }
            return make_token(TOKEN_MINUS, "-");
        case '"': return scan_string();
        default:
            if (isalpha(c) || c == '_') {
                current--; // backtrack
                return scan_identifier();
            }
            if (isdigit(c)) {
                current--; // backtrack
                return scan_number();
            }
            return make_error_token("Unexpected character");
    }
}

void free_lexer() {
    // Nothing to free for now
}