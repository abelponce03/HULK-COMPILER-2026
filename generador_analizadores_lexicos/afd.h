#ifndef AFD_H
#define AFD_H

#include "ast.h"
#include <stdlib.h>

// Tabla global para mapear posiciones de hoja (#) a token IDs.
// Debe inicializarse desde donde construyas el AST para cada token.
extern int pos_to_token[MAX_POSITIONS];


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
    int     **next_state; // tabla next_state[state][ascii]
} DFA;

// Funciones principales
DFA *dfa_create(char *alphabet, int alphabet_size);
void dfa_free(DFA *dfa);
void dfa_build(DFA *dfa, ASTNode *root);
int  dfa_find_state(DFA *dfa, PositionSet *set);
int  dfa_add_state(DFA *dfa, PositionSet *set);
void dfa_print(DFA *dfa);

void dfa_build_table(DFA *dfa);
void dfa_simulate(DFA *dfa, const char *input);

#endif // AFD_H
