/*
 * hulk_codegen_stmt.c — Emisión de IR para declaraciones y top-level
 *
 * Maneja FunctionDef, TypeDef, DecorBlock y orquesta la emisión
 * del ProgramNode completo en dos pasadas:
 *   Pass 1 — Forward-declare funciones y tipos (struct + constructor)
 *   Pass 2 — Emitir cuerpos de funciones, métodos y top-level en main()
 *
 * SRP: Solo emisión de declaraciones y programa top-level.
 */

#include "hulk_codegen_internal.h"

/* ===== Forward declarations ===== */

static void emit_function_def(CodegenContext *c, FunctionDefNode *n);
static void emit_type_def(CodegenContext *c, TypeDefNode *n);
static void emit_decor_block(CodegenContext *c, DecorBlockNode *n);
static void forward_declare_function(CodegenContext *c, FunctionDefNode *n);
static void forward_declare_type(CodegenContext *c, TypeDefNode *n);

/* ============================================================
 *  cg_emit_program — Punto de entrada para todo el programa
 *
 *  Pasada 1: forward-declare funciones + tipos
 *  Pasada 2: emit bodies + top-level stmts en main()
 * ============================================================ */

void cg_emit_program(CodegenContext *c, HulkNode *program) {
    if (!program || program->type != NODE_PROGRAM) {
        cg_error(c, program, "se esperaba un nodo Program");
        return;
    }
    ProgramNode *prog = (ProgramNode*)program;

    /* ---- Pasada 1: Forward declarations ---- */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        switch (decl->type) {
            case NODE_FUNCTION_DEF:
                forward_declare_function(c, (FunctionDefNode*)decl);
                break;
            case NODE_TYPE_DEF:
                forward_declare_type(c, (TypeDefNode*)decl);
                break;
            case NODE_DECOR_BLOCK: {
                DecorBlockNode *db = (DecorBlockNode*)decl;
                if (db->target && db->target->type == NODE_FUNCTION_DEF)
                    forward_declare_function(c, (FunctionDefNode*)db->target);
                else if (db->target && db->target->type == NODE_TYPE_DEF)
                    forward_declare_type(c, (TypeDefNode*)db->target);
                break;
            }
            default: break;
        }
    }

    /* ---- Pasada 2: Emitir cuerpos de funciones y tipos ---- */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        switch (decl->type) {
            case NODE_FUNCTION_DEF:
                emit_function_def(c, (FunctionDefNode*)decl);
                break;
            case NODE_TYPE_DEF:
                emit_type_def(c, (TypeDefNode*)decl);
                break;
            case NODE_DECOR_BLOCK:
                emit_decor_block(c, (DecorBlockNode*)decl);
                break;
            default: break;  /* top-level stmts handled below */
        }
    }

    /* ---- Generar main() con top-level expressions ---- */

    /* Tipo de main: i32 main(void) */
    LLVMTypeRef main_ret = c->t_i32;
    LLVMTypeRef main_ft = LLVMFunctionType(main_ret, NULL, 0, 0);
    LLVMValueRef main_fn = LLVMAddFunction(c->module, "main", main_ft);
    c->current_fn = main_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(c->builder, entry);

    cg_push_scope(c);

    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        /* Solo emitir expresiones / statements top-level (no func/type/decor) */
        if (decl->type != NODE_FUNCTION_DEF &&
            decl->type != NODE_TYPE_DEF &&
            decl->type != NODE_DECOR_BLOCK) {
            LLVMValueRef val = cg_emit_expr(c, decl);
            LLVMTypeRef vt = LLVMTypeOf(val);

            /* Si es una llamada void, no intentar imprimir el resultado */
            if (vt == c->t_void) continue;

            /* Si es una llamada a print, ya se emitió. Si es una expresión
             * suelta, imprimirla como última expresión del programa. */
            if (i == prog->declarations.count - 1) {
                /* Imprimir resultado de la última expresión top-level */
                LLVMTypeRef vt = LLVMTypeOf(val);
                if (vt == c->t_double) {
                    LLVMTypeRef params[1] = { c->t_double };
                    LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
                    LLVMBuildCall2(c->builder, ft, c->fn_hulk_print,
                                   &val, 1, "");
                } else if (vt == c->t_i8ptr) {
                    /* Imprimir string directamente con printf("%s\n", val) */
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(
                        c->builder, "%s\n", "sfmt");
                    LLVMValueRef args[2] = { fmt, val };
                    LLVMTypeRef printf_params[1] = { c->t_i8ptr };
                    LLVMTypeRef printf_ft = LLVMFunctionType(
                        c->t_i32, printf_params, 1, 1);
                    LLVMBuildCall2(c->builder, printf_ft, c->fn_printf,
                                   args, 2, "");
                } else if (vt == c->t_bool) {
                    LLVMTypeRef params[1] = { c->t_bool };
                    LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
                    LLVMValueRef str = LLVMBuildCall2(
                        c->builder, ft, c->fn_hulk_bool_to_str,
                        &val, 1, "bstr");
                    LLVMValueRef fmts = LLVMBuildGlobalStringPtr(
                        c->builder, "%s\n", "sfmt2");
                    LLVMValueRef pargs[2] = { fmts, str };
                    LLVMTypeRef printf_params[1] = { c->t_i8ptr };
                    LLVMTypeRef printf_ft = LLVMFunctionType(
                        c->t_i32, printf_params, 1, 1);
                    LLVMBuildCall2(c->builder, printf_ft, c->fn_printf,
                                   pargs, 2, "");
                }
            }
        }
    }

    cg_pop_scope(c);

    /* return 0 */
    LLVMBuildRet(c->builder, LLVMConstInt(c->t_i32, 0, 0));
}

/* ============================================================
 *  Forward-declare una función
 * ============================================================ */

static LLVMTypeRef infer_return_type(CodegenContext *c, const char *ann) {
    if (!ann) return c->t_double;  /* default */
    if (strcmp(ann, "Number") == 0) return c->t_double;
    if (strcmp(ann, "String") == 0) return c->t_i8ptr;
    if (strcmp(ann, "Boolean") == 0) return c->t_bool;
    if (strcmp(ann, "Void") == 0) return c->t_void;
    /* Para tipos de usuario, retornar pointer genérico */
    CGTypeInfo *ti = cg_type_info_find(c, ann);
    if (ti) return ti->ptr_type;
    return c->t_double;
}

static LLVMTypeRef infer_param_type(CodegenContext *c, const char *ann) {
    if (!ann) return c->t_double;
    if (strcmp(ann, "Number") == 0) return c->t_double;
    if (strcmp(ann, "String") == 0) return c->t_i8ptr;
    if (strcmp(ann, "Boolean") == 0) return c->t_bool;
    CGTypeInfo *ti = cg_type_info_find(c, ann);
    if (ti) return ti->ptr_type;
    return c->t_double;
}

static void forward_declare_function(CodegenContext *c, FunctionDefNode *n) {
    int argc = n->params.count;
    LLVMTypeRef *param_types = calloc(argc > 0 ? argc : 1, sizeof(LLVMTypeRef));

    for (int i = 0; i < argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        param_types[i] = infer_param_type(c, p->type_annotation);
    }

    LLVMTypeRef ret_t = infer_return_type(c, n->return_type);
    LLVMTypeRef fn_type = LLVMFunctionType(ret_t, param_types, argc, 0);
    LLVMValueRef fn = LLVMAddFunction(c->module, n->name, fn_type);

    /* Registrar en scope global */
    cg_define_in(c, c->global, n->name, fn, fn_type, 1);

    free(param_types);
}

/* ============================================================
 *  Emitir cuerpo de función
 * ============================================================ */

static void emit_function_def(CodegenContext *c, FunctionDefNode *n) {
    CGSymbol *sym = cg_lookup(c->global, n->name);
    if (!sym) return;

    LLVMValueRef fn = sym->value;
    LLVMValueRef saved_fn = c->current_fn;
    c->current_fn = fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(c->builder, entry);

    cg_push_scope(c);

    /* Crear allocas para parámetros */
    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        LLVMValueRef param_val = LLVMGetParam(fn, i);
        LLVMTypeRef  param_t   = LLVMTypeOf(param_val);
        LLVMValueRef alloca = cg_create_entry_alloca(c, param_t, p->name);
        LLVMBuildStore(c->builder, param_val, alloca);
        cg_define(c, p->name, alloca, param_t, 0);
    }

    /* Emitir cuerpo */
    LLVMValueRef body_val = cg_emit_expr(c, n->body);

    /* Agregar ret si el bloque actual no tiene terminador */
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
    if (!LLVMGetBasicBlockTerminator(cur_bb)) {
        LLVMTypeRef ret_t = LLVMGetReturnType(LLVMGlobalGetValueType(fn));
        if (ret_t == c->t_void) {
            LLVMBuildRetVoid(c->builder);
        } else {
            LLVMBuildRet(c->builder, body_val);
        }
    }

    cg_pop_scope(c);
    c->current_fn = saved_fn;
}

/* ============================================================
 *  Forward-declare un tipo  (struct + constructor)
 * ============================================================ */

static void forward_declare_type(CodegenContext *c, TypeDefNode *n) {
    /* Crear struct opaco */
    LLVMTypeRef st = LLVMStructCreateNamed(c->llvm_ctx, n->name);
    CGTypeInfo *ti = cg_type_info_create(c, n->name);
    ti->struct_type = st;
    ti->ptr_type = LLVMPointerType(st, 0);

    /* Recoger campos (atributos + parámetros del constructor) */
    int attr_count = 0;
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF)
            attr_count++;
    }

    /* Los campos son: params del constructor + atributos */
    int total_fields = n->params.count + attr_count;
    LLVMTypeRef *field_types = calloc(total_fields > 0 ? total_fields : 1,
                                       sizeof(LLVMTypeRef));
    ti->field_names = calloc(total_fields > 0 ? total_fields : 1,
                              sizeof(const char*));
    ti->field_count = total_fields;

    int idx = 0;
    /* Params del constructor → campos */
    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        field_types[idx] = infer_param_type(c, p->type_annotation);
        ti->field_names[idx] = p->name;
        idx++;
    }
    /* Atributos → campos */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *a = (AttributeDefNode*)n->members.items[i];
            field_types[idx] = infer_param_type(c, a->type_annotation);
            ti->field_names[idx] = a->name;
            idx++;
        }
    }

    LLVMStructSetBody(st, field_types, total_fields, 0);
    free(field_types);

    /* Forward-declare constructor: TypeName(params...) -> TypeName* */
    int ctor_argc = n->params.count;
    LLVMTypeRef *ctor_params = calloc(ctor_argc > 0 ? ctor_argc : 1,
                                       sizeof(LLVMTypeRef));
    for (int i = 0; i < ctor_argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        ctor_params[i] = infer_param_type(c, p->type_annotation);
    }
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->name);
    LLVMTypeRef ctor_ft = LLVMFunctionType(ti->ptr_type,
                                            ctor_params, ctor_argc, 0);
    LLVMValueRef ctor_fn = LLVMAddFunction(c->module, ctor_name, ctor_ft);

    /* Registrar constructor en scope global con el nombre del tipo */
    cg_define_in(c, c->global, n->name, ctor_fn, ctor_ft, 1);
    cg_define_in(c, c->global, ctor_name, ctor_fn, ctor_ft, 1);

    free(ctor_params);

    /* Forward-declare métodos */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_METHOD_DEF) {
            MethodDefNode *m = (MethodDefNode*)n->members.items[i];

            /* Método: self (ptr) + params */
            int m_argc = m->params.count + 1;  /* +1 para self */
            LLVMTypeRef *m_params = calloc(m_argc, sizeof(LLVMTypeRef));
            m_params[0] = ti->ptr_type;  /* self */
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                m_params[j + 1] = infer_param_type(c, p->type_annotation);
            }

            LLVMTypeRef m_ret = infer_return_type(c, m->return_type);
            char mname[256];
            snprintf(mname, sizeof(mname), "%s_%s", n->name, m->name);
            LLVMTypeRef m_ft = LLVMFunctionType(m_ret, m_params, m_argc, 0);
            LLVMValueRef m_fn = LLVMAddFunction(c->module, mname, m_ft);

            cg_define_in(c, c->global, mname, m_fn, m_ft, 1);
            cg_type_add_method(ti, m->name, m_fn);

            free(m_params);
        }
    }
}

/* ============================================================
 *  Emitir tipo: constructor + métodos
 * ============================================================ */

static void emit_type_def(CodegenContext *c, TypeDefNode *n) {
    CGTypeInfo *ti = cg_type_info_find(c, n->name);
    if (!ti) return;

    CGTypeInfo *saved_type = c->enclosing_type;
    c->enclosing_type = ti;

    /* ---- Emitir constructor: TypeName_new ---- */
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->name);
    CGSymbol *ctor_sym = cg_lookup(c->global, ctor_name);
    if (ctor_sym) {
        LLVMValueRef ctor_fn = ctor_sym->value;
        LLVMValueRef saved_fn = c->current_fn;
        c->current_fn = ctor_fn;

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, ctor_fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, entry);

        cg_push_scope(c);

        /* malloc struct */
        LLVMValueRef size = LLVMSizeOf(ti->struct_type);
        LLVMTypeRef malloc_params[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
        LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &size, 1, "raw");
        LLVMValueRef self = LLVMBuildBitCast(c->builder, raw,
                                              ti->ptr_type, "self");
        c->self_ptr = self;

        /* Almacenar parámetros del constructor en campos del struct */
        for (int i = 0; i < n->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)n->params.items[i];
            LLVMValueRef param_val = LLVMGetParam(ctor_fn, i);
            LLVMValueRef gep = LLVMBuildStructGEP2(
                c->builder, ti->struct_type, self, i, "field.ptr");
            LLVMBuildStore(c->builder, param_val, gep);

            /* También crear alloca para que el cuerpo lo use como variable */
            LLVMTypeRef pt = LLVMTypeOf(param_val);
            LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
            LLVMBuildStore(c->builder, param_val, alloca);
            cg_define(c, p->name, alloca, pt, 0);
        }

        /* Inicializar atributos */
        int field_idx = n->params.count;
        for (int i = 0; i < n->members.count; i++) {
            if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF) {
                AttributeDefNode *a = (AttributeDefNode*)n->members.items[i];
                LLVMValueRef val = a->init_expr
                    ? cg_emit_expr(c, a->init_expr)
                    : LLVMConstReal(c->t_double, 0.0);
                LLVMValueRef gep = LLVMBuildStructGEP2(
                    c->builder, ti->struct_type, self, field_idx, "attr.ptr");
                LLVMBuildStore(c->builder, val, gep);
                field_idx++;
            }
        }

        /* Return self */
        LLVMBuildRet(c->builder, self);

        cg_pop_scope(c);
        c->current_fn = saved_fn;
        c->self_ptr = NULL;
    }

    /* ---- Emitir métodos ---- */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_METHOD_DEF) {
            MethodDefNode *m = (MethodDefNode*)n->members.items[i];

            char mname[256];
            snprintf(mname, sizeof(mname), "%s_%s", n->name, m->name);
            CGSymbol *msym = cg_lookup(c->global, mname);
            if (!msym) continue;

            LLVMValueRef m_fn = msym->value;
            LLVMValueRef saved_fn = c->current_fn;
            c->current_fn = m_fn;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, m_fn, "entry");
            LLVMPositionBuilderAtEnd(c->builder, entry);

            cg_push_scope(c);

            /* self es el primer parámetro */
            c->self_ptr = LLVMGetParam(m_fn, 0);

            /* Registrar self en scope */
            cg_define(c, "self", c->self_ptr, ti->ptr_type, 0);

            /* Registrar atributos como accesibles via self.field GEP */
            /* Los parámetros del método */
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                LLVMValueRef param_val = LLVMGetParam(m_fn, j + 1);
                LLVMTypeRef  pt = LLVMTypeOf(param_val);
                LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
                LLVMBuildStore(c->builder, param_val, alloca);
                cg_define(c, p->name, alloca, pt, 0);
            }

            LLVMValueRef body_val = cg_emit_expr(c, m->body);

            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
            if (!LLVMGetBasicBlockTerminator(cur_bb)) {
                LLVMTypeRef ret_t = LLVMGetReturnType(
                    LLVMGlobalGetValueType(m_fn));
                if (ret_t == c->t_void)
                    LLVMBuildRetVoid(c->builder);
                else
                    LLVMBuildRet(c->builder, body_val);
            }

            cg_pop_scope(c);
            c->current_fn = saved_fn;
            c->self_ptr = NULL;
        }
    }

    c->enclosing_type = saved_type;
}

/* ============================================================
 *  Decoradores: composición de funciones en IR
 *
 *  decor d1, d2 function f(x) => body;
 *  →  f = d2(d1(original_f))
 *
 *  Cada decorador es una función que toma un function pointer
 *  y retorna un nuevo function pointer.
 *
 *  Implementación simplificada: los decoradores se aplican como
 *  llamadas anidadas sobre el valor de la función original.
 * ============================================================ */

static void emit_decor_block(CodegenContext *c, DecorBlockNode *n) {
    if (!n->target) return;

    /* Primero emitir la definición del target */
    if (n->target->type == NODE_FUNCTION_DEF) {
        emit_function_def(c, (FunctionDefNode*)n->target);

        FunctionDefNode *fn_node = (FunctionDefNode*)n->target;
        CGSymbol *sym = cg_lookup(c->global, fn_node->name);
        if (!sym) return;

        /* Aplicar decoradores en orden inverso: el último envuelve primero */
        LLVMValueRef current_val = sym->value;
        for (int i = n->decorators.count - 1; i >= 0; i--) {
            DecorItemNode *dec = (DecorItemNode*)n->decorators.items[i];
            CGSymbol *dec_sym = cg_lookup(c->global, dec->name);
            if (!dec_sym) {
                cg_error(c, (HulkNode*)dec,
                         "decorador '%s' no definido", dec->name);
                continue;
            }

            /* Necesitamos llamar al decorador en main() context */
            /* Guardar posición actual del builder y restaurar después */
            LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(c->builder);

            /* Construir llamada: dec(current_val, args...) */
            int total_args = 1 + dec->args.count;
            LLVMValueRef *args = calloc(total_args, sizeof(LLVMValueRef));
            LLVMTypeRef  *argt = calloc(total_args, sizeof(LLVMTypeRef));
            args[0] = current_val;
            argt[0] = LLVMTypeOf(current_val);
            for (int j = 0; j < dec->args.count; j++) {
                args[j + 1] = cg_emit_expr(c, dec->args.items[j]);
                argt[j + 1] = LLVMTypeOf(args[j + 1]);
            }

            LLVMTypeRef dec_fn_type;
            if (LLVMIsAFunction(dec_sym->value)) {
                dec_fn_type = LLVMGlobalGetValueType(dec_sym->value);
            } else {
                LLVMTypeRef ret_t = LLVMTypeOf(current_val);
                dec_fn_type = LLVMFunctionType(ret_t, argt, total_args, 0);
            }

            current_val = LLVMBuildCall2(c->builder, dec_fn_type,
                                          dec_sym->value, args,
                                          total_args, "decor");
            free(args);
            free(argt);

            /* Restaurar builder position */
            if (saved_bb)
                LLVMPositionBuilderAtEnd(c->builder, saved_bb);
        }

        /* Actualizar el símbolo global con el valor decorado */
        sym->value = current_val;

    } else if (n->target->type == NODE_TYPE_DEF) {
        emit_type_def(c, (TypeDefNode*)n->target);
        /* Decorar tipos se puede implementar como decorar el constructor */
    }
}
