/*
 * grammar_utils.h — Helpers compartidos entre grammar.c y grammar_hulk.c
 *
 * Funciones inline mínimas para manipulación de strings durante el
 * parsing de gramáticas.  Definidas como static inline para evitar
 * crear una translation unit adicional por <10 LOC.
 */

#ifndef GRAMMAR_UTILS_H
#define GRAMMAR_UTILS_H

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Duplica una cadena (equivalente a POSIX strdup, pero portable C99)
static inline char* grammar_str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}

// Recorta espacios en blanco al inicio y al final (in-place)
static inline char* grammar_str_trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

#endif /* GRAMMAR_UTILS_H */
