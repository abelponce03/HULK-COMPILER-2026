#include "first_&_follow.h"
#include <stdlib.h>
#include <string.h>

//El objetivo es inicializar cada conjunto First vacio
//con count = 0 y has_epsilon = 0
void first_table_init(First_Table* table) {
    if (!table) return;

    // Recorremos todos los posibles índices
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        table->first[i].count = 0;
        table->first[i].has_epsilon = 0;
        // Opcional: inicializar array elements a -1
        for (int j = 0; j < MAX_SYMBOLS; j++) {
            table->first[i].elements[j] = -1;
        }
    }
}

//Agrega un simbolo a un First_Set si no esta ya presente
static int first_set_add(First_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        //Ya esta presente
        if(set->elements[i] == symbol) return 0;     
    }
    if(set->count < MAX_SYMBOLS)
    {
        set->elements[set->count++] = symbol;
        return 1; // NO ESTABA PRESENTE
    }
    return 0; //No se pudo agregar (lleno)
}

//Union de sets: FIRST(A) ← FIRST(A) ∪ FIRST(α)
static int first_set_union(First_Set* First_A, First_Set* First_α)
{
    int changed = 0;
    //Agregar todos los elementos de First(α) a First(A)
    for(int i = 0; i < First_α->count; i++)
    {
        if(first_set_add(First_A, First_α->elements[i]))
            changed = 1;
    }
    //Manejar epsilon
    if(First_α->has_epsilon && !First_A->has_epsilon)
    {
        First_A->has_epsilon = 1;
        changed = 1;
    }
    return changed;
}

//Calcula FIRST de una secuencia α (array de GrammarSymbol)
static void first_of_sequence(First_Table* table, GrammarSymbol* seq, int n, First_Set* result)
{
    result->count = 0;
    result->has_epsilon = 0;
    int all_have_epsilon = 1;

    for(int i = 0; i < n; i++)
    {
        int index = symbol_index(seq[i]);
        First_Set* fs = &table->first[index];

        //Agregamos FIRST(seq[i]) - {ε}
        for(int j = 0; j < fs->count; j++)
        {
            first_set_add(result, fs->elements[j]);
        }

        if(!fs->has_epsilon)
        {
            all_have_epsilon = 0;
            break; //Detener al no encontrar simbolo que no deriva ε
        }
    }
    //Si todos los simbolos derivan ε, entonces ε ∈ FIRST(α)
    if(all_have_epsilon)
    {
        result->has_epsilon = 1;
    }
}

//Calculo global de la tabla FIRST (iterativo)
//Esto calcula First para todos los simbolos
void compute_first_sets(Grammar* g, First_Table* table)
{
    if(!g || !table) return;

    first_table_init(table); // inicializamos todo a vacio

    //inicializar FIRST de terminales: FIRST(a) = {a}
    for(int i = 0; i < g->t_count; i++)
    {
        GrammarSymbol s = {SYMBOL_TERMINAL, g->terminals[i]};
        int index = symbol_index(s);
        table->first[index].count = 0;
        table->first[index].has_epsilon = 0;
        first_set_add(&table->first[index], g->terminals[i]);
    }

    //Iterativo hasta que no haya cambios (convergencia)
    int changed;
    do 
    {
        changed = 0;

        //Recorrer todas las producciones
        for(int p = 0; p < g->prod_count; p++)
        {
            Production* prod = &g->productions[p];
            int left_index = NT_OFFSET + prod->left;

            First_Set temp;
            first_of_sequence(table, prod->right, prod->right_count, &temp);

            if(first_set_union(&table->first[left_index], &temp))
            {
                changed = 1;
            }

        }
    }
    while(changed);
}


//Todo lo relacionado con el conjunto follow a partir de aqui

static int follow_set_add(Follow_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        if(set->elements[i] == symbol) return 0;
    }

    if(set->count < MAX_SYMBOLS)
    {
        set->elements[set->count++] = symbol;
        return 1; // NO ESTABA PRESENTE
    }

    return 0; //No se pudo agregar (lleno)
}

static int follow_set_union(Follow_Set* Follow_A, Follow_Set* Follow_B)
{
    int changed = 0;
    for(int i = 0; i < Follow_B->count; i++)
    {
        if(follow_set_add(Follow_A, Follow_B->elements[i]))
            changed = 1;
    }
    return changed;
}


void follow_table_init(Follow_Table* table, Grammar* g)
{
    if(!table || !g) return;

    for(int i = 0; i < g->nt_count; i++)
    {
        table->follow[i].count = 0;
    }

    follow_set_add(&table->follow[g->start_symbol], END_MARKER);
}

void compute_follow_sets(Grammar* g, First_Table* first_table, Follow_Table* follow_table)
{
    if(!g || !first_table || !follow_table) return;

    follow_table_init(follow_table, g);

    int changed;

    do
    {
        changed = 0;

        //Recorremos todas las producciones
        for(int p = 0; p < g->prod_count; p++)
        {
            Production* prod = &g->productions[p];
            NonTerminal A = prod->left;
            
            //Recorremos el lado derecho
            for(int i = 0; i < prod->right_count; i++)
            {
                GrammarSymbol Xi = prod->right[i];

                //Solo interesa si Xi es NO TERMINAL
                if(Xi.type != SYMBOL_NON_TERMINAL) continue;

                // β = Xi+1 ... Xn
                GrammarSymbol* beta = NULL;
                int beta_len = 0;

                if(i + 1 < prod->right_count)
                {
                    beta = &prod->right[i + 1];
                    beta_len = prod->right_count - (i + 1);
                }

                First_Set first_beta;
                first_beta.count = 0;
                first_beta.has_epsilon = 0;

                if(beta_len > 0)
                {
                    first_of_sequence(first_table, beta, beta_len, &first_beta);

                    //FIRST(β) - {ε} ⊆ FOLLOW(Xi)
                    for (int k = 0; k < first_beta.count; k++)
                    {
                        if (follow_set_add(
                            &follow_table->follow[Xi.id],
                            first_beta.elements[k]
                        ))
                        changed = 1;
                    }
                }

                // Si β ⇒* ε o β es vacío
                if(beta_len == 0 || first_beta.has_epsilon)
                {
                    if(follow_set_union(
                        &follow_table->follow[Xi.id],
                        &follow_table->follow[A]
                    ))
                    changed = 1;
                }
            }
        }

    }while (changed);
    
}