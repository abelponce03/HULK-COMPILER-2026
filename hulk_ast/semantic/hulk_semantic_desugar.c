/*
 * hulk_semantic_desugar.c — Desugaring de decoradores
 *
 * Transforma:
 *   decor d1, d2(arg) function f(...) => body;
 * En:
 *   function f(...) => body;
 *   f := d2(d1(f), arg);
 *
 * Los decoradores se aplican en orden de lectura (d1 primero, d2 después).
 * Los argumentos del decorador se pasan después de la función envuelta.
 * Los nodos nuevos se crean en la arena del HulkASTContext existente.
 *
 * SRP: Solo transformación AST → AST para decoradores.
 */

#include "hulk_semantic_internal.h"

void sem_desugar(SemanticContext *ctx, HulkNode *program) {
    if (!program || program->type != NODE_PROGRAM) return;
    ProgramNode *prog = (ProgramNode*)program;

    /* Verificar si hay DecorBlock — evitar trabajo innecesario */
    int has_decor = 0;
    for (int i = 0; i < prog->declarations.count && !has_decor; i++) {
        if (prog->declarations.items[i]->type == NODE_DECOR_BLOCK)
            has_decor = 1;
    }
    if (!has_decor) return;

    HulkNodeList new_decls;
    hulk_node_list_init(&new_decls);

    for (int i = 0; i < prog->declarations.count; i++) {
        HulkNode *decl = prog->declarations.items[i];

        if (decl->type != NODE_DECOR_BLOCK) {
            hulk_node_list_push(&new_decls, decl);
            continue;
        }

        DecorBlockNode *db = (DecorBlockNode*)decl;
        int line = db->base.line, col = db->base.col;

        /* 1. Agregar el target (function/type) sin cambios */
        if (db->target)
            hulk_node_list_push(&new_decls, db->target);

        /* 2. Determinar nombre del target */
        const char *name = NULL;
        if (db->target && db->target->type == NODE_FUNCTION_DEF)
            name = ((FunctionDefNode*)db->target)->name;
        else if (db->target && db->target->type == NODE_TYPE_DEF)
            name = ((TypeDefNode*)db->target)->name;

        if (!name || db->decorators.count == 0) continue;

        /* 3. Construir cadena: d_n(... d_2(d_1(f), args_1) ..., args_n) */
        /*    Empezamos con Ident(name) y envolvemos iterativamente.      */
        HulkNode *expr = (HulkNode*)hulk_ast_ident(
            ctx->ast_ctx, name, line, col);

        for (int d = 0; d < db->decorators.count; d++) {
            DecorItemNode *di = (DecorItemNode*)db->decorators.items[d];
            int dl = di->base.line, dc = di->base.col;

            /* Callee: Ident(nombre_decorador) */
            HulkNode *callee = (HulkNode*)hulk_ast_ident(
                ctx->ast_ctx, di->name, dl, dc);

            /* Call: decorator(current_expr [, args_del_decorador...]) */
            CallExprNode *call = hulk_ast_call_expr(
                ctx->ast_ctx, callee, dl, dc);
            hulk_node_list_push(&call->args, expr);

            for (int a = 0; a < di->args.count; a++)
                hulk_node_list_push(&call->args, di->args.items[a]);

            expr = (HulkNode*)call;
        }

        /* 4. Crear asignación destructiva: name := expr */
        HulkNode *target_id = (HulkNode*)hulk_ast_ident(
            ctx->ast_ctx, name, line, col);
        HulkNode *assign = (HulkNode*)hulk_ast_destruct_assign(
            ctx->ast_ctx, target_id, expr, line, col);

        hulk_node_list_push(&new_decls, assign);
    }

    /* Reemplazar la lista de declaraciones */
    hulk_node_list_free(&prog->declarations);
    prog->declarations = new_decls;
}
