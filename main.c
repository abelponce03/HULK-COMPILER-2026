#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generador_analizadores_lexicos/lexer.h"
#include "generador_analizadores_lexicos/afd.h"
#include "generador_analizadores_lexicos/ast.h"
#include "generador_analizadores_lexicos/regex_parser.h"

// ============== DEFINICIÓN DE TOKENS PARA HULK ==============

TokenRegex hulk_tokens[] = {
    // Palabras clave simples primero
    { TOKEN_FUNCTION,   "function" },
    { TOKEN_LET,        "let" },
    { TOKEN_IF,         "if" },
    
    // Operadores simples
    { TOKEN_SEMICOLON,  ";" },
    { TOKEN_ASSIGN,     "=" },
    { TOKEN_PLUS,       "\\+" },
    
    // Literales con clases de caracteres
    { TOKEN_NUMBER,     "[0-9]+" },
    
    // Identificadores con clases de caracteres
    { TOKEN_IDENT,      "[a-zA-Z_][a-zA-Z0-9_]*" },
    
    // Whitespace
    { TOKEN_WS,         "[ \t\n]+" },
};

int hulk_token_count = sizeof(hulk_tokens) / sizeof(hulk_tokens[0]);

// Nombres de tokens para imprimir
const char* token_names[] = {
    "EOF", "WS", "COMMENT",
    "FUNCTION", "TYPE", "INHERITS", "WHILE", "FOR", "IN", "IF", "ELIF", "ELSE",
    "LET", "TRUE", "FALSE", "NEW", "SELF", "BASE", "AS", "IS",
    "SEMICOLON", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "COMMA", "COLON", "DOT",
    "ASSIGN", "ASSIGN_DESTRUCT", "PLUS", "MINUS", "MULT", "DIV", "MOD", "POW",
    "LT", "GT", "LE", "GE", "EQ", "NEQ", "OR", "AND", "CONCAT", "CONCAT_WS", "ARROW",
    "IDENT", "NUMBER", "STRING", "ERROR"
};

const char* get_token_name(int type) {
    if (type >= 0 && type < (int)(sizeof(token_names)/sizeof(token_names[0])))
        return token_names[type];
    return "UNKNOWN";
}

// ============== TEST LEXER ==============

void test_lexer(const char* input) {
    printf("\n========== TEST LEXER ==========\n");
    
    // Inicializaciones
    init_pos_to_token();
    followpos_init_all();
    reset_position_counter();
    
    printf("Construyendo AST del lexer...\n");
    ASTNode *ast = build_lexer_ast(hulk_tokens, hulk_token_count);
    
    if (!ast) {
        printf("Error: no se pudo construir el AST\n");
        return;
    }
    
    printf("Calculando funciones del AST...\n");
    ast_compute_functions(ast);
    ast_compute_followpos(ast);
    
    // Alfabeto ASCII imprimible + whitespace
    char alphabet[128];
    int alphabet_size = 0;
    for (int c = 32; c < 127; c++) {
        alphabet[alphabet_size++] = (char)c;
    }
    alphabet[alphabet_size++] = '\t';
    alphabet[alphabet_size++] = '\n';
    alphabet[alphabet_size++] = '\r';
    
    printf("Construyendo DFA...\n");
    DFA *dfa = dfa_create(alphabet, alphabet_size);
    dfa_build(dfa, ast);
    
    printf("DFA construido con %d estados\n\n", dfa->count);
    
    printf("--- INPUT ---\n%s\n", input);
    printf("\n--- TOKENS ---\n");
    
    lexer_init(dfa, input);
    
    while (1) {
        Token t = lexer_next_token();
        if (t.type == TOKEN_EOF) {
            printf("[EOF]\n");
            break;
        }
        
        printf("%-12s \"%s\"\n", get_token_name(t.type), t.lexeme);
        free(t.lexeme);
    }
    
    dfa_free(dfa);
    ast_free(ast);
    
    printf("\n========== FIN TEST ==========\n");
}

int main(int argc, char* argv[]) {
    printf("===========================================\n");
    printf("   HULK Compiler - Test del Lexer\n");
    printf("===========================================\n");
    
    const char *default_input = "let x = 42;";
    
    if (argc > 1) {
        // Leer archivo si se proporciona
        FILE *f = fopen(argv[1], "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *content = malloc(size + 1);
            fread(content, 1, size, f);
            content[size] = '\0';
            fclose(f);
            test_lexer(content);
            free(content);
        } else {
            printf("No se pudo abrir: %s\n", argv[1]);
            return 1;
        }
    } else {
        test_lexer(default_input);
    }
    
    return 0;
}
