#include <stdio.h>
#include "generador_analizadores_lexicos/lexer.h"
#include "generador_analizadores_lexicos/afd.h"
#include "generador_analizadores_lexicos/ast.h"

int main() {
    // 1. Inicializaciones globales
    init_pos_to_token();
    followpos_init_all();

    // 2. Definición del alfabeto (ASCII simple)
    char alphabet[] = "abcdefghijklmnopqrstuvwxyz"
                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                      "0123456789_"
                      "+-*/=<>!(){}; \t\n";

    // 3. Definición de tokens (orden = prioridad)
    TokenRegex tokens[] = {
        { TOK_IF,        "if" },
        { TOK_WHILE,     "while" },
        { TOK_NUMBER,    "[0-9]+" },
        { TOK_ID,        "[a-zA-Z_][a-zA-Z0-9_]*" },
        { TOK_WS,        "[ \t\n]+" },
    };

    int token_count = sizeof(tokens) / sizeof(tokens[0]);

    // 4. Construir AST del lexer
    ASTNode *ast = build_lexer_ast(tokens, token_count);

    // 5. Calcular funciones del AST
    ast_compute_functions(ast);
    ast_compute_followpos(ast);

    // 6. Construir DFA
    DFA *dfa = dfa_create(alphabet, sizeof(alphabet) - 1);
    dfa_build(dfa, ast);
    dfa_build_table(dfa);

    // 7. Debug opcional
    dfa_print(dfa);

    // 8. Probar lexer
    const char *input = "if while x123 42   y";
    lexer_init(dfa, input);

    printf("INPUT: \"%s\"\n\n", input);

    while (1) {
        Token t = lexer_next_token();
        if (t.type == TOK_EOF) break;

        printf("TOKEN %-10d  \"%s\"\n", t.type, t.lexeme);
        free(t.lexeme);
    }

    dfa_free(dfa);
    return 0;
}
