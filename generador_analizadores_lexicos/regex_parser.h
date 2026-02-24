/*
 * Parser de expresiones regulares usando el generador LL(1)
 * Usa flex para tokenizar y el parser LL(1) con acciones semánticas.
 *
 * El contexto RegexParserContext es opaco: encapsula la gramática,
 * las tablas FIRST/FOLLOW y la tabla LL(1) sin exponer detalles
 * de implementación al consumidor.
 */

#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H

#include "ast.h"
#include "token_types.h"

// Contexto opaco del parser de regex (encapsula gramática + tabla LL(1))
typedef struct RegexParserContext RegexParserContext;

// ============== API PRINCIPAL ==============

// Crea e inicializa el contexto del parser de regex.
// Retorna NULL si no hay memoria.
RegexParserContext* regex_parser_create(void);

// Libera todos los recursos del contexto.
void regex_parser_destroy(RegexParserContext *rctx);

// Parsea una expresión regular y retorna el AST.
// ctx se usa para asignar posiciones únicas (get_next_position).
ASTNode* regex_parse(const char* regex_str, ASTContext *ctx,
                     RegexParserContext *rctx);

// Construye el AST combinado para múltiples tokens.
// Combina todas las regex con OR y marca las hojas con token_id.
ASTNode* build_lexer_ast(TokenRegex* tokens, int token_count,
                         ASTContext *ctx, RegexParserContext *rctx);

#endif /* REGEX_PARSER_H */
