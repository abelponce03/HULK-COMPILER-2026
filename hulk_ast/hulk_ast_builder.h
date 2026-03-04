/*
 * hulk_ast_builder.h — Construcción del AST a partir del flujo de tokens
 *
 * Implementa un parser de descenso recursivo que sigue la gramática
 * LL(1) definida en grammar.ll1 y construye el árbol HulkNode*.
 *
 * Principios SOLID:
 *   SRP — Solo construye el AST; no valida semántica ni genera código.
 *   OCP — No modifica el parser LL(1) existente; es un módulo aparte.
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

#include "hulk_ast.h"
#include "../generador_analizadores_lexicos/afd.h"

// Construye el AST del programa HULK a partir del código fuente.
//
//   ctx   — arena que poseerá todos los nodos (el caller la libera).
//   dfa   — DFA del lexer (ya construido por hulk_compiler_init).
//   input — código fuente HULK (terminado en '\0').
//
// Retorna ProgramNode* casteado a HulkNode*, o NULL si hay errores.
HulkNode* hulk_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input);

#endif /* HULK_AST_BUILDER_H */
