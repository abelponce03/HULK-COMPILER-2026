#ifndef GRAMMAR_H
#define GRAMMAR_H

#define NT_OFFSET 0
#define T_OFFSET 128 //debe ser > max NonTerminal

#include "../generador_analizadores_lexicos/lexer.h"

// Definición de no terminales (basado en la gramática)
typedef enum 
{
    NT_PROGRAM,
    NT_STMTLIST,
    NT_TERMINATEDSTMT,
    NT_STMT,
    NT_FUNCTIONDEF,
    NT_TYPEDEF,
    NT_TYPEPARAMS,
    NT_TYPEINHERITANCE,
    NT_TYPEBASEARGS,
    NT_TYPEBODY,
    NT_TYPEMEMBER,
    NT_TYPEMEMBERTAIL,
    NT_TYPEMEMBERTAIL_PRIME,
    NT_ATTRIBUTEDEF,
    NT_METHODDEF,
    NT_ARGIDLIST,
    NT_ARGIDLISTTAIL,
    NT_ARGID,
    NT_FUNCTIONBODY,
    NT_WHILESTMT,
    NT_WHILEBODY,
    NT_FORSTMT,
    NT_FORBODY,
    NT_BLOCKSTMT,
    NT_EXPR,
    NT_LETEXPR,
    NT_LETBODY,
    NT_VARBINDINGLIST,
    NT_VARBINDINGLISTTAIL,
    NT_VARBINDING,
    NT_TYPEANNOTATION,
    NT_IFEXPR,
    NT_ELIFLIST,
    NT_ELIFBRANCH,
    NT_IFBODY,
    NT_OREXPR,
    NT_OREXPR_PRIME,
    NT_ANDEXPR,
    NT_ANDEXPR_PRIME,
    NT_CMPEXPR,
    NT_CMPEXPR_PRIME,
    NT_CONCATEXPR,
    NT_CONCATEXPR_PRIME,
    NT_ADDEXPR,
    NT_ADDEXPR_PRIME,
    NT_TERM,
    NT_TERM_PRIME,
    NT_FACTOR,
    NT_FACTOR_PRIME,
    NT_POWER,
    NT_UNARY,
    NT_ASEXPR,
    NT_PRIMARY,
    NT_PRIMARYTAIL,
    NT_ARGLIST,
    NT_ARGLISTTAIL
} NonTerminal;

// Definición de símbolos para producciones (terminal, no terminal)
typedef enum 
{
    SYMBOL_TERMINAL,
    SYMBOL_NON_TERMINAL
} SymbolType;

typedef struct 
{
    SymbolType type;
    int id; //TokenType o NonTerminal dependiendo de type
} GrammarSymbol;

// Definición de una producción
typedef struct 
{
    NonTerminal left;
    GrammarSymbol* right;  // Array dinámico de símbolos
    int right_count;  // Número de símbolos en right
} Production;

// Definición de la gramática como cuaterna
typedef struct 
{
    NonTerminal* non_terminals;  // Array de no terminales
    int nt_count;
    TokenType* terminals;  // Array de terminales
    int t_count;
    Production* productions;  // Array de producciones
    int prod_count;
    NonTerminal start_symbol;
} Grammar;



//Codificacion de la gramatica como arrays de Productions
void init_grammar(Grammar* g);


#endif