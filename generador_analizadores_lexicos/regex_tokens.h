/*
 * Tokens para el lexer de expresiones regulares
 */

#ifndef REGEX_TOKENS_H
#define REGEX_TOKENS_H

// Tipos de tokens para regex (deben coincidir con grammar_init_regex)
typedef enum {
    REGEX_T_CHAR     = 0,   // cualquier carácter literal
    REGEX_T_OR       = 1,   // |
    REGEX_T_STAR     = 2,   // *
    REGEX_T_PLUS     = 3,   // +
    REGEX_T_QUESTION = 4,   // ?
    REGEX_T_LPAREN   = 5,   // (
    REGEX_T_RPAREN   = 6,   // )
    REGEX_T_LBRACKET = 7,   // [
    REGEX_T_RBRACKET = 8,   // ]
    REGEX_T_DOT      = 9,   // .
    REGEX_T_CARET    = 10,  // ^
    REGEX_T_DASH     = 11,  // -
    REGEX_T_ESCAPE   = 12,  // \x
    REGEX_T_EOF      = -1   // fin de entrada
} RegexTokenType;

// Valor del carácter (para CHAR y ESCAPE)
extern char regex_char_value;

// Funciones del lexer (generadas por flex)
int regex_lex(void);
void regex_lexer_set_string(const char* str);
void regex_lexer_cleanup(void);

#endif /* REGEX_TOKENS_H */
