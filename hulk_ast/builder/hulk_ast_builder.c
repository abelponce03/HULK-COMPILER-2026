/*
 * hulk_ast_builder.c — API pública del AST builder
 *
 * El parser principal de HULK es el builder LL(1) dirigido por tabla.
 * Se conserva hulk_build_ast como fachada estable para el CLI, el compiler
 * y los tests.
 */

#include "hulk_ast_builder.h"
#include "hulk_ll1_builder.h"

HulkNode* hulk_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input) {
    return hulk_ll1_build_ast(ctx, dfa, input);
}
