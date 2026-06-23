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

/* Forward — definido más abajo */
typedef struct CGTypeInfo_s CGTypeInfo;

typedef struct CGSymbol_s {
    const char    *name;
    LLVMValueRef   value;     /* alloca / global / function */
    LLVMTypeRef    type;      /* tipo LLVM del valor */
    int            is_func;   /* 1 si es función */
    CGTypeInfo    *hulk_type; /* tipo HULK del valor (NULL si primitivo) */
    LLVMValueRef   callable_cell; /* i8* global mutable para funciones HULK */
    LLVMValueRef   adapter_fn;    /* closure adapter: (env, args...) -> ret */
    LLVMTypeRef    adapter_type;
    int            adapter_emitted;
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

struct CGTypeInfo_s {
    const char   *name;
    LLVMTypeRef   struct_type;    /* tipo struct opaco */
    LLVMTypeRef   ptr_type;       /* puntero al struct */
    /* Layout: { i32 __tag__, padre_fields..., self_fields... } */
    int           field_count;        /* total: 1 (tag) + heredados + propios */
    int           field_offset_self;  /* índice donde empiezan los propios */
    const char  **field_names;        /* tamaño = field_count */
    LLVMTypeRef  *field_types_arr;    /* tamaño = field_count */
    int           type_tag;       /* tag numérico único para RTTI */
    /* Métodos definidos por este tipo (no incluye heredados):
     * nombre → LLVMValueRef función con prototipo T_method(T* self, args) */
    CGSymbol    **methods;
    int           method_count;
    int           method_cap;
    /* Herencia: tipo padre (NULL si no tiene) */
    CGTypeInfo   *parent;
    /* Vtable global: arreglo de punteros a método, indexado por slot
     * global (compartido entre tipos). Si NULL todavía no se emitió. */
    LLVMValueRef  vtable_global;
    LLVMTypeRef   vtable_type;
    /* Funciones especiales del tipo */
    LLVMValueRef  fn_new;          /* T_new(params...) -> T*       */
    LLVMValueRef  fn_init;         /* T_init(T* self, params...) -> void */
    LLVMTypeRef   fn_init_type;
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

    /* Slots globales de método: cada nombre único en todo el programa
     * tiene un slot fijo. La vtable de cada tipo es un array
     * [method_slot_count x ptr] indexado por slot. */
    const char      **method_slot_names;
    int               method_slot_count;
    int               method_slot_cap;

    /* Globales emitidos para RTTI dinámico:
     *  - vtables_table: array [num_tipos x ptr] indexado por type_tag,
     *    apunta a la vtable del tipo
     *  - parent_table: array [num_tipos x i32] indexado por type_tag,
     *    contiene el type_tag del padre o -1 para tipos sin padre */
    LLVMValueRef      vtables_table;
    LLVMValueRef      parent_table;

    /* Estado durante emisión */
    LLVMValueRef      current_fn;          /* función actual */
    CGTypeInfo       *enclosing_type;      /* tipo actual (para self) */
    LLVMValueRef      self_ptr;            /* puntero a self en método */
    const char       *current_method_name; /* para resolver base() */
    HulkNode         *current_program;     /* para heurísticas post-hoc */
    int               error_count;

    /* Tabla precomputada (una sola pasada O(n) sobre el programa) de los
     * pares (tipo, índice de argumento) donde algún `new T(...)` pasa un
     * StringLit. Permite a cg_infer_ctor_param_type consultar en O(1)
     * en vez de re-escanear el programa por cada parámetro. */
    struct CGStrArgHint { const char *type_name; int arg_idx; } *str_hints;
    int               str_hints_count;
    int               str_hints_cap;
    int               str_hints_built;     /* 0 hasta la primera consulta */

    /* Built-in runtime functions */
    LLVMValueRef      fn_printf;
    LLVMValueRef      fn_snprintf;
    LLVMValueRef      fn_strlen;
    LLVMValueRef      fn_strcmp;
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
    LLVMValueRef      fn_hulk_print_str;
    LLVMValueRef      fn_hulk_print_bool;
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
/* Devuelve la implementación del método 'name' caminando la cadena
 * de herencia (propio primero, luego ancestros). NULL si ninguno define. */
LLVMValueRef cg_type_resolve_method(CGTypeInfo *ti, const char *name);
/* Encuentra el offset (índice de field) de un atributo 'name'
 * en el layout del tipo (caminando padre si hace falta). Retorna -1
 * si no existe. */
int          cg_type_field_index(CGTypeInfo *ti, const char *name);

/* Slot global de método: registra el nombre si es nuevo y devuelve su
 * índice. Idempotente. */
int          cg_method_slot(CodegenContext *c, const char *name);

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
 *  Dispatcher central; los emisores concretos viven repartidos por
 *  archivo temático (call/oop/control) y se declaran abajo.
 * ============================================================ */

LLVMValueRef cg_emit_expr(CodegenContext *c, HulkNode *node);

/* Llamadas y conversión a string  (hulk_codegen_call.c) */
LLVMValueRef cg_emit_call(CodegenContext *c, CallExprNode *n);
LLVMValueRef cg_emit_to_string(CodegenContext *c, HulkNode *node);
LLVMValueRef cg_emit_make_closure(CodegenContext *c, LLVMValueRef fn,
                                  LLVMValueRef *captures, int capture_count);
LLVMValueRef cg_emit_call_closure_raw(CodegenContext *c,
                                      LLVMValueRef closure,
                                      LLVMValueRef *user_args,
                                      LLVMTypeRef *user_arg_types,
                                      int user_argc,
                                      LLVMTypeRef ret_t,
                                      const char *name);

/* OOP: acceso a miembros, new, self, asignación, tipo estático
 * (hulk_codegen_oop.c) */
LLVMValueRef cg_emit_member_access(CodegenContext *c, MemberAccessNode *n);
LLVMValueRef cg_emit_new(CodegenContext *c, NewExprNode *n);
LLVMValueRef cg_emit_self(CodegenContext *c, SelfNode *n);
LLVMValueRef cg_emit_assign(CodegenContext *c, AssignNode *n);
LLVMValueRef cg_emit_destruct(CodegenContext *c, DestructAssignNode *n);
CGTypeInfo*  cg_static_type_of(CodegenContext *c, HulkNode *expr);

/* Flujo de control como expresión  (hulk_codegen_control.c) */
LLVMValueRef cg_emit_let(CodegenContext *c, LetExprNode *n);
LLVMValueRef cg_emit_if(CodegenContext *c, IfExprNode *n);
LLVMValueRef cg_emit_while(CodegenContext *c, WhileStmtNode *n);
LLVMValueRef cg_emit_for(CodegenContext *c, ForStmtNode *n);
LLVMValueRef cg_emit_block(CodegenContext *c, BlockStmtNode *n);

/* ============================================================
 *  Declaraciones / top-level  (hulk_codegen_stmt.c)
 * ============================================================ */

void cg_emit_program(CodegenContext *c, HulkNode *program);

/* Tipos de usuario: layout, constructor, métodos, vtables/RTTI
 * (hulk_codegen_typedecl.c) */
void cg_forward_declare_type(CodegenContext *c, TypeDefNode *n);
void cg_emit_type_def(CodegenContext *c, TypeDefNode *n);
void cg_emit_rtti_globals(CodegenContext *c);

/* Inferencia de tipos LLVM desde el AST  (hulk_codegen_infer.c) */
LLVMTypeRef cg_llvm_type_for_name(CodegenContext *c, const char *name);
LLVMTypeRef cg_infer_return_type(CodegenContext *c, const char *ann);
LLVMTypeRef cg_infer_body_return_type(CodegenContext *c, HulkNode *body);
LLVMTypeRef cg_infer_param_type(CodegenContext *c, const char *ann);
LLVMTypeRef cg_infer_ctor_param_type(CodegenContext *c, TypeDefNode *td,
                                     const char *param_name);

/* ============================================================
 *  Utilidades
 * ============================================================ */

void cg_error(CodegenContext *c, HulkNode *node, const char *fmt, ...);

/* Helper: crear alloca al inicio de la función actual */
LLVMValueRef cg_create_entry_alloca(CodegenContext *c,
                                     LLVMTypeRef type,
                                     const char *name);

#endif /* HULK_CODEGEN_INTERNAL_H */
