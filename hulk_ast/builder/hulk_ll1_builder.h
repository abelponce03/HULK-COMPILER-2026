/*
 * hulk_ll1_builder.h — Parser LL(1) dirigido por tabla que construye el AST
 *
 * Implementación de la opción B: en vez del descenso recursivo a mano,
 * la gramática de HULK se declara como datos, se computan FIRST/FOLLOW y
 * la tabla LL(1), y un autómata de pila guiado por la tabla construye el
 * AST mediante acciones semánticas sobre una pila semántica. Materializa
 * el "árbol de derivación" del flujo clásico de compilación.
 *
 * El único punto no-LL(1) de HULK (lambda `(x)->…` vs. `(expr)`) se
 * resuelve con un lookahead local documentado.
 */

#ifndef HULK_LL1_BUILDER_H
#define HULK_LL1_BUILDER_H

#include "../core/hulk_ast.h"
#include "../../generador_analizadores_lexicos/afd.h"

/* Construye el AST de un programa HULK con el parser LL(1) dirigido por
 * tabla. Misma firma que hulk_build_ast (descenso recursivo) para poder
 * intercambiarlos. Retorna NULL si hubo error de parsing. */
HulkNode* hulk_ll1_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input);

#endif /* HULK_LL1_BUILDER_H */
