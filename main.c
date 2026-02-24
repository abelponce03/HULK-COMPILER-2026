#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hulk_compiler.h"
#include "error_handler.h"

// ============== MAIN ==============

int main(int argc, char* argv[]) {
    printf("===========================================\n");
    printf("   HULK Compiler - Lexer + Parser LL(1)\n");
    printf("===========================================\n");
    
    // Inicializar compilador (construye el DFA del lexer)
    HulkCompiler hc;
    if (!hulk_compiler_init(&hc)) {
        LOG_FATAL_MSG("main", "no se pudo construir el lexer");
        return 1;
    }
    
    const char *default_input = "let x = 5;";
    const char *grammar_file = "grammar.ll1";
    char *input = NULL;
    int free_input = 0;
    
    // Procesar argumentos
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            input = malloc(size + 1);
            fread(input, 1, size, f);
            input[size] = '\0';
            fclose(f);
            free_input = 1;
        } else {
            input = argv[1];
        }
    } else {
        input = (char*)default_input;
    }
    
    // Ejecutar análisis
    hulk_compiler_test_lexer(&hc, input);
    hulk_compiler_test_parser(&hc, input, grammar_file);
    
    // Limpiar
    if (free_input) free(input);
    hulk_compiler_free(&hc);
    
    return 0;
}
