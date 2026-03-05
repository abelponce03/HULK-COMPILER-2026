/*
 * hulk_codegen.h — Generación de LLVM IR para el lenguaje HULK
 *
 * API pública del módulo de generación de código.
 * Recibe un AST ya verificado semánticamente y produce un módulo LLVM IR
 * que puede compilarse a código máquina nativo o ejecutarse con un JIT.
 *
 * Uso:
 *   HulkASTContext ctx;
 *   hulk_ast_context_init(&ctx);
 *   HulkNode *ast = hulk_build_ast(&ctx, dfa, source);
 *   hulk_semantic_analyze(&ctx, ast);
 *   int ok = hulk_codegen(ast, "output.ll");
 *   hulk_ast_context_free(&ctx);
 */

#ifndef HULK_CODEGEN_H
#define HULK_CODEGEN_H

#include "../core/hulk_ast.h"

/*
 * Genera LLVM IR a partir del AST y lo escribe al archivo indicado.
 *
 *   program   — nodo raíz ProgramNode* del AST (ya desugared)
 *   out_file  — ruta del archivo .ll de salida (NULL → stdout)
 *
 * Retorna 0 en éxito, distinto de 0 si hay errores.
 */
int hulk_codegen(HulkNode *program, const char *out_file);

/*
 * Genera LLVM IR y produce un ejecutable nativo (vía compilación a .o + link).
 *
 *   program   — nodo raíz ProgramNode*
 *   out_file  — ruta del ejecutable de salida
 *
 * Retorna 0 en éxito.
 */
int hulk_codegen_to_executable(HulkNode *program, const char *out_file);

#endif /* HULK_CODEGEN_H */
