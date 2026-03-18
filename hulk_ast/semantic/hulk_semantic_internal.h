/*
 * hulk_semantic_internal.h — Cabecera interna del análisis semántico
 *
 * Define el sistema de tipos, tabla de símbolos, scopes y el contexto
 * semántico compartido por todos los módulos del analizador.
 *
 * Este header es PRIVADO del subsistema semantic/;
 * el código externo solo debe incluir hulk_semantic.h.
 */

#ifndef HULK_SEMANTIC_INTERNAL_H
#define HULK_SEMANTIC_INTERNAL_H

#include "../core/hulk_ast.h"
#include "hulk_semantic.h"
#include "../../error_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================
 *  Sistema de Tipos
 * ============================================================ */

typedef enum {
    HULK_TYPE_OBJECT,    // raíz de la jerarquía
    HULK_TYPE_NUMBER,
    HULK_TYPE_STRING,
    HULK_TYPE_BOOLEAN,
    HULK_TYPE_VOID,      // sentencias sin valor
    HULK_TYPE_ERROR,     // centinela para error recovery
    HULK_TYPE_USER,      // tipo definido por el usuario
} HulkTypeKind;

typedef struct HulkType_s HulkType;
typedef struct Scope_s    Scope;

struct HulkType_s {
    HulkTypeKind kind;
    const char  *name;
    HulkType    *parent;    // herencia (Object es raíz)
    Scope       *members;   // para tipos: scope con atributos + métodos
};

/* ============================================================
 *  Símbolos
 * ============================================================ */

typedef enum {
    SYM_VARIABLE,
    SYM_FUNCTION,
    SYM_TYPE,
    SYM_METHOD,
    SYM_ATTRIBUTE,
} SymbolKind;

typedef struct {
    const char  *name;
    SymbolKind   kind;
    HulkType    *type;          // tipo de var/attr, retorno de func/method
    HulkNode    *decl_node;     // nodo AST donde se declaró (NULL built-ins)
    HulkType   **param_types;   // para func/method
    const char **param_names;   // para func/method
    int          param_count;
} Symbol;

/* ============================================================
 *  Scope (tabla de símbolos con anidamiento)
 * ============================================================ */

struct Scope_s {
    Scope   *parent;
    Symbol **symbols;
    int      sym_count;
    int      sym_cap;
    HulkNode *func_node;
};

/* ============================================================
 *  Contexto Semántico
 * ============================================================ */

typedef struct {
    HulkASTContext *ast_ctx;     // arena para nodos nuevos (desugaring)
    Scope          *current;    // scope actual
    Scope          *global;     // scope global

    /* Tipos built-in (cache) */
    HulkType *t_number;
    HulkType *t_string;
    HulkType *t_boolean;
    HulkType *t_object;
    HulkType *t_void;
    HulkType *t_error;

    /* Registro de tipos (todos, incluyendo built-in) */
    HulkType **types;
    int        type_count;
    int        type_cap;

    /* Registro de scopes (para cleanup) */
    Scope    **all_scopes;
    int        scope_count;
    int        scope_cap;

    /* Estado durante verificación */
    HulkType  *enclosing_type;  // tipo actual (para self)
    int        error_count;
} SemanticContext;

/* ============================================================
 *  Scope  (hulk_semantic_scope.c)
 * ============================================================ */

Scope*  sem_scope_create(SemanticContext *ctx, Scope *parent);
Symbol* sem_define(SemanticContext *ctx, const char *name,
                   SymbolKind kind, HulkType *type, HulkNode *decl);
Symbol* sem_define_in(SemanticContext *ctx, Scope *scope, const char *name,
                      SymbolKind kind, HulkType *type, HulkNode *decl);
Symbol* sem_lookup(Scope *scope, const char *name);
Symbol* sem_lookup_local(Scope *scope, const char *name);
Symbol* sem_lookup_member(HulkType *type, const char *name);
void    sem_push_scope(SemanticContext *ctx);
void    sem_pop_scope(SemanticContext *ctx);
void    sem_capture_variable(SemanticContext *ctx, Symbol *sym);

/* ============================================================
 *  Error  (hulk_semantic_scope.c)
 * ============================================================ */

void sem_error(SemanticContext *ctx, HulkNode *node, const char *fmt, ...);

/* ============================================================
 *  Tipos  (hulk_semantic_types.c)
 * ============================================================ */

void      sem_types_init(SemanticContext *ctx);
void      sem_context_free(SemanticContext *ctx);
HulkType* sem_type_new(SemanticContext *ctx, HulkTypeKind kind,
                        const char *name, HulkType *parent);
int       sem_type_conforms(HulkType *child, HulkType *ancestor);
HulkType* sem_type_join(SemanticContext *ctx, HulkType *a, HulkType *b);
HulkType* sem_type_resolve(SemanticContext *ctx, const char *name);

/* Helper: resuelve type_annotation o retorna t_object (con error si no existe) */
static inline HulkType* sem_resolve_annotation(SemanticContext *c,
                                                 const char *annotation,
                                                 HulkNode *err_node) {
    if (!annotation) return c->t_object;
    HulkType *t = sem_type_resolve(c, annotation);
    if (!t) {
        sem_error(c, err_node, "tipo '%s' no definido", annotation);
        return c->t_error;
    }
    return t;
}

/* ============================================================
 *  Verificación  (hulk_semantic_check.c / check_expr.c)
 * ============================================================ */

void      sem_check_program(SemanticContext *ctx, HulkNode *program);
HulkType* sem_check_expr(SemanticContext *ctx, HulkNode *node);

/* ============================================================
 *  Desugaring  (hulk_semantic_desugar.c)
 * ============================================================ */

void sem_desugar(SemanticContext *ctx, HulkNode *program);

#endif /* HULK_SEMANTIC_INTERNAL_H */
