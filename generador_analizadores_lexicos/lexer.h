#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "afd.h"

typedef struct {
    int   type;      // token_id
    char *lexeme;    // texto reconocido
    int   length;
} Token;


// Función para inicializar el lexer
void lexer_init(DFA *dfa, const char *input);

// Función para obtener el siguiente token
Token lexer_next_token();

#endif