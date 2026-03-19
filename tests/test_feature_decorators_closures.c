/*
 * test_feature_decorators_closures.c — Tests dedicados de la feature
 *
 * Cubre específicamente:
 *   - funciones anónimas con captura léxica
 *   - decoradores currificados
 *   - decoradores sobre métodos
 *   - regresión sintáctica de @ / @@
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_tokens.h"
#include "../generador_analizadores_lexicos/lexer.h"
#include "../generador_parser_ll1/parser.h"
#include "../generador_parser_ll1/grammar.h"
#include "../generador_parser_ll1/first_follow.h"
#include "../hulk_ast/core/hulk_ast.h"
#include "../hulk_ast/builder/hulk_ast_builder.h"
#include "../hulk_ast/semantic/hulk_semantic.h"

#include <stdio.h>
#include <stdlib.h>

static HulkCompiler hc;
static Grammar grammar;
static First_Table first;
static Follow_Table follow;
static LL1_Table ll1;
static int infra_ready = 0;

static Token parser_get_token(void *user) {
    return lexer_next_token((LexerContext *)user);
}

static void ensure_infra(void) {
    if (infra_ready) return;

    FILE *saved = stdout;
    stdout = fopen("/dev/null", "w");
    int ok = hulk_compiler_init(&hc);
    fclose(stdout);
    stdout = saved;

    if (!ok) {
        fprintf(stderr, "FATAL: no se pudo construir el compilador\n");
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

    infra_ready = 1;
}

static int parse_ok(const char *input) {
    ensure_infra();
    ParserContext pctx;
    parser_init(&pctx, &grammar, &ll1, &follow);
    LexerContext lctx;
    lexer_init(&lctx, hc.dfa, input);
    parser_set_lexer(&pctx, parser_get_token, &lctx);
    return parser_parse(&pctx);
}

static HulkNode* build_ast(const char *src, HulkASTContext *ctx) {
    ensure_infra();
    hulk_ast_context_init(ctx);

    FILE *saved_err = stderr;
    stderr = fopen("/dev/null", "w");
    HulkNode *ast = hulk_build_ast(ctx, hc.dfa, src);
    fclose(stderr);
    stderr = saved_err;

    return ast;
}

static int analyze(const char *src) {
    ensure_infra();
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    FILE *saved_err = stderr;
    stderr = fopen("/dev/null", "w");

    HulkNode *ast = hulk_build_ast(&ctx, hc.dfa, src);
    int errors = -1;
    if (ast) errors = hulk_semantic_analyze(&ctx, ast);

    fclose(stderr);
    stderr = saved_err;
    hulk_ast_context_free(&ctx);
    return errors;
}

#define AS_PROG(n)     ((ProgramNode*)(n))
#define AS_LET(n)      ((LetExprNode*)(n))
#define AS_BIND(n)     ((VarBindingNode*)(n))
#define AS_FEXPR(n)    ((FunctionExprNode*)(n))
#define AS_TYPE(n)     ((TypeDefNode*)(n))
#define AS_METHOD(n)   ((MethodDefNode*)(n))
#define AS_DITEM(n)    ((DecorItemNode*)(n))
#define PROG_DECL(p,i) (AS_PROG(p)->declarations.items[(i)])

TEST(parser_accepts_function_expr_and_concat_regression) {
    ASSERT(parse_ok(
        "let n = 1, f = function (x: Number): Number => x + n in "
        "\"a\" @@ \"b\";"));
}

TEST(ast_tracks_closure_capture_candidates) {
    HulkASTContext ctx;
    HulkNode *ast = build_ast(
        "let n = 5, add = function (x: Number): Number => x + n in add(3);",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *root = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, root->type);
    HulkNode *fn_init = AS_BIND(AS_LET(root)->bindings.items[1])->init_expr;
    ASSERT_EQ(NODE_FUNCTION_EXPR, fn_init->type);
    ASSERT_EQ(1, AS_FEXPR(fn_init)->params.count);
    hulk_ast_context_free(&ctx);
}

TEST(semantic_accepts_closure_capture) {
    ASSERT_EQ(0, analyze(
        "let n: Number = 5, add = function (x: Number): Number => x + n in add(3);"));
}

TEST(semantic_accepts_curried_decorator) {
    ASSERT_EQ(0, analyze(
        "function identity(f) => f;\n"
        "function memoize(limit: Number) => identity;\n"
        "decor memoize(100)\n"
        "function fib(n: Number): Number => n;\n"
        "fib(1);"));
}

TEST(semantic_rejects_bad_curried_decorator) {
    ASSERT_GT(analyze(
        "function memoize(limit: Number): Number => limit;\n"
        "decor memoize(100)\n"
        "function fib(n: Number): Number => n;\n"
        "fib(1);"), 0);
}

TEST(ast_parses_method_decorator) {
    HulkASTContext ctx;
    HulkNode *ast = build_ast(
        "type Box(v: Number) { decor log get(): Number => v; }",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *td = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, td->type);
    ASSERT_EQ(1, AS_TYPE(td)->members.count);
    ASSERT_EQ(1, AS_METHOD(AS_TYPE(td)->members.items[0])->decorators.count);
    ASSERT_STR_EQ("log",
        AS_DITEM(AS_METHOD(AS_TYPE(td)->members.items[0])->decorators.items[0])->name);
    hulk_ast_context_free(&ctx);
}

TEST(semantic_accepts_method_decorator) {
    ASSERT_EQ(0, analyze(
        "function logger(f) => f;\n"
        "type Box(v: Number) {\n"
        "  decor logger get(): Number => v;\n"
        "}\n"
        "let b = new Box(3) in b.get();"));
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║  Feature: Closures + Decorators       ║\n");
    printf("╚═══════════════════════════════════════╝\n");

    TEST_SUITE("Parser");
    RUN_TEST(parser_accepts_function_expr_and_concat_regression);

    TEST_SUITE("AST");
    RUN_TEST(ast_tracks_closure_capture_candidates);
    RUN_TEST(ast_parses_method_decorator);

    TEST_SUITE("Semantic");
    RUN_TEST(semantic_accepts_closure_capture);
    RUN_TEST(semantic_accepts_curried_decorator);
    RUN_TEST(semantic_rejects_bad_curried_decorator);
    RUN_TEST(semantic_accepts_method_decorator);

    TEST_REPORT();

    if (infra_ready) hulk_compiler_free(&hc);
    return TEST_EXIT_CODE();
}
