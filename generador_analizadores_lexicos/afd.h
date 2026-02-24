#ifndef AFD_H
#define AFD_H

#include "ast.h"
#include <stdlib.h>


// Un estado del AFD
typedef struct {
    PositionSet positions;  // conjunto de posiciones de este estado
    int *transitions;       // índices de transición por símbolo (o -1)
    int  is_accept;         // 1 si es estado de aceptación
    int  token_id;          // token reconocido si es aceptación
} DFAState;

// El AFD en su totalidad
typedef struct {
    DFAState *states;
    int       count;
    int       capacity;
    char     *alphabet;     // conjunto de símbolos
    int       alphabet_size;
    int     **next_state;   // tabla next_state[state][byte], 256 entradas
} DFA;

// ============== ESTRATEGIA DE PRIORIDAD DE TOKENS ==============
// Callback para resolver conflictos cuando un estado DFA acepta múltiples tokens.
// Recibe los dos token_id en conflicto y retorna el de mayor prioridad.
typedef int (*TokenPriorityFn)(int token_a, int token_b);

// Estrategia por defecto: menor token_id = mayor prioridad
// (keywords antes que IDENT por convención del enum TokenType)
int dfa_priority_min_id(int a, int b);

// Funciones principales
DFA *dfa_create(char *alphabet, int alphabet_size);
void dfa_free(DFA *dfa);

// Construcción del DFA.  Si priority==NULL usa dfa_priority_min_id.
void dfa_build(DFA *dfa, ASTNode *root, ASTContext *ctx,
               TokenPriorityFn priority);

void dfa_build_table(DFA *dfa);

// Exportación para visualización
int dfa_save_dot(DFA *dfa, const char *filename, const char** token_names);
int dfa_save_csv(DFA *dfa, const char *filename, const char** token_names);

#endif // AFD_H
