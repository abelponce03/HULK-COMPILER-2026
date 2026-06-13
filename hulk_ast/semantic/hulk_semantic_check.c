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

/* Pases 1-2 (recolección) en hulk_semantic_collect.c */
static void check_top_level(SemanticContext *c, HulkNode *decl);
static void check_function_def(SemanticContext *c, FunctionDefNode *fn);
static void check_type_def(SemanticContext *c, TypeDefNode *td);
static HulkType* apply_decorators_to_type(SemanticContext *c, HulkNode *site,
                                          HulkType *base_type,
                                          HulkNodeList *decorators);

/* ============================================================
 *  Pase 3 — Verificar cuerpos
 * ============================================================ */

/* Helper: introduce parámetros del constructor de un tipo en el scope */
static void inject_ctor_params(SemanticContext *c, TypeDefNode *td) {
    for (int j = 0; j < td->params.count; j++) {
        VarBindingNode *p = (VarBindingNode*)td->params.items[j];
        HulkType *pt = NULL;
        if (p->type_annotation) {
            pt = sem_resolve_annotation(c, p->type_annotation,
                                          (HulkNode*)p);
        } else {
            pt = sem_infer_self_member_type(c, p->name, td);
            if (!pt) pt = c->t_object;
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
        HulkType *pt = sem_param_annotation_for(c, p, fn->body);
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
            if (sym->callable_type) sym->callable_type->return_type = body_t;
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
                HulkType *pt = sem_param_annotation_for(c, p, md->body);
                sem_define(c, p->name, SYM_VARIABLE, pt, (HulkNode*)p);
            }

            HulkType *body_t = sem_check_expr(c, md->body);

            /* Verificar tipo de retorno del método */
            if (md->return_type) {
                HulkType *ret = sem_resolve_annotation(c, md->return_type, m);
                if (ret && ret != c->t_error &&
                    !sem_type_conforms(body_t, ret))
                    sem_error(c, m,
                        "método '%s': cuerpo retorna %s, se esperaba %s",
                        md->name, body_t->name, ret->name);
            } else {
                Symbol *ms = sem_lookup_member(type, md->name);
                if (ms) {
                    ms->type = body_t;
                    if (ms->callable_type) ms->callable_type->return_type = body_t;
                }
            }

            if (md->decorators.count > 0) {
                Symbol *ms = sem_lookup_member(type, md->name);
                HulkType *method_type = ms && ms->callable_type
                    ? ms->callable_type
                    : sem_function_type_new(c, NULL, 0, body_t);
                HulkType *decorated = apply_decorators_to_type(
                    c, (HulkNode*)md, method_type, &md->decorators);
                if (ms && decorated && decorated->kind == HULK_TYPE_FUNCTION)
                    ms->callable_type = decorated;
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

static HulkType* apply_decorators_to_type(SemanticContext *c, HulkNode *site,
                                          HulkType *base_type,
                                          HulkNodeList *decorators) {
    HulkType *current = base_type;
    for (int i = decorators->count - 1; i >= 0; i--) {
        DecorItemNode *di = (DecorItemNode*)decorators->items[i];
        Symbol *sym = sem_lookup(c->global, di->name);
        if (!sym) {
            sem_error(c, (HulkNode*)di, "decorador '%s' no definido", di->name);
            continue;
        }

        HulkType *dec_type = sym->callable_type ? sym->callable_type : sym->type;
        if (!dec_type || dec_type->kind != HULK_TYPE_FUNCTION) {
            sem_error(c, (HulkNode*)di, "decorador '%s' no es invocable", di->name);
            continue;
        }

        if (di->args.count > 0) {
            if (dec_type->param_count != di->args.count) {
                sem_error(c, (HulkNode*)di,
                    "fábrica decoradora '%s' espera %d argumentos, recibió %d",
                    di->name, dec_type->param_count, di->args.count);
            }
            int cnt = di->args.count < dec_type->param_count
                ? di->args.count : dec_type->param_count;
            for (int a = 0; a < cnt; a++) {
                HulkType *at = sem_check_expr(c, di->args.items[a]);
                if (dec_type->param_types &&
                    !sem_type_conforms(at, dec_type->param_types[a]))
                    sem_error(c, di->args.items[a],
                        "argumento %d de '%s': se esperaba %s, recibido %s",
                        a + 1, di->name, dec_type->param_types[a]->name, at->name);
            }
            for (int a = cnt; a < di->args.count; a++)
                sem_check_expr(c, di->args.items[a]);

            dec_type = dec_type->return_type;
            if (!dec_type) {
                continue;
            }
            if (dec_type->kind == HULK_TYPE_OBJECT) {
                current = c->t_object;
                continue;
            }
            if (dec_type->kind != HULK_TYPE_FUNCTION) {
                sem_error(c, (HulkNode*)di,
                    "fábrica decoradora '%s' debe retornar una función", di->name);
                continue;
            }
        }

        if (dec_type->kind == HULK_TYPE_OBJECT) {
            current = c->t_object;
            continue;
        }
        if (dec_type->param_count != 1) {
            sem_error(c, (HulkNode*)di,
                "decorador '%s' debe aceptar exactamente una función objetivo", di->name);
            continue;
        }
        if (dec_type->param_types &&
            !sem_type_conforms(current, dec_type->param_types[0])) {
            sem_error(c, site,
                "decorador '%s' no acepta la firma de la función objetivo", di->name);
        }

        current = dec_type->return_type ? dec_type->return_type : c->t_object;
        if (current->kind == HULK_TYPE_OBJECT) continue;
        if (current->kind != HULK_TYPE_FUNCTION) {
            sem_error(c, (HulkNode*)di,
                "decorador '%s' debe retornar una función", di->name);
            break;
        }
    }
    return current;
}

/* ============================================================
 *  Orquestador: sem_check_program
 * ============================================================ */

void sem_check_program(SemanticContext *c, HulkNode *program) {
    if (!program || program->type != NODE_PROGRAM) return;
    ProgramNode *prog = (ProgramNode*)program;

    sem_collect_pass1_types(c, prog);
    sem_collect_pass2_resolve(c, prog);

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
