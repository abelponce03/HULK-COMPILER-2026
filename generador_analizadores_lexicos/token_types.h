/*
 * token_types.h — Definición central de TokenType y Token
 *
 * Este header NO depende de ningún otro módulo del proyecto.
 * Tanto el lexer como el parser lo incluyen como dependencia base,
 * rompiendo la cadena circular: grammar.h → lexer.h → afd.h → ast.h
 */

#ifndef TOKEN_TYPES_H
#define TOKEN_TYPES_H

// ============== TIPOS DE TOKEN ==============

typedef enum {
    // Fin de archivo
    TOKEN_EOF = 0,
    TOKEN_WS,         // espacios en blanco
    TOKEN_COMMENT,    // comentarios

    // Palabras clave
    TOKEN_FUNCTION,
    TOKEN_TYPE,
    TOKEN_INHERITS,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_IN,
    TOKEN_IF,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_LET,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NEW,
    TOKEN_SELF,
    TOKEN_BASE,
    TOKEN_AS,
    TOKEN_IS,

    // Operadores y símbolos
    TOKEN_SEMICOLON,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_DOT,
    TOKEN_ASSIGN,
    TOKEN_ASSIGN_DESTRUCT,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULT,
    TOKEN_DIV,
    TOKEN_MOD,
    TOKEN_POW,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_EQ,
    TOKEN_NEQ,
    TOKEN_OR,
    TOKEN_AND,
    TOKEN_CONCAT,
    TOKEN_CONCAT_WS,
    TOKEN_ARROW,

    // Literales
    TOKEN_IDENT,
    TOKEN_NUMBER,
    TOKEN_STRING,

    // Error
    TOKEN_ERROR
} TokenType;

// ============== ESTRUCTURA DE TOKEN ==============

typedef struct {
    TokenType  type;
    char      *lexeme;
    int        length;
    int        line;   // 1-based
    int        col;    // 1-based
} Token;

// ============== DEFINICIÓN TOKEN-REGEX ==============
// Par (token_id, regex) usado para construir el lexer DFA.

typedef struct {
    int token_id;
    const char* regex;
} TokenRegex;

#endif /* TOKEN_TYPES_H */
