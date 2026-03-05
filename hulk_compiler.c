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
#include "hulk_ast/builder/hulk_ast_builder.h"
#include "hulk_ast/printer/hulk_ast_printer.h"

#include <stdio.h>
#include <stdlib.h>
#include "error_handler.h"

// ============== PATRÓN PIPELINE ==============
// Cada fase del compilador se encapsula como un CompilerPhase:
//   execute(ctx) → 1 OK, 0 error.
// Las fases comparten un LexerBuildContext opaco.

typedef struct {
    ASTContext          *ast_ctx;
    RegexParserContext  *rctx;
    ASTNode             *ast;
    DFA                 *dfa;
} LexerBuildContext;

typedef struct {
    const char *name;
    int (*execute)(LexerBuildContext *lbc);
} CompilerPhase;

// --- Fases individuales ---

static int phase_alloc_contexts(LexerBuildContext *lbc) {
    lbc->ast_ctx = malloc(sizeof(ASTContext));
    if (!lbc->ast_ctx) {
        LOG_FATAL_MSG("lexer", "sin memoria para ASTContext");
        return 0;
    }
    ast_context_init(lbc->ast_ctx);

    lbc->rctx = regex_parser_create();
    if (!lbc->rctx) {
        LOG_FATAL_MSG("lexer", "sin memoria para RegexParserContext");
        return 0;
    }
    return 1;
}

static int phase_build_ast(LexerBuildContext *lbc) {
    lbc->ast = build_lexer_ast(hulk_tokens, hulk_token_count,
                               lbc->ast_ctx, lbc->rctx);
    if (!lbc->ast) {
        LOG_ERROR_MSG("lexer", "no se pudo construir el AST");
        return 0;
    }
    return 1;
}

static int phase_compute_functions(LexerBuildContext *lbc) {
    ast_compute_functions(lbc->ast);
    ast_build_leaf_index(lbc->ast, lbc->ast_ctx);
    ast_compute_followpos(lbc->ast, lbc->ast_ctx);
    return 1;
}

static int phase_build_dfa(LexerBuildContext *lbc) {
    char alphabet[128];
    int alphabet_size = 0;
    for (int c = 32; c < 127; c++)
        alphabet[alphabet_size++] = (char)c;
    alphabet[alphabet_size++] = '\t';
    alphabet[alphabet_size++] = '\n';
    alphabet[alphabet_size++] = '\r';

    lbc->dfa = dfa_create(alphabet, alphabet_size);
    dfa_build(lbc->dfa, lbc->ast, lbc->ast_ctx, NULL);
    printf("DFA construido con %d estados\n", lbc->dfa->count);
    return 1;
}

static int phase_export_dfa(LexerBuildContext *lbc) {
    dfa_save_dot(lbc->dfa, "output/lexer_dfa.dot", token_names);
    dfa_save_csv(lbc->dfa, "output/lexer_dfa.csv", token_names);
    return 1;
}

// --- Pipeline del lexer ---

static const CompilerPhase lexer_pipeline[] = {
    { "Asignar contextos",     phase_alloc_contexts     },
    { "Construir AST",         phase_build_ast          },
    { "Calcular funciones",    phase_compute_functions   },
    { "Construir DFA",         phase_build_dfa          },
    { "Exportar DFA",          phase_export_dfa         },
    { NULL, NULL }  // terminador
};

static DFA* build_hulk_lexer(void) {
    printf("\n========== CONSTRUCCIÓN DEL LEXER ==========\n");

    LexerBuildContext lbc = {0};

    // Ejecutar pipeline fase por fase
    for (int i = 0; lexer_pipeline[i].execute; i++) {
        printf("[Pipeline] %s...\n", lexer_pipeline[i].name);
        if (!lexer_pipeline[i].execute(&lbc)) {
            LOG_ERROR_MSG("pipeline", "fallo en fase '%s'",
                          lexer_pipeline[i].name);
            // Limpieza parcial
            if (lbc.dfa) dfa_free(lbc.dfa);
            if (lbc.ast_ctx) { ast_context_free(lbc.ast_ctx); free(lbc.ast_ctx); }
            if (lbc.rctx) regex_parser_destroy(lbc.rctx);
            return NULL;
        }
    }

    // Limpieza de artefactos temporales (el DFA se devuelve)
    DFA *dfa = lbc.dfa;
    ast_context_free(lbc.ast_ctx);
    free(lbc.ast_ctx);
    regex_parser_destroy(lbc.rctx);

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
        LOG_ERROR_MSG("parser", "no se pudo cargar la gramática");
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
        LOG_WARN_MSG("parser", "La gramática NO es LL(1). Hay conflictos.");
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

// ============== CONSTRUCCIÓN DEL AST ==============

HulkNode* hulk_compiler_build_ast(HulkCompiler *hc, const char *input,
                                   HulkASTContext *out_ctx) {
    if (!hc || !hc->dfa || !input || !out_ctx) return NULL;

    hulk_ast_context_init(out_ctx);
    HulkNode *ast = hulk_build_ast(out_ctx, hc->dfa, input);

    if (ast) {
        printf("\n✓ AST construido exitosamente\n");
    } else {
        printf("\n✗ Error construyendo AST\n");
        hulk_ast_context_free(out_ctx);
    }

    return ast;
}
