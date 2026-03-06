/*
 * hulk_semantic_scope.c — Tabla de símbolos y gestión de scopes
 *
 * SRP: Solo gestión de scopes, definición y búsqueda de símbolos,
 *      y reporte de errores semánticos.
 */

#include "hulk_semantic_internal.h"

/* ============================================================
 *  Creación de scopes
 * ============================================================ */

Scope* sem_scope_create(SemanticContext *ctx, Scope *parent) {
    Scope *s = calloc(1, sizeof(Scope));
    if (!s) return NULL;
    s->parent = parent;

    /* Registrar para cleanup al final del análisis */
    if (ctx->scope_count >= ctx->scope_cap) {
        int nc = ctx->scope_cap == 0 ? 16 : ctx->scope_cap * 2;
        Scope **tmp = realloc(ctx->all_scopes, sizeof(Scope*) * nc);
        if (!tmp) { free(s); return NULL; }
        ctx->all_scopes = tmp;
        ctx->scope_cap = nc;
    }
    ctx->all_scopes[ctx->scope_count++] = s;
    return s;
}

/* ============================================================
 *  Definición de símbolos
 * ============================================================ */

/* Define un símbolo en un scope específico */
Symbol* sem_define_in(SemanticContext *ctx, Scope *scope, const char *name,
                      SymbolKind kind, HulkType *type, HulkNode *decl) {
    (void)ctx; /* reservado para futuras extensiones */
    if (!scope || !name) return NULL;

    /* Verificar redefinición en el mismo scope */
    if (sem_lookup_local(scope, name)) return NULL;

    Symbol *sym = calloc(1, sizeof(Symbol));
    if (!sym) return NULL;
    sym->name      = name;
    sym->kind      = kind;
    sym->type      = type;
    sym->decl_node = decl;

    if (scope->sym_count >= scope->sym_cap) {
        int nc = scope->sym_cap == 0 ? 8 : scope->sym_cap * 2;
        Symbol **tmp = realloc(scope->symbols, sizeof(Symbol*) * nc);
        if (!tmp) { free(sym); return NULL; }
        scope->symbols = tmp;
        scope->sym_cap = nc;
    }
    scope->symbols[scope->sym_count++] = sym;
    return sym;
}

/* Define un símbolo en el scope actual */
Symbol* sem_define(SemanticContext *ctx, const char *name,
                   SymbolKind kind, HulkType *type, HulkNode *decl) {
    return sem_define_in(ctx, ctx->current, name, kind, type, decl);
}

/* ============================================================
 *  Búsqueda de símbolos
 * ============================================================ */

Symbol* sem_lookup_local(Scope *scope, const char *name) {
    if (!scope || !name) return NULL;
    for (int i = 0; i < scope->sym_count; i++) {
        if (scope->symbols[i]->name &&
            strcmp(scope->symbols[i]->name, name) == 0)
            return scope->symbols[i];
    }
    return NULL;
}

Symbol* sem_lookup(Scope *scope, const char *name) {
    for (Scope *s = scope; s; s = s->parent) {
        Symbol *sym = sem_lookup_local(s, name);
        if (sym) return sym;
    }
    return NULL;
}

/* Lookup un miembro caminando la cadena de herencia del tipo */
Symbol* sem_lookup_member(HulkType *type, const char *name) {
    if (!name) return NULL;
    for (HulkType *t = type; t; t = t->parent) {
        if (t->members) {
            Symbol *sym = sem_lookup_local(t->members, name);
            if (sym) return sym;
        }
    }
    return NULL;
}

/* ============================================================
 *  Push / Pop de scopes
 * ============================================================ */

void sem_push_scope(SemanticContext *ctx) {
    ctx->current = sem_scope_create(ctx, ctx->current);
}

void sem_pop_scope(SemanticContext *ctx) {
    if (ctx->current && ctx->current->parent)
        ctx->current = ctx->current->parent;
}

/* ============================================================
 *  Reporte de errores semánticos
 * ============================================================ */

void sem_error(SemanticContext *ctx, HulkNode *node, const char *fmt, ...) {
    ctx->error_count++;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    int line = node ? node->line : 0;
    int col  = node ? node->col  : 0;
    LOG_ERROR_MSG("semantic", "[%d:%d] %s", line, col, buf);
}
