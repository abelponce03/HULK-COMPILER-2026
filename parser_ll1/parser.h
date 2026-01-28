#ifndef PARSER_H
#define PARSER_H

#include "../lexer.h"
#include "first_&_follow.h"
#include "grammar.h"

//Tabla LL(1) -------------------------

#define NO_PRODUCTION (-1)

typedef struct
{
    int** table; //M[A,a] = production index o -1 si error
    int nt_count; //numero de no terminales
    int t_count;  //numero de terminales

}LL1_Table;

//Construye la tabla LL(1) a partir de la gramatica, tabla FIRST y FOLLOW
void ll1_table_init(LL1_Table* t, Grammar* g);


//Indexacion de columnas de la tabla LL(1) (terminales + $)
static inline int ll1_col_index(int terminal, Grammar* g) {
    if (terminal == END_MARKER)
        return g->t_count; // última columna
    return terminal;
}

//Representacion del stack del parser ----------------------------

typedef enum
{
    STACK_TERMINAL,
    STACK_NON_TERMINAL,
    STACK_END
}StackSymbolType;

typedef struct
{
    StackSymbolType type;
    int id; //TokenType o NonTerminal dependiendo de type

}StackSymbol;

//Stack simple (array)

#define STACK_MAX 1024

typedef struct
{
    StackSymbol data[STACK_MAX];
    int top;

} Stack;

static void stack_init(Stack* s) {
    s->top = 0;
}

static void stack_push(Stack* s, StackSymbol sym) {
    s->data[s->top++] = sym;
}

static StackSymbol stack_pop(Stack* s) {
    return s->data[--s->top];
}

static StackSymbol stack_peek(Stack* s) {
    return s->data[s->top - 1];
}



#endif