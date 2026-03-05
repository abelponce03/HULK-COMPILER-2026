/*
 * hulk_codegen_types.c — Scopes, mapeo de tipos y utilidades para codegen
 *
 * SRP: Gestión de scopes de codegen, tipos LLVM básicos, registro de
 *      tipos de usuario (struct layouts), y cleanup.
 */

#include "hulk_codegen_internal.h"

/* ============================================================
 *  Scope management
 * ============================================================ */

CGScope* cg_scope_create(CodegenContext *c, CGScope *parent) {
    CGScope *s = calloc(1, sizeof(CGScope));
    if (!s) return NULL;
    s->parent = parent;

    if (c->scope_count >= c->scope_cap) {
        int nc = c->scope_cap == 0 ? 16 : c->scope_cap * 2;
        c->all_scopes = realloc(c->all_scopes, sizeof(CGScope*) * nc);
        c->scope_cap = nc;
    }
    c->all_scopes[c->scope_count++] = s;
    return s;
}

void cg_push_scope(CodegenContext *c) {
    c->current = cg_scope_create(c, c->current);
}

void cg_pop_scope(CodegenContext *c) {
    if (c->current && c->current->parent)
        c->current = c->current->parent;
}

CGSymbol* cg_define_in(CodegenContext *c, CGScope *scope, const char *name,
                       LLVMValueRef val, LLVMTypeRef type, int is_func) {
    (void)c;
    if (!scope || !name) return NULL;
    /* Permitir redefinición en mismo scope (shadowing) para codegen */
    CGSymbol *existing = cg_lookup_local(scope, name);
    if (existing) {
        existing->value   = val;
        existing->type    = type;
        existing->is_func = is_func;
        return existing;
    }

    CGSymbol *sym = calloc(1, sizeof(CGSymbol));
    if (!sym) return NULL;
    sym->name    = name;
    sym->value   = val;
    sym->type    = type;
    sym->is_func = is_func;

    if (scope->sym_count >= scope->sym_cap) {
        int nc = scope->sym_cap == 0 ? 8 : scope->sym_cap * 2;
        scope->symbols = realloc(scope->symbols, sizeof(CGSymbol*) * nc);
        scope->sym_cap = nc;
    }
    scope->symbols[scope->sym_count++] = sym;
    return sym;
}

CGSymbol* cg_define(CodegenContext *c, const char *name,
                    LLVMValueRef val, LLVMTypeRef type, int is_func) {
    return cg_define_in(c, c->current, name, val, type, is_func);
}

CGSymbol* cg_lookup_local(CGScope *scope, const char *name) {
    if (!scope || !name) return NULL;
    for (int i = 0; i < scope->sym_count; i++) {
        if (scope->symbols[i]->name &&
            strcmp(scope->symbols[i]->name, name) == 0)
            return scope->symbols[i];
    }
    return NULL;
}

CGSymbol* cg_lookup(CGScope *scope, const char *name) {
    for (CGScope *s = scope; s; s = s->parent) {
        CGSymbol *sym = cg_lookup_local(s, name);
        if (sym) return sym;
    }
    return NULL;
}

/* ============================================================
 *  Type info registry
 * ============================================================ */

CGTypeInfo* cg_type_info_create(CodegenContext *c, const char *name) {
    CGTypeInfo *ti = calloc(1, sizeof(CGTypeInfo));
    if (!ti) return NULL;
    ti->name = name;

    if (c->type_info_count >= c->type_info_cap) {
        int nc = c->type_info_cap == 0 ? 8 : c->type_info_cap * 2;
        c->type_infos = realloc(c->type_infos, sizeof(CGTypeInfo*) * nc);
        c->type_info_cap = nc;
    }
    c->type_infos[c->type_info_count++] = ti;
    return ti;
}

CGTypeInfo* cg_type_info_find(CodegenContext *c, const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < c->type_info_count; i++) {
        if (c->type_infos[i]->name &&
            strcmp(c->type_infos[i]->name, name) == 0)
            return c->type_infos[i];
    }
    return NULL;
}

void cg_type_add_method(CGTypeInfo *ti, const char *name, LLVMValueRef fn) {
    if (!ti) return;
    /* Check if exists — update */
    for (int i = 0; i < ti->method_count; i++) {
        if (ti->methods[i]->name && strcmp(ti->methods[i]->name, name) == 0) {
            ti->methods[i]->value = fn;
            return;
        }
    }
    CGSymbol *sym = calloc(1, sizeof(CGSymbol));
    sym->name    = name;
    sym->value   = fn;
    sym->is_func = 1;

    if (ti->method_count >= ti->method_cap) {
        int nc = ti->method_cap == 0 ? 4 : ti->method_cap * 2;
        ti->methods = realloc(ti->methods, sizeof(CGSymbol*) * nc);
        ti->method_cap = nc;
    }
    ti->methods[ti->method_count++] = sym;
}

/* ============================================================
 *  Inicializar tipos LLVM básicos
 * ============================================================ */

void cg_types_init(CodegenContext *c) {
    c->t_double = LLVMDoubleTypeInContext(c->llvm_ctx);
    c->t_bool   = LLVMInt1TypeInContext(c->llvm_ctx);
    c->t_i32    = LLVMInt32TypeInContext(c->llvm_ctx);
    c->t_i8ptr  = LLVMPointerType(LLVMInt8TypeInContext(c->llvm_ctx), 0);
    c->t_void   = LLVMVoidTypeInContext(c->llvm_ctx);
}

/* ============================================================
 *  Error reporting
 * ============================================================ */

void cg_error(CodegenContext *c, HulkNode *node, const char *fmt, ...) {
    c->error_count++;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    int line = node ? node->line : 0;
    int col  = node ? node->col  : 0;
    LOG_ERROR_MSG("codegen", "[%d:%d] %s", line, col, buf);
}

/* ============================================================
 *  Alloca helper — inserta alloca en entry block
 * ============================================================ */

LLVMValueRef cg_create_entry_alloca(CodegenContext *c,
                                     LLVMTypeRef type,
                                     const char *name) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(c->current_fn);
    LLVMBuilderRef tmp = LLVMCreateBuilderInContext(c->llvm_ctx);
    LLVMValueRef first_instr = LLVMGetFirstInstruction(entry);
    if (first_instr)
        LLVMPositionBuilderBefore(tmp, first_instr);
    else
        LLVMPositionBuilderAtEnd(tmp, entry);

    LLVMValueRef alloca = LLVMBuildAlloca(tmp, type, name);
    LLVMDisposeBuilder(tmp);
    return alloca;
}

/* ============================================================
 *  Cleanup
 * ============================================================ */

void cg_context_free(CodegenContext *c) {
    /* Scopes y sus símbolos */
    for (int i = 0; i < c->scope_count; i++) {
        CGScope *s = c->all_scopes[i];
        for (int j = 0; j < s->sym_count; j++)
            free(s->symbols[j]);
        free(s->symbols);
        free(s);
    }
    free(c->all_scopes);

    /* Type infos */
    for (int i = 0; i < c->type_info_count; i++) {
        CGTypeInfo *ti = c->type_infos[i];
        for (int j = 0; j < ti->method_count; j++)
            free(ti->methods[j]);
        free(ti->methods);
        free(ti->field_names);
        free(ti);
    }
    free(c->type_infos);

    /* LLVM resources */
    if (c->builder) LLVMDisposeBuilder(c->builder);
    if (c->module)  LLVMDisposeModule(c->module);
    if (c->llvm_ctx) LLVMContextDispose(c->llvm_ctx);
}
