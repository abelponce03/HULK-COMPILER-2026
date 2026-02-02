#include <stdio.h>
#include "generador_analizadores_lexicos/afd.h"
#include "generador_analizadores_lexicos/ast.h"
#include "generador_analizadores_lexicos/lexer.h"
#include "generador_analizadores_lexicos/regex_parser.h"

TokenRegex test_tokens[] = {
    { TOKEN_LET, "let" },
    { TOKEN_IDENT, "[a-zA-Z_][a-zA-Z0-9_]*" },
    { TOKEN_WS, "[ \t\n]+" },
};
int test_count = 3;

int main() {
    init_pos_to_token();
    followpos_init_all();
    reset_position_counter();
    
    ASTNode *ast = build_lexer_ast(test_tokens, test_count);
    if (!ast) { printf("AST NULL\n"); return 1; }
    
    ast_compute_functions(ast);
    ast_compute_followpos(ast);
    
    char alphabet[128];
    int alphabet_size = 0;
    for (int c = 32; c < 127; c++) alphabet[alphabet_size++] = (char)c;
    alphabet[alphabet_size++] = '\t';
    alphabet[alphabet_size++] = '\n';
    
    DFA *dfa = dfa_create(alphabet, alphabet_size);
    dfa_build(dfa, ast);
    printf("DFA: %d estados\n", dfa->count);
    
    dfa_build_table(dfa);
    
    // Debug: ver transiciones desde estado 0
    printf("\nTransiciones desde estado 0:\n");
    for (int c = 'a'; c <= 'z'; c++) {
        int next = dfa->next_state[0][c];
        if (next != -1) {
            printf("  '%c' -> %d (accept=%d, token=%d)\n", 
                   c, next, 
                   dfa->states[next].is_accept,
                   dfa->states[next].token_id);
        }
    }
    
    // Probar lexer
    printf("\n--- Lexer test ---\n");
    lexer_init(dfa, "let x");
    Token t = lexer_next_token();
    printf("Token: type=%d lexeme=%s\n", t.type, t.lexeme);
    
    return 0;
}
