#ifndef HULK_COMPILER_H
#define HULK_COMPILER_H

#include "generador_analizadores_lexicos/afd.h"

// Fachada del compilador HULK.
// Encapsula el DFA del lexer y ofrece las operaciones de alto nivel.
typedef struct {
    DFA *dfa;       // DFA construido a partir de las regex de tokens
} HulkCompiler;

// Inicializa el compilador: construye el DFA del lexer.
// Retorna 1 si todo fue bien, 0 si falló.
int  hulk_compiler_init(HulkCompiler *hc);

// Libera todos los recursos del compilador.
void hulk_compiler_free(HulkCompiler *hc);

// Ejecuta el lexer sobre la entrada e imprime los tokens.
void hulk_compiler_test_lexer(HulkCompiler *hc, const char *input);

// Ejecuta el parser LL(1) sobre la entrada usando grammar_file.
void hulk_compiler_test_parser(HulkCompiler *hc, const char *input,
                               const char *grammar_file);

#endif // HULK_COMPILER_H
