/*
 * hulk_compiler.c — Fachada del compilador HULK
 *
 * Orquesta la construcción del lexer (DFA) y la ejecución del parser LL(1).
 * Encapsula todo el estado del compilador en HulkCompiler, sin globales.
 */

#include "hulk_compiler.h"
#include "hulk_tokens.h"

#include "generador_analizadores_lexicos/lexer.h"
#include "generador_analizadores_lexicos/ast.h"
#include "generador_analizadores_lexicos/regex_parser.h"
#include "generador_parser_ll1/parser.h"
#include "generador_parser_ll1/grammar.h"
#include "generador_parser_ll1/first_follow.h"

#include <stdio.h>
#include <stdlib.h>

// ============== CONSTRUCCIÓN DEL LEXER (interno) ==============

static DFA* build_hulk_lexer(void) {
    printf("\n========== CONSTRUCCIÓN DEL LEXER ==========\n");
    
    ASTContext *ctx = malloc(sizeof(ASTContext));
    if (!ctx) {
        fprintf(stderr, "Error: sin memoria para ASTContext\n");
        return NULL;
    }
    ast_context_init(ctx);
    
    printf("Construyendo AST del lexer...\n");
    ASTNode *ast = build_lexer_ast(hulk_tokens, hulk_token_count, ctx);
    
    if (!ast) {
        printf("Error: no se pudo construir el AST\n");
        free(ctx);
        return NULL;
    }
    
    printf("Calculando funciones del AST...\n");
    ast_compute_functions(ast);
    ast_build_leaf_index(ast, ctx);
    ast_compute_followpos(ast, ctx);
    
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
    dfa_build(dfa, ast, ctx);
    
    printf("DFA construido con %d estados\n", dfa->count);
    
    // Exportar DFA para visualización
    dfa_save_dot(dfa, "output/lexer_dfa.dot", token_names);
    dfa_save_csv(dfa, "output/lexer_dfa.csv", token_names);
    
    ast_free(ast);
    free(ctx);
    
    return dfa;
}

// ============== API PÚBLICA ==============

int hulk_compiler_init(HulkCompiler *hc) {
    hc->dfa = build_hulk_lexer();
    return hc->dfa != NULL;
}

void hulk_compiler_free(HulkCompiler *hc) {
    if (hc->dfa) {
        dfa_free(hc->dfa);
        hc->dfa = NULL;
    }
}

// Callback para el parser: obtiene el siguiente token del LexerContext
static Token parser_get_token(void* user) {
    return lexer_next_token((LexerContext*)user);
}

void hulk_compiler_test_lexer(HulkCompiler *hc, const char *input) {
    printf("\n========== TEST LEXER ==========\n");
    printf("\n--- INPUT ---\n%s\n", input);
    printf("\n--- TOKENS ---\n");
    
    LexerContext lctx;
    lexer_init(&lctx, hc->dfa, input);
    
    while (1) {
        Token t = lexer_next_token(&lctx);
        if (t.type == TOKEN_EOF) {
            printf("[EOF]\n");
            break;
        }
        
        printf("[%d:%d] %-12s \"%s\"\n", t.line, t.col, get_token_name(t.type), t.lexeme);
        free(t.lexeme);
    }
    
    printf("\n========== FIN TEST LEXER ==========\n");
}

void hulk_compiler_test_parser(HulkCompiler *hc, const char *input,
                               const char *grammar_file) {
    printf("\n========== TEST PARSER SINTÁCTICO ==========\n");
    
    // 1. Cargar y preparar gramática
    printf("\n--- Cargando gramática desde %s ---\n", grammar_file);
    
    Grammar grammar;
    grammar_init_hulk(&grammar);
    
    if (!grammar_load_hulk(&grammar, grammar_file)) {
        fprintf(stderr, "Error: no se pudo cargar la gramática\n");
        return;
    }
    
    grammar_print(&grammar);
    
    // 2. Calcular FIRST y FOLLOW
    printf("\n--- Calculando FIRST y FOLLOW ---\n");
    
    First_Table first;
    Follow_Table follow;
    
    compute_first_sets(&grammar, &first);
    compute_follow_sets(&grammar, &first, &follow);
    
    // Imprimir FIRST y FOLLOW
    printf("\nConjuntos FIRST:\n");
    for (int i = 0; i < grammar.nt_count; i++) {
        printf("  FIRST(%s) = { ", grammar.nt_names[i]);
        for (int j = 0; j < first.first[i].count; j++) {
            int t = first.first[i].elements[j];
            const char* tname = "?";
            for (int k = 0; k < grammar.t_count; k++) {
                if (grammar.terminals[k] == t) {
                    tname = grammar.t_names[k];
                    break;
                }
            }
            printf("%s ", tname);
        }
        if (first.first[i].has_epsilon) printf("ε ");
        printf("}\n");
    }
    
    printf("\nConjuntos FOLLOW:\n");
    for (int i = 0; i < grammar.nt_count; i++) {
        printf("  FOLLOW(%s) = { ", grammar.nt_names[i]);
        for (int j = 0; j < follow.follow[i].count; j++) {
            int t = follow.follow[i].elements[j];
            if (t == END_MARKER) {
                printf("$ ");
            } else {
                const char* tname = "?";
                for (int k = 0; k < grammar.t_count; k++) {
                    if (grammar.terminals[k] == t) {
                        tname = grammar.t_names[k];
                        break;
                    }
                }
                printf("%s ", tname);
            }
        }
        printf("}\n");
    }
    
    // 3. Construir tabla LL(1)
    printf("\n--- Construyendo tabla LL(1) ---\n");
    
    LL1_Table ll1;
    int is_ll1 = build_ll1_table(&grammar, &first, &follow, &ll1);
    
    if (!is_ll1) {
        fprintf(stderr, "ADVERTENCIA: La gramática NO es LL(1). Hay conflictos.\n");
    } else {
        printf("La gramática es LL(1) ✓\n");
    }
    
    // Guardar tabla en CSV
    ll1_table_save_csv(&ll1, &grammar, "output/hulk_ll1_table.csv");
    
    // 4. Inicializar parser
    printf("\n--- Analizando entrada ---\n");
    printf("INPUT: %s\n", input);
    
    ParserContext pctx;
    parser_init(&pctx, &grammar, &ll1, &follow);
    
    LexerContext lctx;
    lexer_init(&lctx, hc->dfa, input);
    parser_set_lexer(&pctx, parser_get_token, &lctx);
    
    // 5. Ejecutar parsing
    int result = parser_parse(&pctx);
    
    if (result) {
        printf("\n✓ ANÁLISIS SINTÁCTICO EXITOSO\n");
    } else {
        printf("\n✗ ERRORES EN EL ANÁLISIS SINTÁCTICO (%d errores)\n", pctx.error_count);
    }
    
    // Limpiar
    ll1_table_free(&ll1);
    grammar_free(&grammar);
    
    printf("\n========== FIN TEST PARSER ==========\n");
}
