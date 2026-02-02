#ifndef PARSER_H
#define PARSER_H

#include "first_&_follow.h"
#include "grammar.h"
#include <stdio.h>

// ============== TABLA LL(1) ==============

#define NO_PRODUCTION (-1)
#define SYNC_ENTRY (-2)  // Para recuperación de errores

typedef struct
{
    int** table;      // M[A,a] = production index o -1 si error
    int nt_count;     // número de no terminales
    int t_count;      // número de terminales (+1 para $)
    int* t_map;       // mapeo terminal_id -> columna
    int t_map_size;
} LL1_Table;

// Construye la tabla LL(1) a partir de la gramática, tabla FIRST y FOLLOW
void ll1_table_init(LL1_Table* t, Grammar* g);

// Libera memoria de la tabla LL(1)
void ll1_table_free(LL1_Table* t);

// Construcción de la tabla (retorna 1 si es LL(1), 0 si hay conflictos)
int build_ll1_table(Grammar* g, First_Table* first_table, Follow_Table* follow_table, LL1_Table* ll1);

// Imprime la tabla LL(1) (debugging)
void ll1_table_print(LL1_Table* t, Grammar* g);

// ============== SERIALIZACIÓN ==============

// Guarda la tabla LL(1) en archivo binario
int ll1_table_save(LL1_Table* t, Grammar* g, const char* filename);

// Carga la tabla LL(1) desde archivo binario
int ll1_table_load(LL1_Table* t, Grammar* g, const char* filename);

// ============== STACK DEL PARSER ==============

typedef enum
{
    STACK_TERMINAL,
    STACK_NON_TERMINAL,
    STACK_END
} StackSymbolType;

typedef struct
{
    StackSymbolType type;
    int id;  // TokenType o NonTerminal dependiendo de type
} StackSymbol;

#define STACK_MAX 2048

typedef struct
{
    StackSymbol data[STACK_MAX];
    int top;
} ParserStack;

// ============== PARSER CONTEXT ==============

typedef struct {
    Grammar* grammar;
    LL1_Table* table;
    ParserStack stack;
    
    // Funciones de callback para obtener tokens
    Token (*get_next_token)(void* ctx);
    void* lexer_ctx;
    
    // Token actual (lookahead)
    Token lookahead;
    
    // Para reportar errores
    int error_count;
    int line;
    int column;
} ParserContext;

// ============== FUNCIONES DEL PARSER ==============

// Inicializa el contexto del parser
void parser_init(ParserContext* ctx, Grammar* g, LL1_Table* table);

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

static inline void stack_push(ParserStack* s, StackSymbol sym) {
    if (s->top < STACK_MAX)
        s->data[s->top++] = sym;
}

static inline StackSymbol stack_pop(ParserStack* s) {
    if (s->top > 0)
        return s->data[--s->top];
    return (StackSymbol){STACK_END, END_MARKER};
}

static inline StackSymbol stack_peek(ParserStack* s) {
    if (s->top > 0)
        return s->data[s->top - 1];
    return (StackSymbol){STACK_END, END_MARKER};
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

// Parsea una cadena usando el parser
int parser_parse_string(Parser* p, const char* input);

#endif