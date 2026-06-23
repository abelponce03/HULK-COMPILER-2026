/*
 * hulk_codegen_control.c — Flujo de control como expresión
 *
 * En HULK let/if/while/for/block son expresiones con valor. Aquí se
 * emiten sus basic blocks y el PHI/alloca que materializa su resultado.
 */
#include "hulk_codegen_internal.h"

static LLVMValueRef cg_emit_iterator_method(CodegenContext *c,
                                            LLVMValueRef obj,
                                            const char *method,
                                            LLVMTypeRef ret_t) {
    int slot = cg_method_slot(c, method);
    if (slot < 0 || !c->vtables_table) {
        cg_error(c, NULL, "método iterador '%s' no disponible", method);
        return ret_t == c->t_bool ? LLVMConstInt(c->t_bool, 0, 0)
                                  : LLVMConstReal(c->t_double, 0.0);
    }

    LLVMValueRef tag = LLVMBuildLoad2(c->builder, c->t_i32, obj, "iter.tag");
    LLVMTypeRef vt_table_t = LLVMArrayType(c->t_i8ptr, c->type_info_count);
    LLVMValueRef idxs1[2] = {
        LLVMConstInt(c->t_i32, 0, 0),
        tag
    };
    LLVMValueRef vt_entry = LLVMBuildInBoundsGEP2(
        c->builder, vt_table_t, c->vtables_table, idxs1, 2, "iter.vt.entry");
    LLVMValueRef vt = LLVMBuildLoad2(c->builder, c->t_i8ptr, vt_entry, "iter.vt");

    LLVMTypeRef vt_t = LLVMArrayType(c->t_i8ptr,
        c->method_slot_count > 0 ? c->method_slot_count : 1);
    LLVMValueRef idxs2[2] = {
        LLVMConstInt(c->t_i32, 0, 0),
        LLVMConstInt(c->t_i32, slot, 0)
    };
    LLVMValueRef fn_slot = LLVMBuildInBoundsGEP2(
        c->builder, vt_t, vt, idxs2, 2, "iter.fn.slot");
    LLVMValueRef fn = LLVMBuildLoad2(c->builder, c->t_i8ptr, fn_slot, "iter.fn");
    LLVMTypeRef params[1] = { c->t_i8ptr };
    LLVMTypeRef ft = LLVMFunctionType(ret_t, params, 1, 0);
    return LLVMBuildCall2(c->builder, ft, fn, &obj, 1,
                          ret_t == c->t_void ? "" : "iter.call");
}

LLVMValueRef cg_emit_let(CodegenContext *c, LetExprNode *n) {
    cg_push_scope(c);

    for (int i = 0; i < n->bindings.count; i++) {
        VarBindingNode *vb = (VarBindingNode*)n->bindings.items[i];

        /* Determinar el hulk_type estático del valor:
         *   - Si vb->type_annotation es un user type, usar ese
         *   - Si no, intentar inferir del init expr */
        CGTypeInfo *binding_ti = NULL;
        if (vb->type_annotation)
            binding_ti = cg_type_info_find(c, vb->type_annotation);
        if (!binding_ti && vb->init_expr)
            binding_ti = cg_static_type_of(c, vb->init_expr);

        LLVMValueRef init_val = vb->init_expr
            ? cg_emit_expr(c, vb->init_expr)
            : LLVMConstReal(c->t_double, 0.0);

        /* Si el init es una función global, la registramos directamente
         * como sym->value con is_func=1; el callsite la llamará vía
         * sym->type (LLVMGlobalGetValueType). */
        if (init_val && LLVMIsAFunction(init_val)) {
            LLVMTypeRef fn_type = LLVMGlobalGetValueType(init_val);
            CGSymbol *vsym = cg_define(c, vb->name, init_val, fn_type, 1);
            if (vsym) vsym->hulk_type = binding_ti;
            continue;
        }

        LLVMTypeRef val_type = LLVMTypeOf(init_val);
        LLVMValueRef alloca = cg_create_entry_alloca(c, val_type, vb->name);
        LLVMBuildStore(c->builder, init_val, alloca);
        CGSymbol *vsym = cg_define(c, vb->name, alloca, val_type, 0);
        if (vsym) vsym->hulk_type = binding_ti;
    }

    LLVMValueRef result = cg_emit_expr(c, n->body);
    cg_pop_scope(c);
    return result;
}

LLVMValueRef cg_emit_if(CodegenContext *c, IfExprNode *n) {
    LLVMValueRef cond = cg_emit_expr(c, n->condition);

    /* Convertir a i1 si es double (truthy = != 0.0) */
    LLVMTypeRef ct = LLVMTypeOf(cond);
    if (ct == c->t_double)
        cond = LLVMBuildFCmp(c->builder, LLVMRealONE, cond,
                              LLVMConstReal(c->t_double, 0.0), "tobool");

    LLVMValueRef fn = c->current_fn;
    LLVMBasicBlockRef then_bb  = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "then");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "ifmerge");

    /* Determinar cuántas ramas hay */
    int branch_count = 1 + n->elifs.count + (n->else_body ? 1 : 0);
    LLVMBasicBlockRef *blocks = calloc(branch_count, sizeof(LLVMBasicBlockRef));
    LLVMValueRef      *values = calloc(branch_count, sizeof(LLVMValueRef));
    int idx = 0;

    /* Then branch */
    LLVMBasicBlockRef else_bb;
    if (n->elifs.count > 0 || n->else_body) {
        else_bb = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "else");
    } else {
        else_bb = merge_bb;
    }

    LLVMBuildCondBr(c->builder, cond, then_bb, else_bb);

    /* Emit then */
    LLVMPositionBuilderAtEnd(c->builder, then_bb);
    values[idx] = cg_emit_expr(c, n->then_body);
    blocks[idx] = LLVMGetInsertBlock(c->builder);
    LLVMBuildBr(c->builder, merge_bb);
    idx++;

    /* Emit elifs */
    LLVMBasicBlockRef current_else = else_bb;
    for (int i = 0; i < n->elifs.count; i++) {
        ElifBranchNode *elif = (ElifBranchNode*)n->elifs.items[i];
        LLVMPositionBuilderAtEnd(c->builder, current_else);

        LLVMValueRef econd = cg_emit_expr(c, elif->condition);
        LLVMTypeRef ect = LLVMTypeOf(econd);
        if (ect == c->t_double)
            econd = LLVMBuildFCmp(c->builder, LLVMRealONE, econd,
                                   LLVMConstReal(c->t_double, 0.0), "tobool");

        LLVMBasicBlockRef elif_then = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "elif.then");
        LLVMBasicBlockRef elif_else;
        if (i + 1 < n->elifs.count || n->else_body) {
            elif_else = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "elif.else");
        } else {
            elif_else = merge_bb;
        }

        LLVMBuildCondBr(c->builder, econd, elif_then, elif_else);

        LLVMPositionBuilderAtEnd(c->builder, elif_then);
        values[idx] = cg_emit_expr(c, elif->body);
        blocks[idx] = LLVMGetInsertBlock(c->builder);
        LLVMBuildBr(c->builder, merge_bb);
        idx++;

        current_else = elif_else;
    }

    /* Emit else */
    if (n->else_body) {
        LLVMPositionBuilderAtEnd(c->builder, current_else);
        values[idx] = cg_emit_expr(c, n->else_body);
        blocks[idx] = LLVMGetInsertBlock(c->builder);
        LLVMBuildBr(c->builder, merge_bb);
        idx++;
    }

    /* Merge block con phi */
    LLVMPositionBuilderAtEnd(c->builder, merge_bb);

    if (idx > 0) {
        LLVMTypeRef result_type = LLVMTypeOf(values[0]);
        /* Si TODAS las ramas son void, no se puede emitir PHI de void.
         * En ese caso el if entero es void. */
        if (result_type == c->t_void) {
            int all_void = 1;
            for (int i = 1; i < idx; i++) {
                if (LLVMTypeOf(values[i]) != c->t_void) { all_void = 0; break; }
            }
            if (all_void) {
                free(blocks);
                free(values);
                return LLVMGetUndef(c->t_void);
            }
        }
        LLVMValueRef phi = LLVMBuildPhi(c->builder, result_type, "ifval");
        LLVMAddIncoming(phi, values, blocks, idx);
        free(blocks);
        free(values);
        return phi;
    }

    free(blocks);
    free(values);
    return LLVMConstReal(c->t_double, 0.0);
}

LLVMValueRef cg_emit_while(CodegenContext *c, WhileStmtNode *n) {
    LLVMValueRef fn = c->current_fn;

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "while.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "while.body");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "while.end");

    /* Alloca para almacenar el último valor del body */
    LLVMValueRef result_ptr = cg_create_entry_alloca(c, c->t_double, "while.val");
    LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0), result_ptr);

    LLVMBuildBr(c->builder, cond_bb);

    /* Condition */
    LLVMPositionBuilderAtEnd(c->builder, cond_bb);
    LLVMValueRef cond = cg_emit_expr(c, n->condition);
    LLVMTypeRef ct = LLVMTypeOf(cond);
    if (ct == c->t_double)
        cond = LLVMBuildFCmp(c->builder, LLVMRealONE, cond,
                              LLVMConstReal(c->t_double, 0.0), "tobool");
    LLVMBuildCondBr(c->builder, cond, body_bb, end_bb);

    /* Body */
    LLVMPositionBuilderAtEnd(c->builder, body_bb);
    LLVMValueRef body_val = cg_emit_expr(c, n->body);
    LLVMTypeRef body_t = LLVMTypeOf(body_val);
    int body_is_void = (body_t == c->t_void);
    if (!body_is_void && body_t == c->t_double)
        LLVMBuildStore(c->builder, body_val, result_ptr);
    LLVMBuildBr(c->builder, cond_bb);

    /* End */
    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    /* Si el body es void, el while entero es void (evita imprimir 0
     * espurio al final cuando es top-level). */
    if (body_is_void) return LLVMGetUndef(c->t_void);
    return LLVMBuildLoad2(c->builder, c->t_double, result_ptr, "while.res");
}

LLVMValueRef cg_emit_for(CodegenContext *c, ForStmtNode *n) {
    /* Soportamos dos formas de iterable:
     *   1. Llamada sintáctica a range(start, end) — usamos [start, end)
     *   2. Cualquier otra expresión que evalúe a Number N — iteramos [0, N)
     * Esto evita necesitar el protocolo Iterable mientras tanto. */
    cg_push_scope(c);

    LLVMValueRef start_val = LLVMConstReal(c->t_double, 0.0);
    LLVMValueRef end_val;

    int handled_as_range = 0;
    if (n->iterable && n->iterable->type == NODE_CALL_EXPR) {
        CallExprNode *ce = (CallExprNode*)n->iterable;
        if (ce->callee && ce->callee->type == NODE_IDENT) {
            IdentNode *idn = (IdentNode*)ce->callee;
            if (idn->name && strcmp(idn->name, "range") == 0 &&
                ce->args.count == 2) {
                start_val = cg_emit_expr(c, ce->args.items[0]);
                end_val   = cg_emit_expr(c, ce->args.items[1]);
                handled_as_range = 1;
            }
        }
    }
    if (!handled_as_range) {
        end_val = cg_emit_expr(c, n->iterable);
        if (LLVMTypeOf(end_val) != c->t_double) {
            LLVMValueRef iterator = end_val;
            LLVMValueRef loop_var = cg_create_entry_alloca(c, c->t_double,
                                                           n->var_name);
            LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0),
                           loop_var);
            cg_define(c, n->var_name, loop_var, c->t_double, 0);

            LLVMValueRef fn = c->current_fn;
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "for.iter.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "for.iter.body");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "for.iter.end");

            LLVMValueRef result_ptr = cg_create_entry_alloca(c, c->t_double,
                                                             "for.iter.val");
            LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0),
                           result_ptr);
            LLVMBuildBr(c->builder, cond_bb);

            LLVMPositionBuilderAtEnd(c->builder, cond_bb);
            LLVMValueRef keep = cg_emit_iterator_method(c, iterator, "next",
                                                        c->t_bool);
            LLVMBuildCondBr(c->builder, keep, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(c->builder, body_bb);
            LLVMValueRef current = cg_emit_iterator_method(c, iterator,
                                                           "current",
                                                           c->t_double);
            LLVMBuildStore(c->builder, current, loop_var);
            LLVMValueRef body_val = cg_emit_expr(c, n->body);
            LLVMTypeRef body_t = LLVMTypeOf(body_val);
            int body_is_void = (body_t == c->t_void);
            if (!body_is_void && body_t == c->t_double)
                LLVMBuildStore(c->builder, body_val, result_ptr);
            LLVMBuildBr(c->builder, cond_bb);

            LLVMPositionBuilderAtEnd(c->builder, end_bb);
            cg_pop_scope(c);
            if (body_is_void) return LLVMGetUndef(c->t_void);
            return LLVMBuildLoad2(c->builder, c->t_double, result_ptr,
                                  "for.iter.res");
        }
    }

    LLVMValueRef counter = cg_create_entry_alloca(c, c->t_double, n->var_name);
    LLVMBuildStore(c->builder, start_val, counter);
    cg_define(c, n->var_name, counter, c->t_double, 0);

    LLVMValueRef fn = c->current_fn;
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "for.cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "for.body");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, "for.end");

    /* Alloca para almacenar el último valor del body */
    LLVMValueRef result_ptr = cg_create_entry_alloca(c, c->t_double, "for.val");
    LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0), result_ptr);

    LLVMBuildBr(c->builder, cond_bb);

    /* Condition: counter < end_val */
    LLVMPositionBuilderAtEnd(c->builder, cond_bb);
    LLVMValueRef cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "cur");
    LLVMValueRef cond = LLVMBuildFCmp(c->builder, LLVMRealOLT,
                                       cur, end_val, "forcond");
    LLVMBuildCondBr(c->builder, cond, body_bb, end_bb);

    /* Body */
    LLVMPositionBuilderAtEnd(c->builder, body_bb);
    LLVMValueRef body_val = cg_emit_expr(c, n->body);
    LLVMTypeRef body_t = LLVMTypeOf(body_val);
    int body_is_void = (body_t == c->t_void);
    if (!body_is_void && body_t == c->t_double)
        LLVMBuildStore(c->builder, body_val, result_ptr);
    /* Increment counter */
    cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "cur");
    LLVMValueRef next = LLVMBuildFAdd(c->builder, cur,
                                       LLVMConstReal(c->t_double, 1.0), "inc");
    LLVMBuildStore(c->builder, next, counter);
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    cg_pop_scope(c);
    /* Si el body produjo valores void, el for entero es void — así el
     * top-level no intenta imprimir un valor sin sentido. */
    if (body_is_void) {
        /* Emitir un call void dummy para que el caller vea LLVMTypeOf == void.
         * Reutilizamos hulk_print de double con un valor que se descarte:
         * no, mejor un bitcast. La forma más limpia es un store-then-noop
         * y retornar el call void de un noop. Para no inflar el IR,
         * usamos directamente LLVMGetUndef del void. */
        return LLVMGetUndef(c->t_void);
    }
    return LLVMBuildLoad2(c->builder, c->t_double, result_ptr, "for.res");
}

LLVMValueRef cg_emit_block(CodegenContext *c, BlockStmtNode *n) {
    LLVMValueRef last = LLVMConstReal(c->t_double, 0.0);
    for (int i = 0; i < n->statements.count; i++)
        last = cg_emit_expr(c, n->statements.items[i]);
    return last;
}
