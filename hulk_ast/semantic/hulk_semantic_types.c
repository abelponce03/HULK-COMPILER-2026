/*
 * hulk_semantic_types.c — Sistema de tipos de HULK
 *
 * Registro de tipos (built-in y user), funciones built-in,
 * conformidad de tipos (is-a) y join (ancestro común).
 *
 * SRP: Solo gestión del sistema de tipos y su jerarquía.
 */

#include "hulk_semantic_internal.h"

/* ============================================================
 *  Creación de tipos
 * ============================================================ */

HulkType* sem_type_new(SemanticContext *ctx, HulkTypeKind kind,
                        const char *name, HulkType *parent) {
    HulkType *t = calloc(1, sizeof(HulkType));
    if (!t) return NULL;
    t->kind   = kind;
    t->name   = name;
    t->parent = parent;

    if (ctx->type_count >= ctx->type_cap) {
        int nc = ctx->type_cap == 0 ? 16 : ctx->type_cap * 2;
        HulkType **tmp = realloc(ctx->types, sizeof(HulkType*) * nc);
        if (!tmp) { free(t); return NULL; }
        ctx->types = tmp;
        ctx->type_cap = nc;
    }
    ctx->types[ctx->type_count++] = t;
    return t;
}

/* ============================================================
 *  Registro de funciones built-in
 * ============================================================ */

static void reg_builtin(SemanticContext *ctx, const char *name,
                        HulkType *ret, int np, ...) {
    Scope *prev = ctx->current;
    ctx->current = ctx->global;

    Symbol *sym = sem_define(ctx, name, SYM_FUNCTION, ret, NULL);
    if (sym && np > 0) {
        sym->param_count = np;
        sym->param_types = calloc(np, sizeof(HulkType*));
        sym->param_names = calloc(np, sizeof(const char*));

        va_list ap;
        va_start(ap, np);
        for (int i = 0; i < np; i++) {
            sym->param_names[i] = va_arg(ap, const char*);
            sym->param_types[i] = va_arg(ap, HulkType*);
        }
        va_end(ap);
    }
    ctx->current = prev;
}

/* ============================================================
 *  Inicialización: tipos + funciones built-in
 * ============================================================ */

void sem_types_init(SemanticContext *ctx) {
    /* Tipos built-in */
    ctx->t_object  = sem_type_new(ctx, HULK_TYPE_OBJECT,  "Object",  NULL);
    ctx->t_number  = sem_type_new(ctx, HULK_TYPE_NUMBER,  "Number",  ctx->t_object);
    ctx->t_string  = sem_type_new(ctx, HULK_TYPE_STRING,  "String",  ctx->t_object);
    ctx->t_boolean = sem_type_new(ctx, HULK_TYPE_BOOLEAN, "Boolean", ctx->t_object);
    ctx->t_void    = sem_type_new(ctx, HULK_TYPE_VOID,    "Void",    NULL);
    ctx->t_error   = sem_type_new(ctx, HULK_TYPE_ERROR,   "<error>", NULL);

    /* Registrar tipos en el scope global */
    Scope *prev = ctx->current;
    ctx->current = ctx->global;
    sem_define(ctx, "Object",  SYM_TYPE, ctx->t_object,  NULL);
    sem_define(ctx, "Number",  SYM_TYPE, ctx->t_number,  NULL);
    sem_define(ctx, "String",  SYM_TYPE, ctx->t_string,  NULL);
    sem_define(ctx, "Boolean", SYM_TYPE, ctx->t_boolean, NULL);
    ctx->current = prev;

    /* Funciones built-in de HULK */
    reg_builtin(ctx, "print", ctx->t_object, 1,
                "x", ctx->t_object);
    reg_builtin(ctx, "sqrt", ctx->t_number, 1,
                "x", ctx->t_number);
    reg_builtin(ctx, "sin", ctx->t_number, 1,
                "x", ctx->t_number);
    reg_builtin(ctx, "cos", ctx->t_number, 1,
                "x", ctx->t_number);
    reg_builtin(ctx, "exp", ctx->t_number, 1,
                "x", ctx->t_number);
    reg_builtin(ctx, "log", ctx->t_number, 2,
                "x", ctx->t_number, "base", ctx->t_number);
    reg_builtin(ctx, "rand", ctx->t_number, 0);
    reg_builtin(ctx, "parse", ctx->t_number, 1,
                "s", ctx->t_string);
}

/* ============================================================
 *  Conformidad de tipos (child ≤ ancestor)
 * ============================================================ */

int sem_type_conforms(HulkType *child, HulkType *ancestor) {
    if (!child || !ancestor) return 0;
    if (child == ancestor) return 1;
    /* Error type conforma con todo (error recovery) */
    if (child->kind == HULK_TYPE_ERROR ||
        ancestor->kind == HULK_TYPE_ERROR) return 1;
    /* Todo conforma con Object */
    if (ancestor->kind == HULK_TYPE_OBJECT) return 1;
    /* Recorrer cadena de herencia */
    for (HulkType *t = child->parent; t; t = t->parent) {
        if (t == ancestor) return 1;
    }
    return 0;
}

/* ============================================================
 *  Join: ancestro común más específico
 * ============================================================ */

HulkType* sem_type_join(SemanticContext *ctx, HulkType *a, HulkType *b) {
    if (!a || a->kind == HULK_TYPE_ERROR) return b ? b : ctx->t_object;
    if (!b || b->kind == HULK_TYPE_ERROR) return a;
    if (a == b) return a;
    if (sem_type_conforms(a, b)) return b;
    if (sem_type_conforms(b, a)) return a;
    /* Subir por la jerarquía de a buscando un ancestro que cubra b */
    for (HulkType *t = a->parent; t; t = t->parent) {
        if (sem_type_conforms(b, t)) return t;
    }
    return ctx->t_object;
}

/* ============================================================
 *  Resolución de tipo por nombre
 * ============================================================ */

HulkType* sem_type_resolve(SemanticContext *ctx, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < ctx->type_count; i++) {
        if (ctx->types[i]->name && strcmp(ctx->types[i]->name, name) == 0)
            return ctx->types[i];
    }
    return NULL;
}

/* ============================================================
 *  Cleanup
 * ============================================================ */

void sem_context_free(SemanticContext *ctx) {
    /* Liberar scopes y sus símbolos */
    for (int i = 0; i < ctx->scope_count; i++) {
        Scope *s = ctx->all_scopes[i];
        for (int j = 0; j < s->sym_count; j++) {
            free(s->symbols[j]->param_types);
            free((void*)s->symbols[j]->param_names);
            free(s->symbols[j]);
        }
        free(s->symbols);
        free(s);
    }
    free(ctx->all_scopes);
    /* Liberar tipos */
    for (int i = 0; i < ctx->type_count; i++)
        free(ctx->types[i]);
    free(ctx->types);
}
