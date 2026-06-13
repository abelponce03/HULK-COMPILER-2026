/*
 * hulk_codegen_infer.c — Inferencia de tipos LLVM en codegen
 *
 * HULK permite omitir anotaciones; el backend necesita igual decidir el
 * LLVMTypeRef de parámetros, retornos y campos. Aquí viven las
 * heurísticas sintácticas que examinan el AST para inferir esos tipos:
 *   - infer_return_type / infer_body_return_type: tipo de retorno.
 *   - infer_param_type / infer_ctor_param_type: tipo de parámetros y
 *     campos de constructor (analizando el uso de self.x y los call
 *     sites `new T(...)`).
 */
#include "hulk_codegen_internal.h"

LLVMTypeRef cg_infer_return_type(CodegenContext *c, const char *ann) {
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

LLVMTypeRef cg_infer_body_return_type(CodegenContext *c, HulkNode *body) {
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
            return cg_infer_body_return_type(c,
                b->statements.items[b->statements.count - 1]);
        }
        case NODE_LET_EXPR:
            return cg_infer_body_return_type(c, ((LetExprNode*)body)->body);
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)body;
            LLVMTypeRef t = cg_infer_body_return_type(c, iff->then_body);
            if (iff->else_body) {
                LLVMTypeRef te = cg_infer_body_return_type(c, iff->else_body);
                if (t != te) return NULL;
            }
            return t;
        }
        case NODE_BASE_CALL: return NULL;
        default: return NULL;
    }
}

LLVMTypeRef cg_infer_param_type(CodegenContext *c, const char *ann) {
    if (!ann) return c->t_double;
    if (strcmp(ann, "Number") == 0) return c->t_double;
    if (strcmp(ann, "String") == 0) return c->t_i8ptr;
    if (strcmp(ann, "Boolean") == 0) return c->t_bool;
    CGTypeInfo *ti = cg_type_info_find(c, ann);
    if (ti) return ti->ptr_type;
    return c->t_double;
}

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

LLVMTypeRef cg_infer_ctor_param_type(CodegenContext *c,
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
