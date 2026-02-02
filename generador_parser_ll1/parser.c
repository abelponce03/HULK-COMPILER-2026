#include "parser.h"
#include <stdlib.h>
#include <string.h>

// ============== TABLA LL(1) ==============

void ll1_table_init(LL1_Table* t, Grammar* g) 
{
    t->nt_count = g->nt_count;
    t->t_count = g->t_count + 1; // +1 para $

    // Crear mapeo de terminal_id -> columna
    // Encontrar el máximo terminal_id
    int max_t = 0;
    for (int i = 0; i < g->t_count; i++) {
        if (g->terminals[i] > max_t)
            max_t = g->terminals[i];
    }
    
    t->t_map_size = max_t + 2; // +1 para el máximo, +1 para $
    t->t_map = malloc(sizeof(int) * t->t_map_size);
    for (int i = 0; i < t->t_map_size; i++)
        t->t_map[i] = -1;
    
    // Mapear cada terminal a su columna
    for (int i = 0; i < g->t_count; i++) {
        t->t_map[g->terminals[i]] = i;
    }

    // Crear tabla
    t->table = malloc(sizeof(int*) * t->nt_count);
    for (int i = 0; i < t->nt_count; i++) {
        t->table[i] = malloc(sizeof(int) * t->t_count);
        for (int j = 0; j < t->t_count; j++)
            t->table[i][j] = NO_PRODUCTION;
    }
}

void ll1_table_free(LL1_Table* t)
{
    if (!t) return;
    
    if (t->table) {
        for (int i = 0; i < t->nt_count; i++) {
            if (t->table[i]) free(t->table[i]);
        }
        free(t->table);
    }
    if (t->t_map) free(t->t_map);
    
    t->table = NULL;
    t->t_map = NULL;
}

// Obtener columna para un terminal
static int get_column(LL1_Table* t, Grammar* g, int terminal_id)
{
    if (terminal_id == END_MARKER)
        return g->t_count; // Última columna
    
    if (terminal_id >= 0 && terminal_id < t->t_map_size && t->t_map[terminal_id] >= 0)
        return t->t_map[terminal_id];
    
    return -1; // Terminal no encontrado
}

int build_ll1_table(Grammar* g, First_Table* first_table, Follow_Table* follow_table, LL1_Table* ll1) 
{
    ll1_table_init(ll1, g);

    int is_ll1 = 1;

    for (int p = 0; p < g->prod_count; p++) {
        Production* prod = &g->productions[p];
        int A = prod->left;

        // FIRST(α)
        First_Set first_alpha;
        first_alpha.count = 0;
        first_alpha.has_epsilon = 0;

        first_of_sequence(first_table, prod->right, prod->right_count, &first_alpha);

        // Regla 1: Para cada terminal a en FIRST(α), M[A,a] = producción
        for (int i = 0; i < first_alpha.count; i++) {
            int a = first_alpha.elements[i];
            int col = get_column(ll1, g, a);

            if (col >= 0) {
                if (ll1->table[A][col] != NO_PRODUCTION && ll1->table[A][col] != p) {
                    fprintf(stderr, "Conflicto LL(1) en M[%s, t%d]: prod %d vs %d\n",
                            g->nt_names[A], a, ll1->table[A][col], p);
                    is_ll1 = 0;
                }
                ll1->table[A][col] = p;
            }
        }

        // Regla 2: Si ε ∈ FIRST(α), para cada b en FOLLOW(A), M[A,b] = producción
        if (first_alpha.has_epsilon) {
            Follow_Set* followA = &follow_table->follow[A];

            for (int i = 0; i < followA->count; i++) {
                int b = followA->elements[i];
                int col = get_column(ll1, g, b);

                if (col >= 0) {
                    if (ll1->table[A][col] != NO_PRODUCTION && ll1->table[A][col] != p) {
                        fprintf(stderr, "Conflicto LL(1) en M[%s, t%d]: prod %d vs %d\n",
                                g->nt_names[A], b, ll1->table[A][col], p);
                        is_ll1 = 0;
                    }
                    ll1->table[A][col] = p;
                }
            }
        }
    }

    return is_ll1;
}

void ll1_table_print(LL1_Table* t, Grammar* g)
{
    printf("\n=== Tabla LL(1) ===\n");
    
    // Encabezado
    printf("%20s", "");
    for (int j = 0; j < g->t_count; j++) {
        printf(" %10s", g->t_names[j]);
    }
    printf(" %10s\n", "$");
    
    // Filas
    for (int i = 0; i < t->nt_count; i++) {
        printf("%20s", g->nt_names[i]);
        for (int j = 0; j < t->t_count; j++) {
            int p = t->table[i][j];
            if (p == NO_PRODUCTION)
                printf(" %10s", "-");
            else if (p == SYNC_ENTRY)
                printf(" %10s", "sync");
            else
                printf(" %10d", p);
        }
        printf("\n");
    }
}

// ============== SERIALIZACIÓN ==============

#define LL1_MAGIC 0x4C4C3101  // "LL1\x01"

int ll1_table_save(LL1_Table* t, Grammar* g, const char* filename)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;
    
    // Magic number
    unsigned int magic = LL1_MAGIC;
    fwrite(&magic, sizeof(magic), 1, f);
    
    // Dimensiones
    fwrite(&t->nt_count, sizeof(int), 1, f);
    fwrite(&t->t_count, sizeof(int), 1, f);
    fwrite(&t->t_map_size, sizeof(int), 1, f);
    
    // Mapeo de terminales
    fwrite(t->t_map, sizeof(int), t->t_map_size, f);
    
    // Tabla
    for (int i = 0; i < t->nt_count; i++) {
        fwrite(t->table[i], sizeof(int), t->t_count, f);
    }
    
    // Producciones (necesarias para el parsing)
    fwrite(&g->prod_count, sizeof(int), 1, f);
    for (int i = 0; i < g->prod_count; i++) {
        Production* p = &g->productions[i];
        fwrite(&p->left, sizeof(int), 1, f);
        fwrite(&p->right_count, sizeof(int), 1, f);
        for (int j = 0; j < p->right_count; j++) {
            fwrite(&p->right[j].type, sizeof(int), 1, f);
            fwrite(&p->right[j].id, sizeof(int), 1, f);
        }
    }
    
    fclose(f);
    printf("Tabla LL(1) guardada en %s\n", filename);
    return 1;
}

int ll1_table_load(LL1_Table* t, Grammar* g, const char* filename)
{
    FILE* f = fopen(filename, "rb");
    if (!f) return 0;
    
    // Verificar magic
    unsigned int magic;
    fread(&magic, sizeof(magic), 1, f);
    if (magic != LL1_MAGIC) {
        fclose(f);
        return 0;
    }
    
    // Dimensiones
    fread(&t->nt_count, sizeof(int), 1, f);
    fread(&t->t_count, sizeof(int), 1, f);
    fread(&t->t_map_size, sizeof(int), 1, f);
    
    // Mapeo
    t->t_map = malloc(sizeof(int) * t->t_map_size);
    fread(t->t_map, sizeof(int), t->t_map_size, f);
    
    // Tabla
    t->table = malloc(sizeof(int*) * t->nt_count);
    for (int i = 0; i < t->nt_count; i++) {
        t->table[i] = malloc(sizeof(int) * t->t_count);
        fread(t->table[i], sizeof(int), t->t_count, f);
    }
    
    // Producciones
    int prod_count;
    fread(&prod_count, sizeof(int), 1, f);
    
    if (g->productions) {
        // Liberar producciones existentes
        for (int i = 0; i < g->prod_count; i++)
            if (g->productions[i].right) free(g->productions[i].right);
        free(g->productions);
    }
    
    g->prod_count = prod_count;
    g->prod_capacity = prod_count;
    g->productions = malloc(sizeof(Production) * prod_count);
    
    for (int i = 0; i < prod_count; i++) {
        Production* p = &g->productions[i];
        fread(&p->left, sizeof(int), 1, f);
        fread(&p->right_count, sizeof(int), 1, f);
        p->production_id = i;
        
        if (p->right_count > 0) {
            p->right = malloc(sizeof(GrammarSymbol) * p->right_count);
            for (int j = 0; j < p->right_count; j++) {
                fread(&p->right[j].type, sizeof(int), 1, f);
                fread(&p->right[j].id, sizeof(int), 1, f);
            }
        } else {
            p->right = NULL;
        }
    }
    
    fclose(f);
    printf("Tabla LL(1) cargada desde %s\n", filename);
    return 1;
}

// ============== PARSER ==============

void parser_init(ParserContext* ctx, Grammar* g, LL1_Table* table)
{
    ctx->grammar = g;
    ctx->table = table;
    stack_init(&ctx->stack);
    ctx->get_next_token = NULL;
    ctx->lexer_ctx = NULL;
    ctx->error_count = 0;
    ctx->line = 1;
    ctx->column = 1;
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
    ctx->line = 1;
    ctx->column = 1;
}

int parser_parse(ParserContext* ctx)
{
    if (!ctx->grammar || !ctx->table || !ctx->get_next_token) {
        fprintf(stderr, "Parser no configurado correctamente\n");
        return 0;
    }
    
    Grammar* g = ctx->grammar;
    LL1_Table* ll1 = ctx->table;
    ParserStack* stack = &ctx->stack;
    
    // Inicializar stack: push $ y símbolo inicial
    stack_init(stack);
    stack_push(stack, (StackSymbol){STACK_END, END_MARKER});
    stack_push(stack, (StackSymbol){STACK_NON_TERMINAL, g->start_symbol});
    
    // Obtener primer token
    ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
    
    while (1) {
        StackSymbol top = stack_peek(stack);
        
        // Caso: stack vacío (solo $)
        if (top.type == STACK_END) {
            if (ctx->lookahead.type == TOKEN_EOF) {
                return ctx->error_count == 0; // Éxito
            } else {
                fprintf(stderr, "Error: entrada extra después de parse completo\n");
                ctx->error_count++;
                return 0;
            }
        }
        
        // Caso: Terminal en el stack
        if (top.type == STACK_TERMINAL) {
            if (top.id == ctx->lookahead.type) {
                // Match!
                stack_pop(stack);
                ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
            } else {
                // Error: terminal no coincide
                const char* expected = "?";
                const char* found = "?";
                
                // Buscar nombres
                for (int i = 0; i < g->t_count; i++) {
                    if (g->terminals[i] == top.id)
                        expected = g->t_names[i];
                    if (g->terminals[i] == ctx->lookahead.type)
                        found = g->t_names[i];
                }
                
                fprintf(stderr, "Error sintáctico: se esperaba '%s', se encontró '%s'\n",
                        expected, found);
                ctx->error_count++;
                
                // Recuperación: descartar el terminal del stack
                stack_pop(stack);
            }
        }
        // Caso: No terminal en el stack
        else if (top.type == STACK_NON_TERMINAL) {
            int row = top.id;
            int col;
            
            if (ctx->lookahead.type == TOKEN_EOF) {
                col = g->t_count; // Columna para $
            } else {
                col = get_column(ll1, g, ctx->lookahead.type);
            }
            
            if (col < 0) {
                fprintf(stderr, "Error: token %d no reconocido en gramática\n", 
                        ctx->lookahead.type);
                ctx->error_count++;
                ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
                continue;
            }
            
            int prod_index = ll1->table[row][col];
            
            if (prod_index == NO_PRODUCTION) {
                // Error: no hay producción
                const char* nt_name = g->nt_names[row];
                const char* t_name = "?";
                for (int i = 0; i < g->t_count; i++) {
                    if (g->terminals[i] == ctx->lookahead.type) {
                        t_name = g->t_names[i];
                        break;
                    }
                }
                if (ctx->lookahead.type == TOKEN_EOF)
                    t_name = "$";
                
                fprintf(stderr, "Error sintáctico: no hay producción para [%s, %s]\n",
                        nt_name, t_name);
                ctx->error_count++;
                
                // Recuperación en modo pánico: descartar token
                ctx->lookahead = ctx->get_next_token(ctx->lexer_ctx);
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
            
            // Push RHS en orden inverso (si no es ε)
            for (int i = prod->right_count - 1; i >= 0; i--) {
                GrammarSymbol s = prod->right[i];
                
                if (s.type == SYMBOL_TERMINAL) {
                    stack_push(stack, (StackSymbol){STACK_TERMINAL, s.id});
                } else if (s.type == SYMBOL_NON_TERMINAL) {
                    stack_push(stack, (StackSymbol){STACK_NON_TERMINAL, s.id});
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
        fprintf(stderr, "Error cargando gramática desde %s\n", grammar_file);
        return 0;
    }
    
    // Intentar cargar tabla desde cache
    if (table_cache && ll1_table_load(&p->ll1, &p->grammar, table_cache)) {
        p->initialized = 1;
        parser_init(&p->ctx, &p->grammar, &p->ll1);
        return 1;
    }
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&p->grammar, &p->first);
    compute_follow_sets(&p->grammar, &p->first, &p->follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&p->grammar, &p->first, &p->follow, &p->ll1)) {
        fprintf(stderr, "Advertencia: la gramática no es LL(1)\n");
    }
    
    // Guardar en cache si se especificó
    if (table_cache) {
        ll1_table_save(&p->ll1, &p->grammar, table_cache);
    }
    
    parser_init(&p->ctx, &p->grammar, &p->ll1);
    p->initialized = 1;
    
    return 1;
}

int parser_create_predefined(Parser* p, const char* type, const char* table_cache)
{
    memset(p, 0, sizeof(Parser));
    
    if (strcmp(type, "regex") == 0) {
        grammar_init_regex(&p->grammar);
    } else if (strcmp(type, "hulk") == 0) {
        grammar_init_hulk(&p->grammar);
    } else {
        fprintf(stderr, "Tipo de gramática desconocido: %s\n", type);
        return 0;
    }
    
    // Intentar cargar tabla desde cache
    if (table_cache && ll1_table_load(&p->ll1, &p->grammar, table_cache)) {
        p->initialized = 1;
        parser_init(&p->ctx, &p->grammar, &p->ll1);
        return 1;
    }
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&p->grammar, &p->first);
    compute_follow_sets(&p->grammar, &p->first, &p->follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&p->grammar, &p->first, &p->follow, &p->ll1)) {
        fprintf(stderr, "Advertencia: la gramática no es LL(1)\n");
    }
    
    // Guardar en cache
    if (table_cache) {
        ll1_table_save(&p->ll1, &p->grammar, table_cache);
    }
    
    parser_init(&p->ctx, &p->grammar, &p->ll1);
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
