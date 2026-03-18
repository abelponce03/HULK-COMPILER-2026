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
    s->func_node = parent ? parent->func_node : NULL;

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
 *  Captura de Variables (Closures)
 * ============================================================ */

static int is_captured(HulkNodeList *list, const char *name) {
    for (int i = 0; i < list->count; i++) {
        VarBindingNode *vb = (VarBindingNode*)list->items[i];
        if (strcmp(vb->name, name) == 0) return 1;
    }
    return 0;
}

void sem_capture_variable(SemanticContext *ctx, Symbol *sym) {
    if (!sym || sym->kind != SYM_VARIABLE) return;

    /* Find the scope where sym was defined */
    Scope *decl_scope = NULL;
    for (Scope *s = ctx->current; s; s = s->parent) {
        if (sem_lookup_local(s, sym->name) == sym) {
            decl_scope = s;
            break;
        }
    }
    if (!decl_scope || !decl_scope->func_node) return;

    /* Iterate from current scope up to decl_scope, adding the capture
       to all functions in between. */
    HulkNode *last_func = NULL;
    for (Scope *s = ctx->current; s && s != decl_scope; s = s->parent) {
        HulkNode *f = s->func_node;
        if (f && f != last_func && f != decl_scope->func_node) {
            HulkNodeList *caps = NULL;
            if (f->type == NODE_FUNCTION_DEF)
                caps = &((FunctionDefNode*)f)->captured_vars;
            else if (f->type == NODE_FUNCTION_EXPR)
                caps = &((FunctionExprNode*)f)->captured_vars;

            if (caps && !is_captured(caps, sym->name)) {
                // Add capture binding (fake VarBindingNode just to keep name and annotation)
                int line = f->line, col = f->col;
                VarBindingNode *cap = hulk_ast_var_binding(ctx->ast_ctx, sym->name, NULL, line, col);
                hulk_node_list_push(caps, (HulkNode*)cap);
            }
            last_func = f;
        }
    }
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
