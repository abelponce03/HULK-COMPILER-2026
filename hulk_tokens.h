#ifndef HULK_TOKENS_H
#define HULK_TOKENS_H

#include "generador_analizadores_lexicos/token_types.h"

// Definiciones de tokens del lenguaje HULK (regex + token_id)
extern TokenRegex hulk_tokens[];
extern int        hulk_token_count;

// Nombres legibles de cada TokenType (indexado por TokenType)
extern const char* token_names[];

// Devuelve el nombre legible de un tipo de token
const char* get_token_name(int type);

#endif // HULK_TOKENS_H
