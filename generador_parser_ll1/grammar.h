#ifndef GRAMMAR_H
#define GRAMMAR_H

#define NT_OFFSET 0
#define T_OFFSET 128 //debe ser > max NonTerminal

#include "../generador_analizadores_lexicos/token_types.h"
#include <stdio.h>

// Definición de símbolos para producciones y pilas de parsers
// Unifica terminal, no-terminal, acción, epsilon y fin de pila
typedef enum 
{
    SYMBOL_TERMINAL,
    SYMBOL_NON_TERMINAL,
    SYMBOL_EPSILON,      // Producción vacía
    SYMBOL_ACTION,       // Marcador de acción semántica (para parser de regex)
    SYMBOL_END           // Marcador de fin de pila ($)
} SymbolType;

typedef struct 
{
    SymbolType type;
    int id; // TokenType, NonTerminal ID, SemanticAction ID, o END_MARKER
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

// ============== ABSTRACT FACTORY ==============
// Encapsula la creación e inicialización de gramáticas distintas
// bajo una interfaz uniforme.

typedef struct {
    const char* name;                            // nombre descriptivo
    void (*init)(Grammar* g);                    // inicializa la gramática
    int  (*load)(Grammar* g, const char* file);  // carga desde archivo (puede ser NULL)
} GrammarFactory;

// Factorías predefinidas
extern const GrammarFactory grammar_factory_hulk;
extern const GrammarFactory grammar_factory_regex;

// Busca una factoría por nombre ("hulk", "regex").  Retorna NULL si no existe.
const GrammarFactory* grammar_factory_find(const char* name);

#endif