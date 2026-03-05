/*
 * hulk_semantic_check.c — Verificación semántica: recolección y chequeo
 *
 * Tres fases internas:
 *   Pase 1 — Registrar nombres de tipos (para referencias mutuas).
 *   Pase 2 — Resolver herencia, registrar funciones y miembros.
 *   Pase 3 — Verificar cuerpos de funciones, métodos y expresiones.
 *
 * También contiene la implementación de hulk_semantic_analyze(),
 * punto de entrada público del análisis semántico.
 *
 * SRP: Solo orquestación y verificación de declaraciones top-level.
 *      La verificación de expresiones está en check_expr.c.
 */

#include "hulk_semantic_internal.h"

/* ===== Forward declarations ===== */

static void collect_pass1_types(SemanticContext *c, ProgramNode *prog);
static void collect_pass2_resolve(SemanticContext *c, ProgramNode *prog);
static void collect_function(SemanticContext *c, FunctionDefNode *fn);
static void collect_type_members(SemanticContext *c, TypeDefNode *td);

static void check_top_level(SemanticContext *c, HulkNode *decl);
static void check_function_def(SemanticContext *c, FunctionDefNode *fn);
static void check_type_def(SemanticContext *c, TypeDefNode *td);

/* ============================================================
 *  Pase 1 — Registrar nombres de tipos
 * ============================================================ */

static void collect_pass1_types(SemanticContext *c, ProgramNode *prog) {
    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];
        if (decl->type != NODE_TYPE_DEF) continue;

        TypeDefNode *td = (TypeDefNode*)decl;
        HulkType *t = sem_type_new(c, HULK_TYPE_USER, td->name, c->t_object);

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

static void collect_pass2_resolve(SemanticContext *c, ProgramNode *prog) {
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
    HulkType *ret = c->t_object;
    if (fn->return_type) {
        ret = sem_type_resolve(c, fn->return_type);
        if (!ret) {
            sem_error(c, (HulkNode*)fn,
                "tipo de retorno '%s' no definido", fn->return_type);
            ret = c->t_error;
        }
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
            HulkType *pt = c->t_object;
            if (p->type_annotation) {
                pt = sem_type_resolve(c, p->type_annotation);
                if (!pt) {
                    sem_error(c, (HulkNode*)p,
                        "tipo '%s' no definido", p->type_annotation);
                    pt = c->t_error;
                }
            }
            sym->param_types[i] = pt;
            sym->param_names[i] = p->name;
        }
    }
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
            HulkType *pt = c->t_object;
            if (p->type_annotation) {
                pt = sem_type_resolve(c, p->type_annotation);
                if (!pt) {
                    sem_error(c, (HulkNode*)p,
                        "tipo '%s' no definido", p->type_annotation);
                    pt = c->t_error;
                }
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
                ret = sem_type_resolve(c, md->return_type);
                if (!ret) {
                    sem_error(c, m, "tipo '%s' no definido", md->return_type);
                    ret = c->t_error;
                }
            }
            Symbol *ms = sem_define(c, md->name, SYM_METHOD, ret, m);
            if (ms && md->params.count > 0) {
                ms->param_count = md->params.count;
                ms->param_types = calloc(ms->param_count, sizeof(HulkType*));
                ms->param_names = calloc(ms->param_count, sizeof(const char*));
                for (int j = 0; j < md->params.count; j++) {
                    VarBindingNode *p = (VarBindingNode*)md->params.items[j];
                    HulkType *pt = c->t_object;
                    if (p->type_annotation) {
                        pt = sem_type_resolve(c, p->type_annotation);
                        if (!pt) pt = c->t_error;
                    }
                    ms->param_types[j] = pt;
                    ms->param_names[j] = p->name;
                }
            }
        } else if (m->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *ad = (AttributeDefNode*)m;
            HulkType *at = c->t_object;
            if (ad->type_annotation) {
                at = sem_type_resolve(c, ad->type_annotation);
                if (!at) {
                    sem_error(c, m, "tipo '%s' no definido", ad->type_annotation);
                    at = c->t_error;
                }
            }
            sem_define(c, ad->name, SYM_ATTRIBUTE, at, m);
        }
    }

    c->current = prev;
}

/* ============================================================
 *  Pase 3 — Verificar cuerpos
 * ============================================================ */

/* Helper: introduce parámetros del constructor de un tipo en el scope */
static void inject_ctor_params(SemanticContext *c, TypeDefNode *td) {
    for (int j = 0; j < td->params.count; j++) {
        VarBindingNode *p = (VarBindingNode*)td->params.items[j];
        HulkType *pt = c->t_object;
        if (p->type_annotation) {
            pt = sem_type_resolve(c, p->type_annotation);
            if (!pt) pt = c->t_error;
        }
        sem_define(c, p->name, SYM_VARIABLE, pt, (HulkNode*)p);
    }
}

static void check_top_level(SemanticContext *c, HulkNode *decl) {
    switch (decl->type) {
        case NODE_FUNCTION_DEF:
            check_function_def(c, (FunctionDefNode*)decl);
            break;
        case NODE_TYPE_DEF:
            check_type_def(c, (TypeDefNode*)decl);
            break;
        default:
            /* Expresión o statement top-level */
            sem_check_expr(c, decl);
            break;
    }
}

/* ---------- Verificar cuerpo de función ---------- */

static void check_function_def(SemanticContext *c, FunctionDefNode *fn) {
    sem_push_scope(c);

    for (int i = 0; i < fn->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)fn->params.items[i];
        HulkType *pt = c->t_object;
        if (p->type_annotation) {
            pt = sem_type_resolve(c, p->type_annotation);
            if (!pt) pt = c->t_error;
        }
        sem_define(c, p->name, SYM_VARIABLE, pt, (HulkNode*)p);
    }

    HulkType *body_t = sem_check_expr(c, fn->body);

    /* Confrontar con tipo de retorno declarado */
    Symbol *sym = sem_lookup(c->global, fn->name);
    if (sym) {
        if (fn->return_type && sym->type && sym->type != c->t_error) {
            if (!sem_type_conforms(body_t, sym->type))
                sem_error(c, (HulkNode*)fn,
                    "función '%s': cuerpo retorna %s, se esperaba %s",
                    fn->name, body_t->name, sym->type->name);
        } else if (!fn->return_type) {
            /* Inferir tipo de retorno */
            sym->type = body_t;
        }
    }

    sem_pop_scope(c);
}

/* ---------- Verificar cuerpo de tipo ---------- */

static void check_type_def(SemanticContext *c, TypeDefNode *td) {
    HulkType *type = sem_type_resolve(c, td->name);
    if (!type) return;

    HulkType *prev_type = c->enclosing_type;
    c->enclosing_type = type;

    for (int i = 0; i < td->members.count; i++) {
        HulkNode *m = td->members.items[i];

        if (m->type == NODE_METHOD_DEF) {
            MethodDefNode *md = (MethodDefNode*)m;
            sem_push_scope(c);

            /* self + parámetros del constructor + parámetros del método */
            sem_define(c, "self", SYM_VARIABLE, type, NULL);
            inject_ctor_params(c, td);
            for (int j = 0; j < md->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)md->params.items[j];
                HulkType *pt = c->t_object;
                if (p->type_annotation) {
                    pt = sem_type_resolve(c, p->type_annotation);
                    if (!pt) pt = c->t_error;
                }
                sem_define(c, p->name, SYM_VARIABLE, pt, (HulkNode*)p);
            }

            HulkType *body_t = sem_check_expr(c, md->body);

            /* Verificar tipo de retorno del método */
            if (md->return_type) {
                HulkType *ret = sem_type_resolve(c, md->return_type);
                if (ret && ret != c->t_error &&
                    !sem_type_conforms(body_t, ret))
                    sem_error(c, m,
                        "método '%s': cuerpo retorna %s, se esperaba %s",
                        md->name, body_t->name, ret->name);
            }

            sem_pop_scope(c);

        } else if (m->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *ad = (AttributeDefNode*)m;
            if (!ad->init_expr) continue;

            sem_push_scope(c);
            sem_define(c, "self", SYM_VARIABLE, type, NULL);
            inject_ctor_params(c, td);

            HulkType *init_t = sem_check_expr(c, ad->init_expr);

            if (ad->type_annotation) {
                HulkType *attr_t = sem_type_resolve(c, ad->type_annotation);
                if (attr_t && attr_t != c->t_error &&
                    !sem_type_conforms(init_t, attr_t))
                    sem_error(c, m,
                        "atributo '%s': inicializador es %s, se esperaba %s",
                        ad->name, init_t->name, attr_t->name);
            }

            sem_pop_scope(c);
        }
    }

    c->enclosing_type = prev_type;
}

/* ============================================================
 *  Orquestador: sem_check_program
 * ============================================================ */

void sem_check_program(SemanticContext *c, HulkNode *program) {
    if (!program || program->type != NODE_PROGRAM) return;
    ProgramNode *prog = (ProgramNode*)program;

    collect_pass1_types(c, prog);
    collect_pass2_resolve(c, prog);

    for (int i = 0; i < prog->declarations.count; i++)
        check_top_level(c, prog->declarations.items[i]);
}

/* ============================================================
 *  API Pública: hulk_semantic_analyze
 * ============================================================ */

int hulk_semantic_analyze(HulkASTContext *ast_ctx, HulkNode *program) {
    SemanticContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ast_ctx = ast_ctx;

    /* Crear scope global */
    ctx.global  = sem_scope_create(&ctx, NULL);
    ctx.current = ctx.global;

    /* Inicializar tipos y funciones built-in */
    sem_types_init(&ctx);

    /* Paso 1: desugaring de decoradores */
    sem_desugar(&ctx, program);

    /* Paso 2: verificación semántica */
    sem_check_program(&ctx, program);

    int errors = ctx.error_count;

    /* Cleanup */
    sem_context_free(&ctx);

    return errors;
}
