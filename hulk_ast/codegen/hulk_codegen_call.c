/*
 * hulk_codegen_call.c — Emisión de llamadas y conversión a string
 *
 * Cubre: invocación de funciones/closures/builtins (cg_emit_call),
 * el intercept de print polimórfico, y la coerción de cualquier valor
 * a i8* para concatenación/impresión (cg_emit_to_string).
 */
#include "hulk_codegen_internal.h"

static LLVMValueRef emit_array_new(CodegenContext *c, LLVMValueRef size_d) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
    LLVMValueRef size_i = LLVMBuildFPToSI(c->builder, size_d, i64, "arr.n");
    LLVMValueRef bytes = LLVMBuildAdd(
        c->builder,
        LLVMBuildMul(c->builder, size_i, LLVMConstInt(i64, 8, 0), "arr.bytes.data"),
        LLVMConstInt(i64, 8, 0),
        "arr.bytes");
    LLVMTypeRef malloc_params[1] = { i64 };
    LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
    LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                      c->fn_malloc, &bytes, 1, "arr");
    LLVMValueRef size_i32 = LLVMBuildFPToSI(c->builder, size_d, c->t_i32, "arr.n32");
    LLVMBuildStore(c->builder, size_i32, raw);
    return raw;
}

static LLVMValueRef emit_array_init(CodegenContext *c,
                                    LLVMValueRef size_d,
                                    LLVMValueRef closure) {
    LLVMValueRef raw = emit_array_new(c, size_d);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
    LLVMValueRef size_i = LLVMBuildFPToSI(c->builder, size_d, i64, "init.n");

    LLVMValueRef fn = c->current_fn;
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "arr.init.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "arr.init.body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "arr.init.end");
    LLVMValueRef idx = cg_create_entry_alloca(c, i64, "arr.i");
    LLVMBuildStore(c->builder, LLVMConstInt(i64, 0, 0), idx);
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, cond_bb);
    LLVMValueRef cur = LLVMBuildLoad2(c->builder, i64, idx, "arr.i.cur");
    LLVMValueRef cond = LLVMBuildICmp(c->builder, LLVMIntSLT, cur, size_i, "arr.i.lt");
    LLVMBuildCondBr(c->builder, cond, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(c->builder, body_bb);
    LLVMValueRef cur_d = LLVMBuildSIToFP(c->builder, cur, c->t_double, "arr.i.d");
    LLVMValueRef fn_ptr = LLVMBuildLoad2(c->builder, c->t_i8ptr, closure, "arr.init.fn");
    LLVMTypeRef param_t[2] = { c->t_i8ptr, c->t_double };
    LLVMTypeRef fn_type = LLVMFunctionType(c->t_double, param_t, 2, 0);
    LLVMValueRef cargs[2] = { closure, cur_d };
    LLVMValueRef val = LLVMBuildCall2(c->builder, fn_type, fn_ptr, cargs, 2, "arr.init.val");
    LLVMValueRef offset = LLVMBuildAdd(
        c->builder,
        LLVMBuildMul(c->builder, cur, LLVMConstInt(i64, 8, 0), "arr.ofs.mul"),
        LLVMConstInt(i64, 8, 0),
        "arr.ofs");
    LLVMValueRef ptr = LLVMBuildInBoundsGEP2(
        c->builder, LLVMInt8TypeInContext(c->llvm_ctx), raw, &offset, 1,
        "arr.elem.ptr");
    LLVMBuildStore(c->builder, val, ptr);
    LLVMValueRef next = LLVMBuildAdd(c->builder, cur, LLVMConstInt(i64, 1, 0), "arr.i.next");
    LLVMBuildStore(c->builder, next, idx);
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    return raw;
}

static LLVMValueRef emit_closure_call(CodegenContext *c, CallExprNode *n,
                                      LLVMValueRef closure) {
    int argc = n->args.count;
    LLVMValueRef *argv = calloc(argc + 1, sizeof(LLVMValueRef));
    LLVMTypeRef *argt = calloc(argc + 1, sizeof(LLVMTypeRef));
    argv[0] = closure;
    argt[0] = c->t_i8ptr;
    for (int i = 0; i < argc; i++) {
        argv[i + 1] = cg_emit_expr(c, n->args.items[i]);
        argt[i + 1] = LLVMTypeOf(argv[i + 1]);
    }
    LLVMValueRef fn_ptr = LLVMBuildLoad2(c->builder, c->t_i8ptr, closure, "closure.fn");
    LLVMTypeRef ret_t = cg_llvm_type_for_name(c, n->base.static_type);
    if (!ret_t) ret_t = c->t_double;
    LLVMTypeRef fn_type = LLVMFunctionType(ret_t, argt, argc + 1, 0);
    const char *name = (ret_t == c->t_void) ? "" : "closure.call";
    LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type, fn_ptr,
                                         argv, argc + 1, name);
    free(argv);
    free(argt);
    return result;
}

static LLVMValueRef emit_repeat_macro(CodegenContext *c, CallExprNode *n) {
    LLVMValueRef times = cg_emit_expr(c, n->args.items[0]);
    LLVMValueRef counter = cg_create_entry_alloca(c, c->t_double, "repeat.i");
    LLVMBuildStore(c->builder, times, counter);
    LLVMValueRef result_ptr = cg_create_entry_alloca(c, c->t_double, "repeat.val");
    LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0), result_ptr);

    LLVMValueRef fn = c->current_fn;
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "repeat.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "repeat.body");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "repeat.end");
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, cond_bb);
    LLVMValueRef cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "repeat.cur");
    LLVMValueRef keep = LLVMBuildFCmp(c->builder, LLVMRealOGT, cur,
                                      LLVMConstReal(c->t_double, 0.0),
                                      "repeat.keep");
    LLVMBuildCondBr(c->builder, keep, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(c->builder, body_bb);
    cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "repeat.cur2");
    LLVMValueRef next = LLVMBuildFSub(c->builder, cur,
                                      LLVMConstReal(c->t_double, 1.0),
                                      "repeat.dec");
    LLVMBuildStore(c->builder, next, counter);
    LLVMValueRef body = cg_emit_expr(c, n->args.items[1]);
    if (LLVMTypeOf(body) == c->t_double)
        LLVMBuildStore(c->builder, body, result_ptr);
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    return LLVMBuildLoad2(c->builder, c->t_double, result_ptr, "repeat.res");
}

static LLVMValueRef emit_dynamic_method_call(CodegenContext *c,
                                             CallExprNode *n,
                                             LLVMValueRef obj,
                                             const char *method) {
    int slot = cg_method_slot(c, method);
    if (slot < 0 || !c->vtables_table) {
        cg_error(c, (HulkNode*)n, "método '%s' no encontrado", method);
        return LLVMConstReal(c->t_double, 0.0);
    }

    LLVMValueRef tag = LLVMBuildLoad2(c->builder, c->t_i32, obj, "dyn.tag");
    LLVMTypeRef vt_table_t = LLVMArrayType(c->t_i8ptr, c->type_info_count);
    LLVMValueRef idxs1[2] = { LLVMConstInt(c->t_i32, 0, 0), tag };
    LLVMValueRef vt_entry = LLVMBuildInBoundsGEP2(
        c->builder, vt_table_t, c->vtables_table, idxs1, 2, "dyn.vt.entry");
    LLVMValueRef vt = LLVMBuildLoad2(c->builder, c->t_i8ptr, vt_entry, "dyn.vt");
    LLVMTypeRef vt_t = LLVMArrayType(c->t_i8ptr,
        c->method_slot_count > 0 ? c->method_slot_count : 1);
    LLVMValueRef idxs2[2] = {
        LLVMConstInt(c->t_i32, 0, 0),
        LLVMConstInt(c->t_i32, slot, 0)
    };
    LLVMValueRef fn_slot = LLVMBuildInBoundsGEP2(
        c->builder, vt_t, vt, idxs2, 2, "dyn.fn.slot");
    LLVMValueRef fn = LLVMBuildLoad2(c->builder, c->t_i8ptr, fn_slot, "dyn.fn");

    int argc = n->args.count + 1;
    LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
    LLVMTypeRef *argt = calloc(argc, sizeof(LLVMTypeRef));
    argv[0] = obj;
    argt[0] = c->t_i8ptr;
    for (int i = 0; i < n->args.count; i++) {
        argv[i + 1] = cg_emit_expr(c, n->args.items[i]);
        argt[i + 1] = LLVMTypeOf(argv[i + 1]);
    }
    LLVMTypeRef ret_t = cg_llvm_type_for_name(c, n->base.static_type);
    if (!ret_t) ret_t = c->t_double;
    LLVMTypeRef ft = LLVMFunctionType(ret_t, argt, argc, 0);
    LLVMValueRef result = LLVMBuildCall2(c->builder, ft, fn, argv, argc,
                                         ret_t == c->t_void ? "" : "dyn.call");
    free(argv);
    free(argt);
    return result;
}

LLVMValueRef cg_emit_to_string(CodegenContext *c, HulkNode *node) {
    LLVMValueRef val = cg_emit_expr(c, node);
    LLVMTypeRef vt = LLVMTypeOf(val);

    if (vt == c->t_i8ptr) return val;
    if (vt == c->t_double) {
        LLVMTypeRef params[1] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
        return LLVMBuildCall2(c->builder, ft, c->fn_hulk_num_to_str,
                              &val, 1, "numstr");
    }
    if (vt == c->t_bool) {
        LLVMTypeRef params[1] = { c->t_bool };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
        return LLVMBuildCall2(c->builder, ft, c->fn_hulk_bool_to_str,
                              &val, 1, "boolstr");
    }
    /* Fallback: retornar string vacío */
    return LLVMBuildGlobalStringPtr(c->builder, "<object>", "objstr");
}

static LLVMValueRef emit_polymorphic_print(CodegenContext *c, LLVMValueRef val) {
    LLVMTypeRef vt = LLVMTypeOf(val);

    if (vt == c->t_double) {
        LLVMTypeRef params[1] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
        return LLVMBuildCall2(c->builder, ft, c->fn_hulk_print, &val, 1, "");
    }
    if (vt == c->t_bool) {
        LLVMTypeRef params[1] = { c->t_bool };
        LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
        return LLVMBuildCall2(c->builder, ft, c->fn_hulk_print_bool, &val, 1, "");
    }
    /* i8* / opaque ptr — cubre strings y objetos */
    LLVMTypeRef params[1] = { c->t_i8ptr };
    LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
    return LLVMBuildCall2(c->builder, ft, c->fn_hulk_print_str, &val, 1, "");
}

LLVMValueRef cg_emit_call(CodegenContext *c, CallExprNode *n) {
    /* Caso 1: callee es identificador → llamada directa */
    if (n->callee->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->callee;

        /* print intercept: despacho polimórfico por tipo del argumento.
         * Retornamos el call void para que callers (top-level eval, etc.)
         * lo filtren como void y no intenten re-imprimir el residuo. */
        if (strcmp(id->name, "print") == 0 && n->args.count == 1) {
            LLVMValueRef arg = cg_emit_expr(c, n->args.items[0]);
            return emit_polymorphic_print(c, arg);
        }
        if (strcmp(id->name, "repeat") == 0 && n->args.count == 2)
            return emit_repeat_macro(c, n);
        if (strcmp(id->name, "__array_new") == 0 && n->args.count == 1) {
            LLVMValueRef size = cg_emit_expr(c, n->args.items[0]);
            return emit_array_new(c, size);
        }
        if (strcmp(id->name, "__array_init") == 0 && n->args.count == 2) {
            LLVMValueRef size = cg_emit_expr(c, n->args.items[0]);
            LLVMValueRef fn = cg_emit_expr(c, n->args.items[1]);
            return emit_array_init(c, size, fn);
        }

        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (!sym) {
            cg_error(c, (HulkNode*)n, "función '%s' no definida", id->name);
            return LLVMConstReal(c->t_double, 0.0);
        }

        LLVMValueRef fn_val = sym->value;
        if (!sym->is_func) {
            LLVMValueRef closure = LLVMBuildLoad2(c->builder, sym->type,
                                                  sym->value, "closure");
            return emit_closure_call(c, n, closure);
        }

        int argc = n->args.count;
        LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
        LLVMTypeRef  *argt = calloc(argc, sizeof(LLVMTypeRef));
        for (int i = 0; i < argc; i++) {
            argv[i] = cg_emit_expr(c, n->args.items[i]);
            argt[i] = LLVMTypeOf(argv[i]);
        }

        /* Obtener tipo de función: si podemos, de la función; si no, construir */
        LLVMTypeRef fn_type;
        if (LLVMIsAFunction(fn_val)) {
            fn_type = LLVMGlobalGetValueType(fn_val);
        } else {
            /* Función variádica o pointer — construir tipo */
            LLVMTypeRef ret_t = c->t_double;
            fn_type = LLVMFunctionType(ret_t, argt, argc, 0);
        }

        /* LLVM: void calls can't have a name */
        LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
        const char *call_name = (ret_type == c->t_void) ? "" : "call";
        LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                              fn_val, argv, argc, call_name);
        free(argv);
        free(argt);
        return result;
    }

    /* Caso 2: callee es member access → llamada a método (vtable dispatch) */
    if (n->callee->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)n->callee;
        LLVMValueRef obj = cg_emit_expr(c, ma->object);

        if (ma->member && strcmp(ma->member, "size") == 0) {
            LLVMValueRef raw_size = LLVMBuildLoad2(c->builder, c->t_i32, obj, "arr.size.i");
            return LLVMBuildSIToFP(c->builder, raw_size, c->t_double, "arr.size");
        }

        /* Tipo estático para conocer la firma (tipo de retorno y args) */
        CGTypeInfo *ti = cg_static_type_of(c, ma->object);
        if (!ti && c->enclosing_type && obj == c->self_ptr)
            ti = c->enclosing_type;

        /* La firma del método se toma del tipo estático (válida en la
         * cadena de herencia gracias al layout compatible). */
        LLVMValueRef static_fn = ti ? cg_type_resolve_method(ti, ma->member)
                                     : NULL;
        if (!static_fn) {
            return emit_dynamic_method_call(c, n, obj, ma->member);
        }
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(static_fn);

        /* Construir args: self + user args */
        int argc = n->args.count + 1;
        LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
        argv[0] = obj;
        for (int i = 0; i < n->args.count; i++)
            argv[i + 1] = cg_emit_expr(c, n->args.items[i]);

        /* Dispatch dinámico vía vtable:
         *   tag      = load i32 from obj.gep(0,0)
         *   vtable   = load ptr from gep(@hulk_vtables, 0, tag)
         *   fn_ptr   = load ptr from gep(vtable, 0, slot)
         *   call fn_ptr(argv) */
        int slot = cg_method_slot(c, ma->member);
        if (slot < 0 || !c->vtables_table) {
            /* Fallback: dispatch estático */
            LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                                  static_fn, argv, argc, "scall");
            free(argv);
            return result;
        }

        LLVMValueRef tag_ptr = LLVMBuildStructGEP2(
            c->builder, ti->struct_type, obj, 0, "tag.ptr");
        LLVMValueRef tag = LLVMBuildLoad2(c->builder, c->t_i32,
                                          tag_ptr, "tag");

        /* @hulk_vtables[tag] — array de ptr indexado por tag */
        LLVMTypeRef vt_table_t = LLVMArrayType(c->t_i8ptr, c->type_info_count);
        LLVMValueRef idxs1[2] = {
            LLVMConstInt(c->t_i32, 0, 0),
            tag
        };
        LLVMValueRef vt_entry = LLVMBuildInBoundsGEP2(
            c->builder, vt_table_t, c->vtables_table, idxs1, 2, "vt.entry");
        LLVMValueRef vt = LLVMBuildLoad2(c->builder, c->t_i8ptr,
                                          vt_entry, "vt");

        /* vt[slot] — array de ptr, asumimos longitud == method_slot_count */
        LLVMTypeRef vt_t = LLVMArrayType(c->t_i8ptr,
            c->method_slot_count > 0 ? c->method_slot_count : 1);
        LLVMValueRef idxs2[2] = {
            LLVMConstInt(c->t_i32, 0, 0),
            LLVMConstInt(c->t_i32, slot, 0)
        };
        LLVMValueRef fn_slot_ptr = LLVMBuildInBoundsGEP2(
            c->builder, vt_t, vt, idxs2, 2, "fn.slot");
        LLVMValueRef fn_ptr = LLVMBuildLoad2(c->builder, c->t_i8ptr,
                                              fn_slot_ptr, "fn");

        LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                              fn_ptr, argv, argc, "vcall");
        free(argv);
        return result;
    }

    /* Caso 3: expresión genérica como callee */
    LLVMValueRef callee_val = cg_emit_expr(c, n->callee);
    return emit_closure_call(c, n, callee_val);
}
