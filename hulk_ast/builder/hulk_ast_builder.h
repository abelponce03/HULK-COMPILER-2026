/*
 * hulk_ast_builder.h — API pública para construir el AST
 *
 * Expone la fachada estable hulk_build_ast. La implementación vigente es
 * el parser LL(1) dirigido por tabla de hulk_ll1_builder.
 *
 * Principios SOLID:
 *   SRP — Solo construye el AST; no valida semántica ni genera código.
 *   DIP — Depende de la abstracción del lexer (LexerContext/DFA).
 *
 * Uso:
 *   HulkASTContext ctx;
 *   hulk_ast_context_init(&ctx);
 *   HulkNode *ast = hulk_build_ast(&ctx, dfa, source_code);
 *   if (ast) hulk_ast_print(ast, stdout);
 *   hulk_ast_context_free(&ctx);
 */

#ifndef HULK_AST_BUILDER_H
#define HULK_AST_BUILDER_H

#include "../core/hulk_ast.h"
#include "../../generador_analizadores_lexicos/afd.h"

// Construye el AST del programa HULK a partir del código fuente usando
// el parser LL(1) principal.
//
//   ctx   — arena que poseerá todos los nodos (el caller la libera).
//   dfa   — DFA del lexer (ya construido por hulk_compiler_init).
//   input — código fuente HULK (terminado en '\0').
//
// Retorna ProgramNode* casteado a HulkNode*, o NULL si hay errores.
HulkNode* hulk_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input);

#endif /* HULK_AST_BUILDER_H */
