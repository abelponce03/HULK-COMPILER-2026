#include "afd.h"
#include <string.h>
#include <stdio.h>


// Inicialización del mapa posición→token
int pos_to_token[MAX_POSITIONS];

void init_pos_to_token() 
{
    for (int i = 0; i < MAX_POSITIONS; i++)
        pos_to_token[i] = -1;
}

// Crear un AFD vacío
DFA *dfa_create(char *alphabet, int alphabet_size) 
{
    DFA *dfa = (DFA *)malloc(sizeof(DFA));
    dfa->count = 0;
    dfa->capacity = 16;
    dfa->states = (DFAState *)malloc(sizeof(DFAState) * dfa->capacity);
    
    // IMPORTANTE: Copiar el alfabeto porque puede ser una variable local
    dfa->alphabet = (char *)malloc(alphabet_size + 1);
    memcpy(dfa->alphabet, alphabet, alphabet_size);
    dfa->alphabet[alphabet_size] = '\0';
    
    dfa->alphabet_size = alphabet_size;
    dfa->next_state = NULL;
    dfa->ast_root = NULL;
    return dfa;
}

// Liberar memoria del AFD
void dfa_free(DFA *dfa) {
    if (!dfa) return;
    
    for (int i = 0; i < dfa->count; i++) {
        if (dfa->states[i].transitions)
            free(dfa->states[i].transitions);
    }
    if (dfa->states) free(dfa->states);
    
    // Liberar alfabeto (ahora es memoria dinámica)
    if (dfa->alphabet) free(dfa->alphabet);
    
    // Liberar tabla next_state
    if (dfa->next_state) {
        for (int i = 0; i < dfa->count; i++) {
            if (dfa->next_state[i])
                free(dfa->next_state[i]);
        }
        free(dfa->next_state);
    }
    
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

    // Cola para BFS - usar tamaño fijo grande
    int front = 0;
    static PositionSet worklist[4096];  // Aumentar tamaño
    worklist[start_id] = start;

    while (front < dfa->count) {
        PositionSet current = worklist[front];
        int s_id = front;  // El índice en worklist corresponde al id del estado

        // Verificar si es estado de aceptación
        dfa->states[s_id].is_accept = 0;
        dfa->states[s_id].token_id = -1;
        for (int p = 0; p < MAX_POSITIONS; p++) 
        {   
            // Si la posición está en el conjunto y mapea a un token
            if (set_contains(&current, p) && pos_to_token[p] != -1) {
                dfa->states[s_id].is_accept = 1;
                // Elegir el token con menor ID (prioridad)
                if (dfa->states[s_id].token_id == -1 ||
                    pos_to_token[p] < dfa->states[s_id].token_id) {
                    dfa->states[s_id].token_id = pos_to_token[p];
                }
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
                if (to_id < 4096) {  // Verificar límites
                    worklist[to_id] = next;
                }
            }
            // IMPORTANTE: Acceder siempre por índice después de posible realloc
            dfa->states[s_id].transitions[a] = to_id;
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
void dfa_build_table(DFA *dfa) {
    if (dfa->next_state != NULL) return; // Ya construida
    
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
            unsigned char sym = (unsigned char)dfa->alphabet[a];
            int tid = dfa->states[s].transitions[a];
            if (tid != -1) {
                dfa->next_state[s][sym] = tid;
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

// ============== EXPORTACIÓN A DOT (Graphviz) ==============

// Escapa caracteres especiales para DOT
static void escape_char_dot(char c, char* buf) {
    if (c == '\\') strcpy(buf, "\\\\");
    else if (c == '"') strcpy(buf, "\\\"");
    else if (c == '\n') strcpy(buf, "\\n");
    else if (c == '\t') strcpy(buf, "\\t");
    else if (c == '\r') strcpy(buf, "\\r");
    else if (c == ' ') strcpy(buf, "␣");
    else if (c < 32 || c > 126) sprintf(buf, "0x%02X", (unsigned char)c);
    else { buf[0] = c; buf[1] = '\0'; }
}

int dfa_save_dot(DFA *dfa, const char *filename, const char** token_names) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: no se pudo crear %s\n", filename);
        return 0;
    }
    
    fprintf(f, "digraph DFA {\n");
    fprintf(f, "    rankdir=LR;\n");
    fprintf(f, "    node [shape=circle];\n");
    fprintf(f, "    \n");
    
    // Nodo inicial invisible
    fprintf(f, "    start [shape=point];\n");
    fprintf(f, "    start -> q0;\n\n");
    
    // Definir estados
    for (int i = 0; i < dfa->count; i++) {
        DFAState *s = &dfa->states[i];
        if (s->is_accept) {
            const char* tname = token_names && s->token_id >= 0 ? token_names[s->token_id] : "?";
            fprintf(f, "    q%d [shape=doublecircle, label=\"q%d\\n(%s)\"];\n", 
                    i, i, tname);
        } else {
            fprintf(f, "    q%d [label=\"q%d\"];\n", i, i);
        }
    }
    fprintf(f, "\n");
    
    // Agrupar transiciones por (origen, destino) para compactar etiquetas
    for (int i = 0; i < dfa->count; i++) {
        // Crear mapa de destino -> lista de símbolos
        for (int j = 0; j < dfa->count; j++) {
            char symbols[512] = "";
            int sym_count = 0;
            
            for (int a = 0; a < dfa->alphabet_size; a++) {
                if (dfa->states[i].transitions[a] == j) {
                    char escaped[16];
                    escape_char_dot(dfa->alphabet[a], escaped);
                    
                    if (sym_count > 0) strcat(symbols, ",");
                    strcat(symbols, escaped);
                    sym_count++;
                    
                    // Limitar longitud de etiqueta
                    if (strlen(symbols) > 40) {
                        strcat(symbols, "...");
                        break;
                    }
                }
            }
            
            if (sym_count > 0) {
                fprintf(f, "    q%d -> q%d [label=\"%s\"];\n", i, j, symbols);
            }
        }
    }
    
    fprintf(f, "}\n");
    fclose(f);
    
    printf("DFA exportado a DOT: %s\n", filename);
    printf("  Para visualizar: dot -Tpng %s -o dfa.png\n", filename);
    return 1;
}

// ============== EXPORTACIÓN A CSV ==============

int dfa_save_csv(DFA *dfa, const char *filename, const char** token_names) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Error: no se pudo crear %s\n", filename);
        return 0;
    }
    
    // Encabezado: Estado, Es_Aceptacion, Token, y cada símbolo del alfabeto
    fprintf(f, "Estado,Es_Aceptacion,Token");
    for (int a = 0; a < dfa->alphabet_size; a++) {
        char c = dfa->alphabet[a];
        if (c == ',') fprintf(f, ",\"comma\"");
        else if (c == '\n') fprintf(f, ",\"\\n\"");
        else if (c == '\t') fprintf(f, ",\"\\t\"");
        else if (c == '\r') fprintf(f, ",\"\\r\"");
        else if (c == ' ') fprintf(f, ",\" \"");
        else if (c < 32 || c > 126) fprintf(f, ",\"0x%02X\"", (unsigned char)c);
        else fprintf(f, ",%c", c);
    }
    fprintf(f, "\n");
    
    // Filas: cada estado
    for (int i = 0; i < dfa->count; i++) {
        DFAState *s = &dfa->states[i];
        const char* tname = "";
        if (s->is_accept && token_names && s->token_id >= 0) {
            tname = token_names[s->token_id];
        }
        
        fprintf(f, "q%d,%d,%s", i, s->is_accept, tname);
        
        for (int a = 0; a < dfa->alphabet_size; a++) {
            int next = s->transitions[a];
            if (next == -1) {
                fprintf(f, ",");
            } else {
                fprintf(f, ",q%d", next);
            }
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    printf("DFA exportado a CSV: %s\n", filename);
    return 1;
}
