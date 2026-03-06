#ifndef PARSER_H
#define PARSER_H

#include "first_follow.h"
#include "grammar.h"
#include "ll1_table.h"
#include <stdio.h>

// ============== STACK DEL PARSER ==============

#define STACK_MAX 2048

typedef struct
{
    GrammarSymbol data[STACK_MAX];
    int top;
} ParserStack;

// ============== ESTRATEGIA DE RECUPERACIÓN DE ERRORES ==============
// Callback que el parser invoca al detectar un error sintáctico.
// Parámetros: contexto del parser, mensaje descriptivo.
// Retorna: 1 si se pudo recuperar (continuar), 0 si abortar.
struct ParserContext_s;  // forward declaration
typedef int (*ErrorRecoveryFn)(struct ParserContext_s *ctx, const char *msg);

// ============== PARSER CONTEXT ==============

typedef struct ParserContext_s {
    Grammar* grammar;
    LL1_Table* table;
    Follow_Table* follow;  // Para recuperación de errores (panic mode)
    ParserStack stack;
    
    // Funciones de callback para obtener tokens
    Token (*get_next_token)(void* ctx);
    void* lexer_ctx;
    
    // Token actual (lookahead)
    Token lookahead;
    
    // Control de errores
    int error_count;
    int max_errors;  // Límite antes de abortar (0 = sin límite)
    
    // Estrategia de recuperación de errores (si NULL → panic mode por defecto)
    ErrorRecoveryFn error_recovery;
} ParserContext;

// ============== FUNCIONES DEL PARSER ==============

// Inicializa el contexto del parser
void parser_init(ParserContext* ctx, Grammar* g, LL1_Table* table, Follow_Table* follow);

// Configura el lexer para el parser
void parser_set_lexer(ParserContext* ctx, Token (*get_token)(void*), void* lexer_ctx);

// Ejecuta el análisis sintáctico
// Retorna 1 si tiene éxito, 0 si hay errores
int parser_parse(ParserContext* ctx);

// Resetea el parser para una nueva entrada
void parser_reset(ParserContext* ctx);

// ============== FUNCIONES DE STACK ==============

static inline void stack_init(ParserStack* s) {
    s->top = 0;
}

static inline int stack_empty(ParserStack* s) {
    return s->top == 0;
}

static inline void stack_push(ParserStack* s, GrammarSymbol sym) {
    if (s->top < STACK_MAX)
        s->data[s->top++] = sym;
}

static inline GrammarSymbol stack_pop(ParserStack* s) {
    if (s->top > 0)
        return s->data[--s->top];
    return (GrammarSymbol){SYMBOL_END, END_MARKER};
}

static inline GrammarSymbol stack_peek(ParserStack* s) {
    if (s->top > 0)
        return s->data[s->top - 1];
    return (GrammarSymbol){SYMBOL_END, END_MARKER};
}

// ============== PARSER COMPLETO (TODO EN UNO) ==============

// Función de alto nivel que:
// 1. Carga/crea gramática
// 2. Calcula FIRST/FOLLOW o carga tablas
// 3. Construye tabla LL(1) o la carga
// 4. Parsea la entrada
typedef struct {
    Grammar grammar;
    First_Table first;
    Follow_Table follow;
    LL1_Table ll1;
    ParserContext ctx;
    int initialized;
} Parser;

// Inicializa un parser completo desde archivo de gramática
// table_cache: si no es NULL, intenta cargar/guardar tabla desde este archivo
int parser_create_from_file(Parser* p, const char* grammar_file, const char* table_cache);

// Inicializa un parser con gramática predefinida (regex o hulk)
int parser_create_predefined(Parser* p, const char* type, const char* table_cache);

// Libera recursos del parser
void parser_destroy(Parser* p);

#endif