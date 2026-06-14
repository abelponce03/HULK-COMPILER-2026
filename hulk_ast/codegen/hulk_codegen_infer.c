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

/* Mapea el nombre canónico de un tipo HULK al LLVMTypeRef que lo
 * representa. Retorna NULL para "Object"/"<function>"/desconocido, donde
 * el llamador debe decidir (no hay un LLVMType único razonable). */
LLVMTypeRef cg_llvm_type_for_name(CodegenContext *c, const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "Number") == 0)  return c->t_double;
    if (strcmp(name, "String") == 0)  return c->t_i8ptr;
    if (strcmp(name, "Boolean") == 0) return c->t_bool;
    if (strcmp(name, "Void") == 0)    return c->t_void;
    CGTypeInfo *ti = cg_type_info_find(c, name);
    if (ti) return ti->ptr_type;
    return NULL;
}

LLVMTypeRef cg_infer_return_type(CodegenContext *c, const char *ann) {
    if (!ann) return c->t_double;  /* default */
    LLVMTypeRef t = cg_llvm_type_for_name(c, ann);
    return t ? t : c->t_double;
}

LLVMTypeRef cg_infer_body_return_type(CodegenContext *c, HulkNode *body) {
    if (!body) return NULL;

    /* Camino canónico: el semántico anotó el body con su tipo. Si mapea
     * a un LLVMType concreto, esa es la respuesta (elimina la
     * re-inferencia sintáctica de abajo). "Object" y print→void caen al
     * análisis sintáctico de fallback. */
    if (body->static_type) {
        LLVMTypeRef t = cg_llvm_type_for_name(c, body->static_type);
        if (t) return t;
    }

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

/* Recolector: registra en c->str_hints cada (tipo, idx) tal que algún
 * `new T(...)` del programa pasa un StringLit como argumento idx-ésimo.
 * Recorrido único del AST — esto es lo que hace la inferencia O(n). */
static void hint_add(CodegenContext *c, const char *type_name, int idx) {
    for (int i = 0; i < c->str_hints_count; i++)
        if (c->str_hints[i].arg_idx == idx &&
            c->str_hints[i].type_name &&
            strcmp(c->str_hints[i].type_name, type_name) == 0)
            return;  /* ya registrado */
    if (c->str_hints_count >= c->str_hints_cap) {
        int nc = c->str_hints_cap == 0 ? 16 : c->str_hints_cap * 2;
        struct CGStrArgHint *t = realloc(c->str_hints, sizeof(*t) * nc);
        if (!t) return;
        c->str_hints = t;
        c->str_hints_cap = nc;
    }
    c->str_hints[c->str_hints_count].type_name = type_name;
    c->str_hints[c->str_hints_count].arg_idx = idx;
    c->str_hints_count++;
}

static void collect_string_hints(CodegenContext *c, HulkNode *n) {
    if (!n) return;
    switch (n->type) {
        case NODE_NEW_EXPR: {
            NewExprNode *ne = (NewExprNode*)n;
            for (int i = 0; i < ne->args.count; i++) {
                HulkNode *a = ne->args.items[i];
                if (a && a->type == NODE_STRING_LIT && ne->type_name)
                    hint_add(c, ne->type_name, i);
                collect_string_hints(c, a);
            }
            return;
        }
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            collect_string_hints(c, b->left); collect_string_hints(c, b->right); return;
        }
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            collect_string_hints(c, ce->left); collect_string_hints(c, ce->right); return;
        }
        case NODE_UNARY_OP:
            collect_string_hints(c, ((UnaryOpNode*)n)->operand); return;
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            collect_string_hints(c, ce->callee);
            for (int i = 0; i < ce->args.count; i++)
                collect_string_hints(c, ce->args.items[i]);
            return;
        }
        case NODE_MEMBER_ACCESS:
            collect_string_hints(c, ((MemberAccessNode*)n)->object); return;
        case NODE_INDEX_EXPR: {
            IndexExprNode *ix = (IndexExprNode*)n;
            collect_string_hints(c, ix->object); collect_string_hints(c, ix->index); return;
        }
        case NODE_VECTOR_LIT: {
            VectorLitNode *v = (VectorLitNode*)n;
            for (int i = 0; i < v->items.count; i++)
                collect_string_hints(c, v->items.items[i]);
            return;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            for (int i = 0; i < l->bindings.count; i++)
                collect_string_hints(c, ((VarBindingNode*)l->bindings.items[i])->init_expr);
            collect_string_hints(c, l->body); return;
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            collect_string_hints(c, iff->condition);
            collect_string_hints(c, iff->then_body);
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                collect_string_hints(c, e->condition);
                collect_string_hints(c, e->body);
            }
            collect_string_hints(c, iff->else_body); return;
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            collect_string_hints(c, w->condition); collect_string_hints(c, w->body); return;
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)n;
            collect_string_hints(c, f->iterable); collect_string_hints(c, f->body); return;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            for (int i = 0; i < b->statements.count; i++)
                collect_string_hints(c, b->statements.items[i]);
            return;
        }
        case NODE_ASSIGN:
            collect_string_hints(c, ((AssignNode*)n)->value); return;
        case NODE_DESTRUCT_ASSIGN:
            collect_string_hints(c, ((DestructAssignNode*)n)->value); return;
        case NODE_PROGRAM: {
            ProgramNode *p = (ProgramNode*)n;
            for (int i = 0; i < p->declarations.count; i++)
                collect_string_hints(c, p->declarations.items[i]);
            return;
        }
        case NODE_FUNCTION_DEF:
            collect_string_hints(c, ((FunctionDefNode*)n)->body); return;
        case NODE_TYPE_DEF: {
            TypeDefNode *td = (TypeDefNode*)n;
            for (int i = 0; i < td->parent_args.count; i++)
                collect_string_hints(c, td->parent_args.items[i]);
            for (int i = 0; i < td->members.count; i++)
                collect_string_hints(c, td->members.items[i]);
            return;
        }
        case NODE_METHOD_DEF:
            collect_string_hints(c, ((MethodDefNode*)n)->body); return;
        case NODE_ATTRIBUTE_DEF:
            collect_string_hints(c, ((AttributeDefNode*)n)->init_expr); return;
        default: return;
    }
}

/* Consulta O(hints) si (type_name, idx) recibió un StringLit. La tabla
 * se construye una sola vez (lazy) en la primera consulta. */
static int hint_has_string(CodegenContext *c, const char *type_name, int idx) {
    if (!c->str_hints_built) {
        collect_string_hints(c, c->current_program);
        c->str_hints_built = 1;
    }
    for (int i = 0; i < c->str_hints_count; i++)
        if (c->str_hints[i].arg_idx == idx &&
            c->str_hints[i].type_name &&
            strcmp(c->str_hints[i].type_name, type_name) == 0)
            return 1;
    return 0;
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
        if (idx >= 0 && hint_has_string(c, td->name, idx))
            return c->t_i8ptr;
    }
    return c->t_double;
}
