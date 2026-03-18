/*
 * hulk_ast_nodes.c — Funciones factory de nodos del AST
 *
 * Cada función asigna un nodo concreto desde la arena (HulkASTContext),
 * inicializa sus campos y retorna el puntero.
 *
 * El macro ALLOC_NODE encapsula: asignación + inicialización de la
 * cabecera HulkNode (type, line, col).
 *
 * SRP: Solo creación e inicialización de nodos del AST.
 */

#include "hulk_ast.h"
#include "../../error_handler.h"
#include <stdlib.h>
#include <string.h>

// ============== MACRO HELPER ==============
// Asigna un nodo del tipo dado, inicializa la base, y retorna.

#define ALLOC_NODE(ctx, StructType, node_type, ln, cl) \
    StructType *node = hulk_ast_alloc(ctx, sizeof(StructType)); \
    if (!node) return NULL;                                      \
    node->base.type = (node_type);                               \
    node->base.line = (ln);                                      \
    node->base.col  = (cl);

// ============== FUNCIONES DE CREACIÓN ==============

ProgramNode* hulk_ast_program(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, ProgramNode, NODE_PROGRAM, line, col);
    hulk_node_list_init(&node->declarations);
    return node;
}

FunctionDefNode* hulk_ast_function_def(HulkASTContext *ctx, const char *name,
                                        const char *ret_type, int line, int col) {
    ALLOC_NODE(ctx, FunctionDefNode, NODE_FUNCTION_DEF, line, col);
    node->name        = hulk_ast_strdup(ctx, name);
    node->return_type = hulk_ast_strdup(ctx, ret_type);
    node->body        = NULL;
    hulk_node_list_init(&node->params);
    hulk_node_list_init(&node->captured_vars);
    return node;
}

FunctionExprNode* hulk_ast_function_expr(HulkASTContext *ctx, const char *ret_type, int line, int col) {
    ALLOC_NODE(ctx, FunctionExprNode, NODE_FUNCTION_EXPR, line, col);
    node->return_type = hulk_ast_strdup(ctx, ret_type);
    node->body        = NULL;
    hulk_node_list_init(&node->params);
    hulk_node_list_init(&node->captured_vars);
    return node;
}

TypeDefNode* hulk_ast_type_def(HulkASTContext *ctx, const char *name,
                                const char *parent, int line, int col) {
    ALLOC_NODE(ctx, TypeDefNode, NODE_TYPE_DEF, line, col);
    node->name   = hulk_ast_strdup(ctx, name);
    node->parent = hulk_ast_strdup(ctx, parent);
    hulk_node_list_init(&node->params);
    hulk_node_list_init(&node->parent_args);
    hulk_node_list_init(&node->members);
    return node;
}

MethodDefNode* hulk_ast_method_def(HulkASTContext *ctx, const char *name,
                                    const char *ret_type, int line, int col) {
    ALLOC_NODE(ctx, MethodDefNode, NODE_METHOD_DEF, line, col);
    node->name        = hulk_ast_strdup(ctx, name);
    node->return_type = hulk_ast_strdup(ctx, ret_type);
    node->body        = NULL;
    hulk_node_list_init(&node->params);
    return node;
}

AttributeDefNode* hulk_ast_attribute_def(HulkASTContext *ctx, const char *name,
                                          const char *type_ann, int line, int col) {
    ALLOC_NODE(ctx, AttributeDefNode, NODE_ATTRIBUTE_DEF, line, col);
    node->name            = hulk_ast_strdup(ctx, name);
    node->type_annotation = hulk_ast_strdup(ctx, type_ann);
    node->init_expr       = NULL;
    return node;
}

LetExprNode* hulk_ast_let_expr(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, LetExprNode, NODE_LET_EXPR, line, col);
    hulk_node_list_init(&node->bindings);
    node->body = NULL;
    return node;
}

VarBindingNode* hulk_ast_var_binding(HulkASTContext *ctx, const char *name,
                                      const char *type_ann, int line, int col) {
    ALLOC_NODE(ctx, VarBindingNode, NODE_VAR_BINDING, line, col);
    node->name            = hulk_ast_strdup(ctx, name);
    node->type_annotation = hulk_ast_strdup(ctx, type_ann);
    node->init_expr       = NULL;
    return node;
}

IfExprNode* hulk_ast_if_expr(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, IfExprNode, NODE_IF_EXPR, line, col);
    node->condition = NULL;
    node->then_body = NULL;
    node->else_body = NULL;
    hulk_node_list_init(&node->elifs);
    return node;
}

ElifBranchNode* hulk_ast_elif_branch(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, ElifBranchNode, NODE_ELIF_BRANCH, line, col);
    node->condition = NULL;
    node->body      = NULL;
    return node;
}

WhileStmtNode* hulk_ast_while_stmt(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, WhileStmtNode, NODE_WHILE_STMT, line, col);
    node->condition = NULL;
    node->body      = NULL;
    return node;
}

ForStmtNode* hulk_ast_for_stmt(HulkASTContext *ctx, const char *var_name,
                                int line, int col) {
    ALLOC_NODE(ctx, ForStmtNode, NODE_FOR_STMT, line, col);
    node->var_name = hulk_ast_strdup(ctx, var_name);
    node->iterable = NULL;
    node->body     = NULL;
    return node;
}

BlockStmtNode* hulk_ast_block_stmt(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, BlockStmtNode, NODE_BLOCK_STMT, line, col);
    hulk_node_list_init(&node->statements);
    return node;
}

BinaryOpNode* hulk_ast_binary_op(HulkASTContext *ctx, BinaryOp op,
                                  HulkNode *left, HulkNode *right,
                                  int line, int col) {
    ALLOC_NODE(ctx, BinaryOpNode, NODE_BINARY_OP, line, col);
    node->op    = op;
    node->left  = left;
    node->right = right;
    return node;
}

UnaryOpNode* hulk_ast_unary_op(HulkASTContext *ctx, HulkNode *operand,
                                int line, int col) {
    ALLOC_NODE(ctx, UnaryOpNode, NODE_UNARY_OP, line, col);
    node->operand = operand;
    return node;
}

NumberLitNode* hulk_ast_number_lit(HulkASTContext *ctx, const char *raw,
                                    int line, int col) {
    ALLOC_NODE(ctx, NumberLitNode, NODE_NUMBER_LIT, line, col);
    node->raw   = hulk_ast_strdup(ctx, raw);
    node->value = raw ? atof(raw) : 0.0;
    return node;
}

StringLitNode* hulk_ast_string_lit(HulkASTContext *ctx, const char *value,
                                    int line, int col) {
    ALLOC_NODE(ctx, StringLitNode, NODE_STRING_LIT, line, col);
    node->value = hulk_ast_strdup(ctx, value);
    return node;
}

BoolLitNode* hulk_ast_bool_lit(HulkASTContext *ctx, int value,
                                int line, int col) {
    ALLOC_NODE(ctx, BoolLitNode, NODE_BOOL_LIT, line, col);
    node->value = value;
    return node;
}

IdentNode* hulk_ast_ident(HulkASTContext *ctx, const char *name,
                            int line, int col) {
    ALLOC_NODE(ctx, IdentNode, NODE_IDENT, line, col);
    node->name = hulk_ast_strdup(ctx, name);
    return node;
}

CallExprNode* hulk_ast_call_expr(HulkASTContext *ctx, HulkNode *callee,
                                  int line, int col) {
    ALLOC_NODE(ctx, CallExprNode, NODE_CALL_EXPR, line, col);
    node->callee = callee;
    hulk_node_list_init(&node->args);
    return node;
}

MemberAccessNode* hulk_ast_member_access(HulkASTContext *ctx, HulkNode *object,
                                          const char *member, int line, int col) {
    ALLOC_NODE(ctx, MemberAccessNode, NODE_MEMBER_ACCESS, line, col);
    node->object = object;
    node->member = hulk_ast_strdup(ctx, member);
    return node;
}

NewExprNode* hulk_ast_new_expr(HulkASTContext *ctx, const char *type_name,
                                int line, int col) {
    ALLOC_NODE(ctx, NewExprNode, NODE_NEW_EXPR, line, col);
    node->type_name = hulk_ast_strdup(ctx, type_name);
    hulk_node_list_init(&node->args);
    return node;
}

AssignNode* hulk_ast_assign(HulkASTContext *ctx, HulkNode *target,
                             HulkNode *value, int line, int col) {
    ALLOC_NODE(ctx, AssignNode, NODE_ASSIGN, line, col);
    node->target = target;
    node->value  = value;
    return node;
}

DestructAssignNode* hulk_ast_destruct_assign(HulkASTContext *ctx, HulkNode *target,
                                              HulkNode *value, int line, int col) {
    ALLOC_NODE(ctx, DestructAssignNode, NODE_DESTRUCT_ASSIGN, line, col);
    node->target = target;
    node->value  = value;
    return node;
}

AsExprNode* hulk_ast_as_expr(HulkASTContext *ctx, HulkNode *expr,
                              const char *type_name, int line, int col) {
    ALLOC_NODE(ctx, AsExprNode, NODE_AS_EXPR, line, col);
    node->expr      = expr;
    node->type_name = hulk_ast_strdup(ctx, type_name);
    return node;
}

IsExprNode* hulk_ast_is_expr(HulkASTContext *ctx, HulkNode *expr,
                              const char *type_name, int line, int col) {
    ALLOC_NODE(ctx, IsExprNode, NODE_IS_EXPR, line, col);
    node->expr      = expr;
    node->type_name = hulk_ast_strdup(ctx, type_name);
    return node;
}

SelfNode* hulk_ast_self(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, SelfNode, NODE_SELF, line, col);
    return node;
}

BaseCallNode* hulk_ast_base_call(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, BaseCallNode, NODE_BASE_CALL, line, col);
    hulk_node_list_init(&node->args);
    return node;
}

DecorBlockNode* hulk_ast_decor_block(HulkASTContext *ctx, int line, int col) {
    ALLOC_NODE(ctx, DecorBlockNode, NODE_DECOR_BLOCK, line, col);
    hulk_node_list_init(&node->decorators);
    node->target = NULL;
    return node;
}

DecorItemNode* hulk_ast_decor_item(HulkASTContext *ctx, const char *name,
                                    int line, int col) {
    ALLOC_NODE(ctx, DecorItemNode, NODE_DECOR_ITEM, line, col);
    node->name = hulk_ast_strdup(ctx, name);
    hulk_node_list_init(&node->args);
    return node;
}

ConcatExprNode* hulk_ast_concat_expr(HulkASTContext *ctx, BinaryOp op,
                                      HulkNode *left, HulkNode *right,
                                      int line, int col) {
    ALLOC_NODE(ctx, ConcatExprNode, NODE_CONCAT_EXPR, line, col);
    node->op    = op;
    node->left  = left;
    node->right = right;
    return node;
}
