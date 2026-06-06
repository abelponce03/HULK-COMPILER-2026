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
static void emit_rtti_globals(CodegenContext *c);

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

    /* ---- Pasada 1.5: Construir vtables y tablas RTTI ----
     * Todas las funciones método ya están forward-declared y todos los
     * slots globales ya están asignados; podemos llenar las vtables. */
    emit_rtti_globals(c);

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

/* Heurística para inferir el LLVMTypeRef de retorno de una función a
 * partir de su body. Cubre los casos comunes:
 *   - CallExpr a "print" → void
 *   - StringLit / ConcatExpr → i8*
 *   - BoolLit / comparadores / lógicos → i1
 *   - NumberLit / aritmética → double
 *   - BlockStmt / LetExpr / IfExpr → recursión sobre el / los body(ies)
 *   - new T(...) → T_ptr
 * Retorna NULL si no se puede determinar (caller deja default = double). */
static LLVMTypeRef infer_body_return_type(CodegenContext *c, HulkNode *body) {
    if (!body) return NULL;
    switch (body->type) {
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)body;
            if (ce->callee && ce->callee->type == NODE_IDENT) {
                IdentNode *id = (IdentNode*)ce->callee;
                if (id->name && strcmp(id->name, "print") == 0)
                    return c->t_void;
            }
            return NULL;
        }
        case NODE_STRING_LIT: return c->t_i8ptr;
        case NODE_CONCAT_EXPR: return c->t_i8ptr;
        case NODE_BOOL_LIT: return c->t_bool;
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)body;
            switch (b->op) {
                case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                case OP_EQ: case OP_NEQ: case OP_AND: case OP_OR:
                    return c->t_bool;
                default: return c->t_double;
            }
        }
        case NODE_NUMBER_LIT: return c->t_double;
        case NODE_NEW_EXPR: {
            NewExprNode *ne = (NewExprNode*)body;
            CGTypeInfo *ti = cg_type_info_find(c, ne->type_name);
            return ti ? ti->ptr_type : NULL;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)body;
            if (b->statements.count == 0) return NULL;
            return infer_body_return_type(c,
                b->statements.items[b->statements.count - 1]);
        }
        case NODE_LET_EXPR:
            return infer_body_return_type(c, ((LetExprNode*)body)->body);
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)body;
            LLVMTypeRef t = infer_body_return_type(c, iff->then_body);
            if (iff->else_body) {
                LLVMTypeRef te = infer_body_return_type(c, iff->else_body);
                if (t != te) return NULL;
            }
            return t;
        }
        case NODE_BASE_CALL: return NULL;
        default: return NULL;
    }
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

/* Walker mínimo: ¿el nombre `member` aparece como `self.member` en
 * algún operador `@`/`@@` dentro de los método-bodies del TypeDef?
 * Si sí → t_i8ptr (string). Análogo para aritmético → t_double. */
static int self_member_used_as_string(HulkNode *n, const char *member) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            if (ce->left && ce->left->type == NODE_MEMBER_ACCESS) {
                MemberAccessNode *ma = (MemberAccessNode*)ce->left;
                if (ma->object && ma->object->type == NODE_SELF &&
                    ma->member && strcmp(ma->member, member) == 0)
                    return 1;
            }
            if (ce->right && ce->right->type == NODE_MEMBER_ACCESS) {
                MemberAccessNode *ma = (MemberAccessNode*)ce->right;
                if (ma->object && ma->object->type == NODE_SELF &&
                    ma->member && strcmp(ma->member, member) == 0)
                    return 1;
            }
            return self_member_used_as_string(ce->left, member) ||
                   self_member_used_as_string(ce->right, member);
        }
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            return self_member_used_as_string(b->left, member) ||
                   self_member_used_as_string(b->right, member);
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            if (self_member_used_as_string(iff->condition, member)) return 1;
            if (self_member_used_as_string(iff->then_body, member)) return 1;
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                if (self_member_used_as_string(e->condition, member)) return 1;
                if (self_member_used_as_string(e->body, member)) return 1;
            }
            return self_member_used_as_string(iff->else_body, member);
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            for (int i = 0; i < b->statements.count; i++)
                if (self_member_used_as_string(b->statements.items[i], member))
                    return 1;
            return 0;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                if (self_member_used_as_string(vb->init_expr, member)) return 1;
            }
            return self_member_used_as_string(l->body, member);
        }
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            for (int i = 0; i < ce->args.count; i++)
                if (self_member_used_as_string(ce->args.items[i], member))
                    return 1;
            return 0;
        }
        default: return 0;
    }
}

/* Walker que busca `new TypeName(...)` en cualquier subárbol y, si
 * el (param_idx)-ésimo argumento es un StringLit, retorna 1. */
static int new_call_uses_string_arg(HulkNode *n, const char *type_name,
                                     int param_idx) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_NEW_EXPR: {
            NewExprNode *ne = (NewExprNode*)n;
            if (ne->type_name && type_name &&
                strcmp(ne->type_name, type_name) == 0 &&
                param_idx < ne->args.count) {
                HulkNode *a = ne->args.items[param_idx];
                if (a && a->type == NODE_STRING_LIT) return 1;
            }
            for (int i = 0; i < ne->args.count; i++)
                if (new_call_uses_string_arg(ne->args.items[i], type_name,
                                              param_idx)) return 1;
            return 0;
        }
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            return new_call_uses_string_arg(b->left, type_name, param_idx) ||
                   new_call_uses_string_arg(b->right, type_name, param_idx);
        }
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            return new_call_uses_string_arg(ce->left, type_name, param_idx) ||
                   new_call_uses_string_arg(ce->right, type_name, param_idx);
        }
        case NODE_UNARY_OP:
            return new_call_uses_string_arg(((UnaryOpNode*)n)->operand,
                                             type_name, param_idx);
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            if (new_call_uses_string_arg(ce->callee, type_name, param_idx))
                return 1;
            for (int i = 0; i < ce->args.count; i++)
                if (new_call_uses_string_arg(ce->args.items[i], type_name,
                                              param_idx)) return 1;
            return 0;
        }
        case NODE_MEMBER_ACCESS:
            return new_call_uses_string_arg(((MemberAccessNode*)n)->object,
                                             type_name, param_idx);
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                if (new_call_uses_string_arg(vb->init_expr, type_name,
                                              param_idx)) return 1;
            }
            return new_call_uses_string_arg(l->body, type_name, param_idx);
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            if (new_call_uses_string_arg(iff->condition, type_name,
                                          param_idx)) return 1;
            if (new_call_uses_string_arg(iff->then_body, type_name,
                                          param_idx)) return 1;
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                if (new_call_uses_string_arg(e->condition, type_name,
                                              param_idx)) return 1;
                if (new_call_uses_string_arg(e->body, type_name,
                                              param_idx)) return 1;
            }
            return new_call_uses_string_arg(iff->else_body, type_name,
                                             param_idx);
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            for (int i = 0; i < b->statements.count; i++)
                if (new_call_uses_string_arg(b->statements.items[i],
                                              type_name, param_idx)) return 1;
            return 0;
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return new_call_uses_string_arg(w->condition, type_name,
                                             param_idx) ||
                   new_call_uses_string_arg(w->body, type_name, param_idx);
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)n;
            return new_call_uses_string_arg(f->iterable, type_name,
                                             param_idx) ||
                   new_call_uses_string_arg(f->body, type_name, param_idx);
        }
        case NODE_ASSIGN:
            return new_call_uses_string_arg(((AssignNode*)n)->value,
                                             type_name, param_idx);
        case NODE_DESTRUCT_ASSIGN:
            return new_call_uses_string_arg(((DestructAssignNode*)n)->value,
                                             type_name, param_idx);
        case NODE_PROGRAM: {
            ProgramNode *p = (ProgramNode*)n;
            for (int i = 0; i < p->declarations.count; i++)
                if (new_call_uses_string_arg(p->declarations.items[i],
                                              type_name, param_idx)) return 1;
            return 0;
        }
        case NODE_FUNCTION_DEF: {
            FunctionDefNode *fd = (FunctionDefNode*)n;
            return new_call_uses_string_arg(fd->body, type_name, param_idx);
        }
        case NODE_METHOD_DEF: {
            MethodDefNode *md = (MethodDefNode*)n;
            return new_call_uses_string_arg(md->body, type_name, param_idx);
        }
        case NODE_TYPE_DEF: {
            TypeDefNode *td = (TypeDefNode*)n;
            for (int i = 0; i < td->parent_args.count; i++)
                if (new_call_uses_string_arg(td->parent_args.items[i],
                                              type_name, param_idx)) return 1;
            for (int i = 0; i < td->members.count; i++)
                if (new_call_uses_string_arg(td->members.items[i],
                                              type_name, param_idx)) return 1;
            return 0;
        }
        case NODE_ATTRIBUTE_DEF: {
            AttributeDefNode *ad = (AttributeDefNode*)n;
            return new_call_uses_string_arg(ad->init_expr, type_name, param_idx);
        }
        default: return 0;
    }
}

static LLVMTypeRef infer_ctor_param_type(CodegenContext *c,
                                          TypeDefNode *td,
                                          const char *param_name) {
    /* Heurística 1: si self.param se usa en concat → String. */
    for (int i = 0; i < td->members.count; i++) {
        HulkNode *m = td->members.items[i];
        if (m->type != NODE_METHOD_DEF) continue;
        if (self_member_used_as_string(((MethodDefNode*)m)->body, param_name))
            return c->t_i8ptr;
    }
    /* Heurística 2: si el param se pasa como parent_arg i-ésimo a un
     * padre ya conocido, hereda el tipo del field correspondiente del
     * padre (saltando tag en index 0). */
    if (td->parent) {
        CGTypeInfo *parent_ti = cg_type_info_find(c, td->parent);
        if (parent_ti) {
            for (int i = 0; i < td->parent_args.count; i++) {
                HulkNode *a = td->parent_args.items[i];
                if (a && a->type == NODE_IDENT &&
                    ((IdentNode*)a)->name &&
                    strcmp(((IdentNode*)a)->name, param_name) == 0) {
                    /* el (i+1)-ésimo field del padre (después del tag) */
                    int parent_field = 1 + i;
                    if (parent_field < parent_ti->field_count &&
                        parent_ti->field_types_arr)
                        return parent_ti->field_types_arr[parent_field];
                }
            }
        }
    }
    /* Heurística 3: post-hoc — si algún `new Type(args)` en el programa
     * pasa un StringLit como arg i-ésimo, defaultear a String. */
    if (c->current_program) {
        /* localizar índice del param en td->params */
        int idx = -1;
        for (int i = 0; i < td->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)td->params.items[i];
            if (p->name && strcmp(p->name, param_name) == 0) { idx = i; break; }
        }
        if (idx >= 0 &&
            new_call_uses_string_arg(c->current_program, td->name, idx))
            return c->t_i8ptr;
    }
    return c->t_double;
}

static void forward_declare_function(CodegenContext *c, FunctionDefNode *n) {
    int argc = n->params.count;
    LLVMTypeRef *param_types = calloc(argc > 0 ? argc : 1, sizeof(LLVMTypeRef));

    for (int i = 0; i < argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        param_types[i] = infer_param_type(c, p->type_annotation);
    }

    LLVMTypeRef ret_t;
    if (n->return_type) {
        ret_t = infer_return_type(c, n->return_type);
    } else {
        LLVMTypeRef inferred = infer_body_return_type(c, n->body);
        ret_t = inferred ? inferred : c->t_double;
    }
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
}

/* ============================================================
 *  Forward-declare un tipo  (struct + constructor)
 * ============================================================ */

static void forward_declare_type(CodegenContext *c, TypeDefNode *n) {
    /* Los protocols no tienen representación runtime: solo restringen
     * el typecheck. Se ignoran en codegen. */
    if (n->is_protocol) return;
    /* Crear struct opaco */
    LLVMTypeRef st = LLVMStructCreateNamed(c->llvm_ctx, n->name);
    CGTypeInfo *ti = cg_type_info_create(c, n->name);
    ti->struct_type = st;
    ti->ptr_type = LLVMPointerType(st, 0);

    /* Enlazar con tipo padre si hereda */
    if (n->parent) {
        CGTypeInfo *parent_ti = cg_type_info_find(c, n->parent);
        ti->parent = parent_ti;  /* puede ser NULL si padre aún no declarado */
    }

    /* Layout: { i32 __tag__, padre_fields..., self_fields... }
     * Si no hay padre, "padre_fields" es vacío (sólo el tag inicial). */
    int parent_field_count = ti->parent ? ti->parent->field_count : 1;
    int self_attr_count = 0;
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF)
            self_attr_count++;
    }
    int self_field_count = n->params.count + self_attr_count;
    int total_fields = parent_field_count + self_field_count;
    ti->field_offset_self = parent_field_count;
    ti->field_count = total_fields;

    LLVMTypeRef *field_types = calloc(total_fields, sizeof(LLVMTypeRef));
    ti->field_names = calloc(total_fields, sizeof(const char*));
    ti->field_types_arr = calloc(total_fields, sizeof(LLVMTypeRef));

    if (ti->parent) {
        /* Copiar layout completo del padre */
        for (int i = 0; i < ti->parent->field_count; i++) {
            field_types[i] = ti->parent->field_types_arr[i];
            ti->field_names[i] = ti->parent->field_names[i];
        }
    } else {
        /* Solo el tag */
        field_types[0] = c->t_i32;
        ti->field_names[0] = "__tag__";
    }

    int idx = parent_field_count;
    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        field_types[idx] = p->type_annotation
            ? infer_param_type(c, p->type_annotation)
            : infer_ctor_param_type(c, n, p->name);
        ti->field_names[idx] = p->name;
        idx++;
    }
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *a = (AttributeDefNode*)n->members.items[i];
            field_types[idx] = a->type_annotation
                ? infer_param_type(c, a->type_annotation)
                : infer_ctor_param_type(c, n, a->name);
            ti->field_names[idx] = a->name;
            idx++;
        }
    }

    memcpy(ti->field_types_arr, field_types,
           total_fields * sizeof(LLVMTypeRef));
    LLVMStructSetBody(st, field_types, total_fields, 0);
    free(field_types);

    /* Forward-declare T_init(self, params...) -> void
     * (inicializa: llama padre_init si hay, set tag, copia params a fields,
     *  inicializa atributos). Se separa de T_new para encadenar herencia. */
    int param_argc = n->params.count;
    LLVMTypeRef *init_params = calloc(param_argc + 1, sizeof(LLVMTypeRef));
    init_params[0] = ti->ptr_type;
    for (int i = 0; i < param_argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        init_params[i + 1] = p->type_annotation
            ? infer_param_type(c, p->type_annotation)
            : infer_ctor_param_type(c, n, p->name);
    }
    char init_name[256];
    snprintf(init_name, sizeof(init_name), "%s_init", n->name);
    LLVMTypeRef init_ft = LLVMFunctionType(c->t_void, init_params,
                                            param_argc + 1, 0);
    LLVMValueRef init_fn = LLVMAddFunction(c->module, init_name, init_ft);
    ti->fn_init = init_fn;
    ti->fn_init_type = init_ft;

    /* Forward-declare T_new(params...) -> T* */
    LLVMTypeRef *ctor_params = calloc(param_argc > 0 ? param_argc : 1,
                                       sizeof(LLVMTypeRef));
    for (int i = 0; i < param_argc; i++)
        ctor_params[i] = init_params[i + 1];
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->name);
    LLVMTypeRef ctor_ft = LLVMFunctionType(ti->ptr_type, ctor_params,
                                            param_argc, 0);
    LLVMValueRef ctor_fn = LLVMAddFunction(c->module, ctor_name, ctor_ft);
    ti->fn_new = ctor_fn;

    cg_define_in(c, c->global, n->name, ctor_fn, ctor_ft, 1);
    cg_define_in(c, c->global, strdup(ctor_name), ctor_fn, ctor_ft, 1);

    free(init_params);
    free(ctor_params);

    /* Forward-declare métodos y registrar slots globales */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_METHOD_DEF) {
            MethodDefNode *m = (MethodDefNode*)n->members.items[i];

            int m_argc = m->params.count + 1;  /* +1 para self */
            LLVMTypeRef *m_params = calloc(m_argc, sizeof(LLVMTypeRef));
            m_params[0] = ti->ptr_type;
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                m_params[j + 1] = infer_param_type(c, p->type_annotation);
            }
            LLVMTypeRef m_ret;
            if (m->return_type) {
                m_ret = infer_return_type(c, m->return_type);
            } else {
                LLVMTypeRef inferred = infer_body_return_type(c, m->body);
                m_ret = inferred ? inferred : c->t_double;
            }
            char mname[256];
            snprintf(mname, sizeof(mname), "%s_%s", n->name, m->name);
            LLVMTypeRef m_ft = LLVMFunctionType(m_ret, m_params, m_argc, 0);
            LLVMValueRef m_fn = LLVMAddFunction(c->module, mname, m_ft);

            cg_define_in(c, c->global, strdup(mname), m_fn, m_ft, 1);
            cg_type_add_method(ti, m->name, m_fn);
            cg_method_slot(c, m->name);  /* asigna slot global si no existe */

            free(m_params);
        }
    }
}

/* ============================================================
 *  Emitir tipo: constructor + métodos
 * ============================================================ */

static void emit_type_def(CodegenContext *c, TypeDefNode *n) {
    if (n->is_protocol) return;
    CGTypeInfo *ti = cg_type_info_find(c, n->name);
    if (!ti) return;

    CGTypeInfo *saved_type = c->enclosing_type;
    c->enclosing_type = ti;

    /* ---- Emitir T_init(self, params...) ----
     * Pasos:
     *   1. Si el tipo tiene padre, evaluar parent_args y llamar Parent_init(self, ...)
     *   2. Setear el __tag__ a ti->type_tag (sobreescribe el que puso el padre)
     *   3. Copiar params del constructor a sus fields
     *   4. Inicializar atributos */
    if (ti->fn_init) {
        LLVMValueRef init_fn = ti->fn_init;
        LLVMValueRef saved_fn = c->current_fn;
        c->current_fn = init_fn;

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, init_fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, entry);

        cg_push_scope(c);

        LLVMValueRef self = LLVMGetParam(init_fn, 0);
        c->self_ptr = self;
        CGSymbol *self_sym = cg_define(c, "self", self, ti->ptr_type, 0);
        if (self_sym) self_sym->hulk_type = ti;

        /* Registrar params del constructor como vars en el scope de init,
         * para que init_expr de atributos y parent_args los puedan ver */
        for (int i = 0; i < n->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)n->params.items[i];
            LLVMValueRef param_val = LLVMGetParam(init_fn, i + 1);
            LLVMTypeRef pt = LLVMTypeOf(param_val);
            LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
            LLVMBuildStore(c->builder, param_val, alloca);
            CGSymbol *psym = cg_define(c, p->name, alloca, pt, 0);
            if (psym && p->type_annotation) {
                CGTypeInfo *pti = cg_type_info_find(c, p->type_annotation);
                if (pti) psym->hulk_type = pti;
            }
        }

        /* 1. Encadenar Parent_init si hay padre */
        if (ti->parent && ti->parent->fn_init) {
            int parent_argc = n->parent_args.count;
            LLVMValueRef *args = calloc(parent_argc + 1, sizeof(LLVMValueRef));
            args[0] = self;
            for (int i = 0; i < parent_argc; i++)
                args[i + 1] = cg_emit_expr(c, n->parent_args.items[i]);
            LLVMBuildCall2(c->builder, ti->parent->fn_init_type,
                           ti->parent->fn_init, args, parent_argc + 1, "");
            free(args);
        }

        /* 2. Tag (sobrescribe el del padre — el dispatch usa el más derivado) */
        LLVMValueRef tag_gep = LLVMBuildStructGEP2(
            c->builder, ti->struct_type, self, 0, "tag.ptr");
        LLVMBuildStore(c->builder,
                       LLVMConstInt(c->t_i32, ti->type_tag, 0), tag_gep);

        /* 3. Copiar params a fields propios */
        int field_idx = ti->field_offset_self;
        for (int i = 0; i < n->params.count; i++) {
            LLVMValueRef param_val = LLVMGetParam(init_fn, i + 1);
            LLVMValueRef gep = LLVMBuildStructGEP2(
                c->builder, ti->struct_type, self, field_idx, "field.ptr");
            LLVMBuildStore(c->builder, param_val, gep);
            field_idx++;
        }

        /* 4. Atributos */
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

        LLVMBuildRetVoid(c->builder);

        cg_pop_scope(c);
        c->current_fn = saved_fn;
        c->self_ptr = NULL;
    }

    /* ---- Emitir T_new(params...) ----
     *   self = malloc(sizeof(struct T))
     *   T_init(self, params...)
     *   return self */
    if (ti->fn_new) {
        LLVMValueRef ctor_fn = ti->fn_new;
        LLVMValueRef saved_fn = c->current_fn;
        c->current_fn = ctor_fn;

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, ctor_fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, entry);

        LLVMValueRef size = LLVMSizeOf(ti->struct_type);
        LLVMTypeRef malloc_params[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr,
                                                  malloc_params, 1, 0);
        LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &size, 1, "raw");
        LLVMValueRef self = LLVMBuildBitCast(c->builder, raw,
                                              ti->ptr_type, "self");

        /* Llamar T_init(self, params...) */
        int init_argc = n->params.count + 1;
        LLVMValueRef *init_args = calloc(init_argc, sizeof(LLVMValueRef));
        init_args[0] = self;
        for (int i = 0; i < n->params.count; i++)
            init_args[i + 1] = LLVMGetParam(ctor_fn, i);
        LLVMBuildCall2(c->builder, ti->fn_init_type, ti->fn_init,
                       init_args, init_argc, "");
        free(init_args);

        LLVMBuildRet(c->builder, self);

        c->current_fn = saved_fn;
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
            const char *prev_method_name = c->current_method_name;
            c->current_method_name = m->name;

            /* Registrar self en scope con su hulk_type */
            CGSymbol *self_sym = cg_define(c, "self", c->self_ptr,
                                            ti->ptr_type, 0);
            if (self_sym) self_sym->hulk_type = ti;

            /* Parámetros del método: si el tipo declarado es un tipo HULK,
             * propagar hulk_type para que el field access estático funcione. */
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                LLVMValueRef param_val = LLVMGetParam(m_fn, j + 1);
                LLVMTypeRef  pt = LLVMTypeOf(param_val);
                LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
                LLVMBuildStore(c->builder, param_val, alloca);
                CGSymbol *psym = cg_define(c, p->name, alloca, pt, 0);
                if (psym && p->type_annotation) {
                    CGTypeInfo *pti = cg_type_info_find(c, p->type_annotation);
                    if (pti) psym->hulk_type = pti;
                }
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
            c->current_method_name = prev_method_name;
        }
    }

    c->enclosing_type = saved_type;
}

/* ============================================================
 *  emit_rtti_globals — Vtables y tablas de RTTI
 *
 *  Para cada tipo T construye:
 *    @T_vtable = [N x ptr] con los punteros a método del tipo,
 *      indexados por slot global. Si T no implementa un slot, hereda
 *      del padre; si nadie lo implementa, queda null.
 *  Luego globales compartidos:
 *    @hulk_vtables = [M x ptr] indexado por type_tag, apunta a vtable
 *    @hulk_parents = [M x i32] tag del padre (-1 si raíz)
 * ============================================================ */

static void emit_rtti_globals(CodegenContext *c) {
    int slot_count = c->method_slot_count;
    int type_count = c->type_info_count;
    if (type_count == 0) return;

    LLVMTypeRef ptr_t = c->t_i8ptr;  /* opaque ptr */
    int eff_slots = slot_count > 0 ? slot_count : 1;
    LLVMTypeRef vt_arr_t = LLVMArrayType(ptr_t, eff_slots);

    /* 1. Por cada tipo: construir vtable con cg_type_resolve_method */
    for (int t = 0; t < type_count; t++) {
        CGTypeInfo *ti = c->type_infos[t];
        char name[256];
        snprintf(name, sizeof(name), "%s_vtable", ti->name);
        LLVMValueRef vt_global = LLVMAddGlobal(c->module, vt_arr_t, name);
        LLVMSetLinkage(vt_global, LLVMInternalLinkage);
        LLVMSetGlobalConstant(vt_global, 1);

        LLVMValueRef *entries = calloc(eff_slots, sizeof(LLVMValueRef));
        for (int s = 0; s < slot_count; s++) {
            const char *mname = c->method_slot_names[s];
            LLVMValueRef fn = cg_type_resolve_method(ti, mname);
            entries[s] = fn ? fn : LLVMConstNull(ptr_t);
        }
        if (slot_count == 0)
            entries[0] = LLVMConstNull(ptr_t);
        LLVMValueRef init = LLVMConstArray(ptr_t, entries, eff_slots);
        LLVMSetInitializer(vt_global, init);
        free(entries);

        ti->vtable_global = vt_global;
        ti->vtable_type   = vt_arr_t;
    }

    /* 2. @hulk_vtables[type_count] = { ptr @T0_vtable, ptr @T1_vtable, ... } */
    LLVMTypeRef vt_table_t = LLVMArrayType(ptr_t, type_count);
    LLVMValueRef vt_table = LLVMAddGlobal(c->module, vt_table_t,
                                           "hulk_vtables");
    LLVMSetLinkage(vt_table, LLVMInternalLinkage);
    LLVMSetGlobalConstant(vt_table, 1);
    {
        LLVMValueRef *entries = calloc(type_count, sizeof(LLVMValueRef));
        for (int t = 0; t < type_count; t++)
            entries[t] = c->type_infos[t]->vtable_global;
        LLVMSetInitializer(vt_table,
            LLVMConstArray(ptr_t, entries, type_count));
        free(entries);
    }
    c->vtables_table = vt_table;

    /* 3. @hulk_parents[type_count] = tag del padre o -1 si raíz */
    LLVMTypeRef parent_table_t = LLVMArrayType(c->t_i32, type_count);
    LLVMValueRef parent_table = LLVMAddGlobal(c->module, parent_table_t,
                                               "hulk_parents");
    LLVMSetLinkage(parent_table, LLVMInternalLinkage);
    LLVMSetGlobalConstant(parent_table, 1);
    {
        LLVMValueRef *entries = calloc(type_count, sizeof(LLVMValueRef));
        for (int t = 0; t < type_count; t++) {
            CGTypeInfo *ti = c->type_infos[t];
            int ptag = ti->parent ? ti->parent->type_tag : -1;
            entries[t] = LLVMConstInt(c->t_i32, (unsigned)ptag, 1);
        }
        LLVMSetInitializer(parent_table,
            LLVMConstArray(c->t_i32, entries, type_count));
        free(entries);
    }
    c->parent_table = parent_table;
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
