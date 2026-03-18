/*
 * test_ast_builder.c — Tests unitarios para hulk_ast_builder
 *
 * Cubre:
 *   - Literales: números, strings, booleanos
 *   - Operadores: aritméticos, comparación, lógicos, concat
 *   - Precedencia y asociatividad
 *   - Expresiones: let, if/elif/else, unary
 *   - Sentencias: while, for, block
 *   - Definiciones: function, type, methods, attributes
 *   - Decoradores
 *   - Acceso: member, new, self, base, call
 *   - Asignación y asignación destructiva
 *   - as / is
 *   - Programas compuestos
 *   - Manejo de errores
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_ast/core/hulk_ast.h"
#include "../hulk_ast/builder/hulk_ast_builder.h"

#include <stdio.h>
#include <string.h>

// ============================================================
//  Fixture: compilador compartido (DFA construido una sola vez)
// ============================================================

static HulkCompiler hc;
static int hc_ready = 0;

static void ensure_compiler(void) {
    if (!hc_ready) {
        // Silenciar output del pipeline de construcción
#ifdef _WIN32
        freopen("NUL", "w", stdout);
#else
        freopen("/dev/null", "w", stdout);
#endif
        hc_ready = hulk_compiler_init(&hc);
#ifdef _WIN32
        freopen("CON", "w", stdout);
#else
        freopen("/dev/tty", "w", stdout);
#endif
    }
}

// Helper: construye AST a partir de código fuente HULK.
// El caller debe llamar hulk_ast_context_free(&ctx) después.
static HulkNode* build(const char *src, HulkASTContext *ctx) {
    ensure_compiler();
    hulk_ast_context_init(ctx);
    // Silenciar stderr para errores esperados
    return hulk_build_ast(ctx, hc.dfa, src);
}

// Helpers de casting seguros
#define AS_PROG(n)       ((ProgramNode*)(n))
#define AS_FUNC(n)       ((FunctionDefNode*)(n))
#define AS_TYPE(n)       ((TypeDefNode*)(n))
#define AS_METHOD(n)     ((MethodDefNode*)(n))
#define AS_ATTR(n)       ((AttributeDefNode*)(n))
#define AS_LET(n)        ((LetExprNode*)(n))
#define AS_BIND(n)       ((VarBindingNode*)(n))
#define AS_IF(n)         ((IfExprNode*)(n))
#define AS_ELIF(n)       ((ElifBranchNode*)(n))
#define AS_WHILE(n)      ((WhileStmtNode*)(n))
#define AS_FOR(n)        ((ForStmtNode*)(n))
#define AS_BLOCK(n)      ((BlockStmtNode*)(n))
#define AS_BINOP(n)      ((BinaryOpNode*)(n))
#define AS_UNARY(n)      ((UnaryOpNode*)(n))
#define AS_NUM(n)        ((NumberLitNode*)(n))
#define AS_STR(n)        ((StringLitNode*)(n))
#define AS_BOOL(n)       ((BoolLitNode*)(n))
#define AS_IDENT(n)      ((IdentNode*)(n))
#define AS_CALL(n)       ((CallExprNode*)(n))
#define AS_MEMBER(n)     ((MemberAccessNode*)(n))
#define AS_NEW(n)        ((NewExprNode*)(n))
#define AS_ASSIGN(n)     ((AssignNode*)(n))
#define AS_DASSIGN(n)    ((DestructAssignNode*)(n))
#define AS_AS(n)         ((AsExprNode*)(n))
#define AS_IS(n)         ((IsExprNode*)(n))
#define AS_SELF(n)       ((SelfNode*)(n))
#define AS_BASE(n)       ((BaseCallNode*)(n))
#define AS_DECOR(n)      ((DecorBlockNode*)(n))
#define AS_DITEM(n)      ((DecorItemNode*)(n))
#define AS_CONCAT(n)     ((ConcatExprNode*)(n))

// Helper para primer hijo del program
#define PROG_DECL(prog, i)  (AS_PROG(prog)->declarations.items[(i)])

// ============================================
//  Suite 1: Literales
// ============================================

TEST(number_lit_integer) {
    HulkASTContext ctx;
    HulkNode *ast = build("42;", &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(NODE_PROGRAM, ast->type);
    ASSERT_EQ(1, AS_PROG(ast)->declarations.count);

    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_NUMBER_LIT, n->type);
    ASSERT_STR_EQ("42", AS_NUM(n)->raw);
    hulk_ast_context_free(&ctx);
}

TEST(number_lit_decimal) {
    HulkASTContext ctx;
    HulkNode *ast = build("3.14;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_NUMBER_LIT, n->type);
    ASSERT_STR_EQ("3.14", AS_NUM(n)->raw);
    hulk_ast_context_free(&ctx);
}

TEST(string_lit_basic) {
    HulkASTContext ctx;
    HulkNode *ast = build("\"hello\";", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_STRING_LIT, n->type);
    ASSERT_STR_EQ("hello", AS_STR(n)->value);
    hulk_ast_context_free(&ctx);
}

TEST(string_lit_empty) {
    HulkASTContext ctx;
    HulkNode *ast = build("\"\";", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_STRING_LIT, n->type);
    ASSERT_STR_EQ("", AS_STR(n)->value);
    hulk_ast_context_free(&ctx);
}

TEST(bool_lit_true) {
    HulkASTContext ctx;
    HulkNode *ast = build("true;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BOOL_LIT, n->type);
    ASSERT_EQ(1, AS_BOOL(n)->value);
    hulk_ast_context_free(&ctx);
}

TEST(bool_lit_false) {
    HulkASTContext ctx;
    HulkNode *ast = build("false;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BOOL_LIT, n->type);
    ASSERT_EQ(0, AS_BOOL(n)->value);
    hulk_ast_context_free(&ctx);
}

TEST(ident_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_IDENT, n->type);
    ASSERT_STR_EQ("x", AS_IDENT(n)->name);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 2: Operadores aritméticos
// ============================================

TEST(binary_add) {
    HulkASTContext ctx;
    HulkNode *ast = build("1 + 2;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_ADD, AS_BINOP(n)->op);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BINOP(n)->left->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BINOP(n)->right->type);
    hulk_ast_context_free(&ctx);
}

TEST(binary_sub) {
    HulkASTContext ctx;
    HulkNode *ast = build("5 - 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_SUB, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(binary_mul) {
    HulkASTContext ctx;
    HulkNode *ast = build("2 * 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_MUL, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(binary_div) {
    HulkASTContext ctx;
    HulkNode *ast = build("10 / 2;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_DIV, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(binary_mod) {
    HulkASTContext ctx;
    HulkNode *ast = build("7 % 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_MOD, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(binary_pow) {
    HulkASTContext ctx;
    HulkNode *ast = build("2 ** 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_POW, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 3: Precedencia y asociatividad
// ============================================

// 1 + 2 * 3 → +(1, *(2, 3))
TEST(precedence_mul_over_add) {
    HulkASTContext ctx;
    HulkNode *ast = build("1 + 2 * 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_ADD, AS_BINOP(n)->op);
    // Right should be MUL
    ASSERT_EQ(NODE_BINARY_OP, AS_BINOP(n)->right->type);
    ASSERT_EQ(OP_MUL, AS_BINOP(AS_BINOP(n)->right)->op);
    // Left should be number
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BINOP(n)->left->type);
    hulk_ast_context_free(&ctx);
}

// 1 - 2 + 3 → +( -(1,2), 3)  (left-assoc)
TEST(left_assoc_add_sub) {
    HulkASTContext ctx;
    HulkNode *ast = build("1 - 2 + 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_ADD, AS_BINOP(n)->op);
    // Left should be SUB
    ASSERT_EQ(NODE_BINARY_OP, AS_BINOP(n)->left->type);
    ASSERT_EQ(OP_SUB, AS_BINOP(AS_BINOP(n)->left)->op);
    // Right should be number 3
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BINOP(n)->right->type);
    hulk_ast_context_free(&ctx);
}

// 2 ** 3 ** 4 → **(2, **(3, 4))  (right-assoc)
TEST(right_assoc_pow) {
    HulkASTContext ctx;
    HulkNode *ast = build("2 ** 3 ** 4;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_POW, AS_BINOP(n)->op);
    // Left = 2
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BINOP(n)->left->type);
    // Right = **(3, 4)
    ASSERT_EQ(NODE_BINARY_OP, AS_BINOP(n)->right->type);
    ASSERT_EQ(OP_POW, AS_BINOP(AS_BINOP(n)->right)->op);
    hulk_ast_context_free(&ctx);
}

// (1 + 2) * 3 → *(+(1,2), 3)
TEST(parenthesized_expr) {
    HulkASTContext ctx;
    HulkNode *ast = build("(1 + 2) * 3;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_MUL, AS_BINOP(n)->op);
    // Left should be ADD
    ASSERT_EQ(NODE_BINARY_OP, AS_BINOP(n)->left->type);
    ASSERT_EQ(OP_ADD, AS_BINOP(AS_BINOP(n)->left)->op);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 4: Comparación y lógicos
// ============================================

TEST(cmp_less_than) {
    HulkASTContext ctx;
    HulkNode *ast = build("x < 5;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_LT, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(cmp_equal) {
    HulkASTContext ctx;
    HulkNode *ast = build("a == b;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_EQ, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(cmp_not_equal) {
    HulkASTContext ctx;
    HulkNode *ast = build("a != b;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_NEQ, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(logical_and) {
    HulkASTContext ctx;
    HulkNode *ast = build("a && b;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_AND, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(logical_or) {
    HulkASTContext ctx;
    HulkNode *ast = build("a || b;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_OR, AS_BINOP(n)->op);
    hulk_ast_context_free(&ctx);
}

// a || b && c → ||(a, &&(b, c))  (AND binds tighter)
TEST(and_binds_tighter_than_or) {
    HulkASTContext ctx;
    HulkNode *ast = build("a || b && c;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BINARY_OP, n->type);
    ASSERT_EQ(OP_OR, AS_BINOP(n)->op);
    ASSERT_EQ(NODE_BINARY_OP, AS_BINOP(n)->right->type);
    ASSERT_EQ(OP_AND, AS_BINOP(AS_BINOP(n)->right)->op);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 5: Concatenación
// ============================================

TEST(concat_at) {
    HulkASTContext ctx;
    HulkNode *ast = build("\"a\" @ \"b\";", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CONCAT_EXPR, n->type);
    ASSERT_EQ(OP_CONCAT, AS_CONCAT(n)->op);
    ASSERT_EQ(NODE_STRING_LIT, AS_CONCAT(n)->left->type);
    ASSERT_EQ(NODE_STRING_LIT, AS_CONCAT(n)->right->type);
    hulk_ast_context_free(&ctx);
}

TEST(concat_at_at) {
    HulkASTContext ctx;
    HulkNode *ast = build("\"a\" @@ \"b\";", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CONCAT_EXPR, n->type);
    ASSERT_EQ(OP_CONCAT_WS, AS_CONCAT(n)->op);
    hulk_ast_context_free(&ctx);
}

TEST(concat_chain_left_assoc) {
    HulkASTContext ctx;
    // "a" @@ "b" @@ "c" → @@(@@("a","b"), "c")
    HulkNode *ast = build("\"a\" @@ \"b\" @@ \"c\";", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CONCAT_EXPR, n->type);
    // Left should be another concat
    ASSERT_EQ(NODE_CONCAT_EXPR, AS_CONCAT(n)->left->type);
    // Right should be string "c"
    ASSERT_EQ(NODE_STRING_LIT, AS_CONCAT(n)->right->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 6: Unary
// ============================================

TEST(unary_minus) {
    HulkASTContext ctx;
    HulkNode *ast = build("-5;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_UNARY_OP, n->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_UNARY(n)->operand->type);
    hulk_ast_context_free(&ctx);
}

TEST(unary_double_minus) {
    HulkASTContext ctx;
    // --x → Unary(Unary(x))
    HulkNode *ast = build("--x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_UNARY_OP, n->type);
    ASSERT_EQ(NODE_UNARY_OP, AS_UNARY(n)->operand->type);
    ASSERT_EQ(NODE_IDENT, AS_UNARY(AS_UNARY(n)->operand)->operand->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 7: LetExpr
// ============================================

TEST(let_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("let x = 5 in x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, n->type);
    ASSERT_EQ(1, AS_LET(n)->bindings.count);

    HulkNode *b0 = AS_LET(n)->bindings.items[0];
    ASSERT_EQ(NODE_VAR_BINDING, b0->type);
    ASSERT_STR_EQ("x", AS_BIND(b0)->name);
    ASSERT_NULL(AS_BIND(b0)->type_annotation);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BIND(b0)->init_expr->type);

    ASSERT_EQ(NODE_IDENT, AS_LET(n)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(let_with_type_annotation) {
    HulkASTContext ctx;
    HulkNode *ast = build("let x: Number = 5 in x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, n->type);

    HulkNode *b0 = AS_LET(n)->bindings.items[0];
    ASSERT_STR_EQ("Number", AS_BIND(b0)->type_annotation);
    hulk_ast_context_free(&ctx);
}

TEST(let_multiple_bindings) {
    HulkASTContext ctx;
    HulkNode *ast = build("let x = 1, y = 2 in x + y;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, n->type);
    ASSERT_EQ(2, AS_LET(n)->bindings.count);
    ASSERT_STR_EQ("x", AS_BIND(AS_LET(n)->bindings.items[0])->name);
    ASSERT_STR_EQ("y", AS_BIND(AS_LET(n)->bindings.items[1])->name);
    ASSERT_EQ(NODE_BINARY_OP, AS_LET(n)->body->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 8: IfExpr
// ============================================

TEST(if_else_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("if (true) 1 else 0;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_IF_EXPR, n->type);
    ASSERT_EQ(NODE_BOOL_LIT, AS_IF(n)->condition->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_IF(n)->then_body->type);
    ASSERT_EQ(0, AS_IF(n)->elifs.count);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_IF(n)->else_body->type);
    hulk_ast_context_free(&ctx);
}

TEST(if_elif_else) {
    HulkASTContext ctx;
    HulkNode *ast = build("if (a) 1 elif (b) 2 elif (c) 3 else 4;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_IF_EXPR, n->type);
    ASSERT_EQ(2, AS_IF(n)->elifs.count);

    HulkNode *elif0 = AS_IF(n)->elifs.items[0];
    ASSERT_EQ(NODE_ELIF_BRANCH, elif0->type);
    ASSERT_EQ(NODE_IDENT, AS_ELIF(elif0)->condition->type);
    ASSERT_STR_EQ("b", AS_IDENT(AS_ELIF(elif0)->condition)->name);

    HulkNode *elif1 = AS_IF(n)->elifs.items[1];
    ASSERT_STR_EQ("c", AS_IDENT(AS_ELIF(elif1)->condition)->name);

    ASSERT_EQ(NODE_NUMBER_LIT, AS_IF(n)->else_body->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 9: WhileStmt
// ============================================

TEST(while_basic) {
    HulkASTContext ctx;
    HulkNode *ast = build("while (x > 0) x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_WHILE_STMT, n->type);
    // Condition: (x > 0) — parsed as grouping, inner is BinaryOp
    ASSERT_NOT_NULL(AS_WHILE(n)->condition);
    ASSERT_NOT_NULL(AS_WHILE(n)->body);
    hulk_ast_context_free(&ctx);
}

TEST(while_with_block) {
    HulkASTContext ctx;
    HulkNode *ast = build("while (true) { x; };", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_WHILE_STMT, n->type);
    ASSERT_EQ(NODE_BLOCK_STMT, AS_WHILE(n)->body->type);
    ASSERT_EQ(1, AS_BLOCK(AS_WHILE(n)->body)->statements.count);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 10: ForStmt
// ============================================

TEST(for_basic) {
    HulkASTContext ctx;
    HulkNode *ast = build("for (i in items) print(i);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FOR_STMT, n->type);
    ASSERT_STR_EQ("i", AS_FOR(n)->var_name);
    ASSERT_EQ(NODE_IDENT, AS_FOR(n)->iterable->type);
    ASSERT_STR_EQ("items", AS_IDENT(AS_FOR(n)->iterable)->name);
    // Body is a call to print
    ASSERT_EQ(NODE_CALL_EXPR, AS_FOR(n)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(for_with_block) {
    HulkASTContext ctx;
    HulkNode *ast = build("for (x in list) { x; };", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FOR_STMT, n->type);
    ASSERT_EQ(NODE_BLOCK_STMT, AS_FOR(n)->body->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 11: BlockStmt
// ============================================

TEST(block_multiple_stmts) {
    HulkASTContext ctx;
    HulkNode *ast = build("{ 1; 2; 3; };", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BLOCK_STMT, n->type);
    ASSERT_EQ(3, AS_BLOCK(n)->statements.count);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BLOCK(n)->statements.items[0]->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BLOCK(n)->statements.items[1]->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_BLOCK(n)->statements.items[2]->type);
    hulk_ast_context_free(&ctx);
}

TEST(block_empty) {
    HulkASTContext ctx;
    HulkNode *ast = build("{ };", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BLOCK_STMT, n->type);
    ASSERT_EQ(0, AS_BLOCK(n)->statements.count);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 12: FunctionDef
// ============================================

TEST(function_arrow_body) {
    HulkASTContext ctx;
    HulkNode *ast = build("function add(a: Number, b: Number): Number => a + b;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FUNCTION_DEF, n->type);
    ASSERT_STR_EQ("add", AS_FUNC(n)->name);
    ASSERT_STR_EQ("Number", AS_FUNC(n)->return_type);
    ASSERT_EQ(2, AS_FUNC(n)->params.count);

    HulkNode *p0 = AS_FUNC(n)->params.items[0];
    ASSERT_STR_EQ("a", AS_BIND(p0)->name);
    ASSERT_STR_EQ("Number", AS_BIND(p0)->type_annotation);

    HulkNode *p1 = AS_FUNC(n)->params.items[1];
    ASSERT_STR_EQ("b", AS_BIND(p1)->name);

    ASSERT_EQ(NODE_BINARY_OP, AS_FUNC(n)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(function_block_body) {
    HulkASTContext ctx;
    HulkNode *ast = build("function greet() { 42; }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FUNCTION_DEF, n->type);
    ASSERT_STR_EQ("greet", AS_FUNC(n)->name);
    ASSERT_NULL(AS_FUNC(n)->return_type);
    ASSERT_EQ(0, AS_FUNC(n)->params.count);
    ASSERT_EQ(NODE_BLOCK_STMT, AS_FUNC(n)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(function_no_params_no_return) {
    HulkASTContext ctx;
    HulkNode *ast = build("function foo() => 1;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FUNCTION_DEF, n->type);
    ASSERT_STR_EQ("foo", AS_FUNC(n)->name);
    ASSERT_NULL(AS_FUNC(n)->return_type);
    ASSERT_EQ(0, AS_FUNC(n)->params.count);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 13: TypeDef
// ============================================

TEST(type_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("type Point(x: Number, y: Number) { }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, n->type);
    ASSERT_STR_EQ("Point", AS_TYPE(n)->name);
    ASSERT_NULL(AS_TYPE(n)->parent);
    ASSERT_EQ(2, AS_TYPE(n)->params.count);
    ASSERT_STR_EQ("x", AS_BIND(AS_TYPE(n)->params.items[0])->name);
    ASSERT_STR_EQ("y", AS_BIND(AS_TYPE(n)->params.items[1])->name);
    hulk_ast_context_free(&ctx);
}

TEST(type_with_inheritance) {
    HulkASTContext ctx;
    HulkNode *ast = build("type Dog(n: String) inherits Animal(n) { }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, n->type);
    ASSERT_STR_EQ("Dog", AS_TYPE(n)->name);
    ASSERT_STR_EQ("Animal", AS_TYPE(n)->parent);
    ASSERT_EQ(1, AS_TYPE(n)->parent_args.count);
    hulk_ast_context_free(&ctx);
}

TEST(type_with_method) {
    HulkASTContext ctx;
    HulkNode *ast = build("type Foo() { bar(): Number => 42; }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, n->type);
    ASSERT_EQ(1, AS_TYPE(n)->members.count);

    HulkNode *m = AS_TYPE(n)->members.items[0];
    ASSERT_EQ(NODE_METHOD_DEF, m->type);
    ASSERT_STR_EQ("bar", AS_METHOD(m)->name);
    ASSERT_STR_EQ("Number", AS_METHOD(m)->return_type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_METHOD(m)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(type_with_attribute) {
    HulkASTContext ctx;
    HulkNode *ast = build("type Foo() { x: Number = 5; }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, n->type);
    ASSERT_EQ(1, AS_TYPE(n)->members.count);

    HulkNode *a = AS_TYPE(n)->members.items[0];
    ASSERT_EQ(NODE_ATTRIBUTE_DEF, a->type);
    ASSERT_STR_EQ("x", AS_ATTR(a)->name);
    ASSERT_STR_EQ("Number", AS_ATTR(a)->type_annotation);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_ATTR(a)->init_expr->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 14: Calls, Members, New
// ============================================

TEST(call_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("print(42);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CALL_EXPR, n->type);
    ASSERT_EQ(NODE_IDENT, AS_CALL(n)->callee->type);
    ASSERT_STR_EQ("print", AS_IDENT(AS_CALL(n)->callee)->name);
    ASSERT_EQ(1, AS_CALL(n)->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(call_multiple_args) {
    HulkASTContext ctx;
    HulkNode *ast = build("f(1, 2, 3);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CALL_EXPR, n->type);
    ASSERT_EQ(3, AS_CALL(n)->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(call_no_args) {
    HulkASTContext ctx;
    HulkNode *ast = build("f();", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CALL_EXPR, n->type);
    ASSERT_EQ(0, AS_CALL(n)->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(member_access) {
    HulkASTContext ctx;
    HulkNode *ast = build("obj.field;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_MEMBER_ACCESS, n->type);
    ASSERT_STR_EQ("field", AS_MEMBER(n)->member);
    ASSERT_EQ(NODE_IDENT, AS_MEMBER(n)->object->type);
    hulk_ast_context_free(&ctx);
}

TEST(member_chain) {
    HulkASTContext ctx;
    // a.b.c → MemberAccess(MemberAccess(a, b), c)
    HulkNode *ast = build("a.b.c;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_MEMBER_ACCESS, n->type);
    ASSERT_STR_EQ("c", AS_MEMBER(n)->member);
    ASSERT_EQ(NODE_MEMBER_ACCESS, AS_MEMBER(n)->object->type);
    ASSERT_STR_EQ("b", AS_MEMBER(AS_MEMBER(n)->object)->member);
    hulk_ast_context_free(&ctx);
}

TEST(method_call) {
    HulkASTContext ctx;
    // obj.method(1, 2) → Call(MemberAccess(obj, method), [1, 2])
    HulkNode *ast = build("obj.method(1, 2);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_CALL_EXPR, n->type);
    ASSERT_EQ(2, AS_CALL(n)->args.count);
    ASSERT_EQ(NODE_MEMBER_ACCESS, AS_CALL(n)->callee->type);
    ASSERT_STR_EQ("method", AS_MEMBER(AS_CALL(n)->callee)->member);
    hulk_ast_context_free(&ctx);
}

TEST(new_expr) {
    HulkASTContext ctx;
    HulkNode *ast = build("new Point(3, 4);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_NEW_EXPR, n->type);
    ASSERT_STR_EQ("Point", AS_NEW(n)->type_name);
    ASSERT_EQ(2, AS_NEW(n)->args.count);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 15: Assignment
// ============================================

TEST(destruct_assign) {
    HulkASTContext ctx;
    HulkNode *ast = build("x := 5;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DESTRUCT_ASSIGN, n->type);
    ASSERT_EQ(NODE_IDENT, AS_DASSIGN(n)->target->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_DASSIGN(n)->value->type);
    hulk_ast_context_free(&ctx);
}

TEST(regular_assign) {
    HulkASTContext ctx;
    HulkNode *ast = build("x = 5;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_ASSIGN, n->type);
    ASSERT_EQ(NODE_IDENT, AS_ASSIGN(n)->target->type);
    ASSERT_EQ(NODE_NUMBER_LIT, AS_ASSIGN(n)->value->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 16: Is / As
// ============================================

TEST(is_expr) {
    HulkASTContext ctx;
    HulkNode *ast = build("x is Number;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_IS_EXPR, n->type);
    ASSERT_STR_EQ("Number", AS_IS(n)->type_name);
    ASSERT_EQ(NODE_IDENT, AS_IS(n)->expr->type);
    hulk_ast_context_free(&ctx);
}

TEST(as_expr) {
    HulkASTContext ctx;
    HulkNode *ast = build("x as Number;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_AS_EXPR, n->type);
    ASSERT_STR_EQ("Number", AS_AS(n)->type_name);
    ASSERT_EQ(NODE_IDENT, AS_AS(n)->expr->type);
    hulk_ast_context_free(&ctx);
}

TEST(as_chain) {
    HulkASTContext ctx;
    // x as A as B → As(As(x, A), B)
    HulkNode *ast = build("x as A as B;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_AS_EXPR, n->type);
    ASSERT_STR_EQ("B", AS_AS(n)->type_name);
    ASSERT_EQ(NODE_AS_EXPR, AS_AS(n)->expr->type);
    ASSERT_STR_EQ("A", AS_AS(AS_AS(n)->expr)->type_name);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 17: Self / Base
// ============================================

TEST(self_expr) {
    HulkASTContext ctx;
    HulkNode *ast = build("self;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_SELF, n->type);
    hulk_ast_context_free(&ctx);
}

TEST(self_member_access) {
    HulkASTContext ctx;
    HulkNode *ast = build("self.x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_MEMBER_ACCESS, n->type);
    ASSERT_STR_EQ("x", AS_MEMBER(n)->member);
    ASSERT_EQ(NODE_SELF, AS_MEMBER(n)->object->type);
    hulk_ast_context_free(&ctx);
}

TEST(base_call) {
    HulkASTContext ctx;
    HulkNode *ast = build("base(1, 2);", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_BASE_CALL, n->type);
    ASSERT_EQ(2, AS_BASE(n)->args.count);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 18: Decorators
// ============================================

TEST(decor_simple) {
    HulkASTContext ctx;
    HulkNode *ast = build("decor log function foo() => 1;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DECOR_BLOCK, n->type);
    ASSERT_EQ(1, AS_DECOR(n)->decorators.count);

    HulkNode *d0 = AS_DECOR(n)->decorators.items[0];
    ASSERT_EQ(NODE_DECOR_ITEM, d0->type);
    ASSERT_STR_EQ("log", AS_DITEM(d0)->name);
    ASSERT_EQ(0, AS_DITEM(d0)->args.count);

    ASSERT_NOT_NULL(AS_DECOR(n)->target);
    ASSERT_EQ(NODE_FUNCTION_DEF, AS_DECOR(n)->target->type);
    hulk_ast_context_free(&ctx);
}

TEST(decor_with_args) {
    HulkASTContext ctx;
    HulkNode *ast = build("decor memoize(100) function f() => 1;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DECOR_BLOCK, n->type);
    HulkNode *d0 = AS_DECOR(n)->decorators.items[0];
    ASSERT_STR_EQ("memoize", AS_DITEM(d0)->name);
    ASSERT_EQ(1, AS_DITEM(d0)->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(decor_multiple) {
    HulkASTContext ctx;
    HulkNode *ast = build("decor log, trace function f() => 1;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DECOR_BLOCK, n->type);
    ASSERT_EQ(2, AS_DECOR(n)->decorators.count);
    ASSERT_STR_EQ("log", AS_DITEM(AS_DECOR(n)->decorators.items[0])->name);
    ASSERT_STR_EQ("trace", AS_DITEM(AS_DECOR(n)->decorators.items[1])->name);
    hulk_ast_context_free(&ctx);
}

TEST(decor_on_type) {
    HulkASTContext ctx;
    HulkNode *ast = build("decor serialize type Foo() { }", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_DECOR_BLOCK, n->type);
    ASSERT_EQ(NODE_TYPE_DEF, AS_DECOR(n)->target->type);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 19: Composite programs
// ============================================

TEST(program_multiple_decls) {
    HulkASTContext ctx;
    HulkNode *ast = build(
        "function f() => 1;"
        "type T() { }"
        "42;",
        &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(NODE_PROGRAM, ast->type);
    ASSERT_EQ(3, AS_PROG(ast)->declarations.count);
    ASSERT_EQ(NODE_FUNCTION_DEF, PROG_DECL(ast, 0)->type);
    ASSERT_EQ(NODE_TYPE_DEF, PROG_DECL(ast, 1)->type);
    ASSERT_EQ(NODE_NUMBER_LIT, PROG_DECL(ast, 2)->type);
    hulk_ast_context_free(&ctx);
}

TEST(program_empty) {
    HulkASTContext ctx;
    HulkNode *ast = build("", &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(NODE_PROGRAM, ast->type);
    ASSERT_EQ(0, AS_PROG(ast)->declarations.count);
    hulk_ast_context_free(&ctx);
}

TEST(factorial_recursive) {
    HulkASTContext ctx;
    HulkNode *ast = build(
        "function factorial(n: Number): Number =>"
        "  if (n == 0) 1 else n * factorial(n - 1);",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *fn = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_FUNCTION_DEF, fn->type);
    ASSERT_STR_EQ("factorial", AS_FUNC(fn)->name);
    ASSERT_EQ(1, AS_FUNC(fn)->params.count);
    ASSERT_STR_EQ("Number", AS_FUNC(fn)->return_type);
    ASSERT_EQ(NODE_IF_EXPR, AS_FUNC(fn)->body->type);
    hulk_ast_context_free(&ctx);
}

TEST(type_with_inheritance_and_methods) {
    HulkASTContext ctx;
    HulkNode *ast = build(
        "type Dog(name: String) inherits Animal(name) {"
        "  speak(): String => name;"
        "}",
        &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *td = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_TYPE_DEF, td->type);
    ASSERT_STR_EQ("Dog", AS_TYPE(td)->name);
    ASSERT_STR_EQ("Animal", AS_TYPE(td)->parent);
    ASSERT_EQ(1, AS_TYPE(td)->parent_args.count);
    ASSERT_EQ(1, AS_TYPE(td)->members.count);
    ASSERT_EQ(NODE_METHOD_DEF, AS_TYPE(td)->members.items[0]->type);
    hulk_ast_context_free(&ctx);
}

TEST(let_in_if_nested) {
    HulkASTContext ctx;
    HulkNode *ast = build(
        "let x = 5 in if (x > 0) x else -x;", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, n->type);
    ASSERT_EQ(NODE_IF_EXPR, AS_LET(n)->body->type);

    IfExprNode *ie = AS_IF(AS_LET(n)->body);
    ASSERT_EQ(NODE_BINARY_OP, ie->condition->type);
    ASSERT_EQ(NODE_IDENT, ie->then_body->type);
    ASSERT_EQ(NODE_UNARY_OP, ie->else_body->type);
    hulk_ast_context_free(&ctx);
}

TEST(new_with_method_call) {
    HulkASTContext ctx;
    HulkNode *ast = build(
        "let p = new Point(3, 4) in p.dist();", &ctx);
    ASSERT_NOT_NULL(ast);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(NODE_LET_EXPR, n->type);

    // Binding: p = new Point(3, 4)
    HulkNode *b0 = AS_LET(n)->bindings.items[0];
    ASSERT_EQ(NODE_NEW_EXPR, AS_BIND(b0)->init_expr->type);
    ASSERT_STR_EQ("Point", AS_NEW(AS_BIND(b0)->init_expr)->type_name);

    // Body: p.dist()
    ASSERT_EQ(NODE_CALL_EXPR, AS_LET(n)->body->type);
    CallExprNode *call = AS_CALL(AS_LET(n)->body);
    ASSERT_EQ(NODE_MEMBER_ACCESS, call->callee->type);
    ASSERT_STR_EQ("dist", AS_MEMBER(call->callee)->member);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 20: Error handling
// ============================================

TEST(error_returns_null) {
    HulkASTContext ctx;
    // Redirect stderr to suppress error messages
    HulkNode *ast = build("let = ;", &ctx);
    // Should return NULL on error
    ASSERT_NULL(ast);
    hulk_ast_context_free(&ctx);
}

TEST(error_missing_semicolon) {
    HulkASTContext ctx;
    HulkNode *ast = build("42", &ctx);
    // Missing semicolon → error → NULL
    ASSERT_NULL(ast);
    hulk_ast_context_free(&ctx);
}

TEST(error_null_input) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *ast = hulk_build_ast(&ctx, hc.dfa, NULL);
    ASSERT_NULL(ast);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 21: Line/col tracking
// ============================================

TEST(position_tracking) {
    HulkASTContext ctx;
    HulkNode *ast = build("42;", &ctx);
    ASSERT_NOT_NULL(ast);
    ASSERT_EQ(1, ast->line);
    ASSERT_EQ(1, ast->col);
    HulkNode *n = PROG_DECL(ast, 0);
    ASSERT_EQ(1, n->line);
    ASSERT_EQ(1, n->col);
    hulk_ast_context_free(&ctx);
}

// ============================================================
//  Main
// ============================================================

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║   Tests: AST Builder                  ║\n");
    printf("╚═══════════════════════════════════════╝\n");

    TEST_SUITE("Literales");
    RUN_TEST(number_lit_integer);
    RUN_TEST(number_lit_decimal);
    RUN_TEST(string_lit_basic);
    RUN_TEST(string_lit_empty);
    RUN_TEST(bool_lit_true);
    RUN_TEST(bool_lit_false);
    RUN_TEST(ident_simple);

    TEST_SUITE("Operadores aritméticos");
    RUN_TEST(binary_add);
    RUN_TEST(binary_sub);
    RUN_TEST(binary_mul);
    RUN_TEST(binary_div);
    RUN_TEST(binary_mod);
    RUN_TEST(binary_pow);

    TEST_SUITE("Precedencia y asociatividad");
    RUN_TEST(precedence_mul_over_add);
    RUN_TEST(left_assoc_add_sub);
    RUN_TEST(right_assoc_pow);
    RUN_TEST(parenthesized_expr);

    TEST_SUITE("Comparación y lógicos");
    RUN_TEST(cmp_less_than);
    RUN_TEST(cmp_equal);
    RUN_TEST(cmp_not_equal);
    RUN_TEST(logical_and);
    RUN_TEST(logical_or);
    RUN_TEST(and_binds_tighter_than_or);

    TEST_SUITE("Concatenación");
    RUN_TEST(concat_at);
    RUN_TEST(concat_at_at);
    RUN_TEST(concat_chain_left_assoc);

    TEST_SUITE("Unary");
    RUN_TEST(unary_minus);
    RUN_TEST(unary_double_minus);

    TEST_SUITE("LetExpr");
    RUN_TEST(let_simple);
    RUN_TEST(let_with_type_annotation);
    RUN_TEST(let_multiple_bindings);

    TEST_SUITE("IfExpr");
    RUN_TEST(if_else_simple);
    RUN_TEST(if_elif_else);

    TEST_SUITE("WhileStmt");
    RUN_TEST(while_basic);
    RUN_TEST(while_with_block);

    TEST_SUITE("ForStmt");
    RUN_TEST(for_basic);
    RUN_TEST(for_with_block);

    TEST_SUITE("BlockStmt");
    RUN_TEST(block_multiple_stmts);
    RUN_TEST(block_empty);

    TEST_SUITE("FunctionDef");
    RUN_TEST(function_arrow_body);
    RUN_TEST(function_block_body);
    RUN_TEST(function_no_params_no_return);

    TEST_SUITE("TypeDef");
    RUN_TEST(type_simple);
    RUN_TEST(type_with_inheritance);
    RUN_TEST(type_with_method);
    RUN_TEST(type_with_attribute);

    TEST_SUITE("Calls, Members, New");
    RUN_TEST(call_simple);
    RUN_TEST(call_multiple_args);
    RUN_TEST(call_no_args);
    RUN_TEST(member_access);
    RUN_TEST(member_chain);
    RUN_TEST(method_call);
    RUN_TEST(new_expr);

    TEST_SUITE("Asignación");
    RUN_TEST(destruct_assign);
    RUN_TEST(regular_assign);

    TEST_SUITE("Is / As");
    RUN_TEST(is_expr);
    RUN_TEST(as_expr);
    RUN_TEST(as_chain);

    TEST_SUITE("Self / Base");
    RUN_TEST(self_expr);
    RUN_TEST(self_member_access);
    RUN_TEST(base_call);

    TEST_SUITE("Decoradores");
    RUN_TEST(decor_simple);
    RUN_TEST(decor_with_args);
    RUN_TEST(decor_multiple);
    RUN_TEST(decor_on_type);

    TEST_SUITE("Programas compuestos");
    RUN_TEST(program_multiple_decls);
    RUN_TEST(program_empty);
    RUN_TEST(factorial_recursive);
    RUN_TEST(type_with_inheritance_and_methods);
    RUN_TEST(let_in_if_nested);
    RUN_TEST(new_with_method_call);

    TEST_SUITE("Manejo de errores");
    RUN_TEST(error_returns_null);
    RUN_TEST(error_missing_semicolon);
    RUN_TEST(error_null_input);

    TEST_SUITE("Posiciones");
    RUN_TEST(position_tracking);

    TEST_REPORT();

    // Cleanup
    if (hc_ready) hulk_compiler_free(&hc);

    return TEST_EXIT_CODE();
}
