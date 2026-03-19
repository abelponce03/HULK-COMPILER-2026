/*
 * hulk_ast_builder_internal.h — Cabecera interna del AST Builder
 *
 * Define el contexto ASTBuilder y los prototipos de TODAS las funciones
 * internas de parsing.  Este header es PRIVADO del subsistema ast_builder;
 * el código externo solo debe incluir hulk_ast_builder.h.
 *
 * Principios SOLID:
 *   SRP — Solo expone la interfaz interna de comunicación entre módulos.
 *   ISP — Cada módulo incluye este header pero implementa solo su parte.
 *   DIP — Los módulos dependen de la abstracción ASTBuilder, no entre sí.
 */

#ifndef HULK_AST_BUILDER_INTERNAL_H
#define HULK_AST_BUILDER_INTERNAL_H

#include "../core/hulk_ast.h"
#include "hulk_ast_builder.h"
#include "../../generador_analizadores_lexicos/lexer.h"
#include "../../error_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  Contexto interno del builder
// ============================================================

typedef struct {
    HulkASTContext *ctx;      // arena (propietaria de todos los nodos)
    LexerContext    lexer;    // contexto del lexer
    Token           current;  // token lookahead (no consumido aún)
    int             had_error;
    int             panic;    // en modo pánico (saltando tokens)
} ASTBuilder;

// ============================================================
//  Helpers de tokens  (parse_helpers.c)
// ============================================================

void   advance(ASTBuilder *b);
int    check(ASTBuilder *b, TokenType t);
int    match(ASTBuilder *b, TokenType t);
char*  save_lexeme(ASTBuilder *b);
int    cur_line(ASTBuilder *b);
int    cur_col(ASTBuilder *b);

// ============================================================
//  Reporte de errores  (parse_helpers.c)
// ============================================================

void   error_at(ASTBuilder *b, const char *msg);
int    expect(ASTBuilder *b, TokenType t);
char*  expect_ident(ASTBuilder *b);
void   synchronize(ASTBuilder *b);

// ============================================================
//  Helpers de argumentos y anotaciones  (parse_helpers.c)
// ============================================================

void   parse_arg_list(ASTBuilder *b, HulkNodeList *out);
void   parse_arg_id_list(ASTBuilder *b, HulkNodeList *out);
char*  parse_type_annotation(ASTBuilder *b);

// ============================================================
//  Dispatch de programa  (hulk_ast_builder.c)
// ============================================================

HulkNode* parse_program(ASTBuilder *b);
HulkNode* parse_top_level(ASTBuilder *b);
HulkNode* parse_stmt(ASTBuilder *b);
HulkNode* parse_expr(ASTBuilder *b);

// ============================================================
//  Expresiones  (parse_expressions.c)
// ============================================================

HulkNode* parse_or_expr(ASTBuilder *b);
HulkNode* parse_and_expr(ASTBuilder *b);
HulkNode* parse_cmp_expr(ASTBuilder *b);
HulkNode* parse_concat_expr(ASTBuilder *b);
HulkNode* parse_add_expr(ASTBuilder *b);
HulkNode* parse_term(ASTBuilder *b);
HulkNode* parse_factor(ASTBuilder *b);
HulkNode* parse_unary(ASTBuilder *b);
HulkNode* parse_as_chain(ASTBuilder *b, HulkNode *left);
HulkNode* parse_let_expr(ASTBuilder *b);
HulkNode* parse_if_expr(ASTBuilder *b);

// ============================================================
//  Sentencias  (parse_statements.c)
// ============================================================

HulkNode* parse_while_stmt(ASTBuilder *b);
HulkNode* parse_for_stmt(ASTBuilder *b);
HulkNode* parse_block_stmt(ASTBuilder *b);

// ============================================================
//  Definiciones  (parse_definitions.c)
// ============================================================

HulkNode* parse_function_def(ASTBuilder *b);
HulkNode* parse_function_expr(ASTBuilder *b);
HulkNode* parse_type_def(ASTBuilder *b);
HulkNode* parse_decor_block(ASTBuilder *b);

// ============================================================
//  Primarias  (parse_primary.c)
// ============================================================

HulkNode* parse_primary(ASTBuilder *b);
HulkNode* parse_primary_tail(ASTBuilder *b, HulkNode *left);

#endif /* HULK_AST_BUILDER_INTERNAL_H */
