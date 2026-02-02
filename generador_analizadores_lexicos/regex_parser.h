/*
 * Parser de expresiones regulares usando el generador LL(1)
 * Usa flex para tokenizar y el parser LL(1) con acciones semánticas
 */

#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H

#include "ast.h"
#include "regex_tokens.h"
#include "../generador_parser_ll1/grammar.h"
#include "../generador_parser_ll1/parser.h"

// Estructura para una definición token-regex
typedef struct {
    int token_id;
    const char* regex;
} TokenRegex;

// ============== API PRINCIPAL ==============

// Parsea una expresión regular y retorna el AST
// Usa el parser LL(1) con acciones semánticas
ASTNode* regex_parse(const char* regex_str);

// Construye el AST combinado para múltiples tokens
// Combina todas las regex con OR y marca las hojas con token_id
ASTNode* build_lexer_ast(TokenRegex* tokens, int token_count);

// ============== FUNCIONES INTERNAS ==============

// Inicializa el parser LL(1) para regex (una sola vez)
void regex_parser_init(void);

// Limpia recursos del parser
void regex_parser_cleanup(void);

// Imprime el AST (debugging)
void ast_print(ASTNode* node, int depth);

#endif /* REGEX_PARSER_H */
