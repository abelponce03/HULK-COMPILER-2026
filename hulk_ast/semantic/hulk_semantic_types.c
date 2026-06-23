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
    if (sym) {
        sym->callable_type = sem_function_type_new(ctx, sym->param_types,
                                                   sym->param_count, ret);
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
    sem_type_new(ctx, HULK_TYPE_FUNCTION, "<function>", ctx->t_object);
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
    reg_builtin(ctx, "range", ctx->t_number, 2,
                "min", ctx->t_number, "max", ctx->t_number);
    reg_builtin(ctx, "__array_new", ctx->t_object, 1,
                "size", ctx->t_number);
    reg_builtin(ctx, "__array_init", ctx->t_object, 2,
                "size", ctx->t_number, "initializer", ctx->t_object);
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
    if (child->kind == HULK_TYPE_FUNCTION &&
        ancestor->kind == HULK_TYPE_FUNCTION)
        return sem_function_type_equals(child, ancestor);
    if (ancestor->name) {
        size_t alen = strlen(ancestor->name);
        if (alen >= 2 && strcmp(ancestor->name + alen - 2, "[]") == 0 &&
            child->kind == HULK_TYPE_OBJECT)
            return 1;
        if (alen >= 1 && ancestor->name[alen - 1] == '*') {
            if (child->kind == HULK_TYPE_OBJECT) return 1;
            if (child->members) {
                int has_next = 0, has_current = 0;
                for (int i = 0; i < child->members->sym_count; i++) {
                    Symbol *s = child->members->symbols[i];
                    if (!s || s->kind != SYM_METHOD || !s->name) continue;
                    if (strcmp(s->name, "next") == 0) has_next = 1;
                    if (strcmp(s->name, "current") == 0) has_current = 1;
                }
                if (has_next && has_current) return 1;
            }
        }
    }
    /* Todo conforma con Object */
    if (ancestor->kind == HULK_TYPE_OBJECT) return 1;
    /* Recorrer cadena de herencia */
    for (HulkType *t = child->parent; t; t = t->parent) {
        if (t == ancestor) return 1;
    }
    /* Conformance estructural: si ancestor es protocolo, child conforma
     * si tiene todos los métodos del protocolo (por nombre, sin chequear
     * variance estricta en este momento). */
    if (ancestor->is_protocol && ancestor->members &&
        child->members) {
        for (int i = 0; i < ancestor->members->sym_count; i++) {
            Symbol *psym = ancestor->members->symbols[i];
            if (!psym || psym->kind != SYM_METHOD) continue;
            int found = 0;
            for (int j = 0; j < child->members->sym_count; j++) {
                Symbol *csym = child->members->symbols[j];
                if (csym && csym->kind == SYM_METHOD && csym->name &&
                    psym->name && strcmp(csym->name, psym->name) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) return 0;
        }
        return 1;
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

static char* sem_slice(const char *s, int start, int end) {
    if (!s || end < start) return NULL;
    size_t len = (size_t)(end - start);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s + start, len);
    out[len] = '\0';
    return out;
}

static int find_matching_paren(const char *s, int start, int end) {
    int depth = 0;
    for (int i = start; i < end; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') {
            depth--;
            if (depth == 0) return i;
            if (depth < 0) return -1;
        }
    }
    return -1;
}

static HulkType* resolve_annotation_range(SemanticContext *c,
                                          const char *annotation,
                                          int start,
                                          int end,
                                          HulkNode *err_node);

static int count_function_params(const char *s, int start, int end) {
    if (start >= end) return 0;
    int depth = 0, count = 1;
    for (int i = start; i < end; i++) {
        if (s[i] == '(') depth++;
        else if (s[i] == ')') depth--;
        else if (s[i] == ',' && depth == 0) count++;
    }
    return count;
}

static void fill_function_params(SemanticContext *c, const char *s,
                                 int start, int end, HulkNode *err_node,
                                 HulkType **params) {
    int depth = 0, seg_start = start, out = 0;
    for (int i = start; i <= end; i++) {
        int at_end = (i == end);
        if (!at_end) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') depth--;
        }
        if (at_end || (s[i] == ',' && depth == 0)) {
            params[out++] = resolve_annotation_range(c, s, seg_start, i, err_node);
            seg_start = i + 1;
        }
    }
}

static HulkType* resolve_function_annotation(SemanticContext *c,
                                             const char *annotation,
                                             int start,
                                             int end,
                                             HulkNode *err_node) {
    int close = find_matching_paren(annotation, start, end);
    if (close < 0 || close + 2 >= end ||
        annotation[close + 1] != '-' || annotation[close + 2] != '>') {
        char *bad = sem_slice(annotation, start, end);
        sem_error(c, err_node, "tipo funcional inválido '%s'", bad ? bad : "?");
        free(bad);
        return c->t_error;
    }

    int param_count = count_function_params(annotation, start + 1, close);
    HulkType **params = NULL;
    if (param_count > 0) {
        params = calloc(param_count, sizeof(HulkType*));
        if (!params) return c->t_error;
        fill_function_params(c, annotation, start + 1, close, err_node, params);
    }

    HulkType *ret = resolve_annotation_range(c, annotation, close + 3, end, err_node);
    HulkType *fn_t = sem_function_type_new(c, params, param_count, ret);
    free(params);
    return fn_t ? fn_t : c->t_error;
}

static HulkType* resolve_annotation_range(SemanticContext *c,
                                          const char *annotation,
                                          int start,
                                          int end,
                                          HulkNode *err_node) {
    if (start >= end) {
        sem_error(c, err_node, "anotación de tipo vacía");
        return c->t_error;
    }

    if (annotation[start] == '(')
        return resolve_function_annotation(c, annotation, start, end, err_node);

    char *name = sem_slice(annotation, start, end);
    HulkType *t = name ? sem_type_resolve(c, name) : NULL;
    if (!t && name) {
        size_t len = strlen(name);
        int is_array = len >= 2 && strcmp(name + len - 2, "[]") == 0;
        int is_iter = len >= 1 && name[len - 1] == '*';
        if (is_array || is_iter) {
            char *stable = strdup(name);
            if (stable)
                t = sem_type_new(c, HULK_TYPE_USER, stable, c->t_object);
        }
    }
    if (!t) {
        sem_error(c, err_node, "tipo '%s' no definido", name ? name : "?");
        free(name);
        return c->t_error;
    }
    free(name);
    return t;
}

HulkType* sem_resolve_annotation(SemanticContext *c,
                                 const char *annotation,
                                 HulkNode *err_node) {
    if (!annotation) return c->t_object;
    return resolve_annotation_range(c, annotation, 0, (int)strlen(annotation),
                                    err_node);
}

HulkType* sem_function_type_new(SemanticContext *ctx, HulkType **params,
                                int param_count, HulkType *ret) {
    HulkType *t = sem_type_new(ctx, HULK_TYPE_FUNCTION, "<function>", ctx->t_object);
    if (!t) return NULL;
    t->param_count = param_count;
    t->return_type = ret ? ret : ctx->t_object;
    if (param_count > 0) {
        t->param_types = calloc(param_count, sizeof(HulkType*));
        if (!t->param_types) return t;
        for (int i = 0; i < param_count; i++)
            t->param_types[i] = params ? params[i] : ctx->t_object;
    }
    return t;
}

int sem_function_type_equals(HulkType *a, HulkType *b) {
    if (!a || !b) return 0;
    if (a->kind != HULK_TYPE_FUNCTION || b->kind != HULK_TYPE_FUNCTION)
        return 0;
    if (a->param_count != b->param_count) return 0;
    for (int i = 0; i < a->param_count; i++) {
        if (a->param_types[i] != b->param_types[i]) return 0;
    }
    return a->return_type == b->return_type;
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
    for (int i = 0; i < ctx->type_count; i++) {
        free(ctx->types[i]->param_types);
        free(ctx->types[i]);
    }
    free(ctx->types);
}
