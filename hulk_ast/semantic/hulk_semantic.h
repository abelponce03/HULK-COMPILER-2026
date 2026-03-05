/*
 * hulk_semantic.h — Análisis semántico del lenguaje HULK
 *
 * API pública del módulo semántico.  Ejecuta:
 *   1. Desugaring de decoradores (AST → AST)
 *   2. Resolución de nombres (scopes)
 *   3. Verificación de tipos
 *
 * Uso:
 *   HulkASTContext ctx;
 *   hulk_ast_context_init(&ctx);
 *   HulkNode *ast = hulk_build_ast(&ctx, dfa, source);
 *   int errors = hulk_semantic_analyze(&ctx, ast);
 *   if (errors == 0) { ... }
 *   hulk_ast_context_free(&ctx);
 */

#ifndef HULK_SEMANTIC_H
#define HULK_SEMANTIC_H

#include "../core/hulk_ast.h"

/*
 * Ejecuta el análisis semántico completo sobre el AST.
 *
 *   ast_ctx  — arena del AST (usada para crear nodos nuevos en desugaring)
 *   program  — nodo raíz ProgramNode* del AST
 *
 * Retorna 0 si no hay errores, o la cantidad de errores encontrados.
 */
int hulk_semantic_analyze(HulkASTContext *ast_ctx, HulkNode *program);

#endif /* HULK_SEMANTIC_H */
