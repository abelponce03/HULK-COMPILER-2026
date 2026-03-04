/*
 * test_parser.c — Tests unitarios del parser LL(1)
 *
 * Verifica:
 *  - Parsing exitoso de expresiones HULK válidas
 *  - Detección de errores sintácticos
 *  - Recuperación de errores (panic mode)
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_tokens.h"
#include "../generador_analizadores_lexicos/lexer.h"
#include "../generador_parser_ll1/parser.h"
#include "../generador_parser_ll1/grammar.h"
#include "../generador_parser_ll1/first_follow.h"
#include <stdlib.h>

// ============== HELPER ==============

static HulkCompiler hc;
static Grammar grammar;
static First_Table first;
static Follow_Table follow;
static LL1_Table ll1;
static int infrastructure_ready = 0;

static Token parser_get_token(void *user) {
    return lexer_next_token((LexerContext *)user);
}

static void ensure_infrastructure(void) {
    if (infrastructure_ready) return;

    if (!hulk_compiler_init(&hc)) {
        fprintf(stderr, "FATAL: no se pudo construir el lexer\n");
        exit(1);
    }

    grammar_init_hulk(&grammar);
    if (!grammar_load_hulk(&grammar, "grammar.ll1")) {
        fprintf(stderr, "FATAL: no se pudo cargar grammar.ll1\n");
        exit(1);
    }

    compute_first_sets(&grammar, &first);
    compute_follow_sets(&grammar, &first, &follow);
    build_ll1_table(&grammar, &first, &follow, &ll1);

    infrastructure_ready = 1;
}

// Retorna 1 si el input parsea sin errores, 0 si hay errores
static int parse_ok(const char *input) {
    ensure_infrastructure();
    ParserContext pctx;
    parser_init(&pctx, &grammar, &ll1, &follow);
    LexerContext lctx;
    lexer_init(&lctx, hc.dfa, input);
    parser_set_lexer(&pctx, parser_get_token, &lctx);
    return parser_parse(&pctx);
}

// Retorna la cantidad de errores al parsear
static int parse_errors(const char *input) {
    ensure_infrastructure();
    ParserContext pctx;
    parser_init(&pctx, &grammar, &ll1, &follow);
    LexerContext lctx;
    lexer_init(&lctx, hc.dfa, input);
    parser_set_lexer(&pctx, parser_get_token, &lctx);
    parser_parse(&pctx);
    return pctx.error_count;
}

// ============== TESTS: VALID PROGRAMS ==============

TEST(parse_let_simple) {
    ASSERT(parse_ok("let x = 5 in x;"));
}

TEST(parse_let_in) {
    ASSERT(parse_ok("let x = 5 in x + 1;"));
}

TEST(parse_function_decl) {
    ASSERT(parse_ok("function f(x: Number): Number => x + 1;"));
}

TEST(parse_if_else) {
    ASSERT(parse_ok("if (true) 1 else 2;"));
}

TEST(parse_while_loop) {
    ASSERT(parse_ok("while (true) { let x = 1 in x; };"));
}

TEST(parse_type_decl) {
    ASSERT(parse_ok(
        "type Point(x: Number, y: Number) {"
        "  sum(): Number => self.x + self.y;"
        "}"));
}

TEST(parse_arithmetic) {
    ASSERT(parse_ok("2 + 3 * 4;"));
}

TEST(parse_boolean_expr) {
    ASSERT(parse_ok("true || false && true;"));
}

TEST(parse_comparison) {
    ASSERT(parse_ok("5 < 10;"));
}

TEST(parse_method_call) {
    ASSERT(parse_ok("let p = new Point(1, 2) in p.sum();"));
}

TEST(parse_nested_let) {
    ASSERT(parse_ok("let x = 5 in let y = 10 in x + y;"));
}

TEST(parse_string_concat) {
    ASSERT(parse_ok("\"hello\" @@ \"world\";"));
}

// ============== TESTS: DECORATORS ==============

TEST(decor_single_function) {
    ASSERT(parse_ok(
        "decor log "
        "function factorial(n: Number): Number => "
        "if (n == 0) 1 else n * factorial(n - 1);"));
}

TEST(decor_parametrized) {
    ASSERT(parse_ok(
        "decor memoize(100) "
        "function fib(n: Number): Number => "
        "if (n <= 1) n else fib(n - 1) + fib(n - 2);"));
}

TEST(decor_comma_list) {
    // Multiple decorators with comma: decor a, b(args)
    ASSERT(parse_ok(
        "decor log, memoize(100) "
        "function fib(n: Number): Number => "
        "if (n <= 1) n else fib(n - 1) + fib(n - 2);"));
}

TEST(decor_stacked) {
    // Stacked decor lines
    ASSERT(parse_ok(
        "decor log "
        "decor memoize(100) "
        "function fib(n: Number): Number => "
        "if (n <= 1) n else fib(n - 1) + fib(n - 2);"));
}

TEST(decor_on_type) {
    ASSERT(parse_ok(
        "decor serializable "
        "type Point(x: Number, y: Number) {"
        "  sum(): Number => self.x + self.y;"
        "}"));
}

TEST(decor_multi_arg) {
    // Decorator with multiple arguments (currying)
    ASSERT(parse_ok(
        "decor retry(3, 500) "
        "function connect(): Number => 42;"));
}

TEST(decor_with_undecorated) {
    // Mix of decorated and undecorated functions
    ASSERT(parse_ok(
        "function plain(): Number => 1; "
        "decor log "
        "function decorated(): Number => 2;"));
}

TEST(error_decor_no_target) {
    // decor without function/type → error
    ASSERT_GT(parse_errors("decor log 5 + 3;"), 0);
}

TEST(error_decor_missing_ident) {
    // decor without identifier → error
    ASSERT_GT(parse_errors("decor function f(): Number => 1;"), 0);
}

// ============== TESTS: SYNTAX ERRORS ==============

TEST(error_missing_semicolon) {
    ASSERT_GT(parse_errors("let x = 5"), 0);
}

TEST(error_missing_assign) {
    ASSERT_GT(parse_errors("let x 5;"), 0);
}

TEST(parse_empty_input) {
    // Empty program is valid: Program -> ε
    ASSERT(parse_ok(""));
}

// ============== MAIN ==============

int main(void) {
    printf("\n🧪 HULK Compiler — Parser Unit Tests\n");

    TEST_SUITE("Valid Programs");
    RUN_TEST(parse_let_simple);
    RUN_TEST(parse_let_in);
    RUN_TEST(parse_function_decl);
    RUN_TEST(parse_if_else);
    RUN_TEST(parse_while_loop);
    RUN_TEST(parse_type_decl);
    RUN_TEST(parse_arithmetic);
    RUN_TEST(parse_boolean_expr);
    RUN_TEST(parse_comparison);
    RUN_TEST(parse_method_call);
    RUN_TEST(parse_nested_let);
    RUN_TEST(parse_string_concat);

    TEST_SUITE("Decorators (decor keyword)");
    RUN_TEST(decor_single_function);
    RUN_TEST(decor_parametrized);
    RUN_TEST(decor_comma_list);
    RUN_TEST(decor_stacked);
    RUN_TEST(decor_on_type);
    RUN_TEST(decor_multi_arg);
    RUN_TEST(decor_with_undecorated);
    RUN_TEST(error_decor_no_target);
    RUN_TEST(error_decor_missing_ident);

    TEST_SUITE("Edge Cases");
    RUN_TEST(parse_empty_input);

    TEST_SUITE("Syntax Errors");
    RUN_TEST(error_missing_semicolon);
    RUN_TEST(error_missing_assign);

    TEST_REPORT();

    // Cleanup
    if (infrastructure_ready) {
        ll1_table_free(&ll1);
        grammar_free(&grammar);
        hulk_compiler_free(&hc);
    }

    return TEST_EXIT_CODE();
}
