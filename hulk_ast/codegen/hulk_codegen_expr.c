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

/* Emisores locales a este archivo (escalares y operadores). Los
 * emisores de call/oop/control viven en sus archivos y se declaran
 * en hulk_codegen_internal.h. */
static LLVMValueRef emit_ident(CodegenContext *c, IdentNode *n);
static LLVMValueRef emit_short_circuit(CodegenContext *c, BinaryOpNode *n, int is_and);
static LLVMValueRef emit_equality_op(CodegenContext *c, BinaryOpNode *n,
                                      LLVMValueRef lv, LLVMValueRef rv);
static LLVMValueRef emit_binary_op(CodegenContext *c, BinaryOpNode *n);
static LLVMValueRef emit_unary_op(CodegenContext *c, UnaryOpNode *n);
static LLVMValueRef emit_concat(CodegenContext *c, ConcatExprNode *n);

static int cg_name_has_suffix(const char *name, const char *suffix) {
    if (!name || !suffix) return 0;
    size_t ln = strlen(name), ls = strlen(suffix);
    return ln >= ls && strcmp(name + ln - ls, suffix) == 0;
}

typedef struct {
    const char **names;
    int count;
    int cap;
} NameList;

static int name_list_contains(NameList *l, const char *name) {
    if (!l || !name) return 0;
    for (int i = 0; i < l->count; i++)
        if (l->names[i] && strcmp(l->names[i], name) == 0) return 1;
    return 0;
}

static void name_list_add(NameList *l, const char *name) {
    if (!l || !name || name_list_contains(l, name)) return;
    if (l->count >= l->cap) {
        int nc = l->cap == 0 ? 8 : l->cap * 2;
        const char **tmp = realloc(l->names, sizeof(char*) * nc);
        if (!tmp) return;
        l->names = tmp;
        l->cap = nc;
    }
    l->names[l->count++] = name;
}

static void collect_lambda_captures(CodegenContext *c, HulkNode *node,
                                    NameList *params, NameList *out) {
    if (!node) return;
    switch (node->type) {
        case NODE_IDENT: {
            const char *name = ((IdentNode*)node)->name;
            if (!name_list_contains(params, name) && cg_lookup(c->current, name))
                name_list_add(out, name);
            return;
        }
        case NODE_FUNCTION_EXPR:
            return; /* las lambdas anidadas capturan por su cuenta */
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)node;
            collect_lambda_captures(c, b->left, params, out);
            collect_lambda_captures(c, b->right, params, out);
            return;
        }
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)node;
            collect_lambda_captures(c, ce->left, params, out);
            collect_lambda_captures(c, ce->right, params, out);
            return;
        }
        case NODE_UNARY_OP:
            collect_lambda_captures(c, ((UnaryOpNode*)node)->operand, params, out);
            return;
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)node;
            collect_lambda_captures(c, ce->callee, params, out);
            for (int i = 0; i < ce->args.count; i++)
                collect_lambda_captures(c, ce->args.items[i], params, out);
            return;
        }
        case NODE_MEMBER_ACCESS:
            collect_lambda_captures(c, ((MemberAccessNode*)node)->object, params, out);
            return;
        case NODE_INDEX_EXPR: {
            IndexExprNode *ix = (IndexExprNode*)node;
            collect_lambda_captures(c, ix->object, params, out);
            collect_lambda_captures(c, ix->index, params, out);
            return;
        }
        case NODE_ASSIGN: {
            AssignNode *a = (AssignNode*)node;
            collect_lambda_captures(c, a->target, params, out);
            collect_lambda_captures(c, a->value, params, out);
            return;
        }
        case NODE_DESTRUCT_ASSIGN: {
            DestructAssignNode *d = (DestructAssignNode*)node;
            collect_lambda_captures(c, d->target, params, out);
            collect_lambda_captures(c, d->value, params, out);
            return;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)node;
            NameList nested = *params;
            nested.names = NULL; nested.count = 0; nested.cap = 0;
            for (int i = 0; i < params->count; i++) name_list_add(&nested, params->names[i]);
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                collect_lambda_captures(c, vb->init_expr, &nested, out);
                name_list_add(&nested, vb->name);
            }
            collect_lambda_captures(c, l->body, &nested, out);
            free(nested.names);
            return;
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)node;
            collect_lambda_captures(c, iff->condition, params, out);
            collect_lambda_captures(c, iff->then_body, params, out);
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                collect_lambda_captures(c, e->condition, params, out);
                collect_lambda_captures(c, e->body, params, out);
            }
            collect_lambda_captures(c, iff->else_body, params, out);
            return;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)node;
            for (int i = 0; i < b->statements.count; i++)
                collect_lambda_captures(c, b->statements.items[i], params, out);
            return;
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)node;
            collect_lambda_captures(c, w->condition, params, out);
            collect_lambda_captures(c, w->body, params, out);
            return;
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)node;
            collect_lambda_captures(c, f->iterable, params, out);
            NameList nested = *params;
            nested.names = NULL; nested.count = 0; nested.cap = 0;
            for (int i = 0; i < params->count; i++) name_list_add(&nested, params->names[i]);
            name_list_add(&nested, f->var_name);
            collect_lambda_captures(c, f->body, &nested, out);
            free(nested.names);
            return;
        }
        case NODE_VECTOR_LIT: {
            VectorLitNode *v = (VectorLitNode*)node;
            for (int i = 0; i < v->items.count; i++)
                collect_lambda_captures(c, v->items.items[i], params, out);
            return;
        }
        default:
            return;
    }
}

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
            LLVMTypeRef *ptypes = calloc((argc + 1) > 0 ? (argc + 1) : 1,
                                         sizeof(LLVMTypeRef));
            ptypes[0] = c->t_i8ptr; /* closure environment */
            for (int i = 0; i < argc; i++) {
                VarBindingNode *p = (VarBindingNode*)fn_n->params.items[i];
                ptypes[i + 1] = cg_infer_param_type(c, p->type_annotation);
            }
            LLVMTypeRef ret_t = cg_infer_return_type(c, fn_n->return_type);
            LLVMTypeRef fn_type = LLVMFunctionType(ret_t, ptypes, argc + 1, 0);
            LLVMValueRef fn = LLVMAddFunction(c->module, lname, fn_type);

            typedef struct {
                const char *name;
                LLVMValueRef global;
                LLVMTypeRef type;
            } CaptureBinding;
            NameList params = {0}, cap_names = {0};
            for (int i = 0; i < argc; i++) {
                VarBindingNode *p = (VarBindingNode*)fn_n->params.items[i];
                name_list_add(&params, p->name);
            }
            for (int i = 0; i < fn_n->captures.count; i++) {
                IdentNode *cap = (IdentNode*)fn_n->captures.items[i];
                if (cap) name_list_add(&cap_names, cap->name);
            }
            collect_lambda_captures(c, fn_n->body, &params, &cap_names);

            CaptureBinding *caps = NULL;
            int cap_count = cap_names.count;
            if (cap_count > 0)
                caps = calloc(cap_count, sizeof(CaptureBinding));
            for (int i = 0; i < cap_count; i++) {
                const char *cap_name = cap_names.names[i];
                CGSymbol *sym = cg_lookup(c->current, cap_name);
                if (!sym) continue;
                LLVMValueRef cap_val = sym->is_func
                    ? sym->value
                    : LLVMBuildLoad2(c->builder, sym->type, sym->value, "cap");
                LLVMTypeRef cap_t = LLVMTypeOf(cap_val);
                caps[i].name = cap_name;
                caps[i].global = cap_val;
                caps[i].type = cap_t;
            }

            LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
            LLVMValueRef bytes = LLVMConstInt(i64, 8 * (cap_count + 1), 0);
            LLVMTypeRef malloc_params[1] = { i64 };
            LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
            LLVMValueRef closure = LLVMBuildCall2(c->builder, malloc_ft,
                                                  c->fn_malloc, &bytes, 1,
                                                  "closure");
            LLVMBuildStore(c->builder, fn, closure);
            for (int i = 0; i < cap_count; i++) {
                if (!caps || !caps[i].name) continue;
                LLVMValueRef offset = LLVMConstInt(i64, 8 * (i + 1), 0);
                LLVMValueRef slot = LLVMBuildInBoundsGEP2(
                    c->builder, LLVMInt8TypeInContext(c->llvm_ctx),
                    closure, &offset, 1, "closure.cap.slot");
                LLVMBuildStore(c->builder, caps[i].global, slot);
            }

            /* Emitir el cuerpo */
            LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(c->builder);
            LLVMValueRef saved_fn = c->current_fn;
            c->current_fn = fn;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, fn, "entry");
            LLVMPositionBuilderAtEnd(c->builder, entry);

            CGScope *saved_scope_for_lambda = c->current;
            c->current = c->global;
            cg_push_scope(c);
            LLVMValueRef env = LLVMGetParam(fn, 0);
            for (int i = 0; i < argc; i++) {
                VarBindingNode *p = (VarBindingNode*)fn_n->params.items[i];
                LLVMValueRef pv = LLVMGetParam(fn, i + 1);
                LLVMTypeRef pt = LLVMTypeOf(pv);
                LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
                LLVMBuildStore(c->builder, pv, alloca);
                cg_define(c, p->name, alloca, pt, 0);
            }
            for (int i = 0; i < cap_count; i++) {
                if (caps && caps[i].name) {
                    LLVMValueRef offset = LLVMConstInt(i64, 8 * (i + 1), 0);
                    LLVMValueRef slot = LLVMBuildInBoundsGEP2(
                        c->builder, LLVMInt8TypeInContext(c->llvm_ctx),
                        env, &offset, 1, "closure.cap.load.slot");
                    LLVMValueRef cap_loaded = LLVMBuildLoad2(c->builder,
                        caps[i].type, slot, "closure.cap");
                    LLVMValueRef alloca = cg_create_entry_alloca(c, caps[i].type,
                                                                  caps[i].name);
                    LLVMBuildStore(c->builder, cap_loaded, alloca);
                    cg_define(c, caps[i].name, alloca, caps[i].type, 0);
                }
            }

            LLVMValueRef body_val = cg_emit_expr(c, fn_n->body);
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
            if (!LLVMGetBasicBlockTerminator(cur_bb)) {
                if (ret_t == c->t_void) LLVMBuildRetVoid(c->builder);
                else LLVMBuildRet(c->builder, body_val);
            }
            cg_pop_scope(c);
            c->current = saved_scope_for_lambda;

            c->current_fn = saved_fn;
            if (saved_bb) LLVMPositionBuilderAtEnd(c->builder, saved_bb);
            free(caps);
            free(params.names);
            free(cap_names.names);
            free(ptypes);
            return closure;
        }
        case NODE_BINARY_OP:       return emit_binary_op(c, (BinaryOpNode*)node);
        case NODE_UNARY_OP:        return emit_unary_op(c, (UnaryOpNode*)node);
        case NODE_CONCAT_EXPR:     return emit_concat(c, (ConcatExprNode*)node);
        case NODE_CALL_EXPR:       return cg_emit_call(c, (CallExprNode*)node);
        case NODE_MEMBER_ACCESS:   return cg_emit_member_access(c, (MemberAccessNode*)node);
        case NODE_LET_EXPR:        return cg_emit_let(c, (LetExprNode*)node);
        case NODE_IF_EXPR:         return cg_emit_if(c, (IfExprNode*)node);
        case NODE_WHILE_STMT:      return cg_emit_while(c, (WhileStmtNode*)node);
        case NODE_FOR_STMT:        return cg_emit_for(c, (ForStmtNode*)node);
        case NODE_BLOCK_STMT:      return cg_emit_block(c, (BlockStmtNode*)node);
        case NODE_NEW_EXPR:        return cg_emit_new(c, (NewExprNode*)node);
        case NODE_ASSIGN:          return cg_emit_assign(c, (AssignNode*)node);
        case NODE_DESTRUCT_ASSIGN: return cg_emit_destruct(c, (DestructAssignNode*)node);
        case NODE_SELF:            return cg_emit_self(c, (SelfNode*)node);
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
            LLVMTypeRef elem_t = c->t_double;
            if (cg_name_has_suffix(node->static_type, "[]") ||
                (node->static_type && strcmp(node->static_type, "Object") == 0))
                elem_t = c->t_i8ptr;
            return LLVMBuildLoad2(c->builder, elem_t, gep, "elem");
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

static int is_static_string(HulkNode *n) {
    return n && n->static_type && strcmp(n->static_type, "String") == 0;
}

static LLVMValueRef emit_equality_op(CodegenContext *c, BinaryOpNode *n,
                                      LLVMValueRef lv, LLVMValueRef rv) {
    int is_eq = (n->op == OP_EQ);
    LLVMTypeRef lt = LLVMTypeOf(lv);
    LLVMTypeRef rt = LLVMTypeOf(rv);

    if (is_static_string(n->left) && is_static_string(n->right)) {
        LLVMValueRef args[2] = { lv, rv };
        LLVMTypeRef strcmp_params[2] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef strcmp_ft = LLVMFunctionType(c->t_i32,
                                                  strcmp_params, 2, 0);
        LLVMValueRef cmp = LLVMBuildCall2(c->builder, strcmp_ft,
                                          c->fn_strcmp, args, 2, "strcmp");
        LLVMIntPredicate pred = is_eq ? LLVMIntEQ : LLVMIntNE;
        return LLVMBuildICmp(c->builder, pred, cmp,
                             LLVMConstInt(c->t_i32, 0, 0),
                             is_eq ? "streq" : "strneq");
    }

    if (lt == c->t_double && rt == c->t_double) {
        LLVMRealPredicate pred = is_eq ? LLVMRealOEQ : LLVMRealUNE;
        return LLVMBuildFCmp(c->builder, pred, lv, rv,
                             is_eq ? "eq" : "neq");
    }

    if (LLVMGetTypeKind(lt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(rt) == LLVMIntegerTypeKind) {
        LLVMIntPredicate pred = is_eq ? LLVMIntEQ : LLVMIntNE;
        return LLVMBuildICmp(c->builder, pred, lv, rv,
                             is_eq ? "eq" : "neq");
    }

    if (LLVMGetTypeKind(lt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(rt) == LLVMPointerTypeKind) {
        LLVMIntPredicate pred = is_eq ? LLVMIntEQ : LLVMIntNE;
        return LLVMBuildICmp(c->builder, pred, lv, rv,
                             is_eq ? "ptreq" : "ptrneq");
    }

    return LLVMConstInt(c->t_bool, is_eq ? 0 : 1, 0);
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
        case OP_EQ:
        case OP_NEQ:
            return emit_equality_op(c, n, lv, rv);

        /* OP_AND / OP_OR handled above via short-circuit */
        default: break;
    }
    return LLVMConstReal(c->t_double, 0.0);
}

static LLVMValueRef emit_unary_op(CodegenContext *c, UnaryOpNode *n) {
    LLVMValueRef v = cg_emit_expr(c, n->operand);
    if (n->is_not) {
        /* not lógico: si el operando es i1, XOR con true; si es double
         * (por inferencia laxa), comparar == 0.0. */
        LLVMTypeRef vt = LLVMTypeOf(v);
        if (vt == c->t_bool)
            return LLVMBuildXor(c->builder, v,
                                LLVMConstInt(c->t_bool, 1, 0), "not");
        if (vt == c->t_double)
            return LLVMBuildFCmp(c->builder, LLVMRealOEQ, v,
                                 LLVMConstReal(c->t_double, 0.0), "not");
        return v;
    }
    return LLVMBuildFNeg(c->builder, v, "neg");
}

static LLVMValueRef emit_concat(CodegenContext *c, ConcatExprNode *n) {
    LLVMValueRef ls = cg_emit_to_string(c, n->left);
    LLVMValueRef rs = cg_emit_to_string(c, n->right);

    LLVMValueRef fn = (n->op == OP_CONCAT_WS) ?
        c->fn_hulk_concat_ws : c->fn_hulk_concat;
    LLVMValueRef args[2] = { ls, rs };

    LLVMTypeRef concat_params[2] = { c->t_i8ptr, c->t_i8ptr };
    LLVMTypeRef concat_ft = LLVMFunctionType(c->t_i8ptr, concat_params, 2, 0);
    return LLVMBuildCall2(c->builder, concat_ft, fn, args, 2, "concat");
}
