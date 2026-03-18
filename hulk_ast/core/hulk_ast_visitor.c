/*
 * hulk_ast_visitor.c — Patrón Visitor y nombres de debug del AST
 *
 * Implementa el mecanismo de dispatch del Visitor, que permite agregar
 * nuevas operaciones sobre el AST (printing, análisis semántico, etc.)
 * sin modificar las structs de los nodos.
 *
 * También contiene las tablas de nombres para debugging.
 *
 * SRP: Solo dispatch del visitor y utilidades de inspección.
 * OCP: Nuevas operaciones se extienden registrando callbacks.
 */

#include "hulk_ast.h"
#include <stdlib.h>

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
    "Self", "BaseCall", "DecorBlock", "DecorItem", "ConcatExpr",
    "FunctionExpr"
};

_Static_assert(sizeof(node_type_names)/sizeof(node_type_names[0]) == NODE_HULK_COUNT,
               "node_type_names out of sync with HulkNodeType enum");

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

_Static_assert(sizeof(binary_op_names)/sizeof(binary_op_names[0]) == OP_CONCAT_WS + 1,
               "binary_op_names out of sync with BinaryOp enum");

const char* hulk_binary_op_name(BinaryOp op) {
    if (op >= 0 && op <= OP_CONCAT_WS)
        return binary_op_names[op];
    return "?op?";
}
