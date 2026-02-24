/*
 * ll1_table.h — Tabla de análisis LL(1)
 *
 * Estructura de datos y operaciones para la tabla LL(1):
 * construcción desde FIRST/FOLLOW, consulta, serialización y exportación CSV.
 *
 * Extraído de parser.h para cumplir Interface Segregation (ISP):
 * los consumidores que solo necesitan la tabla (e.g. regex_parser)
 * ya no dependen del motor de parsing.
 */

#ifndef LL1_TABLE_H
#define LL1_TABLE_H

#include "grammar.h"
#include "first_follow.h"
#include <stdio.h>

// ============== CONSTANTES ==============

#define NO_PRODUCTION (-1)
#define SYNC_ENTRY (-2)  // Para recuperación de errores

// ============== ESTRUCTURA ==============

typedef struct
{
    int** table;      // M[A,a] = production index o -1 si error
    int nt_count;     // número de no terminales
    int t_count;      // número de terminales (+1 para $)
    int* t_map;       // mapeo terminal_id -> columna
    int t_map_size;
} LL1_Table;

// ============== API ==============

// Inicializa la tabla LL(1) a partir de las dimensiones de la gramática
void ll1_table_init(LL1_Table* t, Grammar* g);

// Libera memoria de la tabla LL(1)
void ll1_table_free(LL1_Table* t);

// Obtiene la columna correspondiente a un terminal_id (o END_MARKER).
// Retorna -1 si el terminal no está mapeado.
int ll1_table_get_column(LL1_Table* t, Grammar* g, int terminal_id);

// Construcción de la tabla (retorna 1 si es LL(1), 0 si hay conflictos)
int build_ll1_table(Grammar* g, First_Table* first_table,
                    Follow_Table* follow_table, LL1_Table* ll1);

// Imprime la tabla LL(1) (debugging)
void ll1_table_print(LL1_Table* t, Grammar* g);

// Exporta la tabla LL(1) a formato CSV
int ll1_table_save_csv(LL1_Table* t, Grammar* g, const char* filename);

// ============== SERIALIZACIÓN ==============

// Guarda la tabla LL(1) en archivo binario
int ll1_table_save(LL1_Table* t, Grammar* g, const char* filename);

// Carga la tabla LL(1) desde archivo binario
int ll1_table_load(LL1_Table* t, Grammar* g, const char* filename);

#endif /* LL1_TABLE_H */
