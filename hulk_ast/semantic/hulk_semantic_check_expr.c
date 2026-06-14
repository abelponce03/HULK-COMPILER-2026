/*
 * hulk_semantic_check_expr.c — Verificación de tipos de expresiones
 *
 * Evaluación bottom-up (atributos sintetizados): cada nodo calcula su
 * HulkType a partir de los tipos de sus hijos. La inferencia ad-hoc de
 * tipos no anotados vive en hulk_semantic_infer.c.
 */

#include "hulk_semantic_internal.h"

/* ===== Forward declarations ===== */

static HulkType* check_ident(SemanticContext *c, IdentNode *n);
static HulkType* check_function_expr(SemanticContext *c, FunctionExprNode *n);
static HulkType* check_binary_op(SemanticContext *c, BinaryOpNode *n);
static HulkType* check_unary_op(SemanticContext *c, UnaryOpNode *n);
static HulkType* check_concat(SemanticContext *c, ConcatExprNode *n);
static HulkType* check_call(SemanticContext *c, CallExprNode *n);
static HulkType* check_member(SemanticContext *c, MemberAccessNode *n);
/* sem_check_* (control y OOP) viven en hulk_semantic_check_stmt.c y se
 * declaran en hulk_semantic_internal.h. */

/* ============================================================
 *  Dispatcher principal
 * ============================================================ */

HulkType* sem_check_expr(SemanticContext *c, HulkNode *node) {
    if (!node) return c->t_error;
    HulkType *t;
    switch (node->type) {
        case NODE_NUMBER_LIT:      t = c->t_number; break;
        case NODE_STRING_LIT:      t = c->t_string; break;
        case NODE_BOOL_LIT:        t = c->t_boolean; break;
        case NODE_IDENT:           t = check_ident(c, (IdentNode*)node); break;
        case NODE_FUNCTION_EXPR:   t = check_function_expr(c, (FunctionExprNode*)node); break;
        case NODE_BINARY_OP:       t = check_binary_op(c, (BinaryOpNode*)node); break;
        case NODE_UNARY_OP:        t = check_unary_op(c, (UnaryOpNode*)node); break;
        case NODE_CONCAT_EXPR:     t = check_concat(c, (ConcatExprNode*)node); break;
        case NODE_CALL_EXPR:       t = check_call(c, (CallExprNode*)node); break;
        case NODE_MEMBER_ACCESS:   t = check_member(c, (MemberAccessNode*)node); break;
        case NODE_LET_EXPR:        t = sem_check_let(c, (LetExprNode*)node); break;
        case NODE_IF_EXPR:         t = sem_check_if(c, (IfExprNode*)node); break;
        case NODE_WHILE_STMT:      t = sem_check_while(c, (WhileStmtNode*)node); break;
        case NODE_FOR_STMT:        t = sem_check_for(c, (ForStmtNode*)node); break;
        case NODE_BLOCK_STMT:      t = sem_check_block(c, (BlockStmtNode*)node); break;
        case NODE_NEW_EXPR:        t = sem_check_new(c, (NewExprNode*)node); break;
        case NODE_ASSIGN:          t = sem_check_assign(c, (AssignNode*)node); break;
        case NODE_DESTRUCT_ASSIGN: t = sem_check_destruct(c, (DestructAssignNode*)node); break;
        case NODE_AS_EXPR:         t = sem_check_as(c, (AsExprNode*)node); break;
        case NODE_IS_EXPR:         t = sem_check_is(c, (IsExprNode*)node); break;
        case NODE_SELF:            t = sem_check_self(c, (SelfNode*)node); break;
        case NODE_BASE_CALL:       t = sem_check_base(c, (BaseCallNode*)node); break;
        case NODE_VECTOR_LIT: {
            VectorLitNode *vn = (VectorLitNode*)node;
            for (int i = 0; i < vn->items.count; i++)
                sem_check_expr(c, vn->items.items[i]);
            /* Por simplicidad, asumimos vector de Number. */
            t = c->t_number;
            break;
        }
        case NODE_INDEX_EXPR: {
            IndexExprNode *ix = (IndexExprNode*)node;
            sem_check_expr(c, ix->object);
            HulkType *idx_t = sem_check_expr(c, ix->index);
            if (!sem_type_conforms(idx_t, c->t_number))
                sem_error(c, node, "índice de vector debe ser Number (es %s)",
                          idx_t->name);
            t = c->t_number;
            break;
        }
        default:                   t = c->t_error; break;
    }
    /* Anotar el nodo con el nombre canónico del tipo inferido — esto
     * materializa el árbol semántico anotado que el codegen consulta.
     * No se anota el tipo <error> (recuperación de errores). */
    if (t && t->name && t->kind != HULK_TYPE_ERROR)
        node->static_type = t->name;
    return t;
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
