/*
 * hulk_codegen_expr.c — Emisión de IR para expresiones
 *
 * Cada función recibe un nodo AST y retorna un LLVMValueRef con el
 * resultado de evaluar la expresión.  Usa evaluación bottom-up.
 *
 * SRP: Solo emisión de IR para expresiones.
 */

#include "hulk_codegen_internal.h"
#include <math.h>

/* ===== Forward declarations ===== */

static LLVMValueRef emit_ident(CodegenContext *c, IdentNode *n);
static LLVMValueRef emit_binary_op(CodegenContext *c, BinaryOpNode *n);
static LLVMValueRef emit_unary_op(CodegenContext *c, UnaryOpNode *n);
static LLVMValueRef emit_concat(CodegenContext *c, ConcatExprNode *n);
static LLVMValueRef emit_call(CodegenContext *c, CallExprNode *n);
static LLVMValueRef emit_member_access(CodegenContext *c, MemberAccessNode *n);
static LLVMValueRef emit_let(CodegenContext *c, LetExprNode *n);
static LLVMValueRef emit_if(CodegenContext *c, IfExprNode *n);
static LLVMValueRef emit_while(CodegenContext *c, WhileStmtNode *n);
static LLVMValueRef emit_for(CodegenContext *c, ForStmtNode *n);
static LLVMValueRef emit_block(CodegenContext *c, BlockStmtNode *n);
static LLVMValueRef emit_new(CodegenContext *c, NewExprNode *n);
static LLVMValueRef emit_assign(CodegenContext *c, AssignNode *n);
static LLVMValueRef emit_destruct(CodegenContext *c, DestructAssignNode *n);
static LLVMValueRef emit_self(CodegenContext *c, SelfNode *n);
static CGTypeInfo*  cg_static_type_of(CodegenContext *c, HulkNode *expr);

/* ============================================================
 *  Dispatcher
 * ============================================================ */

LLVMValueRef cg_emit_expr(CodegenContext *c, HulkNode *node) {
    if (!node) return LLVMConstReal(c->t_double, 0.0);

    switch (node->type) {
        case NODE_NUMBER_LIT: {
            NumberLitNode *n = (NumberLitNode*)node;
            return LLVMConstReal(c->t_double, n->value);
        }
        case NODE_STRING_LIT: {
            StringLitNode *n = (StringLitNode*)node;
            LLVMValueRef str = LLVMBuildGlobalStringPtr(
                c->builder, n->value ? n->value : "", "str");
            return str;
        }
        case NODE_BOOL_LIT: {
            BoolLitNode *n = (BoolLitNode*)node;
            return LLVMConstInt(c->t_bool, n->value ? 1 : 0, 0);
        }
        case NODE_IDENT:           return emit_ident(c, (IdentNode*)node);
        case NODE_FUNCTION_EXPR: {
            /* Emitimos la lambda como una función global con nombre único
             * y retornamos su LLVMValueRef. El caller (típicamente let)
             * la propaga como if were is_func=1. */
            FunctionExprNode *fn_n = (FunctionExprNode*)node;
            static int lambda_counter = 0;
            char lname[32];
            snprintf(lname, sizeof(lname), "lambda.%d", lambda_counter++);

            int argc = fn_n->params.count;
            LLVMTypeRef *ptypes = calloc(argc > 0 ? argc : 1, sizeof(LLVMTypeRef));
            for (int i = 0; i < argc; i++) {
                VarBindingNode *p = (VarBindingNode*)fn_n->params.items[i];
                if (p->type_annotation) {
                    if (strcmp(p->type_annotation, "String") == 0)
                        ptypes[i] = c->t_i8ptr;
                    else if (strcmp(p->type_annotation, "Boolean") == 0)
                        ptypes[i] = c->t_bool;
                    else
                        ptypes[i] = c->t_double;
                } else {
                    ptypes[i] = c->t_double;  /* default */
                }
            }
            LLVMTypeRef ret_t = c->t_double;
            if (fn_n->return_type && strcmp(fn_n->return_type, "Boolean") == 0)
                ret_t = c->t_bool;
            else if (fn_n->return_type && strcmp(fn_n->return_type, "String") == 0)
                ret_t = c->t_i8ptr;
            LLVMTypeRef fn_type = LLVMFunctionType(ret_t, ptypes, argc, 0);
            LLVMValueRef fn = LLVMAddFunction(c->module, lname, fn_type);

            /* Emitir el cuerpo */
            LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(c->builder);
            LLVMValueRef saved_fn = c->current_fn;
            c->current_fn = fn;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "entry");
            LLVMPositionBuilderAtEnd(c->builder, entry);

            cg_push_scope(c);
            for (int i = 0; i < argc; i++) {
                VarBindingNode *p = (VarBindingNode*)fn_n->params.items[i];
                LLVMValueRef pv = LLVMGetParam(fn, i);
                LLVMTypeRef pt = LLVMTypeOf(pv);
                LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
                LLVMBuildStore(c->builder, pv, alloca);
                cg_define(c, p->name, alloca, pt, 0);
            }

            LLVMValueRef body_val = cg_emit_expr(c, fn_n->body);
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
            if (!LLVMGetBasicBlockTerminator(cur_bb)) {
                if (ret_t == c->t_void) LLVMBuildRetVoid(c->builder);
                else LLVMBuildRet(c->builder, body_val);
            }
            cg_pop_scope(c);

            c->current_fn = saved_fn;
            if (saved_bb) LLVMPositionBuilderAtEnd(c->builder, saved_bb);
            free(ptypes);
            return fn;
        }
        case NODE_BINARY_OP:       return emit_binary_op(c, (BinaryOpNode*)node);
        case NODE_UNARY_OP:        return emit_unary_op(c, (UnaryOpNode*)node);
        case NODE_CONCAT_EXPR:     return emit_concat(c, (ConcatExprNode*)node);
        case NODE_CALL_EXPR:       return emit_call(c, (CallExprNode*)node);
        case NODE_MEMBER_ACCESS:   return emit_member_access(c, (MemberAccessNode*)node);
        case NODE_LET_EXPR:        return emit_let(c, (LetExprNode*)node);
        case NODE_IF_EXPR:         return emit_if(c, (IfExprNode*)node);
        case NODE_WHILE_STMT:      return emit_while(c, (WhileStmtNode*)node);
        case NODE_FOR_STMT:        return emit_for(c, (ForStmtNode*)node);
        case NODE_BLOCK_STMT:      return emit_block(c, (BlockStmtNode*)node);
        case NODE_NEW_EXPR:        return emit_new(c, (NewExprNode*)node);
        case NODE_ASSIGN:          return emit_assign(c, (AssignNode*)node);
        case NODE_DESTRUCT_ASSIGN: return emit_destruct(c, (DestructAssignNode*)node);
        case NODE_SELF:            return emit_self(c, (SelfNode*)node);
        case NODE_AS_EXPR: {
            /* as es un no-op en IR con opaque pointers; el caller usa
             * cg_static_type_of para conocer el tipo destino vía AsExpr. */
            AsExprNode *n = (AsExprNode*)node;
            return cg_emit_expr(c, n->expr);
        }
        case NODE_IS_EXPR: {
            /* is dinámico:
             *   target_tag = constante del tipo objetivo
             *   cur_tag    = load tag de val
             *   while cur_tag != -1:
             *     if cur_tag == target_tag → true
             *     cur_tag = parent_table[cur_tag]
             *   return false */
            IsExprNode *n = (IsExprNode*)node;
            LLVMValueRef val = cg_emit_expr(c, n->expr);
            CGTypeInfo *target_ti = cg_type_info_find(c, n->type_name);
            if (!target_ti || !c->parent_table) {
                /* Comparación con tipos primitivos / fallback estático */
                LLVMTypeRef vt = LLVMTypeOf(val);
                int result = 0;
                if (n->type_name) {
                    if (strcmp(n->type_name, "Number") == 0)
                        result = (vt == c->t_double);
                    else if (strcmp(n->type_name, "String") == 0)
                        result = (vt == c->t_i8ptr);
                    else if (strcmp(n->type_name, "Boolean") == 0)
                        result = (vt == c->t_bool);
                    else result = 1;
                }
                return LLVMConstInt(c->t_bool, result ? 1 : 0, 0);
            }

            /* Cargar el tag del objeto: gep(obj, 0, 0) → i32 */
            CGTypeInfo *static_ti = target_ti;  /* layout para gep tag */
            LLVMValueRef tag_ptr = LLVMBuildStructGEP2(
                c->builder, static_ti->struct_type, val, 0, "is.tag.ptr");
            LLVMValueRef tag0 = LLVMBuildLoad2(c->builder, c->t_i32,
                                                tag_ptr, "is.tag0");

            int tcount = c->type_info_count;
            int target_tag = target_ti->type_tag;

            LLVMValueRef fn = c->current_fn;
            LLVMBasicBlockRef cur_bb  = LLVMGetInsertBlock(c->builder);
            LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "is.loop");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "is.body");
            LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "is.true");
            LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "is.step");
            LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "is.end");
            LLVMBuildBr(c->builder, loop_bb);

            /* loop: cur = phi [tag0, entry], [parent_tag, step] */
            LLVMPositionBuilderAtEnd(c->builder, loop_bb);
            LLVMValueRef cur_phi = LLVMBuildPhi(c->builder, c->t_i32, "is.cur");
            LLVMValueRef neg1 = LLVMConstInt(c->t_i32, (unsigned)-1, 1);
            LLVMValueRef stop = LLVMBuildICmp(c->builder, LLVMIntEQ,
                                              cur_phi, neg1, "is.stop");
            LLVMBuildCondBr(c->builder, stop, end_bb, body_bb);

            /* body: if cur == target → true */
            LLVMPositionBuilderAtEnd(c->builder, body_bb);
            LLVMValueRef hit = LLVMBuildICmp(c->builder, LLVMIntEQ,
                cur_phi, LLVMConstInt(c->t_i32, target_tag, 0), "is.hit");
            LLVMBuildCondBr(c->builder, hit, true_bb, step_bb);

            /* step: cur = parent_table[cur] */
            LLVMPositionBuilderAtEnd(c->builder, step_bb);
            LLVMTypeRef parent_arr_t = LLVMArrayType(c->t_i32, tcount);
            LLVMValueRef pidxs[2] = {
                LLVMConstInt(c->t_i32, 0, 0),
                cur_phi
            };
            LLVMValueRef pent = LLVMBuildInBoundsGEP2(
                c->builder, parent_arr_t, c->parent_table, pidxs, 2, "p.ent");
            LLVMValueRef next_tag = LLVMBuildLoad2(c->builder, c->t_i32,
                                                    pent, "is.next");
            LLVMBasicBlockRef step_end = LLVMGetInsertBlock(c->builder);
            LLVMBuildBr(c->builder, loop_bb);

            /* true */
            LLVMPositionBuilderAtEnd(c->builder, true_bb);
            LLVMBuildBr(c->builder, end_bb);

            /* phi income */
            LLVMValueRef phi_vals[2] = { tag0, next_tag };
            LLVMBasicBlockRef phi_bbs[2] = { cur_bb, step_end };
            LLVMAddIncoming(cur_phi, phi_vals, phi_bbs, 2);

            /* end: phi result bool */
            LLVMPositionBuilderAtEnd(c->builder, end_bb);
            LLVMValueRef rphi = LLVMBuildPhi(c->builder, c->t_bool, "is.res");
            LLVMValueRef rvals[2] = {
                LLVMConstInt(c->t_bool, 0, 0),  /* del end-from-loop */
                LLVMConstInt(c->t_bool, 1, 0)   /* del true */
            };
            LLVMBasicBlockRef rbbs[2] = { loop_bb, true_bb };
            LLVMAddIncoming(rphi, rvals, rbbs, 2);
            return rphi;
        }
        case NODE_VECTOR_LIT: {
            /* Layout: { i32 size, double[N] items } como struct anónimo.
             * malloc(sizeof(i32) + N*sizeof(double)), store size, store
             * cada elemento. Retornar el ptr (i8*). */
            VectorLitNode *vn = (VectorLitNode*)node;
            int n = vn->items.count;

            LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
            int byte_size = 8 + 8 * (n > 0 ? n : 1);  /* align 8 */
            LLVMValueRef size_const = LLVMConstInt(i64, byte_size, 0);
            LLVMTypeRef malloc_params[1] = { i64 };
            LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr,
                                                      malloc_params, 1, 0);
            LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                              c->fn_malloc, &size_const, 1, "vec");

            /* size en bytes [0..3] (i32). */
            LLVMBuildStore(c->builder,
                LLVMConstInt(c->t_i32, n, 0), raw);

            /* items[i] = items[i+8 bytes]. Para simplicidad emitimos via
             * ptr aritmético en i64 sobre el i8*. Usamos GEP con i8
             * offset y bitcast al double*. */
            for (int i = 0; i < n; i++) {
                LLVMValueRef offset = LLVMConstInt(i64, 8 + 8 * i, 0);
                LLVMValueRef gep = LLVMBuildInBoundsGEP2(
                    c->builder,
                    LLVMInt8TypeInContext(c->llvm_ctx),
                    raw, &offset, 1, "vec.elem.ptr");
                LLVMValueRef val = cg_emit_expr(c, vn->items.items[i]);
                LLVMBuildStore(c->builder, val, gep);
            }
            return raw;
        }
        case NODE_INDEX_EXPR: {
            IndexExprNode *ix = (IndexExprNode*)node;
            LLVMValueRef obj = cg_emit_expr(c, ix->object);
            LLVMValueRef idx_d = cg_emit_expr(c, ix->index);

            /* Convertir índice double → i64 */
            LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
            LLVMValueRef idx_i = LLVMBuildFPToSI(c->builder, idx_d, i64, "idx");

            /* offset = 8 (header) + 8 * idx */
            LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
            LLVMValueRef mul = LLVMBuildMul(c->builder, idx_i, eight, "ofsmul");
            LLVMValueRef offset = LLVMBuildAdd(c->builder, mul, eight, "ofs");

            LLVMValueRef gep = LLVMBuildInBoundsGEP2(
                c->builder,
                LLVMInt8TypeInContext(c->llvm_ctx),
                obj, &offset, 1, "elem.ptr");
            return LLVMBuildLoad2(c->builder, c->t_double, gep, "elem");
        }
        case NODE_BASE_CALL: {
            /* base() — llamar a la implementación del método padre con
             * el mismo nombre que el enclosing method. Caminamos la
             * cadena de herencia hasta encontrar la implementación. */
            BaseCallNode *bn = (BaseCallNode*)node;
            if (!c->enclosing_type || !c->enclosing_type->parent ||
                !c->current_method_name || !c->self_ptr) {
                cg_error(c, node, "base() sin tipo padre o método válido");
                return LLVMConstReal(c->t_double, 0.0);
            }
            LLVMValueRef parent_fn = cg_type_resolve_method(
                c->enclosing_type->parent, c->current_method_name);
            if (!parent_fn) {
                cg_error(c, node, "base(): padre no implementa '%s'",
                         c->current_method_name);
                return LLVMConstReal(c->t_double, 0.0);
            }
            LLVMTypeRef parent_fn_type = LLVMGlobalGetValueType(parent_fn);

            /* args = self + user args. Aunque el caller no pase nada,
             * self siempre va. */
            int argc = bn->args.count + 1;
            LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
            argv[0] = c->self_ptr;
            for (int i = 0; i < bn->args.count; i++)
                argv[i + 1] = cg_emit_expr(c, bn->args.items[i]);
            LLVMValueRef result = LLVMBuildCall2(
                c->builder, parent_fn_type, parent_fn, argv, argc, "base");
            free(argv);
            return result;
        }
        default:
            cg_error(c, node, "nodo no soportado en codegen: %d", node->type);
            return LLVMConstReal(c->t_double, 0.0);
    }
}

/* ============================================================
 *  Identificadores
 * ============================================================ */

static LLVMValueRef emit_ident(CodegenContext *c, IdentNode *n) {
    CGSymbol *sym = cg_lookup(c->current, n->name);
    if (!sym) {
        cg_error(c, (HulkNode*)n, "variable '%s' no definida", n->name);
        return LLVMConstReal(c->t_double, 0.0);
    }
    if (sym->is_func) return sym->value;

    /* Variable: cargar desde alloca */
    return LLVMBuildLoad2(c->builder, sym->type, sym->value, n->name);
}

/* ============================================================
 *  Operadores binarios
 * ============================================================ */

static LLVMValueRef emit_short_circuit(CodegenContext *c, BinaryOpNode *n,
                                        int is_and) {
    /*
     * Short-circuit evaluation:
     *   AND: if (left) right else false
     *   OR:  if (left) true  else right
     */
    LLVMValueRef lv = cg_emit_expr(c, n->left);
    LLVMTypeRef lt = LLVMTypeOf(lv);
    if (lt != c->t_bool)
        lv = LLVMBuildFCmp(c->builder, LLVMRealONE, lv,
                            LLVMConstReal(c->t_double, 0.0), "tobool");

    LLVMValueRef fn = c->current_fn;
    LLVMBasicBlockRef rhs_bb   = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, is_and ? "and.rhs" : "or.rhs");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(
        c->llvm_ctx, fn, is_and ? "and.merge" : "or.merge");

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(c->builder);

    if (is_and)
        LLVMBuildCondBr(c->builder, lv, rhs_bb, merge_bb);
    else
        LLVMBuildCondBr(c->builder, lv, merge_bb, rhs_bb);

    /* Evaluate RHS */
    LLVMPositionBuilderAtEnd(c->builder, rhs_bb);
    LLVMValueRef rv = cg_emit_expr(c, n->right);
    LLVMTypeRef rt = LLVMTypeOf(rv);
    if (rt != c->t_bool)
        rv = LLVMBuildFCmp(c->builder, LLVMRealONE, rv,
                            LLVMConstReal(c->t_double, 0.0), "tobool");
    LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(c->builder);
    LLVMBuildBr(c->builder, merge_bb);

    /* Merge with PHI */
    LLVMPositionBuilderAtEnd(c->builder, merge_bb);
    LLVMValueRef phi = LLVMBuildPhi(c->builder, c->t_bool,
                                     is_and ? "and.val" : "or.val");
    LLVMValueRef short_val = LLVMConstInt(c->t_bool, is_and ? 0 : 1, 0);
    LLVMValueRef vals[2]   = { short_val, rv };
    LLVMBasicBlockRef bbs[2] = { entry_bb, rhs_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
}

static LLVMValueRef emit_binary_op(CodegenContext *c, BinaryOpNode *n) {
    /* Short-circuit for logical operators */
    if (n->op == OP_AND) return emit_short_circuit(c, n, 1);
    if (n->op == OP_OR)  return emit_short_circuit(c, n, 0);

    LLVMValueRef lv = cg_emit_expr(c, n->left);
    LLVMValueRef rv = cg_emit_expr(c, n->right);

    switch (n->op) {
        case OP_ADD: return LLVMBuildFAdd(c->builder, lv, rv, "add");
        case OP_SUB: return LLVMBuildFSub(c->builder, lv, rv, "sub");
        case OP_MUL: return LLVMBuildFMul(c->builder, lv, rv, "mul");
        case OP_DIV: return LLVMBuildFDiv(c->builder, lv, rv, "div");
        case OP_MOD: return LLVMBuildFRem(c->builder, lv, rv, "mod");

        case OP_POW: {
            /* pow(l, r) via libm */
            LLVMValueRef args[2] = { lv, rv };
            LLVMTypeRef pow_params[2] = { c->t_double, c->t_double };
            LLVMTypeRef pow_ft = LLVMFunctionType(c->t_double, pow_params, 2, 0);
            return LLVMBuildCall2(c->builder, pow_ft, c->fn_pow, args, 2, "pow");
        }

        /* Comparaciones: double → i1 */
        case OP_LT: return LLVMBuildFCmp(c->builder, LLVMRealOLT, lv, rv, "lt");
        case OP_GT: return LLVMBuildFCmp(c->builder, LLVMRealOGT, lv, rv, "gt");
        case OP_LE: return LLVMBuildFCmp(c->builder, LLVMRealOLE, lv, rv, "le");
        case OP_GE: return LLVMBuildFCmp(c->builder, LLVMRealOGE, lv, rv, "ge");
        case OP_EQ: return LLVMBuildFCmp(c->builder, LLVMRealOEQ, lv, rv, "eq");
        case OP_NEQ: return LLVMBuildFCmp(c->builder, LLVMRealUNE, lv, rv, "neq");

        /* OP_AND / OP_OR handled above via short-circuit */
        default: break;
    }
    return LLVMConstReal(c->t_double, 0.0);
}

/* ============================================================
 *  Operador unario: -expr
 * ============================================================ */

static LLVMValueRef emit_unary_op(CodegenContext *c, UnaryOpNode *n) {
    LLVMValueRef v = cg_emit_expr(c, n->operand);
    return LLVMBuildFNeg(c->builder, v, "neg");
}

/* ============================================================
 *  Concatenación: @ y @@
 * ============================================================ */

static LLVMValueRef emit_to_string(CodegenContext *c, HulkNode *node);

static LLVMValueRef emit_concat(CodegenContext *c, ConcatExprNode *n) {
    LLVMValueRef ls = emit_to_string(c, n->left);
    LLVMValueRef rs = emit_to_string(c, n->right);

    LLVMValueRef fn = (n->op == OP_CONCAT_WS) ?
        c->fn_hulk_concat_ws : c->fn_hulk_concat;
    LLVMValueRef args[2] = { ls, rs };

    LLVMTypeRef concat_params[2] = { c->t_i8ptr, c->t_i8ptr };
    LLVMTypeRef concat_ft = LLVMFunctionType(c->t_i8ptr, concat_params, 2, 0);
    return LLVMBuildCall2(c->builder, concat_ft, fn, args, 2, "concat");
}

/* Helper: convierte un valor a i8* dependiendo de su tipo LLVM */
static LLVMValueRef emit_to_string(CodegenContext *c, HulkNode *node) {
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

/* ============================================================
 *  Llamada a función
 * ============================================================ */

/* Helper: print polimórfico. Despacha al runtime apropiado según el
 * LLVMType del argumento. Retorna el call instruction (siempre void). */
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

static LLVMValueRef emit_call(CodegenContext *c, CallExprNode *n) {
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

/* ============================================================
 *  Acceso a miembro: obj.field
 * ============================================================ */

/* Helper: ancestro común más específico de dos tipos HULK. Si uno
 * conforma al otro, retorna el ancestro. Si no, sube por la cadena. */
static CGTypeInfo* cg_type_lca(CGTypeInfo *a, CGTypeInfo *b) {
    if (!a) return b;
    if (!b) return a;
    if (a == b) return a;
    /* Si a desciende de b */
    for (CGTypeInfo *t = a; t; t = t->parent)
        if (t == b) return b;
    /* Si b desciende de a */
    for (CGTypeInfo *t = b; t; t = t->parent)
        if (t == a) return a;
    /* Subir por a buscando un ancestro común con b */
    for (CGTypeInfo *ta = a->parent; ta; ta = ta->parent) {
        for (CGTypeInfo *tb = b; tb; tb = tb->parent)
            if (ta == tb) return ta;
    }
    return NULL;
}

/* Helper: dado un nodo expresión que produce un objeto, intenta
 * determinar su CGTypeInfo* estático sin emitir IR adicional. Usa el
 * scope (Ident), el enclosing_type (self), o el nombre del tipo (new).
 * Retorna NULL si no se puede determinar. */
static CGTypeInfo* cg_static_type_of(CodegenContext *c, HulkNode *expr) {
    if (!expr) return NULL;
    switch (expr->type) {
        case NODE_SELF:
            return c->enclosing_type;
        case NODE_IDENT: {
            IdentNode *id = (IdentNode*)expr;
            CGSymbol *sym = cg_lookup(c->current, id->name);
            return sym ? sym->hulk_type : NULL;
        }
        case NODE_NEW_EXPR: {
            NewExprNode *ne = (NewExprNode*)expr;
            return cg_type_info_find(c, ne->type_name);
        }
        case NODE_AS_EXPR: {
            AsExprNode *ae = (AsExprNode*)expr;
            return cg_type_info_find(c, ae->type_name);
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)expr;
            CGTypeInfo *agg = cg_static_type_of(c, iff->then_body);
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                agg = cg_type_lca(agg, cg_static_type_of(c, e->body));
            }
            if (iff->else_body)
                agg = cg_type_lca(agg, cg_static_type_of(c, iff->else_body));
            return agg;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)expr;
            if (b->statements.count == 0) return NULL;
            return cg_static_type_of(c,
                b->statements.items[b->statements.count - 1]);
        }
        case NODE_LET_EXPR:
            return cg_static_type_of(c, ((LetExprNode*)expr)->body);
        default: return NULL;
    }
}

static LLVMValueRef emit_member_access(CodegenContext *c, MemberAccessNode *n) {
    LLVMValueRef obj = cg_emit_expr(c, n->object);

    /* Determinar el CGTypeInfo del receiver de forma estática. */
    CGTypeInfo *ti = cg_static_type_of(c, n->object);
    if (!ti && c->enclosing_type && obj == c->self_ptr)
        ti = c->enclosing_type;

    /* Buscar campo en la jerarquía: como nuestro layout incluye los
     * fields del padre al inicio, ti->field_names tiene todos. */
    if (ti) {
        for (CGTypeInfo *cur = ti; cur; cur = cur->parent) {
            for (int f = 0; f < cur->field_count; f++) {
                if (strcmp(cur->field_names[f], n->member) == 0) {
                    LLVMValueRef target_obj = obj;
                    if (cur != ti)
                        target_obj = LLVMBuildBitCast(c->builder, obj,
                                                      cur->ptr_type, "upcast");
                    LLVMValueRef gep = LLVMBuildStructGEP2(
                        c->builder, cur->struct_type, target_obj, f, n->member);
                    LLVMTypeRef field_t = LLVMStructGetTypeAtIndex(
                        cur->struct_type, f);
                    return LLVMBuildLoad2(c->builder, field_t, gep, "field");
                }
            }
        }
    }
    cg_error(c, (HulkNode*)n, "campo '%s' no encontrado", n->member);
    return LLVMConstReal(c->t_double, 0.0);
}

/* ============================================================
 *  Let: crear allocas, emitir body
 * ============================================================ */

static LLVMValueRef emit_let(CodegenContext *c, LetExprNode *n) {
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

        /* Si el init es una función (lambda), la registramos directamente
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

/* ============================================================
 *  If/elif/else — expresión (produce valor)
 * ============================================================ */

static LLVMValueRef emit_if(CodegenContext *c, IfExprNode *n) {
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

/* ============================================================
 *  While
 * ============================================================ */

static LLVMValueRef emit_while(CodegenContext *c, WhileStmtNode *n) {
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

/* ============================================================
 *  For (desugared como while con iterador)
 * ============================================================ */

static LLVMValueRef emit_for(CodegenContext *c, ForStmtNode *n) {
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
    if (!handled_as_range)
        end_val = cg_emit_expr(c, n->iterable);

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

/* ============================================================
 *  Block: emitir cada statement, retornar el último
 * ============================================================ */

static LLVMValueRef emit_block(CodegenContext *c, BlockStmtNode *n) {
    LLVMValueRef last = LLVMConstReal(c->t_double, 0.0);
    for (int i = 0; i < n->statements.count; i++)
        last = cg_emit_expr(c, n->statements.items[i]);
    return last;
}

/* ============================================================
 *  New: malloc + inicializar campos
 * ============================================================ */

static LLVMValueRef emit_new(CodegenContext *c, NewExprNode *n) {
    /* Buscar constructor generado: TypeName_new */
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->type_name);
    CGSymbol *ctor_sym = cg_lookup(c->global, ctor_name);

    if (ctor_sym && ctor_sym->value && LLVMIsAFunction(ctor_sym->value)) {
        /* Llamar al constructor generado */
        int argc = n->args.count;
        LLVMValueRef *argv = calloc(argc > 0 ? argc : 1, sizeof(LLVMValueRef));
        for (int i = 0; i < argc; i++)
            argv[i] = cg_emit_expr(c, n->args.items[i]);

        LLVMTypeRef fn_type = LLVMGlobalGetValueType(ctor_sym->value);
        LLVMValueRef result = LLVMBuildCall2(
            c->builder, fn_type, ctor_sym->value, argv, argc, "new");
        free(argv);
        return result;
    }

    /* Fallback: tipo no tiene constructor generado — malloc + init campos */
    CGTypeInfo *ti = cg_type_info_find(c, n->type_name);
    if (!ti) {
        cg_error(c, (HulkNode*)n, "tipo '%s' no registrado", n->type_name);
        return LLVMConstNull(c->t_i8ptr);
    }

    LLVMValueRef size = LLVMSizeOf(ti->struct_type);
    LLVMTypeRef malloc_params[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
    LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
    LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                       c->fn_malloc, &size, 1, "raw");
    LLVMValueRef obj = LLVMBuildBitCast(c->builder, raw, ti->ptr_type, "obj");

    int arg_count = n->args.count < ti->field_count
                    ? n->args.count : ti->field_count;
    for (int i = 0; i < arg_count; i++) {
        LLVMValueRef val = cg_emit_expr(c, n->args.items[i]);
        LLVMValueRef gep = LLVMBuildStructGEP2(
            c->builder, ti->struct_type, obj, i, "field.ptr");
        LLVMBuildStore(c->builder, val, gep);
    }
    return obj;
}

/* ============================================================
 *  Asignación
 * ============================================================ */

static LLVMValueRef emit_assign(CodegenContext *c, AssignNode *n) {
    LLVMValueRef val = cg_emit_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (sym && !sym->is_func)
            LLVMBuildStore(c->builder, val, sym->value);
        else
            cg_error(c, (HulkNode*)n, "no se puede asignar a '%s'", id->name);
    }
    return val;
}

static LLVMValueRef emit_destruct(CodegenContext *c, DestructAssignNode *n) {
    LLVMValueRef val = cg_emit_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (sym) {
            if (sym->is_func) {
                /* Reasignar función (decoradores) */
                sym->value   = val;
                sym->is_func = LLVMIsAFunction(val) ? 1 : 0;
            } else {
                LLVMBuildStore(c->builder, val, sym->value);
            }
        }
    }
    return val;
}

/* ============================================================
 *  Self
 * ============================================================ */

static LLVMValueRef emit_self(CodegenContext *c, SelfNode *n) {
    if (c->self_ptr) return c->self_ptr;
    cg_error(c, (HulkNode*)n, "'self' fuera de un tipo");
    return LLVMConstNull(c->t_i8ptr);
}
