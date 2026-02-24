#ifndef LEXER_H
#define LEXER_H

#include "token_types.h"
#include "afd.h"

// Contexto del lexer (elimina estado global)
typedef struct {
    DFA        *dfa;
    const char *input;
    int         pos;
    int         line;
    int         col;
} LexerContext;

// Inicializar el lexer con contexto
void lexer_init(LexerContext *ctx, DFA *dfa, const char *input);

// Obtener el siguiente token (ignora whitespace y comentarios)
Token lexer_next_token(LexerContext *ctx);

#endif