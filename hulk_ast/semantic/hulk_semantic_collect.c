/*
 * hulk_semantic_collect.c — Recolección de símbolos (pases 1 y 2)
 *
 * Antes de verificar cuerpos, el analizador registra en la tabla de
 * símbolos todos los tipos, funciones y miembros, y resuelve la
 * herencia (con detección de ciclos). Así las referencias mutuas
 * (una función que usa otra definida más abajo, herencia hacia
 * adelante) se resuelven. El pase 3 (verificación) vive en
 * hulk_semantic_check.c.
 */
#include "hulk_semantic_internal.h"

static void collect_function(SemanticContext *c, FunctionDefNode *fn);
static void collect_type_members(SemanticContext *c, TypeDefNode *td);


/* ============================================================
 *  Pase 1 — Registrar nombres de tipos
 * ============================================================ */

void sem_collect_pass1_types(SemanticContext *c, ProgramNode *prog) {
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type != NODE_TYPE_DEF) continue;

        TypeDefNode *td = (TypeDefNode*)decl;
        HulkType *t = sem_type_new(c, HULK_TYPE_USER, td->name, c->t_object);
        t->is_protocol = td->is_protocol;

        Scope *prev = c->current;
        c->current = c->global;
        if (!sem_define(c, td->name, SYM_TYPE, t, decl))
            sem_error(c, decl, "tipo '%s' ya definido", td->name);
        c->current = prev;
    }
}

/* ============================================================
 *  Pase 2 — Herencia + funciones + miembros de tipos
 * ============================================================ */

void sem_collect_pass2_resolve(SemanticContext *c, ProgramNode *prog) {
    /* 2a: resolver herencia */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type != NODE_TYPE_DEF) continue;

        TypeDefNode *td = (TypeDefNode*)decl;
        if (!td->parent) continue;

        HulkType *t      = sem_type_resolve(c, td->name);
        HulkType *parent = sem_type_resolve(c, td->parent);
        if (t && parent) {
            t->parent = parent;
        } else if (t) {
            sem_error(c, decl, "tipo padre '%s' no definido", td->parent);
        }
    }

    /* 2a.1: detectar ciclos de herencia */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type != NODE_TYPE_DEF) continue;
        TypeDefNode *td = (TypeDefNode*)decl;
        HulkType *t = sem_type_resolve(c, td->name);
        if (!t || !t->parent) continue;
        /* Tortoise-and-hare cycle detection */
        HulkType *slow = t, *fast = t;
        int cycle = 0;
        while (fast && fast->parent) {
            slow = slow->parent;
            fast = fast->parent->parent;
            if (slow == fast) { cycle = 1; break; }
        }
        if (cycle) {
            sem_error(c, decl,
                "ciclo de herencia detectado: tipo '%s'", td->name);
            t->parent = c->t_object;  /* romper el ciclo */
        }
    }

    /* 2b: registrar funciones */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type == NODE_FUNCTION_DEF)
            collect_function(c, (FunctionDefNode*)decl);
    }

    /* 2c: registrar miembros de tipos */
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type == NODE_TYPE_DEF)
            collect_type_members(c, (TypeDefNode*)decl);
    }
}

/* ---------- Registrar una función ---------- */


static void collect_function(SemanticContext *c, FunctionDefNode *fn) {
    /* Default Object salvo para funciones recursivas sin anotación,
     * donde Number es el caso útil para que el chequeo del cuerpo no
     * vea Object en la autollamada. */
    HulkType *ret = c->t_object;
    if (!fn->return_type && sem_body_calls_name(fn->body, fn->name))
        ret = c->t_number;
    if (fn->return_type) {
        ret = sem_resolve_annotation(c, fn->return_type, (HulkNode*)fn);
    }

    Scope *prev = c->current;
    c->current = c->global;
    Symbol *sym = sem_define(c, fn->name, SYM_FUNCTION, ret, (HulkNode*)fn);
    c->current = prev;

    if (!sym) {
        sem_error(c, (HulkNode*)fn, "función '%s' ya definida", fn->name);
        return;
    }

    sym->param_count = fn->params.count;
    if (sym->param_count > 0) {
        sym->param_types = calloc(sym->param_count, sizeof(HulkType*));
        sym->param_names = calloc(sym->param_count, sizeof(const char*));
        for (int i = 0; i < fn->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)fn->params.items[i];
            HulkType *pt = sem_param_annotation_for(c, p, fn->body);
            sym->param_types[i] = pt;
            sym->param_names[i] = p->name;
        }
    }
    sym->callable_type = sem_function_type_new(c, sym->param_types,
                                               sym->param_count, ret);
}

/* ---------- Registrar miembros de un tipo ---------- */

static void collect_type_members(SemanticContext *c, TypeDefNode *td) {
    HulkType *type = sem_type_resolve(c, td->name);
    if (!type) return;

    /* Scope de miembros (sin parent — acceso directo por tipo) */
    type->members = sem_scope_create(c, NULL);

    /* Parámetros del constructor → almacenados en el símbolo del tipo */
    Symbol *tsym = sem_lookup(c->global, td->name);
    if (tsym && td->params.count > 0) {
        tsym->param_count = td->params.count;
        tsym->param_types = calloc(td->params.count, sizeof(HulkType*));
        tsym->param_names = calloc(td->params.count, sizeof(const char*));
        for (int i = 0; i < td->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)td->params.items[i];
            HulkType *pt = NULL;
            if (p->type_annotation) {
                pt = sem_resolve_annotation(c, p->type_annotation,
                                              (HulkNode*)p);
            } else {
                /* Inferir desde uso de self.X en métodos cuando el attr
                 * X se inicializa con este param (alias param→attr). */
                pt = sem_infer_self_member_type(c, p->name, td);
                if (!pt) pt = c->t_object;
            }
            tsym->param_types[i] = pt;
            tsym->param_names[i] = p->name;
        }
    }

    /* Registrar métodos y atributos */
    Scope *prev = c->current;
    c->current = type->members;

    for (int i = 0; i < td->members.count; i++) {
        HulkNode *m = td->members.items[i];

        if (m->type == NODE_METHOD_DEF) {
            MethodDefNode *md = (MethodDefNode*)m;
            HulkType *ret = c->t_object;
            if (md->return_type) {
                ret = sem_resolve_annotation(c, md->return_type, m);
            }
            Symbol *ms = sem_define(c, md->name, SYM_METHOD, ret, m);
            if (ms && md->params.count > 0) {
                ms->param_count = md->params.count;
                ms->param_types = calloc(ms->param_count, sizeof(HulkType*));
                ms->param_names = calloc(ms->param_count, sizeof(const char*));
                for (int j = 0; j < md->params.count; j++) {
                    VarBindingNode *p = (VarBindingNode*)md->params.items[j];
                    HulkType *pt = sem_param_annotation_for(c, p, md->body);
                    ms->param_types[j] = pt;
                    ms->param_names[j] = p->name;
                }
            }
            if (ms)
                ms->callable_type = sem_function_type_new(c, ms->param_types,
                                                          ms->param_count, ret);
        } else if (m->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *ad = (AttributeDefNode*)m;
            HulkType *at = NULL;
            if (ad->type_annotation) {
                at = sem_resolve_annotation(c, ad->type_annotation, m);
            } else {
                at = sem_infer_self_member_type(c, ad->name, td);
                if (!at) at = c->t_object;
            }
            sem_define(c, ad->name, SYM_ATTRIBUTE, at, m);
        }
    }

    c->current = prev;
}
