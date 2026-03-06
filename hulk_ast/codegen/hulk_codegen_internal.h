/*
 * hulk_codegen_internal.h — Cabecera interna del generador de código
 *
 * Define el CodegenContext con el módulo LLVM, el builder,
 * tabla de símbolos (nombre → LLVMValueRef alloca/global),
 * mapeo de tipos y estado de compilación.
 *
 * Este header es PRIVADO del subsistema codegen/.
 */

#ifndef HULK_CODEGEN_INTERNAL_H
#define HULK_CODEGEN_INTERNAL_H

#include "../core/hulk_ast.h"
#include "hulk_codegen.h"
#include "../../error_handler.h"

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ============================================================
 *  Tabla de símbolos para codegen (nombre → LLVMValueRef)
 * ============================================================ */

typedef struct CGSymbol_s {
    const char    *name;
    LLVMValueRef   value;     /* alloca / global / function */
    LLVMTypeRef    type;      /* tipo LLVM del valor */
    int            is_func;   /* 1 si es función */
} CGSymbol;

typedef struct CGScope_s CGScope;

struct CGScope_s {
    CGScope    *parent;
    CGSymbol  **symbols;
    int         sym_count;
    int         sym_cap;
};

/* ============================================================
 *  Registro de tipos de usuario (TypeDef → struct LLVM)
 * ============================================================ */

typedef struct CGTypeInfo_s CGTypeInfo;

struct CGTypeInfo_s {
    const char   *name;
    LLVMTypeRef   struct_type;    /* tipo struct opaco */
    LLVMTypeRef   ptr_type;       /* puntero al struct */
    int           field_count;
    const char  **field_names;    /* nombres de atributos */
    int           type_tag;       /* tag numérico único para RTTI */
    /* Métodos: nombre → LLVMValueRef función */
    CGSymbol    **methods;
    int           method_count;
    int           method_cap;
    /* Herencia: tipo padre (NULL si no tiene) */
    CGTypeInfo   *parent;
};

/* ============================================================
 *  Contexto de Generación de Código
 * ============================================================ */

typedef struct {
    LLVMContextRef    llvm_ctx;
    LLVMModuleRef     module;
    LLVMBuilderRef    builder;

    /* Tipos LLVM básicos (cache) */
    LLVMTypeRef       t_double;
    LLVMTypeRef       t_bool;       /* i1 */
    LLVMTypeRef       t_i32;
    LLVMTypeRef       t_i8ptr;      /* i8* (string) */
    LLVMTypeRef       t_void;

    /* Scope chain */
    CGScope          *current;
    CGScope          *global;

    /* Registro de scopes para cleanup */
    CGScope         **all_scopes;
    int               scope_count;
    int               scope_cap;

    /* Registro de tipos de usuario */
    CGTypeInfo      **type_infos;
    int               type_info_count;
    int               type_info_cap;

    /* Estado durante emisión */
    LLVMValueRef      current_fn;       /* función actual */
    CGTypeInfo       *enclosing_type;   /* tipo actual (para self) */
    LLVMValueRef      self_ptr;         /* puntero a self en método */
    int               error_count;

    /* Built-in runtime functions */
    LLVMValueRef      fn_printf;
    LLVMValueRef      fn_snprintf;
    LLVMValueRef      fn_strlen;
    LLVMValueRef      fn_strcpy;
    LLVMValueRef      fn_strcat;
    LLVMValueRef      fn_malloc;
    LLVMValueRef      fn_sqrt;
    LLVMValueRef      fn_sin;
    LLVMValueRef      fn_cos;
    LLVMValueRef      fn_exp;
    LLVMValueRef      fn_log;
    LLVMValueRef      fn_pow;
    LLVMValueRef      fn_fmod;
    LLVMValueRef      fn_rand;
    LLVMValueRef      fn_srand;
    LLVMValueRef      fn_time;
    LLVMValueRef      fn_atof;

    /* HULK runtime wrappers */
    LLVMValueRef      fn_hulk_print;
    LLVMValueRef      fn_hulk_concat;
    LLVMValueRef      fn_hulk_concat_ws;
    LLVMValueRef      fn_hulk_num_to_str;
    LLVMValueRef      fn_hulk_bool_to_str;
} CodegenContext;

/* ============================================================
 *  Scope  (hulk_codegen_types.c)
 * ============================================================ */

CGScope*   cg_scope_create(CodegenContext *c, CGScope *parent);
void       cg_push_scope(CodegenContext *c);
void       cg_pop_scope(CodegenContext *c);
CGSymbol*  cg_define(CodegenContext *c, const char *name,
                     LLVMValueRef val, LLVMTypeRef type, int is_func);
CGSymbol*  cg_define_in(CodegenContext *c, CGScope *scope, const char *name,
                        LLVMValueRef val, LLVMTypeRef type, int is_func);
CGSymbol*  cg_lookup(CGScope *scope, const char *name);
CGSymbol*  cg_lookup_local(CGScope *scope, const char *name);

/* ============================================================
 *  Type info  (hulk_codegen_types.c)
 * ============================================================ */

CGTypeInfo* cg_type_info_create(CodegenContext *c, const char *name);
CGTypeInfo* cg_type_info_find(CodegenContext *c, const char *name);
CGTypeInfo* cg_type_info_find_by_tag(CodegenContext *c, int tag);
void        cg_type_add_method(CGTypeInfo *ti, const char *name,
                               LLVMValueRef fn);

/* ============================================================
 *  Tipos LLVM  (hulk_codegen_types.c)
 * ============================================================ */

void cg_types_init(CodegenContext *c);
void cg_context_free(CodegenContext *c);

/* ============================================================
 *  Runtime  (hulk_codegen.c)
 * ============================================================ */

void cg_declare_runtime(CodegenContext *c);
void cg_define_runtime_helpers(CodegenContext *c);

/* ============================================================
 *  Expresiones  (hulk_codegen_expr.c)
 * ============================================================ */

LLVMValueRef cg_emit_expr(CodegenContext *c, HulkNode *node);

/* ============================================================
 *  Declaraciones / top-level  (hulk_codegen_stmt.c)
 * ============================================================ */

void cg_emit_program(CodegenContext *c, HulkNode *program);

/* ============================================================
 *  Utilidades
 * ============================================================ */

void cg_error(CodegenContext *c, HulkNode *node, const char *fmt, ...);

/* Helper: crear alloca al inicio de la función actual */
LLVMValueRef cg_create_entry_alloca(CodegenContext *c,
                                     LLVMTypeRef type,
                                     const char *name);

#endif /* HULK_CODEGEN_INTERNAL_H */
