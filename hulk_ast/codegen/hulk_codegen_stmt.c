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

/* Emisores locales a este archivo. Los de tipos viven en
 * hulk_codegen_typedecl.c y los de inferencia en hulk_codegen_infer.c
 * (ver hulk_codegen_internal.h). */
static void emit_function_def(CodegenContext *c, FunctionDefNode *n);
static void forward_declare_function(CodegenContext *c, FunctionDefNode *n);
static void emit_decor_block(CodegenContext *c, DecorBlockNode *n);
static void emit_function_adapter(CodegenContext *c, FunctionDefNode *n,
                                  CGSymbol *sym);
static void init_function_closure_cells(CodegenContext *c);

void cg_emit_program(CodegenContext *c, HulkNode *program) {
    if (!program || program->type != NODE_PROGRAM) {
        cg_error(c, program, "se esperaba un nodo Program");
        return;
    }
    ProgramNode *prog = (ProgramNode*)program;
    c->current_program = program;

    /* ---- Pasada 1: Forward declarations ---- */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        switch (decl->type) {
            case NODE_FUNCTION_DEF:
                forward_declare_function(c, (FunctionDefNode*)decl);
                break;
            case NODE_TYPE_DEF:
                cg_forward_declare_type(c, (TypeDefNode*)decl);
                break;
            case NODE_DECOR_BLOCK: {
                DecorBlockNode *db = (DecorBlockNode*)decl;
                if (db->target && db->target->type == NODE_FUNCTION_DEF)
                    forward_declare_function(c, (FunctionDefNode*)db->target);
                else if (db->target && db->target->type == NODE_TYPE_DEF)
                    cg_forward_declare_type(c, (TypeDefNode*)db->target);
                break;
            }
            default: break;
        }
    }

    /* ---- Pasada 1.5: Construir vtables y tablas RTTI ----
     * Todas las funciones método ya están forward-declared y todos los
     * slots globales ya están asignados; podemos llenar las vtables. */
    cg_emit_rtti_globals(c);

    /* ---- Pasada 2: Emitir cuerpos de funciones y tipos ---- */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        switch (decl->type) {
            case NODE_FUNCTION_DEF:
                emit_function_def(c, (FunctionDefNode*)decl);
                break;
            case NODE_TYPE_DEF:
                cg_emit_type_def(c, (TypeDefNode*)decl);
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

    init_function_closure_cells(c);

    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (!decl) continue;

        /* Solo emitir expresiones / statements top-level (no func/type/decor) */
        if (decl->type != NODE_FUNCTION_DEF &&
            decl->type != NODE_TYPE_DEF &&
            decl->type != NODE_DECOR_BLOCK) {
            /* Descender en let/block para detectar si la expresión
             * efectiva final es un loop — en ese caso no imprimimos
             * el while.res / for.res residual al top-level. */
            HulkNode *effective = decl;
            while (effective) {
                if (effective->type == NODE_LET_EXPR)
                    effective = ((LetExprNode*)effective)->body;
                else if (effective->type == NODE_BLOCK_STMT) {
                    BlockStmtNode *b = (BlockStmtNode*)effective;
                    if (b->statements.count == 0) break;
                    effective = b->statements.items[b->statements.count - 1];
                } else break;
            }
            int is_loop_top = effective && (
                effective->type == NODE_WHILE_STMT ||
                effective->type == NODE_FOR_STMT);
            LLVMValueRef val = cg_emit_expr(c, decl);
            LLVMTypeRef vt = LLVMTypeOf(val);

            /* Si es una llamada void, no intentar imprimir el resultado */
            if (vt == c->t_void) continue;

            /* Si es una llamada a print, ya se emitió (el intercept retorna
             * void). Si es una expresión suelta, imprimirla como última
             * expresión del programa. */
            if (i == prog->declarations.count - 1 && !is_loop_top) {
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

static void forward_declare_function(CodegenContext *c, FunctionDefNode *n) {
    int argc = n->params.count;
    LLVMTypeRef *param_types = calloc(argc > 0 ? argc : 1, sizeof(LLVMTypeRef));

    for (int i = 0; i < argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        param_types[i] = cg_infer_param_type(c, p->type_annotation);
    }

    LLVMTypeRef ret_t;
    if (n->return_type) {
        ret_t = cg_infer_return_type(c, n->return_type);
    } else {
        LLVMTypeRef inferred = cg_infer_body_return_type(c, n->body);
        ret_t = inferred ? inferred : c->t_double;
    }
    LLVMTypeRef fn_type = LLVMFunctionType(ret_t, param_types, argc, 0);
    LLVMValueRef fn = LLVMAddFunction(c->module, n->name, fn_type);

    /* Registrar en scope global */
    CGSymbol *sym = cg_define_in(c, c->global, n->name, fn, fn_type, 1);
    if (sym) {
        char adapter_name[256];
        snprintf(adapter_name, sizeof(adapter_name), "%s__closure_adapter",
                 n->name);

        LLVMTypeRef *adapter_params = calloc(argc + 1, sizeof(LLVMTypeRef));
        adapter_params[0] = c->t_i8ptr;
        for (int i = 0; i < argc; i++)
            adapter_params[i + 1] = param_types[i];
        LLVMTypeRef adapter_type = LLVMFunctionType(ret_t, adapter_params,
                                                    argc + 1, 0);
        LLVMValueRef adapter_fn = LLVMAddFunction(c->module, adapter_name,
                                                  adapter_type);

        char cell_name[256];
        snprintf(cell_name, sizeof(cell_name), "%s__closure_cell", n->name);
        LLVMValueRef cell = LLVMAddGlobal(c->module, c->t_i8ptr, cell_name);
        LLVMSetInitializer(cell, LLVMConstNull(c->t_i8ptr));

        sym->callable_cell = cell;
        sym->adapter_fn = adapter_fn;
        sym->adapter_type = adapter_type;
        free(adapter_params);
    }

    free(param_types);
}

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
        CGSymbol *psym = cg_define(c, p->name, alloca, param_t, 0);
        if (psym && p->type_annotation) {
            CGTypeInfo *pti = cg_type_info_find(c, p->type_annotation);
            if (pti) psym->hulk_type = pti;
        }
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

    emit_function_adapter(c, n, sym);
}

static void emit_function_adapter(CodegenContext *c, FunctionDefNode *n,
                                  CGSymbol *sym) {
    if (!sym || !sym->adapter_fn || sym->adapter_emitted) return;
    sym->adapter_emitted = 1;

    LLVMValueRef saved_fn = c->current_fn;
    c->current_fn = sym->adapter_fn;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, sym->adapter_fn, "entry");
    LLVMPositionBuilderAtEnd(c->builder, entry);

    int argc = n->params.count;
    LLVMValueRef *argv = calloc(argc > 0 ? argc : 1, sizeof(LLVMValueRef));
    for (int i = 0; i < argc; i++)
        argv[i] = LLVMGetParam(sym->adapter_fn, i + 1);

    LLVMTypeRef fn_type = LLVMGlobalGetValueType(sym->value);
    LLVMTypeRef ret_t = LLVMGetReturnType(fn_type);
    LLVMValueRef result = LLVMBuildCall2(
        c->builder, fn_type, sym->value, argv, argc,
        ret_t == c->t_void ? "" : "adapter.call");
    if (ret_t == c->t_void)
        LLVMBuildRetVoid(c->builder);
    else
        LLVMBuildRet(c->builder, result);

    free(argv);
    c->current_fn = saved_fn;
}

static void init_function_closure_cells(CodegenContext *c) {
    if (!c->global) return;
    for (int i = 0; i < c->global->sym_count; i++) {
        CGSymbol *sym = c->global->symbols[i];
        if (!sym || !sym->callable_cell || !sym->adapter_fn) continue;
        LLVMValueRef closure = cg_emit_make_closure(c, sym->adapter_fn,
                                                    NULL, 0);
        LLVMBuildStore(c->builder, closure, sym->callable_cell);
    }
}

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
        cg_emit_type_def(c, (TypeDefNode*)n->target);
        /* Decorar tipos se puede implementar como decorar el constructor */
    }
}
