/*
 * hulk_semantic_check_expr.c — Verificación de tipos de expresiones
 *
 * Cada función recorre un tipo de nodo del AST y retorna su HulkType*.
 * Se usa evaluación bottom-up (atributos sintetizados): el tipo de un
 * nodo se calcula a partir de los tipos de sus hijos.
 *
 * SRP: Solo verificación de tipos de expresiones.
 */

#include "hulk_semantic_internal.h"

/* ============================================================
 *  Inferencia ad-hoc del tipo de un parámetro
 *
 *  Walker sintáctico que escanea el body buscando ocurrencias del
 *  identificador `param_name` en contextos que disambiguan el tipo:
 *    - Operandos de + - * / ^ % < > <= >=      → Number
 *    - Operandos de & | !                       → Boolean
 *  Si ninguna ocurrencia revela tipo, retorna NULL (caller deja Object).
 * ============================================================ */

typedef enum { INF_NONE = 0, INF_NUMBER, INF_BOOLEAN, INF_STRING } InferTag;

static InferTag join_inf(InferTag a, InferTag b) {
    if (a == b) return a;
    if (a == INF_NONE) return b;
    if (b == INF_NONE) return a;
    return INF_NUMBER;  /* Number gana en conflicto numérico */
}

static int is_param_ident(HulkNode *n, const char *name) {
    return n && n->type == NODE_IDENT &&
           strcmp(((IdentNode*)n)->name, name) == 0;
}

static InferTag walk_infer(HulkNode *n, const char *name) {
    if (!n) return INF_NONE;
    switch (n->type) {
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            InferTag here = INF_NONE;
            switch (b->op) {
                case OP_ADD: case OP_SUB: case OP_MUL:
                case OP_DIV: case OP_MOD: case OP_POW:
                case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                    if (is_param_ident(b->left, name) ||
                        is_param_ident(b->right, name))
                        here = INF_NUMBER;
                    break;
                case OP_AND: case OP_OR:
                    if (is_param_ident(b->left, name) ||
                        is_param_ident(b->right, name))
                        here = INF_BOOLEAN;
                    break;
                default: break;
            }
            return join_inf(here,
                join_inf(walk_infer(b->left, name),
                          walk_infer(b->right, name)));
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)n;
            InferTag here = INF_NONE;
            if (is_param_ident(u->operand, name)) here = INF_NUMBER;
            return join_inf(here, walk_infer(u->operand, name));
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            InferTag a = walk_infer(iff->condition, name);
            a = join_inf(a, walk_infer(iff->then_body, name));
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                a = join_inf(a, walk_infer(e->condition, name));
                a = join_inf(a, walk_infer(e->body, name));
            }
            a = join_inf(a, walk_infer(iff->else_body, name));
            return a;
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return join_inf(walk_infer(w->condition, name),
                             walk_infer(w->body, name));
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)n;
            return join_inf(walk_infer(f->iterable, name),
                             walk_infer(f->body, name));
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < b->statements.count; i++)
                a = join_inf(a, walk_infer(b->statements.items[i], name));
            return a;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                a = join_inf(a, walk_infer(vb->init_expr, name));
            }
            a = join_inf(a, walk_infer(l->body, name));
            return a;
        }
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < ce->args.count; i++)
                a = join_inf(a, walk_infer(ce->args.items[i], name));
            return a;
        }
        case NODE_ASSIGN: {
            AssignNode *as = (AssignNode*)n;
            return walk_infer(as->value, name);
        }
        case NODE_DESTRUCT_ASSIGN: {
            DestructAssignNode *as = (DestructAssignNode*)n;
            return walk_infer(as->value, name);
        }
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            return join_inf(walk_infer(ce->left, name),
                             walk_infer(ce->right, name));
        }
        default: return INF_NONE;
    }
}

HulkType* sem_infer_param_type(SemanticContext *c, const char *param_name,
                                HulkNode *body) {
    if (!param_name || !body) return NULL;
    switch (walk_infer(body, param_name)) {
        case INF_NUMBER:  return c->t_number;
        case INF_BOOLEAN: return c->t_boolean;
        default:          return NULL;
    }
}

/* ============================================================
 *  Inferencia de attrs/params del tipo a partir del uso de self.X
 *
 *  Walker que recorre los método-bodies del tipo y, si encuentra
 *  `self.X` como operando de + - * / ^ % < > <= >=, retorna Number.
 * ============================================================ */

static int is_self_dot(HulkNode *n, const char *member) {
    if (!n || n->type != NODE_MEMBER_ACCESS) return 0;
    MemberAccessNode *ma = (MemberAccessNode*)n;
    if (!ma->object || ma->object->type != NODE_SELF) return 0;
    return ma->member && strcmp(ma->member, member) == 0;
}

static InferTag walk_self_member(HulkNode *n, const char *member) {
    if (!n) return INF_NONE;
    switch (n->type) {
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            InferTag here = INF_NONE;
            switch (b->op) {
                case OP_ADD: case OP_SUB: case OP_MUL:
                case OP_DIV: case OP_MOD: case OP_POW:
                case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                    if (is_self_dot(b->left, member) ||
                        is_self_dot(b->right, member))
                        here = INF_NUMBER;
                    break;
                case OP_AND: case OP_OR:
                    if (is_self_dot(b->left, member) ||
                        is_self_dot(b->right, member))
                        here = INF_BOOLEAN;
                    break;
                default: break;
            }
            return join_inf(here,
                join_inf(walk_self_member(b->left, member),
                          walk_self_member(b->right, member)));
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)n;
            InferTag here = INF_NONE;
            if (is_self_dot(u->operand, member)) here = INF_NUMBER;
            return join_inf(here, walk_self_member(u->operand, member));
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            InferTag a = walk_self_member(iff->condition, member);
            a = join_inf(a, walk_self_member(iff->then_body, member));
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                a = join_inf(a, walk_self_member(e->condition, member));
                a = join_inf(a, walk_self_member(e->body, member));
            }
            a = join_inf(a, walk_self_member(iff->else_body, member));
            return a;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < b->statements.count; i++)
                a = join_inf(a, walk_self_member(b->statements.items[i], member));
            return a;
        }
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < ce->args.count; i++)
                a = join_inf(a, walk_self_member(ce->args.items[i], member));
            return a;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                a = join_inf(a, walk_self_member(vb->init_expr, member));
            }
            return join_inf(a, walk_self_member(l->body, member));
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return join_inf(walk_self_member(w->condition, member),
                             walk_self_member(w->body, member));
        }
        case NODE_DESTRUCT_ASSIGN:
            return walk_self_member(((DestructAssignNode*)n)->value, member);
        case NODE_ASSIGN:
            return walk_self_member(((AssignNode*)n)->value, member);
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            InferTag here = INF_NONE;
            if (is_self_dot(ce->left, member) ||
                is_self_dot(ce->right, member))
                here = INF_STRING;
            return join_inf(here,
                join_inf(walk_self_member(ce->left, member),
                          walk_self_member(ce->right, member)));
        }
        default: return INF_NONE;
    }
}

HulkType* sem_infer_self_member_type(SemanticContext *c, const char *member,
                                      TypeDefNode *td) {
    if (!member || !td) return NULL;
    InferTag agg = INF_NONE;
    for (int i = 0; i < td->members.count; i++) {
        HulkNode *m = td->members.items[i];
        if (m->type != NODE_METHOD_DEF) continue;
        agg = join_inf(agg, walk_self_member(
            ((MethodDefNode*)m)->body, member));
    }
    switch (agg) {
        case INF_NUMBER:  return c->t_number;
        case INF_BOOLEAN: return c->t_boolean;
        case INF_STRING:  return c->t_string;
        default:          return NULL;
    }
}

/* ===== Forward declarations ===== */

static HulkType* check_ident(SemanticContext *c, IdentNode *n);
static HulkType* check_function_expr(SemanticContext *c, FunctionExprNode *n);
static HulkType* check_binary_op(SemanticContext *c, BinaryOpNode *n);
static HulkType* check_unary_op(SemanticContext *c, UnaryOpNode *n);
static HulkType* check_concat(SemanticContext *c, ConcatExprNode *n);
static HulkType* check_call(SemanticContext *c, CallExprNode *n);
static HulkType* check_member(SemanticContext *c, MemberAccessNode *n);
static HulkType* check_let(SemanticContext *c, LetExprNode *n);
static HulkType* check_if(SemanticContext *c, IfExprNode *n);
static HulkType* check_while(SemanticContext *c, WhileStmtNode *n);
static HulkType* check_for(SemanticContext *c, ForStmtNode *n);
static HulkType* check_block(SemanticContext *c, BlockStmtNode *n);
static HulkType* check_new(SemanticContext *c, NewExprNode *n);
static HulkType* check_assign(SemanticContext *c, AssignNode *n);
static HulkType* check_destruct(SemanticContext *c, DestructAssignNode *n);
static HulkType* check_as(SemanticContext *c, AsExprNode *n);
static HulkType* check_is(SemanticContext *c, IsExprNode *n);
static HulkType* check_self(SemanticContext *c, SelfNode *n);
static HulkType* check_base(SemanticContext *c, BaseCallNode *n);

/* ============================================================
 *  Dispatcher principal
 * ============================================================ */

HulkType* sem_check_expr(SemanticContext *c, HulkNode *node) {
    if (!node) return c->t_error;
    switch (node->type) {
        case NODE_NUMBER_LIT:      return c->t_number;
        case NODE_STRING_LIT:      return c->t_string;
        case NODE_BOOL_LIT:        return c->t_boolean;
        case NODE_IDENT:           return check_ident(c, (IdentNode*)node);
        case NODE_FUNCTION_EXPR:   return check_function_expr(c, (FunctionExprNode*)node);
        case NODE_BINARY_OP:       return check_binary_op(c, (BinaryOpNode*)node);
        case NODE_UNARY_OP:        return check_unary_op(c, (UnaryOpNode*)node);
        case NODE_CONCAT_EXPR:     return check_concat(c, (ConcatExprNode*)node);
        case NODE_CALL_EXPR:       return check_call(c, (CallExprNode*)node);
        case NODE_MEMBER_ACCESS:   return check_member(c, (MemberAccessNode*)node);
        case NODE_LET_EXPR:        return check_let(c, (LetExprNode*)node);
        case NODE_IF_EXPR:         return check_if(c, (IfExprNode*)node);
        case NODE_WHILE_STMT:      return check_while(c, (WhileStmtNode*)node);
        case NODE_FOR_STMT:        return check_for(c, (ForStmtNode*)node);
        case NODE_BLOCK_STMT:      return check_block(c, (BlockStmtNode*)node);
        case NODE_NEW_EXPR:        return check_new(c, (NewExprNode*)node);
        case NODE_ASSIGN:          return check_assign(c, (AssignNode*)node);
        case NODE_DESTRUCT_ASSIGN: return check_destruct(c, (DestructAssignNode*)node);
        case NODE_AS_EXPR:         return check_as(c, (AsExprNode*)node);
        case NODE_IS_EXPR:         return check_is(c, (IsExprNode*)node);
        case NODE_SELF:            return check_self(c, (SelfNode*)node);
        case NODE_BASE_CALL:       return check_base(c, (BaseCallNode*)node);
        case NODE_VECTOR_LIT: {
            VectorLitNode *vn = (VectorLitNode*)node;
            for (int i = 0; i < vn->items.count; i++)
                sem_check_expr(c, vn->items.items[i]);
            /* Por simplicidad, asumimos vector de Number. */
            return c->t_number;
        }
        case NODE_INDEX_EXPR: {
            IndexExprNode *ix = (IndexExprNode*)node;
            sem_check_expr(c, ix->object);
            HulkType *idx_t = sem_check_expr(c, ix->index);
            if (!sem_type_conforms(idx_t, c->t_number))
                sem_error(c, node, "índice de vector debe ser Number (es %s)",
                          idx_t->name);
            return c->t_number;
        }
        default:                   return c->t_error;
    }
}

/* ============================================================
 *  Identificadores — resolución de nombres
 * ============================================================ */

static HulkType* check_ident(SemanticContext *c, IdentNode *n) {
    Symbol *sym = NULL;
    Scope *found_scope = NULL;
    for (Scope *s = c->current; s; s = s->parent) {
        sym = sem_lookup_local(s, n->name);
        if (sym) {
            found_scope = s;
            break;
        }
    }
    if (!sym) {
        sem_error(c, (HulkNode*)n, "nombre '%s' no definido", n->name);
        return c->t_error;
    }

    if (c->capture_target && c->capture_scope && found_scope) {
        Scope *limit = c->capture_scope;
        int local_to_function = 0;
        for (Scope *s = c->current; s; s = s->parent) {
            if (s == found_scope) {
                local_to_function = 1;
                break;
            }
            if (s == limit) break;
        }

        if (!local_to_function && sym->kind == SYM_VARIABLE) {
            int already_captured = 0;
            for (int i = 0; i < c->capture_target->captures.count; i++) {
                IdentNode *cap = (IdentNode*)c->capture_target->captures.items[i];
                if (strcmp(cap->name, n->name) == 0) {
                    already_captured = 1;
                    break;
                }
            }
            if (!already_captured) {
                hulk_node_list_push(&c->capture_target->captures,
                    (HulkNode*)hulk_ast_ident(c->ast_ctx, n->name,
                                              n->base.line, n->base.col));
            }
        }
    }

    if ((sym->kind == SYM_FUNCTION || sym->kind == SYM_METHOD) && sym->callable_type)
        return sym->callable_type;
    return sym->type ? sym->type : c->t_object;
}

static HulkType* check_function_expr(SemanticContext *c, FunctionExprNode *n) {
    HulkType **param_types = NULL;
    int param_count = n->params.count;

    if (param_count > 0) {
        param_types = calloc(param_count, sizeof(HulkType*));
        if (!param_types) return c->t_error;
    }

    sem_push_scope(c);
    Scope *fn_scope = c->current;

    FunctionExprNode *prev_capture_target = c->capture_target;
    Scope *prev_capture_scope = c->capture_scope;
    c->capture_target = n;
    c->capture_scope = fn_scope;

    for (int i = 0; i < param_count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        HulkType *pt = sem_param_annotation_for(c, p, n->body);
        param_types[i] = pt;
        sem_define(c, p->name, SYM_VARIABLE, pt, (HulkNode*)p);
    }

    HulkType *body_t = sem_check_expr(c, n->body);
    HulkType *ret_t = body_t;
    if (n->return_type) {
        ret_t = sem_resolve_annotation(c, n->return_type, (HulkNode*)n);
        if (ret_t && ret_t != c->t_error && !sem_type_conforms(body_t, ret_t))
            sem_error(c, (HulkNode*)n,
                "closure retorna %s, se esperaba %s",
                body_t->name, ret_t->name);
    }

    c->capture_target = prev_capture_target;
    c->capture_scope = prev_capture_scope;
    sem_pop_scope(c);

    HulkType *fn_t = sem_function_type_new(c, param_types, param_count, ret_t);
    free(param_types);
    return fn_t ? fn_t : c->t_error;
}

/* ============================================================
 *  Operadores binarios — reglas de tipo por operador
 * ============================================================ */

static HulkType* check_binary_op(SemanticContext *c, BinaryOpNode *n) {
    HulkType *lt = sem_check_expr(c, n->left);
    HulkType *rt = sem_check_expr(c, n->right);
    const char *op_name = hulk_binary_op_name(n->op);

    switch (n->op) {
        /* Aritméticos: Number × Number → Number */
        case OP_ADD: case OP_SUB: case OP_MUL:
        case OP_DIV: case OP_MOD: case OP_POW:
            if (!sem_type_conforms(lt, c->t_number))
                sem_error(c, (HulkNode*)n,
                    "operando izquierdo de '%s' debe ser Number (es %s)",
                    op_name, lt->name);
            if (!sem_type_conforms(rt, c->t_number))
                sem_error(c, (HulkNode*)n,
                    "operando derecho de '%s' debe ser Number (es %s)",
                    op_name, rt->name);
            return c->t_number;

        /* Comparación numérica: Number × Number → Boolean */
        case OP_LT: case OP_GT: case OP_LE: case OP_GE:
            if (!sem_type_conforms(lt, c->t_number))
                sem_error(c, (HulkNode*)n,
                    "operando izquierdo de '%s' debe ser Number (es %s)",
                    op_name, lt->name);
            if (!sem_type_conforms(rt, c->t_number))
                sem_error(c, (HulkNode*)n,
                    "operando derecho de '%s' debe ser Number (es %s)",
                    op_name, rt->name);
            return c->t_boolean;

        /* Igualdad: T × T → Boolean (aceptamos cualquier par) */
        case OP_EQ: case OP_NEQ:
            return c->t_boolean;

        /* Lógicos: Boolean × Boolean → Boolean */
        case OP_AND: case OP_OR:
            if (!sem_type_conforms(lt, c->t_boolean))
                sem_error(c, (HulkNode*)n,
                    "operando izquierdo de '%s' debe ser Boolean (es %s)",
                    op_name, lt->name);
            if (!sem_type_conforms(rt, c->t_boolean))
                sem_error(c, (HulkNode*)n,
                    "operando derecho de '%s' debe ser Boolean (es %s)",
                    op_name, rt->name);
            return c->t_boolean;

        default: break;
    }
    return c->t_error;
}

/* ============================================================
 *  Negación unaria: Number → Number
 * ============================================================ */

static HulkType* check_unary_op(SemanticContext *c, UnaryOpNode *n) {
    HulkType *t = sem_check_expr(c, n->operand);
    if (n->is_not) {
        if (!sem_type_conforms(t, c->t_boolean))
            sem_error(c, (HulkNode*)n,
                "operando de '!' debe ser Boolean (es %s)", t->name);
        return c->t_boolean;
    }
    if (!sem_type_conforms(t, c->t_number))
        sem_error(c, (HulkNode*)n,
            "operando de negación debe ser Number (es %s)", t->name);
    return c->t_number;
}

/* ============================================================
 *  Concatenación: T × T → String
 * ============================================================ */

static HulkType* check_concat(SemanticContext *c, ConcatExprNode *n) {
    sem_check_expr(c, n->left);
    sem_check_expr(c, n->right);
    return c->t_string;
}

/* ============================================================
 *  Llamada a función / método
 * ============================================================ */

/* Helper: verifica argumentos contra un símbolo func/method */
static HulkType* verify_call_args(SemanticContext *c, CallExprNode *n,
                                   Symbol *sym, const char *label) {
    if (n->args.count != sym->param_count) {
        sem_error(c, (HulkNode*)n,
            "'%s' espera %d argumentos, recibió %d",
            label, sym->param_count, n->args.count);
    }
    int count = n->args.count < sym->param_count
                ? n->args.count : sym->param_count;
    for (int i = 0; i < count; i++) {
        HulkType *at = sem_check_expr(c, n->args.items[i]);
        if (sym->param_types && !sem_type_conforms(at, sym->param_types[i]))
            sem_error(c, n->args.items[i],
                "argumento %d de '%s': se esperaba %s, recibido %s",
                i + 1, label, sym->param_types[i]->name, at->name);
    }
    /* Verificar args sobrantes (si hay más de los esperados) */
    for (int i = count; i < n->args.count; i++)
        sem_check_expr(c, n->args.items[i]);

    return sym->type ? sym->type : c->t_object;
}

static HulkType* verify_function_type_call(SemanticContext *c, CallExprNode *n,
                                           HulkType *fn_t, const char *label) {
    if (!fn_t || fn_t->kind != HULK_TYPE_FUNCTION)
        return c->t_error;

    if (n->args.count != fn_t->param_count) {
        sem_error(c, (HulkNode*)n,
            "'%s' espera %d argumentos, recibió %d",
            label, fn_t->param_count, n->args.count);
    }
    int count = n->args.count < fn_t->param_count ? n->args.count : fn_t->param_count;
    for (int i = 0; i < count; i++) {
        HulkType *at = sem_check_expr(c, n->args.items[i]);
        if (fn_t->param_types && !sem_type_conforms(at, fn_t->param_types[i]))
            sem_error(c, n->args.items[i],
                "argumento %d de '%s': se esperaba %s, recibido %s",
                i + 1, label, fn_t->param_types[i]->name, at->name);
    }
    for (int i = count; i < n->args.count; i++)
        sem_check_expr(c, n->args.items[i]);
    return fn_t->return_type ? fn_t->return_type : c->t_object;
}

static HulkType* check_call(SemanticContext *c, CallExprNode *n) {
    /* Caso 1: callee es un identificador → llamada a función */
    if (n->callee->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->callee;
        Symbol *sym = sem_lookup(c->current, id->name);
        if (!sym) {
            sem_error(c, (HulkNode*)n, "función '%s' no definida", id->name);
            for (int i = 0; i < n->args.count; i++)
                sem_check_expr(c, n->args.items[i]);
            return c->t_error;
        }
        if (sym->kind == SYM_FUNCTION || sym->kind == SYM_METHOD)
            return verify_call_args(c, n, sym, id->name);

        if (sym->type && sym->type->kind == HULK_TYPE_FUNCTION)
            return verify_function_type_call(c, n, sym->type, id->name);

        for (int i = 0; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
        if (sym->type && sym->type != c->t_object)
            sem_error(c, (HulkNode*)n,
                "'%s' no es invocable (es %s)", id->name, sym->type->name);
        return c->t_object;
    }

    /* Caso 2: callee es member access → llamada a método */
    if (n->callee->type == NODE_MEMBER_ACCESS) {
        MemberAccessNode *ma = (MemberAccessNode*)n->callee;
        HulkType *obj_t = sem_check_expr(c, ma->object);

        if (obj_t) {
            Symbol *method = sem_lookup_member(obj_t, ma->member);
            if (method && method->kind == SYM_METHOD) {
                char label[256];
                snprintf(label, sizeof(label), "%s.%s",
                         obj_t->name, ma->member);
                return verify_call_args(c, n, method, label);
            }
        }
        if (obj_t && obj_t->kind != HULK_TYPE_ERROR)
            sem_error(c, (HulkNode*)n,
                "tipo '%s' no tiene método '%s'",
                obj_t->name, ma->member);

        for (int i = 0; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
        return c->t_error;
    }

    /* Caso 3: expresión genérica como callee */
    HulkType *callee_t = sem_check_expr(c, n->callee);
    if (callee_t && callee_t->kind == HULK_TYPE_FUNCTION)
        return verify_function_type_call(c, n, callee_t, "closure");
    for (int i = 0; i < n->args.count; i++)
        sem_check_expr(c, n->args.items[i]);
    if (callee_t && callee_t != c->t_object)
        sem_error(c, (HulkNode*)n,
            "expresión de tipo %s no es invocable", callee_t->name);
    return c->t_object;
}

/* ============================================================
 *  Acceso a miembro: obj.member
 * ============================================================ */

static HulkType* check_member(SemanticContext *c, MemberAccessNode *n) {
    HulkType *obj_t = sem_check_expr(c, n->object);
    if (obj_t) {
        Symbol *sym = sem_lookup_member(obj_t, n->member);
        if (sym) return sym->type ? sym->type : c->t_object;
    }
    if (obj_t && obj_t->kind != HULK_TYPE_ERROR)
        sem_error(c, (HulkNode*)n,
            "tipo '%s' no tiene miembro '%s'", obj_t->name, n->member);
    return c->t_error;
}

/* ============================================================
 *  Let: crea scope, registra bindings, verifica body
 * ============================================================ */

static HulkType* check_let(SemanticContext *c, LetExprNode *n) {
    sem_push_scope(c);
    for (int i = 0; i < n->bindings.count; i++) {
        VarBindingNode *vb = (VarBindingNode*)n->bindings.items[i];
        HulkType *init_t = vb->init_expr
            ? sem_check_expr(c, vb->init_expr) : c->t_object;

        HulkType *decl_t = NULL;
        if (vb->type_annotation) {
            decl_t = sem_resolve_annotation(c, vb->type_annotation,
                                              (HulkNode*)vb);
            if (decl_t != c->t_error && !sem_type_conforms(init_t, decl_t))
                sem_error(c, (HulkNode*)vb,
                    "inicializador de '%s' es %s, se esperaba %s",
                    vb->name, init_t->name, decl_t->name);
        } else {
            decl_t = init_t;  /* inferir del inicializador */
        }

        if (!sem_define(c, vb->name, SYM_VARIABLE, decl_t, (HulkNode*)vb))
            sem_error(c, (HulkNode*)vb,
                "variable '%s' ya definida en este scope", vb->name);
    }
    HulkType *body_t = sem_check_expr(c, n->body);
    sem_pop_scope(c);
    return body_t;
}

/* ============================================================
 *  If/elif/else: condiciones Boolean, join de ramas
 * ============================================================ */

static HulkType* check_if(SemanticContext *c, IfExprNode *n) {
    HulkType *cond_t = sem_check_expr(c, n->condition);
    if (!sem_type_conforms(cond_t, c->t_boolean))
        sem_error(c, (HulkNode*)n,
            "condición del if debe ser Boolean (es %s)", cond_t->name);

    HulkType *result = sem_check_expr(c, n->then_body);

    for (int i = 0; i < n->elifs.count; i++) {
        ElifBranchNode *elif = (ElifBranchNode*)n->elifs.items[i];
        HulkType *ec = sem_check_expr(c, elif->condition);
        if (!sem_type_conforms(ec, c->t_boolean))
            sem_error(c, (HulkNode*)elif,
                "condición del elif debe ser Boolean");
        HulkType *eb = sem_check_expr(c, elif->body);
        result = sem_type_join(c, result, eb);
    }

    HulkType *else_t = sem_check_expr(c, n->else_body);
    return sem_type_join(c, result, else_t);
}

/* ============================================================
 *  While: condición Boolean, retorna tipo del body
 * ============================================================ */

static HulkType* check_while(SemanticContext *c, WhileStmtNode *n) {
    HulkType *cond_t = sem_check_expr(c, n->condition);
    if (!sem_type_conforms(cond_t, c->t_boolean))
        sem_error(c, (HulkNode*)n,
            "condición del while debe ser Boolean (es %s)", cond_t->name);
    return sem_check_expr(c, n->body);
}

/* ============================================================
 *  For: scope con variable de iteración
 * ============================================================ */

static HulkType* check_for(SemanticContext *c, ForStmtNode *n) {
    sem_check_expr(c, n->iterable);
    sem_push_scope(c);
    /* La variable de iteración es Number cuando el iterable es range(...)
     * (el único iterable builtin soportado por ahora); de lo contrario
     * Object. Esto permite usarla en contextos aritméticos. */
    HulkType *var_t = c->t_object;
    if (n->iterable && n->iterable->type == NODE_CALL_EXPR) {
        CallExprNode *ce = (CallExprNode*)n->iterable;
        if (ce->callee && ce->callee->type == NODE_IDENT &&
            strcmp(((IdentNode*)ce->callee)->name, "range") == 0)
            var_t = c->t_number;
    }
    sem_define(c, n->var_name, SYM_VARIABLE, var_t, (HulkNode*)n);
    HulkType *body_t = sem_check_expr(c, n->body);
    sem_pop_scope(c);
    return body_t;
}

/* ============================================================
 *  Block: tipo = tipo de la última sentencia
 * ============================================================ */

static HulkType* check_block(SemanticContext *c, BlockStmtNode *n) {
    HulkType *last = c->t_void;
    for (int i = 0; i < n->statements.count; i++)
        last = sem_check_expr(c, n->statements.items[i]);
    return last;
}

/* ============================================================
 *  New: instanciación de tipo
 * ============================================================ */

static HulkType* check_new(SemanticContext *c, NewExprNode *n) {
    HulkType *type = sem_type_resolve(c, n->type_name);
    if (!type) {
        sem_error(c, (HulkNode*)n, "tipo '%s' no definido", n->type_name);
        for (int i = 0; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
        return c->t_error;
    }
    /* Verificar args contra parámetros del constructor */
    Symbol *tsym = sem_lookup(c->global, n->type_name);
    if (tsym && tsym->param_count >= 0) {
        if (n->args.count != tsym->param_count)
            sem_error(c, (HulkNode*)n,
                "constructor de '%s' espera %d args, recibió %d",
                n->type_name, tsym->param_count, n->args.count);
        int cnt = n->args.count < tsym->param_count
                  ? n->args.count : tsym->param_count;
        for (int i = 0; i < cnt; i++) {
            HulkType *at = sem_check_expr(c, n->args.items[i]);
            if (tsym->param_types && !sem_type_conforms(at, tsym->param_types[i]))
                sem_error(c, n->args.items[i],
                    "arg %d del constructor de '%s': se esperaba %s, recibido %s",
                    i + 1, n->type_name, tsym->param_types[i]->name, at->name);
        }
        for (int i = cnt; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
    } else {
        for (int i = 0; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
    }
    return type;
}

/* ============================================================
 *  Asignación: target = value  /  target := value
 * ============================================================ */

static HulkType* check_assign(SemanticContext *c, AssignNode *n) {
    HulkType *val_t = sem_check_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        Symbol *sym = sem_lookup(c->current, id->name);
        if (!sym)
            sem_error(c, (HulkNode*)n,
                "variable '%s' no definida", id->name);
        else if (!sem_type_conforms(val_t, sym->type))
            sem_error(c, (HulkNode*)n,
                "no se puede asignar %s a '%s' de tipo %s",
                val_t->name, id->name, sym->type->name);
    } else {
        sem_check_expr(c, n->target);
    }
    return val_t;
}

static HulkType* check_destruct(SemanticContext *c, DestructAssignNode *n) {
    HulkType *val_t = sem_check_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        Symbol *sym = sem_lookup(c->current, id->name);
        if (!sym)
            sem_error(c, (HulkNode*)n,
                "variable '%s' no definida", id->name);
        /* := es destructivo — aceptamos cualquier tipo */
    } else {
        sem_check_expr(c, n->target);
    }
    return val_t;
}

/* ============================================================
 *  As: downcast — expr as Type → Type
 * ============================================================ */

static HulkType* check_as(SemanticContext *c, AsExprNode *n) {
    sem_check_expr(c, n->expr);
    HulkType *target = sem_type_resolve(c, n->type_name);
    if (!target) {
        sem_error(c, (HulkNode*)n,
            "tipo '%s' no definido en 'as'", n->type_name);
        return c->t_error;
    }
    return target;
}

/* ============================================================
 *  Is: test de tipo — expr is Type → Boolean
 * ============================================================ */

static HulkType* check_is(SemanticContext *c, IsExprNode *n) {
    sem_check_expr(c, n->expr);
    if (!sem_type_resolve(c, n->type_name))
        sem_error(c, (HulkNode*)n,
            "tipo '%s' no definido en 'is'", n->type_name);
    return c->t_boolean;
}

/* ============================================================
 *  Self: solo válido dentro de un type
 * ============================================================ */

static HulkType* check_self(SemanticContext *c, SelfNode *n) {
    if (!c->enclosing_type) {
        sem_error(c, (HulkNode*)n,
            "'self' solo puede usarse dentro de un tipo");
        return c->t_error;
    }
    return c->enclosing_type;
}

/* ============================================================
 *  Base: llama al constructor padre, solo dentro de un type
 * ============================================================ */

static HulkType* check_base(SemanticContext *c, BaseCallNode *n) {
    if (!c->enclosing_type) {
        sem_error(c, (HulkNode*)n,
            "'base' solo puede usarse dentro de un tipo");
        for (int i = 0; i < n->args.count; i++)
            sem_check_expr(c, n->args.items[i]);
        return c->t_error;
    }
    HulkType *parent = c->enclosing_type->parent;
    if (!parent || parent->kind == HULK_TYPE_OBJECT)
        sem_error(c, (HulkNode*)n,
            "tipo '%s' no tiene padre explícito para 'base'",
            c->enclosing_type->name);

    for (int i = 0; i < n->args.count; i++)
        sem_check_expr(c, n->args.items[i]);
    return c->enclosing_type;
}
