#ifndef FIRST_H
#define FIRST_H

#include "parser.h"


#define MAX_SYMBOLS 256

#define EPSILON_INDEX (-1) // No es un simbolo, solo pertenece a FIRST

#define END_MARKER (-2) //representa $

typedef struct 
{
    int elements[MAX_SYMBOLS]; //IDs de simbolos
    int count; 
    int has_epsilon; // Bandera para epsilon
}First_Set;

//esto es para resolver problemas de indexacion de terminales y no terminales
static inline int symbol_index(GrammarSymbol s)
{
    if(s.type == SYMBOL_NON_TERMINAL)
        return s.id + NT_OFFSET;
    else
        return s.id + T_OFFSET;
}

//Tabla de conjuntos FIRST
typedef struct 
{
    First_Set first[MAX_SYMBOLS]; //Indexado por symbol_index()
}First_Table;


// Inicializa la tabla FIRST
void first_table_init(First_Table* table);

//Agrega un simbolo a un First_Set si no esta ya presente
static int first_set_add(First_Set* set, int symbol);

//Union de sets: FIRST(A) ← FIRST(A) ∪ FIRST(α)
static int first_set_union(First_Set* First_A, First_Set* First_α);

//Calcula FIRST de una secuencia α (array de GrammarSymbol)
static void first_of_sequence(First_Table* table, GrammarSymbol* seq, int n, First_Set* result);

//Calculo global de la tabla FIRST (iterativo)
//Esto calcula First para todos los simbolos
void compute_first_sets(Grammar* g, First_Table* table);


//Estructura de datos para el FOLLOW ----------------------------------
//FOLLOW solo se define para no terminales 

typedef struct
{
    int elements[MAX_SYMBOLS]; //IDs de simbolos o END_MARKER
    int count;
}Follow_Set;

typedef struct
{
    Follow_Set follow[MAX_SYMBOLS]; //Indexado por NonTerminal

}Follow_Table;


//inicializa la tabla FOLLOW
void follow_table_init(Follow_Table* table, Grammar* g);

static int follow_set_add(Follow_Set* set, int symbol);

static int follow_set_union(Follow_Set* Follow_A, Follow_Set* Follow_B);

//Calculo global de la tabla FOLLOW (iterativo)
void compute_follow_sets(Grammar* g, First_Table* first_table, Follow_Table* follow_table);


#endif