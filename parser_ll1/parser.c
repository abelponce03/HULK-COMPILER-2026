#include "parser.h"
#include <stdlib.h>
#include <string.h>



void ll1_table_init(LL1_Table* t, Grammar* g) 
{
    t->nt_count = g->nt_count;
    t->t_count = g->t_count + 1; // +1 para $

    t->table = malloc(sizeof(int*) * t->nt_count);
    for (int i = 0; i < t->nt_count; i++) {
        t->table[i] = malloc(sizeof(int) * t->t_count);
        for (int j = 0; j < t->t_count; j++)
            t->table[i][j] = NO_PRODUCTION;
    }
}


//Construccion de la tabla LL(1) el corazon del parser predictivo.

int build_ll1_table(
    Grammar* g,
    First_Table* first_table,
    Follow_Table* follow_table,
    LL1_Table* ll1
) {
    ll1_table_init(ll1, g);

    int is_ll1 = 1;

    for (int p = 0; p < g->prod_count; p++) {
        Production* prod = &g->productions[p];
        NonTerminal A = prod->left;

        // FIRST(α)
        First_Set first_alpha;
        first_alpha.count = 0;
        first_alpha.has_epsilon = 0;

        first_of_sequence(
            first_table,
            prod->right,
            prod->right_count,
            &first_alpha
        );

        // Regla 1: FIRST(α) - {ε}
        for (int i = 0; i < first_alpha.count; i++) {
            int a = first_alpha.elements[i];
            int col = ll1_col_index(a, g);

            if (ll1->table[A][col] != NO_PRODUCTION)
                is_ll1 = 0; // conflicto

            ll1->table[A][col] = p;
        }

        // Regla 2: si ε ∈ FIRST(α)
        if (first_alpha.has_epsilon) {
            Follow_Set* followA = &follow_table->follow[A];

            for (int i = 0; i < followA->count; i++) {
                int b = followA->elements[i];
                int col = ll1_col_index(b, g);

                if (ll1->table[A][col] != NO_PRODUCTION)
                    is_ll1 = 0; // conflicto

                ll1->table[A][col] = p;
            }
        }
    }

    return is_ll1;
}

//Implementacion Completa de LL(1) 

int parse() 
{
    
    Grammar grammar;
    First_Table first;
    Follow_Table follow;
    LL1_Table ll1;

    compute_first_sets(&grammar, &first);
    compute_follow_sets(&grammar, &first, &follow);

    if (!build_ll1_table(&grammar, &first, &follow, &ll1)) {
        fprintf(stderr, "Error: la gramática no es LL(1)\n");
        return 0;
    }

    Stack stack;
    stack_init(&stack);

    // push $
    stack_push(&stack, (StackSymbol){ STACK_END, END_MARKER });

    // push símbolo inicial
    stack_push(&stack, (StackSymbol){
        STACK_NON_TERMINAL,
        grammar.start_symbol
    });

    Token lookahead = get_next_token();

    while (1) {
        StackSymbol top = stack_peek(&stack);

        // Caso aceptación
        if (top.type == STACK_END && lookahead.type == TOKEN_EOF) {
            return 1; // éxito
        }

        // Terminal en el stack
        if (top.type == STACK_TERMINAL) {
            if (top.id == lookahead.type) {
                stack_pop(&stack);
                lookahead = get_next_token();
            } else {
                fprintf(stderr,
                        "Error sintáctico: se esperaba %d y se encontró %d\n",
                        top.id, lookahead.type);
                return 0;
            }
        }

        // No terminal en el stack
        else if (top.type == STACK_NON_TERMINAL) {
            int row = top.id;
            int col;

            if (lookahead.type == TOKEN_EOF)
                col = grammar.t_count; // $
            else
                col = lookahead.type;

            int prod_index = ll1.table[row][col];

            if (prod_index == NO_PRODUCTION) {
                fprintf(stderr,
                        "Error sintáctico: no hay producción para (%d, %d)\n",
                        row, lookahead.type);
                return 0;
            }

            stack_pop(&stack);

            Production* prod = &grammar.productions[prod_index];

            // push RHS en orden inverso
            for (int i = prod->right_count - 1; i >= 0; i--) {
                GrammarSymbol s = prod->right[i];

                if (s.type == SYMBOL_TERMINAL) {
                    stack_push(&stack, (StackSymbol){
                        STACK_TERMINAL,
                        s.id
                    });
                } else {
                    stack_push(&stack, (StackSymbol){
                        STACK_NON_TERMINAL,
                        s.id
                    });
                }
            }
        }
    }
}
