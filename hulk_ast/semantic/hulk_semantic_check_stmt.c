/*
 * hulk_semantic_check_stmt.c — Verificación de control y OOP
 *
 * Reglas de tipo para las construcciones que no son expresiones
 * "escalares": let, if, while, for, block (flujo de control con valor)
 * y new, asignación, is/as, self, base (orientación a objetos).
 * Separado de hulk_semantic_check_expr.c (literales, operadores,
 * llamadas) por SRP. El dispatcher sem_check_expr sigue en _check_expr.c.
 */

#include "hulk_semantic_internal.h"


/* ============================================================
 *  Let: crea scope, registra bindings, verifica body
 * ============================================================ */

HulkType* sem_check_let(SemanticContext *c, LetExprNode *n) {
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

HulkType* sem_check_if(SemanticContext *c, IfExprNode *n) {
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

HulkType* sem_check_while(SemanticContext *c, WhileStmtNode *n) {
    HulkType *cond_t = sem_check_expr(c, n->condition);
    if (!sem_type_conforms(cond_t, c->t_boolean))
        sem_error(c, (HulkNode*)n,
            "condición del while debe ser Boolean (es %s)", cond_t->name);
    return sem_check_expr(c, n->body);
}

/* ============================================================
 *  For: scope con variable de iteración
 * ============================================================ */

static int sem_type_has_iterator_shape(SemanticContext *c, HulkType *type) {
    if (!type || type->kind == HULK_TYPE_ERROR) return 0;

    Symbol *next = sem_lookup_member(type, "next");
    Symbol *current = sem_lookup_member(type, "current");
    if (!next || !current) return 0;
    if (next->kind != SYM_METHOD || current->kind != SYM_METHOD) return 0;
    if (next->type && !sem_type_conforms(next->type, c->t_boolean))
        return 0;

    return 1;
}

HulkType* sem_check_for(SemanticContext *c, ForStmtNode *n) {
    HulkType *iter_t = sem_check_expr(c, n->iterable);
    /* La variable de iteración es Number cuando el iterable es range(...)
     * (builtin soportado por codegen). Los tipos de usuario con forma
     * next/current se aceptan como iterables, aunque su valor actual aún
     * se mantiene como Object mientras el codegen de generadores esté
     * pendiente. */
    HulkType *var_t = c->t_object;
    int is_iterable = 0;
    if (n->iterable && n->iterable->type == NODE_CALL_EXPR) {
        CallExprNode *ce = (CallExprNode*)n->iterable;
        if (ce->callee && ce->callee->type == NODE_IDENT &&
            strcmp(((IdentNode*)ce->callee)->name, "range") == 0) {
            is_iterable = 1;
            var_t = c->t_number;
        }
    }
    if (!is_iterable && sem_type_has_iterator_shape(c, iter_t))
        is_iterable = 1;
    if (!is_iterable)
        sem_error(c, n->iterable ? n->iterable : (HulkNode*)n,
            "expresión en 'for' no es iterable");

    sem_push_scope(c);
    sem_define(c, n->var_name, SYM_VARIABLE, var_t, (HulkNode*)n);
    HulkType *body_t = sem_check_expr(c, n->body);
    sem_pop_scope(c);
    return body_t;
}

/* ============================================================
 *  Block: tipo = tipo de la última sentencia
 * ============================================================ */

HulkType* sem_check_block(SemanticContext *c, BlockStmtNode *n) {
    HulkType *last = c->t_void;
    for (int i = 0; i < n->statements.count; i++)
        last = sem_check_expr(c, n->statements.items[i]);
    return last;
}

/* ============================================================
 *  New: instanciación de tipo
 * ============================================================ */

HulkType* sem_check_new(SemanticContext *c, NewExprNode *n) {
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

HulkType* sem_check_assign(SemanticContext *c, AssignNode *n) {
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

HulkType* sem_check_destruct(SemanticContext *c, DestructAssignNode *n) {
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

HulkType* sem_check_as(SemanticContext *c, AsExprNode *n) {
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

HulkType* sem_check_is(SemanticContext *c, IsExprNode *n) {
    sem_check_expr(c, n->expr);
    if (!sem_type_resolve(c, n->type_name))
        sem_error(c, (HulkNode*)n,
            "tipo '%s' no definido en 'is'", n->type_name);
    return c->t_boolean;
}

/* ============================================================
 *  Self: solo válido dentro de un type
 * ============================================================ */

HulkType* sem_check_self(SemanticContext *c, SelfNode *n) {
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

HulkType* sem_check_base(SemanticContext *c, BaseCallNode *n) {
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
