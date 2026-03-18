/*
 * hulk_ast.h — AST (Abstract Syntax Tree) del lenguaje HULK
 *
 * Define todos los tipos de nodo del AST, cada uno mapeado a una
 * producción significativa de la gramática LL(1).
 *
 * Principios SOLID:
 *   SRP — Cada nodo almacena solo su información sintáctica.
 *   OCP — Nuevas operaciones se agregan vía Visitor, sin tocar nodos.
 *   LSP — Todo nodo es casteable a HulkNode* (cabecera común).
 *   ISP — Los visitors tienen callbacks opcionales (pueden ser NULL).
 *   DIP — El parser depende de la interfaz de callbacks, no del AST concreto.
 *
 * Uso del Object Pool:
 *   Todos los nodos se asignan desde un HulkASTContext (arena).
 *   Se liberan todos juntos con hulk_ast_context_free().
 */

#ifndef HULK_AST_H
#define HULK_AST_H

#include "../../generador_analizadores_lexicos/token_types.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============== TIPOS DE NODO ==============

typedef enum {
    NODE_PROGRAM,
    NODE_FUNCTION_DEF,
    NODE_TYPE_DEF,
    NODE_METHOD_DEF,
    NODE_ATTRIBUTE_DEF,
    NODE_LET_EXPR,
    NODE_VAR_BINDING,
    NODE_IF_EXPR,
    NODE_ELIF_BRANCH,
    NODE_WHILE_STMT,
    NODE_FOR_STMT,
    NODE_BLOCK_STMT,
    NODE_BINARY_OP,
    NODE_UNARY_OP,
    NODE_NUMBER_LIT,
    NODE_STRING_LIT,
    NODE_BOOL_LIT,
    NODE_IDENT,
    NODE_CALL_EXPR,
    NODE_MEMBER_ACCESS,
    NODE_NEW_EXPR,
    NODE_ASSIGN,
    NODE_DESTRUCT_ASSIGN,
    NODE_AS_EXPR,
    NODE_IS_EXPR,
    NODE_SELF,
    NODE_BASE_CALL,
    NODE_DECOR_BLOCK,
    NODE_DECOR_ITEM,
    NODE_CONCAT_EXPR,
    NODE_FUNCTION_EXPR,
    NODE_HULK_COUNT   // centinela: cantidad total de tipos
} HulkNodeType;

// ============== OPERADORES BINARIOS ==============

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_LT, OP_GT, OP_LE, OP_GE, OP_EQ, OP_NEQ,
    OP_AND, OP_OR,
    OP_CONCAT, OP_CONCAT_WS
} BinaryOp;

// ============== LISTA DINÁMICA DE NODOS ==============
// Usada para listas de hijos (parámetros, argumentos, statements, etc.)

struct HulkNode_s;  // forward

typedef struct {
    struct HulkNode_s **items;
    int count;
    int capacity;
} HulkNodeList;

void hulk_node_list_init(HulkNodeList *list);
void hulk_node_list_push(HulkNodeList *list, struct HulkNode_s *node);
void hulk_node_list_free(HulkNodeList *list);

// ============== NODO BASE ==============
// Todo nodo comienza con este encabezado.  Permite tratar cualquier
// nodo como HulkNode* (polimorfismo en C).

typedef struct HulkNode_s {
    HulkNodeType type;
    int line;   // posición en el fuente (1-based)
    int col;
} HulkNode;

// ============== NODOS CONCRETOS ==============
// Cada struct embebe HulkNode como primer campo para LSP.

// Program → lista de top-level declarations
typedef struct {
    HulkNode base;
    HulkNodeList declarations;  // FunctionDef | TypeDef | DecorBlock | Stmt
} ProgramNode;

// function name(params): ReturnType => body | { body }
typedef struct {
    HulkNode base;
    char *name;
    HulkNodeList params;       // VarBindingNode (name + type)
    char *return_type;         // NULL si no tiene anotación
    HulkNode *body;            // Expr o BlockStmt
    HulkNodeList captured_vars; // Variables capturadas del ámbito léxico exterior
} FunctionDefNode;

// function(params): ReturnType => body | { body }
typedef struct {
    HulkNode base;
    HulkNodeList params;       // VarBindingNode (name + type)
    char *return_type;         // NULL si no tiene anotación
    HulkNode *body;            // Expr o BlockStmt
    HulkNodeList captured_vars; // Variables capturadas del ámbito léxico exterior
} FunctionExprNode;

// type Name(params) inherits Parent(args) { body }
typedef struct {
    HulkNode base;
    char *name;
    HulkNodeList params;       // ArgId (name + type)
    char *parent;              // NULL si no hereda
    HulkNodeList parent_args;  // args del padre (puede estar vacía)
    HulkNodeList members;      // MethodDef | AttributeDef
} TypeDefNode;

// Método dentro de type: name(params): Type => body
typedef struct {
    HulkNode base;
    char *name;
    HulkNodeList params;
    char *return_type;
    HulkNode *body;
} MethodDefNode;

// Atributo dentro de type: name: Type = expr;
typedef struct {
    HulkNode base;
    char *name;
    char *type_annotation;     // NULL si no tiene
    HulkNode *init_expr;
} AttributeDefNode;

// let bindings in body
typedef struct {
    HulkNode base;
    HulkNodeList bindings;     // VarBindingNode
    HulkNode *body;
} LetExprNode;

// name: Type = expr  (usado en let y en parámetros)
typedef struct {
    HulkNode base;
    char *name;
    char *type_annotation;     // NULL si no tiene
    HulkNode *init_expr;       // NULL para parámetros sin default
} VarBindingNode;

// if (cond) body elif ... else body
typedef struct {
    HulkNode base;
    HulkNode *condition;
    HulkNode *then_body;
    HulkNodeList elifs;        // ElifBranchNode
    HulkNode *else_body;
} IfExprNode;

// elif (cond) body
typedef struct {
    HulkNode base;
    HulkNode *condition;
    HulkNode *body;
} ElifBranchNode;

// while cond body
typedef struct {
    HulkNode base;
    HulkNode *condition;
    HulkNode *body;
} WhileStmtNode;

// for (name in iterable) body
typedef struct {
    HulkNode base;
    char *var_name;
    HulkNode *iterable;
    HulkNode *body;
} ForStmtNode;

// { stmt; stmt; ... }
typedef struct {
    HulkNode base;
    HulkNodeList statements;
} BlockStmtNode;

// left OP right
typedef struct {
    HulkNode base;
    BinaryOp op;
    HulkNode *left;
    HulkNode *right;
} BinaryOpNode;

// -expr  (unary minus)
typedef struct {
    HulkNode base;
    HulkNode *operand;
} UnaryOpNode;

// 42, 3.14
typedef struct {
    HulkNode base;
    double value;
    char *raw;    // lexema original ("42", "3.14")
} NumberLitNode;

// "hello"
typedef struct {
    HulkNode base;
    char *value;  // sin comillas
} StringLitNode;

// true | false
typedef struct {
    HulkNode base;
    int value;    // 1 = true, 0 = false
} BoolLitNode;

// identificador (variable, referencia a tipo, etc.)
typedef struct {
    HulkNode base;
    char *name;
} IdentNode;

// callee(args)
typedef struct {
    HulkNode base;
    HulkNode *callee;          // Ident o MemberAccess
    HulkNodeList args;
} CallExprNode;

// object.member
typedef struct {
    HulkNode base;
    HulkNode *object;
    char *member;
} MemberAccessNode;

// new TypeName(args)
typedef struct {
    HulkNode base;
    char *type_name;
    HulkNodeList args;
} NewExprNode;

// target = value
typedef struct {
    HulkNode base;
    HulkNode *target;
    HulkNode *value;
} AssignNode;

// target := value
typedef struct {
    HulkNode base;
    HulkNode *target;
    HulkNode *value;
} DestructAssignNode;

// expr as TypeName
typedef struct {
    HulkNode base;
    HulkNode *expr;
    char *type_name;
} AsExprNode;

// expr is TypeName  (comparación de tipos)
typedef struct {
    HulkNode base;
    HulkNode *expr;
    char *type_name;
} IsExprNode;

// self
typedef struct {
    HulkNode base;
} SelfNode;

// base(args)
typedef struct {
    HulkNode base;
    HulkNodeList args;
} BaseCallNode;

// decor log, memoize(100) function/type ...
typedef struct {
    HulkNode base;
    HulkNodeList decorators;   // DecorItemNode
    HulkNode *target;          // FunctionDefNode o TypeDefNode
} DecorBlockNode;

// Un decorador individual: name(args) o solo name
typedef struct {
    HulkNode base;
    char *name;
    HulkNodeList args;         // vacía si no tiene paréntesis
} DecorItemNode;

// left @ right  o  left @@ right (concat explícito)
typedef struct {
    HulkNode base;
    BinaryOp op;               // OP_CONCAT o OP_CONCAT_WS
    HulkNode *left;
    HulkNode *right;
} ConcatExprNode;

// ============== OBJECT POOL / ARENA ==============
// Todos los nodos se asignan desde esta arena.
// Se liberan todos juntos con hulk_ast_context_free().

#define HULK_AST_POOL_INIT_CAP 256

typedef struct {
    void **blocks;       // punteros a bloques asignados
    int block_count;
    int block_capacity;
} HulkASTContext;

void  hulk_ast_context_init(HulkASTContext *ctx);
void  hulk_ast_context_free(HulkASTContext *ctx);
void* hulk_ast_alloc(HulkASTContext *ctx, size_t size);
char* hulk_ast_strdup(HulkASTContext *ctx, const char *s);

// ============== FUNCIONES DE CREACIÓN DE NODOS ==============
// Cada función asigna desde el pool y retorna el nodo inicializado.
// El caller agrega hijos usando hulk_node_list_push().

ProgramNode*        hulk_ast_program(HulkASTContext *ctx, int line, int col);
FunctionDefNode*    hulk_ast_function_def(HulkASTContext *ctx, const char *name, const char *ret_type, int line, int col);
FunctionExprNode*   hulk_ast_function_expr(HulkASTContext *ctx, const char *ret_type, int line, int col);
TypeDefNode*        hulk_ast_type_def(HulkASTContext *ctx, const char *name, const char *parent, int line, int col);
MethodDefNode*      hulk_ast_method_def(HulkASTContext *ctx, const char *name, const char *ret_type, int line, int col);
AttributeDefNode*   hulk_ast_attribute_def(HulkASTContext *ctx, const char *name, const char *type_ann, int line, int col);
LetExprNode*        hulk_ast_let_expr(HulkASTContext *ctx, int line, int col);
VarBindingNode*     hulk_ast_var_binding(HulkASTContext *ctx, const char *name, const char *type_ann, int line, int col);
IfExprNode*         hulk_ast_if_expr(HulkASTContext *ctx, int line, int col);
ElifBranchNode*     hulk_ast_elif_branch(HulkASTContext *ctx, int line, int col);
WhileStmtNode*      hulk_ast_while_stmt(HulkASTContext *ctx, int line, int col);
ForStmtNode*        hulk_ast_for_stmt(HulkASTContext *ctx, const char *var_name, int line, int col);
BlockStmtNode*      hulk_ast_block_stmt(HulkASTContext *ctx, int line, int col);
BinaryOpNode*       hulk_ast_binary_op(HulkASTContext *ctx, BinaryOp op, HulkNode *left, HulkNode *right, int line, int col);
UnaryOpNode*        hulk_ast_unary_op(HulkASTContext *ctx, HulkNode *operand, int line, int col);
NumberLitNode*      hulk_ast_number_lit(HulkASTContext *ctx, const char *raw, int line, int col);
StringLitNode*      hulk_ast_string_lit(HulkASTContext *ctx, const char *value, int line, int col);
BoolLitNode*        hulk_ast_bool_lit(HulkASTContext *ctx, int value, int line, int col);
IdentNode*          hulk_ast_ident(HulkASTContext *ctx, const char *name, int line, int col);
CallExprNode*       hulk_ast_call_expr(HulkASTContext *ctx, HulkNode *callee, int line, int col);
MemberAccessNode*   hulk_ast_member_access(HulkASTContext *ctx, HulkNode *object, const char *member, int line, int col);
NewExprNode*        hulk_ast_new_expr(HulkASTContext *ctx, const char *type_name, int line, int col);
AssignNode*         hulk_ast_assign(HulkASTContext *ctx, HulkNode *target, HulkNode *value, int line, int col);
DestructAssignNode* hulk_ast_destruct_assign(HulkASTContext *ctx, HulkNode *target, HulkNode *value, int line, int col);
AsExprNode*         hulk_ast_as_expr(HulkASTContext *ctx, HulkNode *expr, const char *type_name, int line, int col);
IsExprNode*         hulk_ast_is_expr(HulkASTContext *ctx, HulkNode *expr, const char *type_name, int line, int col);
SelfNode*           hulk_ast_self(HulkASTContext *ctx, int line, int col);
BaseCallNode*       hulk_ast_base_call(HulkASTContext *ctx, int line, int col);
DecorBlockNode*     hulk_ast_decor_block(HulkASTContext *ctx, int line, int col);
DecorItemNode*      hulk_ast_decor_item(HulkASTContext *ctx, const char *name, int line, int col);
ConcatExprNode*     hulk_ast_concat_expr(HulkASTContext *ctx, BinaryOp op, HulkNode *left, HulkNode *right, int line, int col);

// ============== PATRÓN VISITOR ==============
// Permite agregar operaciones sobre el AST sin modificar los nodos.
// Cada callback recibe el nodo (ya casteado al tipo concreto) y un
// puntero opaco de datos del usuario.
//
// Si un callback es NULL, el visitor lo ignora para ese tipo de nodo.

typedef struct HulkASTVisitor_s HulkASTVisitor;

typedef void (*HulkVisitFn)(HulkNode *node, HulkASTVisitor *visitor, void *data);

struct HulkASTVisitor_s {
    HulkVisitFn visit[NODE_HULK_COUNT];  // un callback por tipo de nodo
};

// Inicializa todos los callbacks a NULL
void hulk_visitor_init(HulkASTVisitor *v);

// Despacha el nodo al callback correspondiente del visitor
void hulk_ast_accept(HulkNode *node, HulkASTVisitor *visitor, void *data);

// Recorre un HulkNodeList aceptando cada elemento
void hulk_ast_accept_list(HulkNodeList *list, HulkASTVisitor *visitor, void *data);

// ============== NOMBRES PARA DEBUGGING ==============

const char* hulk_node_type_name(HulkNodeType type);
const char* hulk_binary_op_name(BinaryOp op);

#endif /* HULK_AST_H */
