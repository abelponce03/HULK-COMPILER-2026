#include "parser.h"
#include "../error_handler.h"
#include <stdlib.h>
#include <string.h>

// ============== PARSER ==============

void parser_init(ParserContext* ctx, Grammar* g, LL1_Table* table, Follow_Table* follow)
{
    ctx->grammar = g;
    ctx->table = table;
    ctx->follow = follow;
    stack_init(&ctx->stack);
    ctx->get_next_token = NULL;
    ctx->lexer_ctx = NULL;
    ctx->error_count = 0;
    ctx->max_errors = 50;
    ctx->error_recovery = NULL;  // usa panic mode por defecto
}

void parser_set_lexer(ParserContext* ctx, Token (*get_token)(void*), void* lexer_ctx)
{
    ctx->get_next_token = get_token;
    ctx->lexer_ctx = lexer_ctx;
}

void parser_reset(ParserContext* ctx)
{
    stack_init(&ctx->stack);
    ctx->error_count = 0;
}

// Verifica si un terminal está en FOLLOW(nt)
static int follow_contains(Follow_Table* ft, int nt, int terminal) {
    if (!ft) return 0;
    Follow_Set* fs = &ft->follow[nt];
    for (int i = 0; i < fs->count; i++) {
        if (fs->elements[i] == terminal) return 1;
    }
    return 0;
}

// Busca el nombre de un terminal en la gramática
static const char* get_terminal_name(Grammar* g, int token_type) {
    if (token_type == TOKEN_EOF) return "$";
    for (int i = 0; i < g->t_count; i++) {
        if (g->terminals[i] == token_type)
            return g->t_names[i];
    }
    return "?";
}

int parser_parse(ParserContext* ctx)
{
    if (!ctx->grammar || !ctx->table || !ctx->get_next_token) {
        LOG_ERROR_MSG("parser", "Parser no configurado correctamente");
        return 0;
    }
    
    Grammar* g = ctx->grammar;
    LL1_Table* ll1 = ctx->table;
    ParserStack* stack = &ctx->stack;
    
    // Inicializar stack: push $ y símbolo inicial
    stack_init(stack);
    stack_push(stack, (GrammarSymbol){SYMBOL_END, END_MARKER});
    stack_push(stack, (GrammarSymbol){SYMBOL_NON_TERMINAL, g->start_symbol});
    
    // Obtener primer token
    ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
    
    while (1) {
        // Verificar límite de errores
        if (ctx->max_errors > 0 && ctx->error_count >= ctx->max_errors) {
            LOG_ERROR_MSG("parser", "Demasiados errores (%d), abortando análisis", ctx->error_count);
            return 0;
        }
        
        GrammarSymbol top = stack_peek(stack);
        
        // Caso: stack vacío (solo $)
        if (top.type == SYMBOL_END) {
            if (ctx->lookahead.type == TOKEN_EOF) {
                return ctx->error_count == 0; // Éxito
            } else {
                LOG_ERROR_MSG("parser", "[%d:%d] entrada extra después del parse completo",
                              ctx->lookahead.line, ctx->lookahead.col);
                ctx->error_count++;
                return 0;
            }
        }
        
        // Caso: Terminal en el stack
        if (top.type == SYMBOL_TERMINAL) {
            if (top.id == (int)ctx->lookahead.type) {
                // Match!
                if (ctx->lookahead.lexeme) free(ctx->lookahead.lexeme);
                stack_pop(stack);
                ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
            } else {
                // Error: terminal no coincide
                const char* expected = get_terminal_name(g, top.id);
                const char* found = get_terminal_name(g, ctx->lookahead.type);
                
                LOG_ERROR_MSG("parser", "[%d:%d] se esperaba '%s', se encontró '%s'",
                              ctx->lookahead.line, ctx->lookahead.col, expected, found);
                ctx->error_count++;
                
                // Intentar recuperación personalizada
                if (ctx->error_recovery &&
                    ctx->error_recovery(ctx, "terminal mismatch")) {
                    continue;
                }
                
                // Default: descartar el terminal del stack
                stack_pop(stack);
            }
        }
        // Caso: No terminal en el stack
        else if (top.type == SYMBOL_NON_TERMINAL) {
            int row = top.id;
            int col;
            
            if (ctx->lookahead.type == TOKEN_EOF) {
                col = g->t_count; // Columna para $
            } else {
                col = ll1_table_get_column(ll1, g, ctx->lookahead.type);
            }
            
            if (col < 0) {
                LOG_ERROR_MSG("parser", "[%d:%d] token %d no reconocido en gramática",
                              ctx->lookahead.line, ctx->lookahead.col, ctx->lookahead.type);
                ctx->error_count++;
                if (ctx->lookahead.lexeme) free(ctx->lookahead.lexeme);
                ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
                continue;
            }
            
            int prod_index = ll1->table[row][col];
            
            if (prod_index == NO_PRODUCTION) {
                const char* nt_name = g->nt_names[row];
                const char* t_name = get_terminal_name(g, ctx->lookahead.type);
                
                LOG_ERROR_MSG("parser", "[%d:%d] no hay producción para [%s, %s]",
                              ctx->lookahead.line, ctx->lookahead.col, nt_name, t_name);
                ctx->error_count++;
                
                // Intentar recuperación personalizada primero
                if (ctx->error_recovery &&
                    ctx->error_recovery(ctx, "no production")) {
                    continue;
                }
                
                // Default: modo pánico con FOLLOW (Dragon Book §4.4.1)
                if (ctx->follow) {
                    // Saltar tokens hasta encontrar uno en FOLLOW(A) o EOF
                    while (ctx->lookahead.type != TOKEN_EOF) {
                        int la = ctx->lookahead.type;
                        if (follow_contains(ctx->follow, row, la)) break;
                        if (ctx->lookahead.lexeme) free(ctx->lookahead.lexeme);
                        ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
                    }
                    // Pop A - se sincroniza con el siguiente token válido
                    if (ctx->lookahead.type == TOKEN_EOF) {
                        if (follow_contains(ctx->follow, row, END_MARKER)) {
                            stack_pop(stack);
                        }
                    } else {
                        stack_pop(stack);
                    }
                } else {
                    // Fallback sin FOLLOW: descartar un token
                    if (ctx->lookahead.lexeme) free(ctx->lookahead.lexeme);
                    ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
                }
                continue;
            }
            
            if (prod_index == SYNC_ENTRY) {
                // Recuperación: pop del no terminal
                stack_pop(stack);
                continue;
            }
            
            // Aplicar producción
            stack_pop(stack);
            
            Production* prod = &g->productions[prod_index];
            
            // Verificar desbordamiento de pila
            if (stack->top + prod->right_count >= STACK_MAX) {
                LOG_FATAL_MSG("parser", "desbordamiento de pila del parser (STACK_MAX=%d)", STACK_MAX);
                return 0;
            }
            
            // Push RHS en orden inverso (si no es ε)
            for (int i = prod->right_count - 1; i >= 0; i--) {
                GrammarSymbol s = prod->right[i];
                
                if (s.type == SYMBOL_TERMINAL) {
                    stack_push(stack, (GrammarSymbol){SYMBOL_TERMINAL, s.id});
                } else if (s.type == SYMBOL_NON_TERMINAL) {
                    stack_push(stack, (GrammarSymbol){SYMBOL_NON_TERMINAL, s.id});
                }
                // SYMBOL_EPSILON no se hace push
            }
        }
    }
}

// ============== PARSER DE ALTO NIVEL ==============

int parser_create_from_file(Parser* p, const char* grammar_file, const char* table_cache)
{
    memset(p, 0, sizeof(Parser));
    
    grammar_init(&p->grammar, "loaded");
    
    if (!grammar_load_from_file(&p->grammar, grammar_file)) {
        LOG_ERROR_MSG("parser", "Error cargando gramática desde %s", grammar_file);
        return 0;
    }
    
    // Intentar cargar tabla desde cache
    if (table_cache && ll1_table_load(&p->ll1, &p->grammar, table_cache)) {
        p->initialized = 1;
        parser_init(&p->ctx, &p->grammar, &p->ll1, NULL);
        return 1;
    }
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&p->grammar, &p->first);
    compute_follow_sets(&p->grammar, &p->first, &p->follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&p->grammar, &p->first, &p->follow, &p->ll1)) {
        LOG_WARN_MSG("parser", "la gramática no es LL(1)");
    }
    
    // Guardar en cache si se especificó
    if (table_cache) {
        ll1_table_save(&p->ll1, &p->grammar, table_cache);
    }
    
    parser_init(&p->ctx, &p->grammar, &p->ll1, &p->follow);
    p->initialized = 1;
    
    return 1;
}

int parser_create_predefined(Parser* p, const char* type, const char* table_cache)
{
    memset(p, 0, sizeof(Parser));
    
    const GrammarFactory *factory = grammar_factory_find(type);
    if (!factory) {
        LOG_ERROR_MSG("parser", "Tipo de gramática desconocido: %s", type);
        return 0;
    }
    factory->init(&p->grammar);
    
    // Intentar cargar tabla desde cache
    if (table_cache && ll1_table_load(&p->ll1, &p->grammar, table_cache)) {
        p->initialized = 1;
        parser_init(&p->ctx, &p->grammar, &p->ll1, NULL);
        return 1;
    }
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&p->grammar, &p->first);
    compute_follow_sets(&p->grammar, &p->first, &p->follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&p->grammar, &p->first, &p->follow, &p->ll1)) {
        LOG_WARN_MSG("parser", "la gramática no es LL(1)");
    }
    
    // Guardar en cache
    if (table_cache) {
        ll1_table_save(&p->ll1, &p->grammar, table_cache);
    }
    
    parser_init(&p->ctx, &p->grammar, &p->ll1, &p->follow);
    p->initialized = 1;
    
    return 1;
}

void parser_destroy(Parser* p)
{
    if (!p) return;
    
    grammar_free(&p->grammar);
    ll1_table_free(&p->ll1);
    p->initialized = 0;
}
