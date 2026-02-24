/*
 * ll1_table.c — Tabla de análisis LL(1)
 *
 * Responsabilidad única: construir, consultar, serializar y exportar
 * la tabla de parsing LL(1).  Extraído de parser.c para cumplir SRP/ISP.
 */

#include "ll1_table.h"
#include "../error_handler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============== INICIALIZACIÓN / LIBERACIÓN ==============

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

// ============== CONSULTA ==============

int ll1_table_get_column(LL1_Table* t, Grammar* g, int terminal_id)
{
    if (terminal_id == END_MARKER)
        return g->t_count; // Última columna
    
    if (terminal_id >= 0 && terminal_id < t->t_map_size && t->t_map[terminal_id] >= 0)
        return t->t_map[terminal_id];
    
    return -1; // Terminal no encontrado
}

// ============== CONSTRUCCIÓN ==============

int build_ll1_table(Grammar* g, First_Table* first_table,
                    Follow_Table* follow_table, LL1_Table* ll1) 
{
    ll1_table_init(ll1, g);

    int is_ll1 = 1;

    for (int p = 0; p < g->prod_count; p++) {
        Production* prod = &g->productions[p];
        int A = prod->left;
        
        // Determinar si es producción epsilon
        int is_epsilon_prod = (prod->right_count == 0 || 
                               (prod->right_count == 1 && prod->right[0].type == SYMBOL_EPSILON));

        // FIRST(α)
        First_Set first_alpha;
        first_alpha.count = 0;
        first_alpha.has_epsilon = 0;

        first_of_sequence(first_table, prod->right, prod->right_count, &first_alpha);

        // Regla 1: Para cada terminal a en FIRST(α), M[A,a] = producción
        for (int i = 0; i < first_alpha.count; i++) {
            int a = first_alpha.elements[i];
            int col = ll1_table_get_column(ll1, g, a);

            if (col >= 0) {
                if (ll1->table[A][col] != NO_PRODUCTION && ll1->table[A][col] != p) {
                    LOG_WARN_MSG("ll1", "Conflicto LL(1) en M[%s, t%d]: prod %d vs %d",
                                 g->nt_names[A], a, ll1->table[A][col], p);
                    is_ll1 = 0;
                    // Preferir la producción NO epsilon
                    Production* existing = &g->productions[ll1->table[A][col]];
                    int existing_is_epsilon = (existing->right_count == 0 || 
                                               (existing->right_count == 1 && existing->right[0].type == SYMBOL_EPSILON));
                    // Si la existente es epsilon y la nueva no, usar la nueva
                    if (existing_is_epsilon && !is_epsilon_prod) {
                        ll1->table[A][col] = p;
                    }
                    // Si la nueva es epsilon, mantener la existente
                } else {
                    ll1->table[A][col] = p;
                }
            }
        }

        // Regla 2: Si ε ∈ FIRST(α), para cada b en FOLLOW(A), M[A,b] = producción
        if (first_alpha.has_epsilon) {
            Follow_Set* followA = &follow_table->follow[A];

            for (int i = 0; i < followA->count; i++) {
                int b = followA->elements[i];
                int col = ll1_table_get_column(ll1, g, b);

                if (col >= 0) {
                    if (ll1->table[A][col] != NO_PRODUCTION && ll1->table[A][col] != p) {
                        LOG_WARN_MSG("ll1", "Conflicto LL(1) en M[%s, t%d]: prod %d vs %d",
                                     g->nt_names[A], b, ll1->table[A][col], p);
                        is_ll1 = 0;
                        // Preferir la producción NO epsilon (que ya existe)
                        // No sobrescribir con producción epsilon
                    } else {
                        ll1->table[A][col] = p;
                    }
                }
            }
        }
    }

    return is_ll1;
}

// ============== IMPRESIÓN ==============

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

// ============== EXPORTAR A CSV ==============

int ll1_table_save_csv(LL1_Table* t, Grammar* g, const char* filename)
{
    FILE* f = fopen(filename, "w");
    if (!f) {
        LOG_ERROR_MSG("ll1", "no se pudo crear %s", filename);
        return 0;
    }
    
    // Encabezado CSV
    fprintf(f, "No-Terminal");
    for (int j = 0; j < g->t_count; j++) {
        fprintf(f, ",%s", g->t_names[j]);
    }
    fprintf(f, ",$\n");
    
    // Filas con producciones
    for (int i = 0; i < t->nt_count; i++) {
        fprintf(f, "%s", g->nt_names[i]);
        for (int j = 0; j < t->t_count; j++) {
            int p = t->table[i][j];
            if (p == NO_PRODUCTION) {
                fprintf(f, ",");
            } else if (p == SYNC_ENTRY) {
                fprintf(f, ",sync");
            } else {
                // Escribir la producción completa
                Production* prod = &g->productions[p];
                fprintf(f, ",\"%s ->", g->nt_names[prod->left]);
                if (prod->right_count == 0) {
                    fprintf(f, " ε");
                } else {
                    for (int k = 0; k < prod->right_count; k++) {
                        GrammarSymbol* s = &prod->right[k];
                        if (s->type == SYMBOL_TERMINAL) {
                            // Buscar nombre del terminal
                            const char* tname = "?";
                            for (int ti = 0; ti < g->t_count; ti++) {
                                if (g->terminals[ti] == s->id) {
                                    tname = g->t_names[ti];
                                    break;
                                }
                            }
                            fprintf(f, " %s", tname);
                        } else {
                            fprintf(f, " %s", g->nt_names[s->id]);
                        }
                    }
                }
                fprintf(f, "\"");
            }
        }
        fprintf(f, "\n");
    }
    
    fclose(f);
    printf("Tabla LL(1) exportada a CSV: %s\n", filename);
    return 1;
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
