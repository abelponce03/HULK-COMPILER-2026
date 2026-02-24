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
    
    RegexParserContext *rctx = regex_parser_create();
    if (!rctx) {
        fprintf(stderr, "Error: sin memoria para RegexParserContext\n");
        free(ctx);
        return NULL;
    }
    
    printf("Construyendo AST del lexer...\n");
    ASTNode *ast = build_lexer_ast(hulk_tokens, hulk_token_count, ctx, rctx);
    
    if (!ast) {
        printf("Error: no se pudo construir el AST\n");
        regex_parser_destroy(rctx);
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
    regex_parser_destroy(rctx);
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

// ============== SETUP INTERNO DEL PARSER ==============

// Carga gramática, calcula FIRST/FOLLOW y construye tabla LL(1).
// Retorna 1 si la gramática es LL(1), 0 si hay conflictos, -1 si error.
static int build_parser_tables(const char *grammar_file,
                               Grammar *grammar, First_Table *first,
                               Follow_Table *follow, LL1_Table *ll1) {
    printf("\n--- Cargando gramática desde %s ---\n", grammar_file);
    
    grammar_init_hulk(grammar);
    
    if (!grammar_load_hulk(grammar, grammar_file)) {
        fprintf(stderr, "Error: no se pudo cargar la gramática\n");
        return -1;
    }
    
    grammar_print(grammar);
    
    printf("\n--- Calculando FIRST y FOLLOW ---\n");
    compute_first_sets(grammar, first);
    compute_follow_sets(grammar, first, follow);
    print_first_sets(grammar, first);
    print_follow_sets(grammar, follow);
    
    printf("\n--- Construyendo tabla LL(1) ---\n");
    int is_ll1 = build_ll1_table(grammar, first, follow, ll1);
    
    if (!is_ll1) {
        fprintf(stderr, "ADVERTENCIA: La gramática NO es LL(1). Hay conflictos.\n");
    } else {
        printf("La gramática es LL(1) ✓\n");
    }
    
    ll1_table_save_csv(ll1, grammar, "output/hulk_ll1_table.csv");
    return is_ll1;
}

// Ejecuta el análisis sintáctico sobre la entrada.
// Retorna 1 si el parse fue exitoso, 0 si hubo errores.
static int run_parse(HulkCompiler *hc, const char *input,
                     Grammar *grammar, LL1_Table *ll1,
                     Follow_Table *follow) {
    printf("\n--- Analizando entrada ---\n");
    printf("INPUT: %s\n", input);
    
    ParserContext pctx;
    parser_init(&pctx, grammar, ll1, follow);
    
    LexerContext lctx;
    lexer_init(&lctx, hc->dfa, input);
    parser_set_lexer(&pctx, parser_get_token, &lctx);
    
    int result = parser_parse(&pctx);
    
    if (result) {
        printf("\n✓ ANÁLISIS SINTÁCTICO EXITOSO\n");
    } else {
        printf("\n✗ ERRORES EN EL ANÁLISIS SINTÁCTICO (%d errores)\n", pctx.error_count);
    }
    
    return result;
}

void hulk_compiler_test_parser(HulkCompiler *hc, const char *input,
                               const char *grammar_file) {
    printf("\n========== TEST PARSER SINTÁCTICO ==========\n");
    
    Grammar grammar;
    First_Table first;
    Follow_Table follow;
    LL1_Table ll1;
    
    int status = build_parser_tables(grammar_file, &grammar, &first, &follow, &ll1);
    if (status < 0) return;
    
    run_parse(hc, input, &grammar, &ll1, &follow);
    
    // Limpiar
    ll1_table_free(&ll1);
    grammar_free(&grammar);
    
    printf("\n========== FIN TEST PARSER ==========\n");
}
