/*
 * hulk_codegen_call.c — Emisión de llamadas y conversión a string
 *
 * Cubre: invocación de funciones/closures/builtins (cg_emit_call),
 * el intercept de print polimórfico, y la coerción de cualquier valor
 * a i8* para concatenación/impresión (cg_emit_to_string).
 */
#include "hulk_codegen_internal.h"

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

        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (!sym) {
            cg_error(c, (HulkNode*)n, "función '%s' no definida", id->name);
            return LLVMConstReal(c->t_double, 0.0);
        }

        LLVMValueRef fn_val = sym->value;
        if (!sym->is_func) {
            /* Variable que contiene function pointer — cargar */
            fn_val = LLVMBuildLoad2(c->builder, sym->type, sym->value, "fptr");
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

        /* Tipo estático para conocer la firma (tipo de retorno y args) */
        CGTypeInfo *ti = cg_static_type_of(c, ma->object);
        if (!ti && c->enclosing_type && obj == c->self_ptr)
            ti = c->enclosing_type;

        /* La firma del método se toma del tipo estático (válida en la
         * cadena de herencia gracias al layout compatible). */
        LLVMValueRef static_fn = ti ? cg_type_resolve_method(ti, ma->member)
                                     : NULL;
        if (!static_fn) {
            cg_error(c, (HulkNode*)n, "método '%s' no encontrado", ma->member);
            return LLVMConstReal(c->t_double, 0.0);
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
    int argc = n->args.count;
    LLVMValueRef *argv = calloc(argc > 0 ? argc : 1, sizeof(LLVMValueRef));
    LLVMTypeRef  *argt = calloc(argc > 0 ? argc : 1, sizeof(LLVMTypeRef));
    for (int i = 0; i < argc; i++) {
        argv[i] = cg_emit_expr(c, n->args.items[i]);
        argt[i] = LLVMTypeOf(argv[i]);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(c->t_double, argt, argc, 0);
    LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                          callee_val, argv, argc, "gcall");
    free(argv);
    free(argt);
    return result;
}
