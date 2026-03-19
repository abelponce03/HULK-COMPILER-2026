/*
 * hulk_semantic_desugar.c — Desugaring de decoradores
 *
 * Transforma:
 *   decor d1, d2(arg) function f(...) => body;
 * En:
 *   function f(...) => body;
 *   f := d1(d2(arg)(f));
 *
 * Los decoradores se aplican de derecha a izquierda.
 * Si un decorador tiene argumentos, se trata como fábrica currificada:
 *   decor memoize(100) function f ...
 *   => f := memoize(100)(f)
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

        /* 3. Construir cadena: d1(d2(...(f))) con fábricas currificadas */
        HulkNode *expr = (HulkNode*)hulk_ast_ident(
            ctx->ast_ctx, name, line, col);

        for (int d = db->decorators.count - 1; d >= 0; d--) {
            DecorItemNode *di = (DecorItemNode*)db->decorators.items[d];
            int dl = di->base.line, dc = di->base.col;

            HulkNode *callee = (HulkNode*)hulk_ast_ident(ctx->ast_ctx, di->name,
                                                         dl, dc);
            if (di->args.count > 0) {
                CallExprNode *factory = hulk_ast_call_expr(ctx->ast_ctx, callee, dl, dc);
                for (int a = 0; a < di->args.count; a++)
                    hulk_node_list_push(&factory->args, di->args.items[a]);

                CallExprNode *apply = hulk_ast_call_expr(
                    ctx->ast_ctx, (HulkNode*)factory, dl, dc);
                hulk_node_list_push(&apply->args, expr);
                expr = (HulkNode*)apply;
            } else {
                CallExprNode *apply = hulk_ast_call_expr(
                    ctx->ast_ctx, callee, dl, dc);
                hulk_node_list_push(&apply->args, expr);
                expr = (HulkNode*)apply;
            }
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
