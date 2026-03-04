/*
 * test_lexer.c — Tests unitarios del analizador léxico
 *
 * Verifica:
 *  - Tokenización correcta de palabras clave, operadores, literales
 *  - Manejo de whitespace y comentarios (se ignoran)
 *  - Posiciones line/col correctas
 *  - Errores léxicos
 *  - EOF correcto
 */

#include "test_framework.h"
#include "../hulk_tokens.h"
#include "../hulk_compiler.h"
#include "../generador_analizadores_lexicos/lexer.h"
#include <stdlib.h>

// ============== HELPER ==============
// Compilador compartido entre todos los tests (se inicializa una vez)
static HulkCompiler hc;
static int hc_ready = 0;

static void ensure_compiler(void) {
    if (!hc_ready) {
        hc_ready = hulk_compiler_init(&hc);
    }
}

// Tokeniza una cadena y retorna un array dinámico de tokens.
// El caller debe liberar tokens[i].lexeme y el array.
static Token* tokenize(const char *input, int *out_count) {
    ensure_compiler();
    LexerContext lctx;
    lexer_init(&lctx, hc.dfa, input);

    int cap = 32, count = 0;
    Token *tokens = malloc(sizeof(Token) * cap);

    while (1) {
        Token t = lexer_next_token(&lctx);
        if (count >= cap) {
            cap *= 2;
            tokens = realloc(tokens, sizeof(Token) * cap);
        }
        tokens[count++] = t;
        if (t.type == TOKEN_EOF) break;
    }
    *out_count = count;
    return tokens;
}

static void free_tokens(Token *tokens, int count) {
    for (int i = 0; i < count; i++)
        if (tokens[i].lexeme) free(tokens[i].lexeme);
    free(tokens);
}

// ============== TESTS: KEYWORDS ==============

TEST(keyword_let) {
    int n; Token *t = tokenize("let", &n);
    ASSERT_EQ(TOKEN_LET, t[0].type);
    ASSERT_STR_EQ("let", t[0].lexeme);
    ASSERT_EQ(TOKEN_EOF, t[n-1].type);
    free_tokens(t, n);
}

TEST(keyword_function) {
    int n; Token *t = tokenize("function", &n);
    ASSERT_EQ(TOKEN_FUNCTION, t[0].type);
    free_tokens(t, n);
}

TEST(keyword_if_else) {
    int n; Token *t = tokenize("if else elif", &n);
    ASSERT_EQ(TOKEN_IF, t[0].type);
    ASSERT_EQ(TOKEN_ELSE, t[1].type);
    ASSERT_EQ(TOKEN_ELIF, t[2].type);
    free_tokens(t, n);
}

TEST(keyword_while_for_in) {
    int n; Token *t = tokenize("while for in", &n);
    ASSERT_EQ(TOKEN_WHILE, t[0].type);
    ASSERT_EQ(TOKEN_FOR, t[1].type);
    ASSERT_EQ(TOKEN_IN, t[2].type);
    free_tokens(t, n);
}

TEST(keyword_type_inherits_new) {
    int n; Token *t = tokenize("type inherits new", &n);
    ASSERT_EQ(TOKEN_TYPE, t[0].type);
    ASSERT_EQ(TOKEN_INHERITS, t[1].type);
    ASSERT_EQ(TOKEN_NEW, t[2].type);
    free_tokens(t, n);
}

TEST(keyword_true_false) {
    int n; Token *t = tokenize("true false", &n);
    ASSERT_EQ(TOKEN_TRUE, t[0].type);
    ASSERT_EQ(TOKEN_FALSE, t[1].type);
    free_tokens(t, n);
}

TEST(keyword_self_base_is_as) {
    int n; Token *t = tokenize("self base is as", &n);
    ASSERT_EQ(TOKEN_SELF, t[0].type);
    ASSERT_EQ(TOKEN_BASE, t[1].type);
    ASSERT_EQ(TOKEN_IS, t[2].type);
    ASSERT_EQ(TOKEN_AS, t[3].type);
    free_tokens(t, n);
}

TEST(keyword_decor) {
    int n; Token *t = tokenize("decor", &n);
    ASSERT_EQ(TOKEN_DECOR, t[0].type);
    ASSERT_STR_EQ("decor", t[0].lexeme);
    free_tokens(t, n);
}

TEST(decor_not_identifier) {
    // "decoration" starts with "decor" but is an identifier
    int n; Token *t = tokenize("decoration", &n);
    ASSERT_EQ(TOKEN_IDENT, t[0].type);
    ASSERT_STR_EQ("decoration", t[0].lexeme);
    free_tokens(t, n);
}

// ============== TESTS: IDENTIFIERS ==============

TEST(simple_identifier) {
    int n; Token *t = tokenize("myVar", &n);
    ASSERT_EQ(TOKEN_IDENT, t[0].type);
    ASSERT_STR_EQ("myVar", t[0].lexeme);
    free_tokens(t, n);
}

TEST(identifier_with_underscore) {
    int n; Token *t = tokenize("_test my_var x1", &n);
    ASSERT_EQ(TOKEN_IDENT, t[0].type);
    ASSERT_STR_EQ("_test", t[0].lexeme);
    ASSERT_EQ(TOKEN_IDENT, t[1].type);
    ASSERT_STR_EQ("my_var", t[1].lexeme);
    ASSERT_EQ(TOKEN_IDENT, t[2].type);
    ASSERT_STR_EQ("x1", t[2].lexeme);
    free_tokens(t, n);
}

TEST(identifier_not_keyword) {
    // "letter" empieza con "let" pero no es keyword
    int n; Token *t = tokenize("letter", &n);
    ASSERT_EQ(TOKEN_IDENT, t[0].type);
    ASSERT_STR_EQ("letter", t[0].lexeme);
    free_tokens(t, n);
}

// ============== TESTS: NUMBERS ==============

TEST(integer_number) {
    int n; Token *t = tokenize("42", &n);
    ASSERT_EQ(TOKEN_NUMBER, t[0].type);
    ASSERT_STR_EQ("42", t[0].lexeme);
    free_tokens(t, n);
}

TEST(decimal_number) {
    int n; Token *t = tokenize("3.14", &n);
    ASSERT_EQ(TOKEN_NUMBER, t[0].type);
    ASSERT_STR_EQ("3.14", t[0].lexeme);
    free_tokens(t, n);
}

TEST(number_zero) {
    int n; Token *t = tokenize("0", &n);
    ASSERT_EQ(TOKEN_NUMBER, t[0].type);
    ASSERT_STR_EQ("0", t[0].lexeme);
    free_tokens(t, n);
}

// ============== TESTS: STRINGS ==============

TEST(simple_string) {
    int n; Token *t = tokenize("\"hello\"", &n);
    ASSERT_EQ(TOKEN_STRING, t[0].type);
    ASSERT_STR_EQ("\"hello\"", t[0].lexeme);
    free_tokens(t, n);
}

// ============== TESTS: OPERATORS ==============

TEST(arithmetic_operators) {
    int n; Token *t = tokenize("+ - * / %", &n);
    ASSERT_EQ(TOKEN_PLUS, t[0].type);
    ASSERT_EQ(TOKEN_MINUS, t[1].type);
    ASSERT_EQ(TOKEN_MULT, t[2].type);
    ASSERT_EQ(TOKEN_DIV, t[3].type);
    ASSERT_EQ(TOKEN_MOD, t[4].type);
    free_tokens(t, n);
}

TEST(comparison_operators) {
    int n; Token *t = tokenize("< > <= >= == !=", &n);
    ASSERT_EQ(TOKEN_LT, t[0].type);
    ASSERT_EQ(TOKEN_GT, t[1].type);
    ASSERT_EQ(TOKEN_LE, t[2].type);
    ASSERT_EQ(TOKEN_GE, t[3].type);
    ASSERT_EQ(TOKEN_EQ, t[4].type);
    ASSERT_EQ(TOKEN_NEQ, t[5].type);
    free_tokens(t, n);
}

TEST(assignment_operators) {
    int n; Token *t = tokenize("= :=", &n);
    ASSERT_EQ(TOKEN_ASSIGN, t[0].type);
    ASSERT_EQ(TOKEN_ASSIGN_DESTRUCT, t[1].type);
    free_tokens(t, n);
}

TEST(logical_operators) {
    // || y &&
    int n; Token *t = tokenize("|| &&", &n);
    ASSERT_EQ(TOKEN_OR, t[0].type);
    ASSERT_EQ(TOKEN_AND, t[1].type);
    free_tokens(t, n);
}

TEST(concat_operators) {
    int n; Token *t = tokenize("@ @@", &n);
    ASSERT_EQ(TOKEN_CONCAT, t[0].type);
    ASSERT_EQ(TOKEN_CONCAT_WS, t[1].type);
    free_tokens(t, n);
}

TEST(power_operator) {
    int n; Token *t = tokenize("**", &n);
    ASSERT_EQ(TOKEN_POW, t[0].type);
    free_tokens(t, n);
}

TEST(arrow_operator) {
    int n; Token *t = tokenize("=>", &n);
    ASSERT_EQ(TOKEN_ARROW, t[0].type);
    free_tokens(t, n);
}

// ============== TESTS: DELIMITERS ==============

TEST(delimiters) {
    int n; Token *t = tokenize("( ) { } , ; : .", &n);
    ASSERT_EQ(TOKEN_LPAREN, t[0].type);
    ASSERT_EQ(TOKEN_RPAREN, t[1].type);
    ASSERT_EQ(TOKEN_LBRACE, t[2].type);
    ASSERT_EQ(TOKEN_RBRACE, t[3].type);
    ASSERT_EQ(TOKEN_COMMA, t[4].type);
    ASSERT_EQ(TOKEN_SEMICOLON, t[5].type);
    ASSERT_EQ(TOKEN_COLON, t[6].type);
    ASSERT_EQ(TOKEN_DOT, t[7].type);
    free_tokens(t, n);
}

// ============== TESTS: WHITESPACE & COMMENTS ==============

TEST(whitespace_ignored) {
    int n; Token *t = tokenize("  let  \t  x  \n  ", &n);
    ASSERT_EQ(TOKEN_LET, t[0].type);
    ASSERT_EQ(TOKEN_IDENT, t[1].type);
    ASSERT_EQ(TOKEN_EOF, t[2].type);
    free_tokens(t, n);
}

TEST(comments_ignored) {
    int n; Token *t = tokenize("let x // this is a comment\n= 5", &n);
    ASSERT_EQ(TOKEN_LET, t[0].type);
    ASSERT_EQ(TOKEN_IDENT, t[1].type);
    ASSERT_EQ(TOKEN_ASSIGN, t[2].type);
    ASSERT_EQ(TOKEN_NUMBER, t[3].type);
    free_tokens(t, n);
}

// ============== TESTS: LINE/COL POSITIONS ==============

TEST(line_col_tracking) {
    int n; Token *t = tokenize("let\nx", &n);
    ASSERT_EQ(1, t[0].line);
    ASSERT_EQ(1, t[0].col);
    ASSERT_EQ(2, t[1].line);
    ASSERT_EQ(1, t[1].col);
    free_tokens(t, n);
}

// ============== TESTS: COMPOUND EXPRESSIONS ==============

TEST(let_expression) {
    int n; Token *t = tokenize("let x = 5;", &n);
    ASSERT_EQ(TOKEN_LET, t[0].type);
    ASSERT_EQ(TOKEN_IDENT, t[1].type);
    ASSERT_EQ(TOKEN_ASSIGN, t[2].type);
    ASSERT_EQ(TOKEN_NUMBER, t[3].type);
    ASSERT_EQ(TOKEN_SEMICOLON, t[4].type);
    ASSERT_EQ(TOKEN_EOF, t[5].type);
    free_tokens(t, n);
}

TEST(function_declaration) {
    int n; Token *t = tokenize("function f(x: Number): Number => x + 1;", &n);
    ASSERT_EQ(TOKEN_FUNCTION, t[0].type);
    ASSERT_EQ(TOKEN_IDENT, t[1].type);     // f
    ASSERT_EQ(TOKEN_LPAREN, t[2].type);
    ASSERT_EQ(TOKEN_IDENT, t[3].type);     // x
    ASSERT_EQ(TOKEN_COLON, t[4].type);
    ASSERT_EQ(TOKEN_IDENT, t[5].type);     // Number
    ASSERT_EQ(TOKEN_RPAREN, t[6].type);
    ASSERT_EQ(TOKEN_COLON, t[7].type);
    ASSERT_EQ(TOKEN_IDENT, t[8].type);     // Number
    ASSERT_EQ(TOKEN_ARROW, t[9].type);
    free_tokens(t, n);
}

TEST(empty_input) {
    int n; Token *t = tokenize("", &n);
    ASSERT_EQ(1, n);
    ASSERT_EQ(TOKEN_EOF, t[0].type);
    free_tokens(t, n);
}

// ============== MAIN ==============

int main(void) {
    printf("\n🧪 HULK Compiler — Lexer Unit Tests\n");

    TEST_SUITE("Keywords");
    RUN_TEST(keyword_let);
    RUN_TEST(keyword_function);
    RUN_TEST(keyword_if_else);
    RUN_TEST(keyword_while_for_in);
    RUN_TEST(keyword_type_inherits_new);
    RUN_TEST(keyword_true_false);
    RUN_TEST(keyword_self_base_is_as);
    RUN_TEST(keyword_decor);
    RUN_TEST(decor_not_identifier);

    TEST_SUITE("Identifiers");
    RUN_TEST(simple_identifier);
    RUN_TEST(identifier_with_underscore);
    RUN_TEST(identifier_not_keyword);

    TEST_SUITE("Numbers");
    RUN_TEST(integer_number);
    RUN_TEST(decimal_number);
    RUN_TEST(number_zero);

    TEST_SUITE("Strings");
    RUN_TEST(simple_string);

    TEST_SUITE("Operators");
    RUN_TEST(arithmetic_operators);
    RUN_TEST(comparison_operators);
    RUN_TEST(assignment_operators);
    RUN_TEST(logical_operators);
    RUN_TEST(concat_operators);
    RUN_TEST(power_operator);
    RUN_TEST(arrow_operator);

    TEST_SUITE("Delimiters");
    RUN_TEST(delimiters);

    TEST_SUITE("Whitespace & Comments");
    RUN_TEST(whitespace_ignored);
    RUN_TEST(comments_ignored);

    TEST_SUITE("Line/Column Tracking");
    RUN_TEST(line_col_tracking);

    TEST_SUITE("Compound Expressions");
    RUN_TEST(let_expression);
    RUN_TEST(function_declaration);
    RUN_TEST(empty_input);

    TEST_REPORT();

    // Cleanup
    if (hc_ready) hulk_compiler_free(&hc);

    return TEST_EXIT_CODE();
}
