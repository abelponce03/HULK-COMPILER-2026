#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Definición de tokens basada en la gramática
typedef enum {
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

    // Fin de archivo
    TOKEN_EOF,

    // Error
    TOKEN_ERROR
} TokenType;

typedef struct {
    TokenType type;
    char* lexeme;
    int line;
} Token;

// Función para inicializar el lexer
void init_lexer(const char* source);

// Función para obtener el siguiente token
Token get_next_token();

// Función para liberar memoria
void free_lexer();

#endif