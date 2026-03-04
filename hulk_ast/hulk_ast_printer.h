/*
 * hulk_ast_printer.h — Visitor que imprime el AST como árbol indentado
 *
 * Primer visitor concreto.  Demuestra el patrón:
 *   - Cada tipo de nodo tiene su callback de impresión.
 *   - Los callbacks recursivamente visitan hijos.
 *   - SRP: la lógica de impresión está separada de los nodos.
 *
 * Uso:
 *   hulk_ast_print(root, stdout);
 */

#ifndef HULK_AST_PRINTER_H
#define HULK_AST_PRINTER_H

#include "hulk_ast.h"
#include <stdio.h>

// Imprime el AST completo al stream indicado (stdout, archivo, etc.)
void hulk_ast_print(HulkNode *root, FILE *out);

#endif /* HULK_AST_PRINTER_H */
