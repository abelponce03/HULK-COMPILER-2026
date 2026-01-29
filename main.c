#include <stdio.h>
#include <stdlib.h>
#include "generador_parser_ll1/parser.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <archivo fuente>\n", argv[0]);
        return 1;
    }

    FILE* file = fopen(argv[1], "r");
    if (!file) {
        perror("Error al abrir el archivo");
        return 1;
    }

    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* source = malloc(length + 1);
    fread(source, 1, length, file);
    source[length] = '\0';
    fclose(file);

    init_parser(source);

    if (parse()) {
        printf("Parsing exitoso!\n");
    } else {
        printf("Error de parsing.\n");
    }

    free(source);
    free_lexer();

    return 0;
}