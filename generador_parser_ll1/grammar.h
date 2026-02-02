#ifndef GRAMMAR_H
#define GRAMMAR_H

#define NT_OFFSET 0
#define T_OFFSET 128 //debe ser > max NonTerminal

#include "../generador_analizadores_lexicos/lexer.h"
#include <stdio.h>

// ============== NO TERMINALES PARA HULK ==============
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
    NT_ARGLISTTAIL,
    NT_COUNT  // Contador de no terminales HULK
} NonTerminal;

// ============== NO TERMINALES PARA REGEX ==============
typedef enum {
    RNT_REGEX = 0,      // regex -> concat (OR concat)*
    RNT_CONCAT,         // concat -> repeat+
    RNT_REPEAT,         // repeat -> atom postfix
    RNT_POSTFIX,        // postfix -> STAR | PLUS | QUESTION | ε
    RNT_ATOM,           // atom -> CHAR | LPAREN regex RPAREN | LBRACKET charclass RBRACKET | DOT
    RNT_CHARCLASS,      // charclass -> CARET? charclass_items
    RNT_CHARCLASS_ITEMS,// charclass_items -> charclass_item charclass_items | ε
    RNT_CHARCLASS_ITEM, // charclass_item -> CHAR range_opt
    RNT_RANGE_OPT,      // range_opt -> DASH CHAR | ε
    RNT_COUNT           // Contador de no terminales Regex
} RegexNonTerminal;

// Definición de símbolos para producciones (terminal, no terminal)
typedef enum 
{
    SYMBOL_TERMINAL,
    SYMBOL_NON_TERMINAL,
    SYMBOL_EPSILON       // Representa producción vacía
} SymbolType;

typedef struct 
{
    SymbolType type;
    int id; //TokenType o NonTerminal dependiendo de type
} GrammarSymbol;

// Definición de una producción
typedef struct 
{
    int left;                 // NonTerminal (índice)
    GrammarSymbol* right;     // Array dinámico de símbolos
    int right_count;          // Número de símbolos en right
    int production_id;        // ID único de la producción
} Production;

// Definición de la gramática como cuaterna
typedef struct 
{
    char** nt_names;          // Nombres de no terminales (para debugging/parsing)
    int* non_terminals;       // Array de IDs de no terminales
    int nt_count;
    
    char** t_names;           // Nombres de terminales
    int* terminals;           // Array de IDs de terminales
    int t_count;
    
    Production* productions;  // Array de producciones
    int prod_count;
    int prod_capacity;
    
    int start_symbol;         // Símbolo inicial
    
    char* name;               // Nombre de la gramática (para archivos)
} Grammar;

// ============== FUNCIONES DE GRAMÁTICA ==============

// Inicializa una gramática vacía
void grammar_init(Grammar* g, const char* name);

// Libera memoria de la gramática
void grammar_free(Grammar* g);

// Agrega un no terminal
int grammar_add_nonterminal(Grammar* g, const char* name);

// Agrega un terminal
int grammar_add_terminal(Grammar* g, const char* name, int token_id);

// Agrega una producción
int grammar_add_production(Grammar* g, int left, GrammarSymbol* right, int right_count);

// Busca un no terminal por nombre
int grammar_find_nonterminal(Grammar* g, const char* name);

// Busca un terminal por nombre
int grammar_find_terminal(Grammar* g, const char* name);

// Carga gramática desde archivo .ll1
int grammar_load_from_file(Grammar* g, const char* filename);

// Carga gramática HULK desde archivo con mapeo de tokens
int grammar_load_hulk(Grammar* g, const char* filename);

// Imprime la gramática (debugging)
void grammar_print(Grammar* g);

// ============== GRAMÁTICAS PREDEFINIDAS ==============

// Inicializa la gramática de HULK
void grammar_init_hulk(Grammar* g);

// Inicializa la gramática de expresiones regulares
void grammar_init_regex(Grammar* g);

#endif