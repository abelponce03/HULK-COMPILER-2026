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
static LLVMValueRef emit_function_expr(CodegenContext *c, FunctionExprNode *n);

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
        case NODE_FUNCTION_EXPR:   return emit_function_expr(c, (FunctionExprNode*)node);
        case NODE_AS_EXPR: {
            /* as es un no-op en IR (el tipo estático cambia, no el runtime) */
            AsExprNode *n = (AsExprNode*)node;
            return cg_emit_expr(c, n->expr);
        }
        case NODE_IS_EXPR: {
            /* is: comprueba type tag contra el tag del tipo objetivo */
            IsExprNode *n = (IsExprNode*)node;
            LLVMValueRef val = cg_emit_expr(c, n->expr);
            CGTypeInfo *target_ti = cg_type_info_find(c, n->type_name);
            if (!target_ti) {
                /* Tipo no registrado como user type — check LLVM type */
                LLVMTypeRef vt = LLVMTypeOf(val);
                int result = 0;
                if (n->type_name) {
                    if (strcmp(n->type_name, "Number") == 0)
                        result = (vt == c->t_double);
                    else if (strcmp(n->type_name, "String") == 0)
                        result = (vt == c->t_i8ptr);
                    else if (strcmp(n->type_name, "Boolean") == 0)
                        result = (vt == c->t_bool);
                    else result = 1; /* Object matches everything */
                }
                return LLVMConstInt(c->t_bool, result ? 1 : 0, 0);
            }
            /* For user types: check if the LLVM ptr type matches any
             * type in the inheritance chain of the target */
            LLVMTypeRef vt = LLVMTypeOf(val);
            int match = 0;
            for (CGTypeInfo *ti = target_ti; ti; ti = ti->parent) {
                if (vt == ti->ptr_type) { match = 1; break; }
            }
            /* Also check if val's type conforms to target (child is target) */
            if (!match) {
                for (int i = 0; i < c->type_info_count; i++) {
                    CGTypeInfo *vti = c->type_infos[i];
                    if (vt == vti->ptr_type) {
                        /* Walk vti's parents to see if target_ti is ancestor */
                        for (CGTypeInfo *p = vti; p; p = p->parent) {
                            if (p == target_ti) { match = 1; break; }
                        }
                        break;
                    }
                }
            }
            return LLVMConstInt(c->t_bool, match ? 1 : 0, 0);
        }
        case NODE_BASE_CALL: {
            /* base() — llamar al constructor padre */
            BaseCallNode *bn = (BaseCallNode*)node;
            if (c->enclosing_type && c->enclosing_type->parent) {
                char ctor_name[256];
                snprintf(ctor_name, sizeof(ctor_name), "%s_new",
                         c->enclosing_type->parent->name);
                CGSymbol *psym = cg_lookup(c->global, ctor_name);
                if (psym && psym->value) {
                    int argc = bn->args.count;
                    LLVMValueRef *argv = calloc(argc > 0 ? argc : 1,
                                                sizeof(LLVMValueRef));
                    for (int i = 0; i < argc; i++)
                        argv[i] = cg_emit_expr(c, bn->args.items[i]);
                    LLVMTypeRef fn_type = LLVMGlobalGetValueType(psym->value);
                    LLVMValueRef result = LLVMBuildCall2(
                        c->builder, fn_type, psym->value, argv, argc, "base");
                    free(argv);
                    return result;
                }
            }
            /* Fallback si no hay padre */
            cg_error(c, node, "base() sin tipo padre válido");
            return LLVMConstReal(c->t_double, 0.0);
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
    
    if (sym->is_func) {
        /* Es una función global — empaquetar en struct Closure */
        LLVMValueRef null_env = LLVMConstNull(c->t_i8ptr);
        LLVMValueRef fn_ptr_cast = LLVMBuildBitCast(c->builder, sym->value, c->t_i8ptr, "fncast");
        LLVMValueRef closure_v = LLVMGetUndef(c->t_closure);
        closure_v = LLVMBuildInsertValue(c->builder, closure_v, fn_ptr_cast, 0, "c1");
        closure_v = LLVMBuildInsertValue(c->builder, closure_v, null_env, 1, "c2");

        LLVMValueRef size = LLVMSizeOf(c->t_closure);
        LLVMTypeRef mparams[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
        LLVMTypeRef m_ft = LLVMFunctionType(c->t_i8ptr, mparams, 1, 0);
        LLVMValueRef raw = LLVMBuildCall2(c->builder, m_ft, c->fn_malloc, &size, 1, "raw");
        LLVMValueRef ptr = LLVMBuildBitCast(c->builder, raw, c->t_closure_ptr, "clos.ptr");
        LLVMBuildStore(c->builder, closure_v, ptr);
        return ptr;
    }

    /* Variable: cargar desde alloca / ptr guardado */
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

static LLVMValueRef emit_call(CodegenContext *c, CallExprNode *n) {
    /* Caso 1: callee es identificador de función global */
    if (n->callee->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->callee;
        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (!sym) {
            cg_error(c, (HulkNode*)n, "función '%s' no definida", id->name);
            return LLVMConstReal(c->t_double, 0.0);
        }

        if (sym->is_func) {
            /* Llamada directa a función global (env_ptr = NULL) */
            int argc = n->args.count + 1; /* +1 para env_ptr */
            LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
            LLVMTypeRef  *argt = calloc(argc, sizeof(LLVMTypeRef));
            
            argv[0] = LLVMConstNull(c->t_i8ptr);
            argt[0] = c->t_i8ptr;
            
            for (int i = 0; i < n->args.count; i++) {
                argv[i + 1] = cg_emit_expr(c, n->args.items[i]);
                argt[i + 1] = LLVMTypeOf(argv[i + 1]);
            }

            LLVMTypeRef fn_type = LLVMGlobalGetValueType(sym->value);
            LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
            const char *call_name = (ret_type == c->t_void) ? "" : "call";
            LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                                  sym->value, argv, argc, call_name);
            free(argv);
            free(argt);
            return result;
        }
    }

    /* Caso 2: callee es member access → llamada a método (sin cambios, usa self) */
    if (n->callee->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)n->callee;
        LLVMValueRef obj = cg_emit_expr(c, ma->object);

        /* Descubrir el tipo del objeto por su LLVMTypeRef */
        LLVMTypeRef obj_type = LLVMTypeOf(obj);
        CGTypeInfo *ti = NULL;
        for (int i = 0; i < c->type_info_count; i++) {
            if (c->type_infos[i]->ptr_type == obj_type) {
                ti = c->type_infos[i];
                break;
            }
        }
        if (!ti && c->enclosing_type && obj == c->self_ptr)
            ti = c->enclosing_type;

        /* Buscar método caminando herencia */
        char fname[256];
        CGSymbol *msym = NULL;
        if (ti) {
            for (CGTypeInfo *cur = ti; cur && !msym; cur = cur->parent) {
                snprintf(fname, sizeof(fname), "%s_%s", cur->name, ma->member);
                msym = cg_lookup(c->global, fname);
            }
        }
        if (!msym) {
            /* Fallback: buscar por nombre plano */
            snprintf(fname, sizeof(fname), "%s", ma->member);
            msym = cg_lookup(c->global, fname);
        }

        /* Build args: self + user args */
        int argc = n->args.count + 1;
        LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
        argv[0] = obj;
        for (int i = 0; i < n->args.count; i++)
            argv[i + 1] = cg_emit_expr(c, n->args.items[i]);

        LLVMValueRef fn_val;
        LLVMTypeRef  fn_type;
        if (msym && msym->value) {
            fn_val  = msym->value;
            fn_type = LLVMGlobalGetValueType(fn_val);
        } else {
            /* Fallback: construir tipo genérico */
            LLVMTypeRef *argt = calloc(argc, sizeof(LLVMTypeRef));
            for (int i = 0; i < argc; i++)
                argt[i] = LLVMTypeOf(argv[i]);
            fn_type = LLVMFunctionType(c->t_double, argt, argc, 0);
            free(argt);
            cg_error(c, (HulkNode*)n, "método '%s' no encontrado", fname);
            free(argv);
            return LLVMConstReal(c->t_double, 0.0);
        }

        LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
        const char *call_name = (ret_type == c->t_void) ? "" : "mcall";
        LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                              fn_val, argv, argc, call_name);
        free(argv);
        return result;
    }

    /* Caso 3: callee es una variable / expresión -> Closure pointer */
    LLVMValueRef closure_ptr = cg_emit_expr(c, n->callee);
    
    /* GEP fn_ptr (index 0) y env_ptr (index 1) */
    LLVMValueRef fn_ptr_gep = LLVMBuildStructGEP2(c->builder, c->t_closure, closure_ptr, 0, "fn.gep");
    LLVMValueRef fn_ptr_i8 = LLVMBuildLoad2(c->builder, c->t_i8ptr, fn_ptr_gep, "fn.i8");
    
    LLVMValueRef env_ptr_gep = LLVMBuildStructGEP2(c->builder, c->t_closure, closure_ptr, 1, "env.gep");
    LLVMValueRef env_ptr = LLVMBuildLoad2(c->builder, c->t_i8ptr, env_ptr_gep, "env.ptr");

    int argc = n->args.count + 1;
    LLVMValueRef *argv = calloc(argc, sizeof(LLVMValueRef));
    LLVMTypeRef  *argt = calloc(argc, sizeof(LLVMTypeRef));
    
    argv[0] = env_ptr;
    argt[0] = c->t_i8ptr;
    
    for (int i = 0; i < n->args.count; i++) {
        argv[i + 1] = cg_emit_expr(c, n->args.items[i]);
        argt[i + 1] = LLVMTypeOf(argv[i + 1]);
    }
    
    /* Construir signature a partir de argumentos pasados (HULK = dynamic)
       NOTA: En HULK real, inferiríamos tipos, pero por defecto asume double */
    LLVMTypeRef fn_type = LLVMFunctionType(c->t_double, argt, argc, 0);
    /* Castear el puntero crudo a función tipada */
    LLVMTypeRef fn_ptr_t = LLVMPointerType(fn_type, 0);
    LLVMValueRef fn_val = LLVMBuildBitCast(c->builder, fn_ptr_i8, fn_ptr_t, "fn.cast");

    LLVMValueRef result = LLVMBuildCall2(c->builder, fn_type,
                                          fn_val, argv, argc, "gcall");
    free(argv);
    free(argt);
    return result;
}

/* ============================================================
 *  Acceso a miembro: obj.field
 * ============================================================ */

static LLVMValueRef emit_member_access(CodegenContext *c, MemberAccessNode *n) {
    LLVMValueRef obj = cg_emit_expr(c, n->object);
    LLVMTypeRef obj_type = LLVMTypeOf(obj);

    /* Buscar el CGTypeInfo que corresponde al tipo del objeto */
    CGTypeInfo *ti = NULL;
    for (int t = 0; t < c->type_info_count; t++) {
        if (c->type_infos[t]->ptr_type == obj_type) {
            ti = c->type_infos[t];
            break;
        }
    }
    /* Si estamos dentro de un tipo y obj es self, usar enclosing_type */
    if (!ti && c->enclosing_type && obj == c->self_ptr)
        ti = c->enclosing_type;

    /* Buscar campo caminando la cadena de herencia */
    if (ti) {
        for (CGTypeInfo *cur = ti; cur; cur = cur->parent) {
            for (int f = 0; f < cur->field_count; f++) {
                if (strcmp(cur->field_names[f], n->member) == 0) {
                    /* Si cur != ti, necesitamos cast a la struct padre */
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
        LLVMValueRef init_val = vb->init_expr
            ? cg_emit_expr(c, vb->init_expr)
            : LLVMConstReal(c->t_double, 0.0);

        LLVMTypeRef val_type = LLVMTypeOf(init_val);
        LLVMValueRef alloca = cg_create_entry_alloca(c, val_type, vb->name);
        LLVMBuildStore(c->builder, init_val, alloca);
        cg_define(c, vb->name, alloca, val_type, 0);
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
    /* Store body result if it's a double */
    if (LLVMTypeOf(body_val) == c->t_double)
        LLVMBuildStore(c->builder, body_val, result_ptr);
    LLVMBuildBr(c->builder, cond_bb);

    /* End */
    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    return LLVMBuildLoad2(c->builder, c->t_double, result_ptr, "while.res");
}

/* ============================================================
 *  For (desugared como while con iterador)
 * ============================================================ */

static LLVMValueRef emit_for(CodegenContext *c, ForStmtNode *n) {
    /* Simple approach: for (x in iterable) body
     * Tratamos iterable como un Number (range), iteramos 0..iterable-1 */
    cg_push_scope(c);

    LLVMValueRef iter_val = cg_emit_expr(c, n->iterable);
    LLVMValueRef counter = cg_create_entry_alloca(c, c->t_double, n->var_name);
    LLVMBuildStore(c->builder, LLVMConstReal(c->t_double, 0.0), counter);
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

    /* Condition: counter < iterable */
    LLVMPositionBuilderAtEnd(c->builder, cond_bb);
    LLVMValueRef cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "cur");
    LLVMValueRef cond = LLVMBuildFCmp(c->builder, LLVMRealOLT,
                                       cur, iter_val, "forcond");
    LLVMBuildCondBr(c->builder, cond, body_bb, end_bb);

    /* Body */
    LLVMPositionBuilderAtEnd(c->builder, body_bb);
    LLVMValueRef body_val = cg_emit_expr(c, n->body);
    if (LLVMTypeOf(body_val) == c->t_double)
        LLVMBuildStore(c->builder, body_val, result_ptr);
    /* Increment counter */
    cur = LLVMBuildLoad2(c->builder, c->t_double, counter, "cur");
    LLVMValueRef next = LLVMBuildFAdd(c->builder, cur,
                                       LLVMConstReal(c->t_double, 1.0), "inc");
    LLVMBuildStore(c->builder, next, counter);
    LLVMBuildBr(c->builder, cond_bb);

    LLVMPositionBuilderAtEnd(c->builder, end_bb);
    cg_pop_scope(c);
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

/* ============================================================
 *  Function Expression (Closures)
 * ============================================================ */

static LLVMValueRef emit_function_expr(CodegenContext *c, FunctionExprNode *n) {
    /* 1. Malloc environment struct and store captured variables */
    int cap_count = n->captured_vars.count;
    LLVMTypeRef *cap_types = NULL;
    LLVMValueRef *cap_vals = NULL;
    LLVMTypeRef env_struct_ty = c->t_i8ptr;

    if (cap_count > 0) {
        cap_types = calloc(cap_count, sizeof(LLVMTypeRef));
        cap_vals = calloc(cap_count, sizeof(LLVMValueRef));
        for (int i = 0; i < cap_count; i++) {
            VarBindingNode *vb = (VarBindingNode*)n->captured_vars.items[i];
            CGSymbol *sym = cg_lookup(c->current, vb->name);
            if (!sym) {
                cg_error(c, (HulkNode*)vb, "variable capturada '%s' no encontrada", vb->name);
                cap_types[i] = c->t_double;
                cap_vals[i] = LLVMConstReal(c->t_double, 0.0);
                continue;
            }
            cap_types[i] = sym->type;
            if (sym->is_func) {
                /* Función global capturada — la empaquetamos */
                LLVMValueRef null_env = LLVMConstNull(c->t_i8ptr);
                LLVMValueRef fn_ptr_cast = LLVMBuildBitCast(c->builder, sym->value, c->t_i8ptr, "fncast");
                LLVMValueRef closure_v = LLVMGetUndef(c->t_closure);
                closure_v = LLVMBuildInsertValue(c->builder, closure_v, fn_ptr_cast, 0, "c1");
                closure_v = LLVMBuildInsertValue(c->builder, closure_v, null_env, 1, "c2");

                LLVMValueRef size = LLVMSizeOf(c->t_closure);
                LLVMTypeRef mparams[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
                LLVMTypeRef m_ft = LLVMFunctionType(c->t_i8ptr, mparams, 1, 0);
                LLVMValueRef raw = LLVMBuildCall2(c->builder, m_ft, c->fn_malloc, &size, 1, "raw");
                LLVMValueRef ptr = LLVMBuildBitCast(c->builder, raw, c->t_closure_ptr, "clos.ptr");
                LLVMBuildStore(c->builder, closure_v, ptr);
                
                cap_types[i] = c->t_closure_ptr;
                cap_vals[i] = ptr;
            } else {
                cap_vals[i] = LLVMBuildLoad2(c->builder, sym->type, sym->value, "cap.val");
            }
        }
        env_struct_ty = LLVMStructTypeInContext(c->llvm_ctx, cap_types, cap_count, 0);
    }

    LLVMValueRef typed_env = NULL;
    LLVMValueRef raw_env = LLVMConstNull(c->t_i8ptr);

    if (cap_count > 0) {
        LLVMValueRef env_size = LLVMSizeOf(env_struct_ty);
        LLVMTypeRef malloc_p = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, &malloc_p, 1, 0);
        raw_env = LLVMBuildCall2(c->builder, malloc_ft, c->fn_malloc, &env_size, 1, "raw.env");
        typed_env = LLVMBuildBitCast(c->builder, raw_env, LLVMPointerType(env_struct_ty, 0), "env.ptr");

        for (int i = 0; i < cap_count; i++) {
            LLVMValueRef gep = LLVMBuildStructGEP2(c->builder, env_struct_ty, typed_env, i, "env.gep");
            LLVMBuildStore(c->builder, cap_vals[i], gep);
        }
    }

    /* 2. Create inner function */
    int argc = n->params.count + 1;
    LLVMTypeRef *param_types = calloc(argc, sizeof(LLVMTypeRef));
    param_types[0] = c->t_i8ptr;
    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        param_types[i + 1] = cg_infer_param_type(c, p->type_annotation);
    }

    LLVMTypeRef ret_t = cg_infer_return_type(c, n->return_type);
    LLVMTypeRef fn_type = LLVMFunctionType(ret_t, param_types, argc, 0);
    
    static int anon_count = 0;
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "__anon_fn_%d", ++anon_count);
    LLVMValueRef fn = LLVMAddFunction(c->module, fn_name, fn_type);

    /* 3. Emit inner function body */
    LLVMValueRef saved_fn = c->current_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(c->builder);

    c->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c->llvm_ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(c->builder, entry);

    cg_push_scope(c);

    LLVMValueRef env_param = LLVMGetParam(fn, 0);

    /* Unpack environment onto local allocas */
    if (cap_count > 0) {
        LLVMValueRef typed_env_arg = LLVMBuildBitCast(c->builder, env_param, LLVMPointerType(env_struct_ty, 0), "env.cast");
        for (int i = 0; i < cap_count; i++) {
            VarBindingNode *vb = (VarBindingNode*)n->captured_vars.items[i];
            LLVMValueRef gep = LLVMBuildStructGEP2(c->builder, env_struct_ty, typed_env_arg, i, "env.gep");
            LLVMValueRef val = LLVMBuildLoad2(c->builder, cap_types[i], gep, "env.val");
            
            LLVMValueRef alloca = cg_create_entry_alloca(c, cap_types[i], vb->name);
            LLVMBuildStore(c->builder, val, alloca);
            cg_define(c, vb->name, alloca, cap_types[i], 0);
        }
    }

    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        LLVMValueRef param_val = LLVMGetParam(fn, i + 1);
        LLVMTypeRef  param_t   = LLVMTypeOf(param_val);
        LLVMValueRef alloca = cg_create_entry_alloca(c, param_t, p->name);
        LLVMBuildStore(c->builder, param_val, alloca);
        cg_define(c, p->name, alloca, param_t, 0);
    }

    LLVMValueRef body_val = cg_emit_expr(c, n->body);

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
    if (!LLVMGetBasicBlockTerminator(cur_bb)) {
        if (ret_t == c->t_void) LLVMBuildRetVoid(c->builder);
        else LLVMBuildRet(c->builder, body_val);
    }

    cg_pop_scope(c);

    c->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(c->builder, saved_bb);

    if (cap_types) { free(cap_types); free(cap_vals); }
    free(param_types);

    /* 4. Build return Closure struct */
    LLVMValueRef fn_ptr_cast = LLVMBuildBitCast(c->builder, fn, c->t_i8ptr, "fncast");
    LLVMValueRef closure_v = LLVMGetUndef(c->t_closure);
    closure_v = LLVMBuildInsertValue(c->builder, closure_v, fn_ptr_cast, 0, "c1");
    closure_v = LLVMBuildInsertValue(c->builder, closure_v, raw_env, 1, "c2");

    LLVMValueRef size = LLVMSizeOf(c->t_closure);
    LLVMTypeRef mparams[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
    LLVMTypeRef m_ft = LLVMFunctionType(c->t_i8ptr, mparams, 1, 0);
    LLVMValueRef raw_cls = LLVMBuildCall2(c->builder, m_ft, c->fn_malloc, &size, 1, "raw.cls");
    LLVMValueRef ptr_cls = LLVMBuildBitCast(c->builder, raw_cls, c->t_closure_ptr, "clos.ptr");
    LLVMBuildStore(c->builder, closure_v, ptr_cls);

    return ptr_cls;
}
