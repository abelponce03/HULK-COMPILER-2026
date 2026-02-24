#ifndef FIRST_FOLLOW_H
#define FIRST_FOLLOW_H

#include "grammar.h"

#define MAX_SYMBOLS 256

#define EPSILON_INDEX (-1) // No es un simbolo, solo pertenece a FIRST

#define END_MARKER (-2) //representa $

// ============== FIRST SET ==============

typedef struct 
{
    int elements[MAX_SYMBOLS]; //IDs de simbolos
    int count; 
    int has_epsilon; // Bandera para epsilon
} First_Set;

// Indexación para la tabla FIRST
static inline int symbol_index(GrammarSymbol s)
{
    if(s.type == SYMBOL_NON_TERMINAL)
        return s.id + NT_OFFSET;
    else
        return s.id + T_OFFSET;
}

// Tabla de conjuntos FIRST
typedef struct 
{
    First_Set first[MAX_SYMBOLS]; //Indexado por symbol_index()
} First_Table;

// ============== FOLLOW SET ==============
// FOLLOW solo se define para no terminales 

typedef struct
{
    int elements[MAX_SYMBOLS]; //IDs de simbolos o END_MARKER
    int count;
} Follow_Set;

typedef struct
{
    Follow_Set follow[MAX_SYMBOLS]; //Indexado por NonTerminal
} Follow_Table;

// ============== FUNCIONES PÚBLICAS ==============

// Inicializa la tabla FIRST
void first_table_init(First_Table* table);

// Calculo global de la tabla FIRST (iterativo)
void compute_first_sets(Grammar* g, First_Table* table);

// Calcula FIRST de una secuencia α (array de GrammarSymbol)
void first_of_sequence(First_Table* table, GrammarSymbol* seq, int n, First_Set* result);

// Inicializa la tabla FOLLOW
void follow_table_init(Follow_Table* table, Grammar* g);

// Calculo global de la tabla FOLLOW (iterativo)
void compute_follow_sets(Grammar* g, First_Table* first_table, Follow_Table* follow_table);

// ============== FUNCIONES DE MANIPULACIÓN DE SETS ==============

// Agrega un simbolo a un First_Set si no esta ya presente
int first_set_add(First_Set* set, int symbol);

// Union de sets: FIRST(A) ← FIRST(A) ∪ FIRST(α)
int first_set_union(First_Set* First_A, First_Set* First_alpha);

// Agrega un simbolo a un Follow_Set si no esta ya presente
int follow_set_add(Follow_Set* set, int symbol);

// Union de Follow sets
int follow_set_union(Follow_Set* Follow_A, Follow_Set* Follow_B);

// Verifica si un símbolo está en un First_Set
int first_set_contains(First_Set* set, int symbol);

// Verifica si un símbolo está en un Follow_Set  
int follow_set_contains(Follow_Set* set, int symbol);

// ============== DEBUG ==============

void print_first_sets(Grammar* g, First_Table* table);
void print_follow_sets(Grammar* g, Follow_Table* table);

#endif