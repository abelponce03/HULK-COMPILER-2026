/*
 * test_ll1_builder.c — Tests del parser LL(1) paralelo.
 *
 * Este binario valida hulk_ll1_build_ast directamente; no cambia el parser
 * de producción hulk_build_ast.
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_ast/core/hulk_ast.h"
#include "../hulk_ast/builder/hulk_ll1_builder.h"

#include <stdio.h>

static HulkCompiler hc;
static int hc_ready = 0;

static void ensure_compiler(void) {
    if (hc_ready) return;
    FILE *saved = stdout;
    stdout = fopen("/dev/null", "w");
    hc_ready = hulk_compiler_init(&hc);
    fclose(stdout);
    stdout = saved;
}

static HulkNode* build_ll1(const char *src, HulkASTContext *ctx) {
    ensure_compiler();
    hulk_ast_context_init(ctx);
    return hulk_ll1_build_ast(ctx, hc.dfa, src);
}

#define AS_PROG(n)      ((ProgramNode*)(n))
#define AS_FUNC(n)      ((FunctionDefNode*)(n))
#define AS_FEXPR(n)     ((FunctionExprNode*)(n))
#define AS_TYPE(n)      ((TypeDefNode*)(n))
#define AS_METHOD(n)    ((MethodDefNode*)(n))
#define AS_ATTR(n)      ((AttributeDefNode*)(n))
#define AS_LET(n)       ((LetExprNode*)(n))
#define AS_BIND(n)      ((VarBindingNode*)(n))
#define AS_DECOR(n)     ((DecorBlockNode*)(n))
#define AS_DITEM(n)     ((DecorItemNode*)(n))
#define AS_CALL(n)      ((CallExprNode*)(n))
#define AS_IDENT(n)     ((IdentNode*)(n))
#define AS_VECTOR(n)    ((VectorLitNode*)(n))
#define AS_BINARY(n)    ((BinaryOpNode*)(n))
#define PROG_DECL(p, i) (AS_PROG(p)->declarations.items[(i)])

TEST(ll1_parses_function_definitions) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "function add(a: Number, b: Number): Number -> a + b;"
        "function unit() { 1; }",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(NODE_PROGRAM, ast->type);
    ASSERT_EQ(2, AS_PROG(ast)->declarations.count);
    ASSERT_EQ(NODE_FUNCTION_DEF, PROG_DECL(ast, 0)->type);
    ASSERT_STR_EQ("add", AS_FUNC(PROG_DECL(ast, 0))->name);
    ASSERT_STR_EQ("Number", AS_FUNC(PROG_DECL(ast, 0))->return_type);
    ASSERT_EQ(2, AS_FUNC(PROG_DECL(ast, 0))->params.count);
    ASSERT_EQ(NODE_BLOCK_STMT, AS_FUNC(PROG_DECL(ast, 1))->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_function_annotation_and_keyword_lambda) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "let f: (Number)->Number = function (x: Number): Number -> x + 1 in f(2);",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *root = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, root->type);
    HulkNode *binding = AS_LET(root)->bindings.items[0];
    ASSERT_STR_EQ("(Number)->Number", AS_BIND(binding)->type_annotation);
    ASSERT_EQ(NODE_FUNCTION_EXPR, AS_BIND(binding)->init_expr->type);
    ASSERT_STR_EQ("Number", AS_FEXPR(AS_BIND(binding)->init_expr)->return_type);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_parenthesized_lambda) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "let f = (x: Number): Number -> x + 1 in f(2);",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *root = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, root->type);
    HulkNode *binding = AS_LET(root)->bindings.items[0];
    ASSERT_EQ(NODE_FUNCTION_EXPR, AS_BIND(binding)->init_expr->type);
    ASSERT_EQ(1, AS_FEXPR(AS_BIND(binding)->init_expr)->params.count);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_types_and_protocols) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "protocol Shape { area(): Number; }"
        "type Circle(r: Number) inherits Shape { "
        "  radius: Number = r;"
        "  area(): Number -> r;"
        "}",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(2, AS_PROG(ast)->declarations.count);
    HulkNode *proto = PROG_DECL(ast, 0);
    HulkNode *type = PROG_DECL(ast, 1);
    ASSERT_EQ(NODE_TYPE_DEF, proto->type);
    ASSERT_EQ(1, AS_TYPE(proto)->is_protocol);
    ASSERT_EQ(1, AS_TYPE(proto)->members.count);
    ASSERT_EQ(NODE_TYPE_DEF, type->type);
    ASSERT_STR_EQ("Shape", AS_TYPE(type)->parent);
    ASSERT_EQ(2, AS_TYPE(type)->members.count);
    ASSERT_EQ(NODE_ATTRIBUTE_DEF, AS_TYPE(type)->members.items[0]->type);
    ASSERT_EQ(NODE_METHOD_DEF, AS_TYPE(type)->members.items[1]->type);
    ASSERT_STR_EQ("radius", AS_ATTR(AS_TYPE(type)->members.items[0])->name);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_decorated_definitions_and_methods) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "decor log, trace function foo(): Number -> 1;"
        "type Box(v: Number) { decor log get(): Number -> v; }",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(2, AS_PROG(ast)->declarations.count);

    HulkNode *decor = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DECOR_BLOCK, decor->type);
    ASSERT_EQ(2, AS_DECOR(decor)->decorators.count);
    ASSERT_STR_EQ("log", AS_DITEM(AS_DECOR(decor)->decorators.items[0])->name);
    ASSERT_EQ(NODE_FUNCTION_DEF, AS_DECOR(decor)->target->type);

    HulkNode *type = PROG_DECL(ast, 1);
    ASSERT_EQ(NODE_TYPE_DEF, type->type);
    ASSERT_EQ(1, AS_TYPE(type)->members.count);
    HulkNode *method = AS_TYPE(type)->members.items[0];
    ASSERT_EQ(NODE_METHOD_DEF, method->type);
    ASSERT_EQ(1, AS_METHOD(method)->decorators.count);
    ASSERT_STR_EQ("log", AS_DITEM(AS_METHOD(method)->decorators.items[0])->name);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_define_and_arrow_alias) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "define twice(x: Number): Number => x * 2;"
        "twice(21);",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(2, AS_PROG(ast)->declarations.count);
    ASSERT_EQ(NODE_FUNCTION_DEF, PROG_DECL(ast, 0)->type);
    ASSERT_STR_EQ("twice", AS_FUNC(PROG_DECL(ast, 0))->name);
    ASSERT_STR_EQ("Number", AS_FUNC(PROG_DECL(ast, 0))->return_type);
    ASSERT_EQ(NODE_CALL_EXPR, PROG_DECL(ast, 1)->type);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_arrays_and_c_initializer) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "let arr: Number[] = new Number[3]{ i -> i + 1 },"
        "    xs = {1, 2, 3}"
        " in arr[1] + xs[2];",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *root = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, root->type);
    ASSERT_EQ(2, AS_LET(root)->bindings.count);

    HulkNode *arr_bind = AS_LET(root)->bindings.items[0];
    ASSERT_STR_EQ("Number[]", AS_BIND(arr_bind)->type_annotation);
    ASSERT_EQ(NODE_CALL_EXPR, AS_BIND(arr_bind)->init_expr->type);
    HulkNode *callee = AS_CALL(AS_BIND(arr_bind)->init_expr)->callee;
    ASSERT_EQ(NODE_IDENT, callee->type);
    ASSERT_STR_EQ("__array_init", AS_IDENT(callee)->name);
    ASSERT_EQ(2, AS_CALL(AS_BIND(arr_bind)->init_expr)->args.count);
    ASSERT_EQ(NODE_FUNCTION_EXPR, AS_CALL(AS_BIND(arr_bind)->init_expr)->args.items[1]->type);

    HulkNode *xs_bind = AS_LET(root)->bindings.items[1];
    ASSERT_EQ(NODE_VECTOR_LIT, AS_BIND(xs_bind)->init_expr->type);
    ASSERT_EQ(3, AS_VECTOR(AS_BIND(xs_bind)->init_expr)->items.count);
    hulk_ast_context_free(&ctx);
}

TEST(ll1_parses_base_identifier_and_type_suffixes) {
    HulkASTContext ctx;
    HulkNode *ast = build_ll1(
        "function consume(it: Number*): Number -> 1;"
        "let base = 1 in base + if (true) 2 else 3;",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(2, AS_PROG(ast)->declarations.count);
    ASSERT_EQ(NODE_FUNCTION_DEF, PROG_DECL(ast, 0)->type);
    ASSERT_STR_EQ("Number*", AS_BIND(AS_FUNC(PROG_DECL(ast, 0))->params.items[0])->type_annotation);

    HulkNode *root = PROG_DECL(ast, 1);
    ASSERT_EQ(NODE_LET_EXPR, root->type);
    ASSERT_STR_EQ("base", AS_BIND(AS_LET(root)->bindings.items[0])->name);
    ASSERT_EQ(NODE_BINARY_OP, AS_LET(root)->body->type);
    ASSERT_EQ(OP_ADD, AS_BINARY(AS_LET(root)->body)->op);
    ASSERT_EQ(NODE_IF_EXPR, AS_BINARY(AS_LET(root)->body)->right->type);
    hulk_ast_context_free(&ctx);
}

int main(void) {
    TEST_SUITE("LL(1) AST Builder");
    RUN_TEST(ll1_parses_function_definitions);
    RUN_TEST(ll1_parses_function_annotation_and_keyword_lambda);
    RUN_TEST(ll1_parses_parenthesized_lambda);
    RUN_TEST(ll1_parses_types_and_protocols);
    RUN_TEST(ll1_parses_decorated_definitions_and_methods);
    RUN_TEST(ll1_parses_define_and_arrow_alias);
    RUN_TEST(ll1_parses_arrays_and_c_initializer);
    RUN_TEST(ll1_parses_base_identifier_and_type_suffixes);
    TEST_REPORT();
    if (hc_ready) hulk_compiler_free(&hc);
    return TEST_EXIT_CODE();
}
