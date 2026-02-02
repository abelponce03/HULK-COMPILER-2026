#include "first_&_follow.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============== FUNCIONES DE FIRST SET ==============

void first_table_init(First_Table* table) {
    if (!table) return;

    for (int i = 0; i < MAX_SYMBOLS; i++) {
        table->first[i].count = 0;
        table->first[i].has_epsilon = 0;
        for (int j = 0; j < MAX_SYMBOLS; j++) {
            table->first[i].elements[j] = -1;
        }
    }
}

int first_set_add(First_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        if(set->elements[i] == symbol) return 0;     
    }
    if(set->count < MAX_SYMBOLS)
    {
        set->elements[set->count++] = symbol;
        return 1;
    }
    return 0;
}

int first_set_union(First_Set* First_A, First_Set* First_alpha)
{
    int changed = 0;
    for(int i = 0; i < First_alpha->count; i++)
    {
        if(first_set_add(First_A, First_alpha->elements[i]))
            changed = 1;
    }
    if(First_alpha->has_epsilon && !First_A->has_epsilon)
    {
        First_A->has_epsilon = 1;
        changed = 1;
    }
    return changed;
}

int first_set_contains(First_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        if(set->elements[i] == symbol) return 1;
    }
    return 0;
}

void first_of_sequence(First_Table* table, GrammarSymbol* seq, int n, First_Set* result)
{
    result->count = 0;
    result->has_epsilon = 0;
    
    // Secuencia vacía -> ε está en FIRST
    if (n == 0) {
        result->has_epsilon = 1;
        return;
    }
    
    int all_have_epsilon = 1;

    for(int i = 0; i < n; i++)
    {
        int index = symbol_index(seq[i]);
        First_Set* fs = &table->first[index];

        // Agregamos FIRST(seq[i]) - {ε}
        for(int j = 0; j < fs->count; j++)
        {
            first_set_add(result, fs->elements[j]);
        }

        if(!fs->has_epsilon)
        {
            all_have_epsilon = 0;
            break;
        }
    }
    
    if(all_have_epsilon)
    {
        result->has_epsilon = 1;
    }
}

void compute_first_sets(Grammar* g, First_Table* table)
{
    if(!g || !table) return;

    first_table_init(table);

    // Inicializar FIRST de terminales: FIRST(a) = {a}
    for(int i = 0; i < g->t_count; i++)
    {
        GrammarSymbol s = {SYMBOL_TERMINAL, g->terminals[i]};
        int index = symbol_index(s);
        table->first[index].count = 0;
        table->first[index].has_epsilon = 0;
        first_set_add(&table->first[index], g->terminals[i]);
    }

    // Iterativo hasta convergencia
    int changed;
    do 
    {
        changed = 0;

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

// ============== FUNCIONES DE FOLLOW SET ==============

int follow_set_add(Follow_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        if(set->elements[i] == symbol) return 0;
    }

    if(set->count < MAX_SYMBOLS)
    {
        set->elements[set->count++] = symbol;
        return 1;
    }

    return 0;
}

int follow_set_union(Follow_Set* Follow_A, Follow_Set* Follow_B)
{
    int changed = 0;
    for(int i = 0; i < Follow_B->count; i++)
    {
        if(follow_set_add(Follow_A, Follow_B->elements[i]))
            changed = 1;
    }
    return changed;
}

int follow_set_contains(Follow_Set* set, int symbol)
{
    for(int i = 0; i < set->count; i++)
    {
        if(set->elements[i] == symbol) return 1;
    }
    return 0;
}

void follow_table_init(Follow_Table* table, Grammar* g)
{
    if(!table || !g) return;

    for(int i = 0; i < MAX_SYMBOLS; i++)
    {
        table->follow[i].count = 0;
    }

    // $ ∈ FOLLOW(S)
    if (g->start_symbol >= 0) {
        follow_set_add(&table->follow[g->start_symbol], END_MARKER);
    }
}

void compute_follow_sets(Grammar* g, First_Table* first_table, Follow_Table* follow_table)
{
    if(!g || !first_table || !follow_table) return;

    follow_table_init(follow_table, g);

    int changed;

    do
    {
        changed = 0;

        for(int p = 0; p < g->prod_count; p++)
        {
            Production* prod = &g->productions[p];
            int A = prod->left;
            
            for(int i = 0; i < prod->right_count; i++)
            {
                GrammarSymbol Xi = prod->right[i];

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

                    // FIRST(β) - {ε} ⊆ FOLLOW(Xi)
                    for (int k = 0; k < first_beta.count; k++)
                    {
                        if (follow_set_add(&follow_table->follow[Xi.id], first_beta.elements[k]))
                            changed = 1;
                    }
                }

                // Si β ⇒* ε o β es vacío
                if(beta_len == 0 || first_beta.has_epsilon)
                {
                    if(follow_set_union(&follow_table->follow[Xi.id], &follow_table->follow[A]))
                        changed = 1;
                }
            }
        }

    } while (changed);
}

// ============== DEBUG ==============

void print_first_sets(Grammar* g, First_Table* table) {
    printf("\n=== FIRST Sets ===\n");
    
    for (int i = 0; i < g->nt_count; i++) {
        printf("FIRST(%s) = {", g->nt_names[i]);
        
        First_Set* fs = &table->first[NT_OFFSET + i];
        for (int j = 0; j < fs->count; j++) {
            if (j > 0) printf(", ");
            // Buscar nombre del terminal
            int found = 0;
            for (int k = 0; k < g->t_count; k++) {
                if (g->terminals[k] == fs->elements[j]) {
                    printf("%s", g->t_names[k]);
                    found = 1;
                    break;
                }
            }
            if (!found) printf("t%d", fs->elements[j]);
        }
        if (fs->has_epsilon) {
            if (fs->count > 0) printf(", ");
            printf("ε");
        }
        printf("}\n");
    }
}

void print_follow_sets(Grammar* g, Follow_Table* table) {
    printf("\n=== FOLLOW Sets ===\n");
    
    for (int i = 0; i < g->nt_count; i++) {
        printf("FOLLOW(%s) = {", g->nt_names[i]);
        
        Follow_Set* fs = &table->follow[i];
        for (int j = 0; j < fs->count; j++) {
            if (j > 0) printf(", ");
            if (fs->elements[j] == END_MARKER) {
                printf("$");
            } else {
                int found = 0;
                for (int k = 0; k < g->t_count; k++) {
                    if (g->terminals[k] == fs->elements[j]) {
                        printf("%s", g->t_names[k]);
                        found = 1;
                        break;
                    }
                }
                if (!found) printf("t%d", fs->elements[j]);
            }
        }
        printf("}\n");
    }
}