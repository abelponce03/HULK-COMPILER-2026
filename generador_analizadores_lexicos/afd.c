#include "afd.h"
#include <string.h>
#include <stdio.h>


// Inicialización del mapa posición→token
int pos_to_token[MAX_POSITIONS] = {0};


// Crear un AFD vacío
DFA *dfa_create(char *alphabet, int alphabet_size) {
    DFA *dfa = (DFA *)malloc(sizeof(DFA));
    dfa->count = 0;
    dfa->capacity = 16;
    dfa->states = (DFAState *)malloc(sizeof(DFAState) * dfa->capacity);
    dfa->alphabet = alphabet;
    dfa->alphabet_size = alphabet_size;
    return dfa;
}

// Liberar memoria del AFD
void dfa_free(DFA *dfa) {
    for (int i = 0; i < dfa->count; i++) {
        free(dfa->states[i].transitions);
    }
    free(dfa->states);
    free(dfa);
}

// Comparar dos conjuntos de posiciones
int positions_equal(PositionSet *a, PositionSet *b) {
    return memcmp(a->bits, b->bits, sizeof(a->bits)) == 0;
}

// Buscar si el conjunto ya existe como estado
int dfa_find_state(DFA *dfa, PositionSet *set) {
    for (int i = 0; i < dfa->count; i++) {
        if (positions_equal(&dfa->states[i].positions, set)) {
            return i;
        }
    }
    return -1;
}

// Agrega un estado nuevo al AFD
int dfa_add_state(DFA *dfa, PositionSet *set) {
    if (dfa->count == dfa->capacity) {
        dfa->capacity *= 2;
        dfa->states = (DFAState *)realloc(dfa->states, sizeof(DFAState) * dfa->capacity);
    }

    int id = dfa->count++;
    DFAState *s = &dfa->states[id];

    s->positions = *set;
    s->transitions = (int *)malloc(sizeof(int) * dfa->alphabet_size);
    for (int i = 0; i < dfa->alphabet_size; i++) {
        s->transitions[i] = -1;
    }
    s->is_accept = 0;
    s->token_id = -1;
    return id;
}

// Construir el AFD desde el AST
void dfa_build(DFA *dfa, ASTNode *root) {
    // Estado inicial
    PositionSet start = root->firstpos;
    int start_id = dfa_add_state(dfa, &start);

    // Cola para BFS
    int front = 0;
    PositionSet worklist[dfa->capacity];
    worklist[start_id] = start;

    while (front < dfa->count) {
        PositionSet current = worklist[front];
        int s_id = dfa_find_state(dfa, &current);
        DFAState *s = &dfa->states[s_id];

        // Marcar estado de aceptación si contiene posiciones finales (#)
        for (int p = 0; p < MAX_POSITIONS; p++) {
            if (set_contains(&current, p) && pos_to_token[p] != 0) {
                s->is_accept = 1;
                // Asignar token según el mapeo pos->token
                s->token_id = pos_to_token[p];
                // Si hay múltiples aceptaciones posibles, 
                //puedes decidir priorizar:
                // (por ejemplo, menor token_id 
                //o un orden predefinido)
                // Aquí simple toma el primero que aparece.
                break;
            }
        }


        // Para cada símbolo del alfabeto
        for (int a = 0; a < dfa->alphabet_size; a++) {
            char sym = dfa->alphabet[a];
            PositionSet next;
            set_init(&next);

            // Recolectar uniones de followpos
            for (int p = 0; p < MAX_POSITIONS; p++) {
                if (set_contains(&current, p)) {
                    // Obtener símbolo en la posición p
                    ASTNode *leaf = find_leaf_by_pos(root, p);
                    if (leaf && leaf->symbol == sym) {
                        set_union(&next, &next, &followpos[p]);
                    }
                }
            }

            if (!memcmp(next.bits, (PositionSet){0}.bits, sizeof(next.bits))) {
                // vacío, no transicion
                continue;
            }

            int to_id = dfa_find_state(dfa, &next);
            if (to_id == -1) {
                to_id = dfa_add_state(dfa, &next);
                worklist[to_id] = next;
            }
            s->transitions[a] = to_id;
        }

        front++;
    }
}

// Imprimir AFD (para debugging)
void dfa_print(DFA *dfa) {
    printf("AFD con %d estados\n", dfa->count);
    for (int i = 0; i < dfa->count; i++) {
        printf("estado %d: accept=%d token=%d\n",
               i, dfa->states[i].is_accept, dfa->states[i].token_id);
        for (int a = 0; a < dfa->alphabet_size; a++) {
            if (dfa->states[i].transitions[a] != -1) {
                printf("  '%c' -> %d\n", dfa->alphabet[a],
                       dfa->states[i].transitions[a]);
            }
        }
    }
}

// Construye la tabla next_state simple
static void dfa_build_table(DFA *dfa) {
    int n = dfa->count;
    int A = 128; // ASCII 0..127

    dfa->next_state = malloc(n * sizeof(int *));
    for (int s = 0; s < n; s++) {
        dfa->next_state[s] = calloc(A, sizeof(int));
        for (int c = 0; c < A; c++) {
            dfa->next_state[s][c] = -1;
        }
    }

    for (int s = 0; s < n; s++) {
        for (int a = 0; a < dfa->alphabet_size; a++) {
            char sym = dfa->alphabet[a];
            int tid = dfa->states[s].transitions[a];
            if (tid != -1) {
                dfa->next_state[s][(int)sym] = tid;
            }
        }
    }
}

void dfa_simulate(DFA *dfa, const char *input) {
    if (dfa->next_state == NULL) {
        dfa_build_table(dfa);
    }

    int estado = 0;     // estado inicial
    int pos = 0;
    
    while (input[pos]) {
        unsigned char c = input[pos];
        int sig = dfa->next_state[estado][c];

        if (sig == -1) {
            // transicion inválida
            printf("Error lexico en '%c' (pos %d)\n", c, pos);
            return;
        }

        estado = sig;
        pos++;
    }

    if (dfa->states[estado].is_accept) {
        printf("Cadena aceptada como token %d\n",
               dfa->states[estado].token_id);
    } else {
        printf("Cadena NO aceptada\n");
    }
}

