/*
 * hulk_ast.c — Implementación del AST de HULK
 *
 * Contiene:
 *   - Object Pool (arena) para asignación eficiente de nodos
 *   - Funciones de creación de cada tipo de nodo
 *   - HulkNodeList (lista dinámica de hijos)
 *   - Visitor dispatch y traversal
 *   - Nombres para debugging
 */

#include "hulk_ast.h"
#include "../error_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============== HULK NODE LIST ==============

void hulk_node_list_init(HulkNodeList *list) {
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

void hulk_node_list_push(HulkNodeList *list, HulkNode *node) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = realloc(list->items, sizeof(HulkNode*) * new_cap);
        if (!list->items) {
            LOG_FATAL_MSG("hulk_ast", "sin memoria para HulkNodeList");
            return;
        }
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

void hulk_node_list_free(HulkNodeList *list) {
    if (list->items) {
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

// ============== OBJECT POOL (ARENA) ==============

void hulk_ast_context_init(HulkASTContext *ctx) {
    ctx->blocks         = NULL;
    ctx->block_count    = 0;
    ctx->block_capacity = 0;
}

void hulk_ast_context_free(HulkASTContext *ctx) {
    for (int i = 0; i < ctx->block_count; i++) {
        free(ctx->blocks[i]);
    }
    free(ctx->blocks);
    ctx->blocks         = NULL;
    ctx->block_count    = 0;
    ctx->block_capacity = 0;
}

void* hulk_ast_alloc(HulkASTContext *ctx, size_t size) {
    void *block = calloc(1, size);
    if (!block) {
        LOG_FATAL_MSG("hulk_ast", "sin memoria (%zu bytes)", size);
        return NULL;
    }
    // Registrar en el pool para liberación posterior
    if (ctx->block_count >= ctx->block_capacity) {
        int new_cap = ctx->block_capacity == 0
                      ? HULK_AST_POOL_INIT_CAP
                      : ctx->block_capacity * 2;
        ctx->blocks = realloc(ctx->blocks, sizeof(void*) * new_cap);
        if (!ctx->blocks) {
            LOG_FATAL_MSG("hulk_ast", "sin memoria para pool");
            free(block);
            return NULL;
        }
        ctx->block_capacity = new_cap;
    }
    ctx->blocks[ctx->block_count++] = block;
    return block;
}

char* hulk_ast_strdup(HulkASTContext *ctx, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = hulk_ast_alloc(ctx, len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

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

// ============== VISITOR ==============

void hulk_visitor_init(HulkASTVisitor *v) {
    for (int i = 0; i < NODE_HULK_COUNT; i++)
        v->visit[i] = NULL;
}

void hulk_ast_accept(HulkNode *node, HulkASTVisitor *visitor, void *data) {
    if (!node || !visitor) return;
    if (node->type < 0 || node->type >= NODE_HULK_COUNT) return;
    HulkVisitFn fn = visitor->visit[node->type];
    if (fn) fn(node, visitor, data);
}

void hulk_ast_accept_list(HulkNodeList *list, HulkASTVisitor *visitor, void *data) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        hulk_ast_accept(list->items[i], visitor, data);
    }
}

// ============== NOMBRES PARA DEBUGGING ==============

static const char* node_type_names[] = {
    "Program", "FunctionDef", "TypeDef", "MethodDef", "AttributeDef",
    "LetExpr", "VarBinding", "IfExpr", "ElifBranch", "WhileStmt",
    "ForStmt", "BlockStmt", "BinaryOp", "UnaryOp", "NumberLit",
    "StringLit", "BoolLit", "Ident", "CallExpr", "MemberAccess",
    "NewExpr", "Assign", "DestructAssign", "AsExpr", "IsExpr",
    "Self", "BaseCall", "DecorBlock", "DecorItem", "ConcatExpr"
};

const char* hulk_node_type_name(HulkNodeType type) {
    if (type >= 0 && type < NODE_HULK_COUNT)
        return node_type_names[type];
    return "Unknown";
}

static const char* binary_op_names[] = {
    "+", "-", "*", "/", "%", "**",
    "<", ">", "<=", ">=", "==", "!=",
    "&&", "||", "@", "@@"
};

const char* hulk_binary_op_name(BinaryOp op) {
    if (op >= 0 && op <= OP_CONCAT_WS)
        return binary_op_names[op];
    return "?op?";
}
