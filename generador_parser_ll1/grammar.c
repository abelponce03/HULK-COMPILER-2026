#include "grammar.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============== FUNCIONES AUXILIARES ==============

static char* str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    if (copy) strcpy(copy, s);
    return copy;
}

static char* str_trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

// ============== INICIALIZACIÓN ==============

void grammar_init(Grammar* g, const char* name) {
    if (!g) return;
    
    g->name = name ? str_dup(name) : str_dup("unnamed");
    
    g->nt_names = NULL;
    g->non_terminals = NULL;
    g->nt_count = 0;
    
    g->t_names = NULL;
    g->terminals = NULL;
    g->t_count = 0;
    
    g->productions = NULL;
    g->prod_count = 0;
    g->prod_capacity = 0;
    
    g->start_symbol = -1;
}

void grammar_free(Grammar* g) {
    if (!g) return;
    
    if (g->name) free(g->name);
    
    if (g->nt_names) {
        for (int i = 0; i < g->nt_count; i++)
            if (g->nt_names[i]) free(g->nt_names[i]);
        free(g->nt_names);
    }
    if (g->non_terminals) free(g->non_terminals);
    
    if (g->t_names) {
        for (int i = 0; i < g->t_count; i++)
            if (g->t_names[i]) free(g->t_names[i]);
        free(g->t_names);
    }
    if (g->terminals) free(g->terminals);
    
    if (g->productions) {
        for (int i = 0; i < g->prod_count; i++)
            if (g->productions[i].right) free(g->productions[i].right);
        free(g->productions);
    }
}

// ============== AGREGAR SÍMBOLOS ==============

int grammar_add_nonterminal(Grammar* g, const char* name) {
    if (!g || !name) return -1;
    
    // Verificar si ya existe
    int existing = grammar_find_nonterminal(g, name);
    if (existing >= 0) return existing;
    
    // Expandir arrays
    g->nt_names = realloc(g->nt_names, sizeof(char*) * (g->nt_count + 1));
    g->non_terminals = realloc(g->non_terminals, sizeof(int) * (g->nt_count + 1));
    
    int id = g->nt_count;
    g->nt_names[id] = str_dup(name);
    g->non_terminals[id] = id;
    g->nt_count++;
    
    // El primer no terminal agregado es el símbolo inicial
    if (g->start_symbol < 0)
        g->start_symbol = id;
    
    return id;
}

int grammar_add_terminal(Grammar* g, const char* name, int token_id) {
    if (!g || !name) return -1;
    
    // Verificar si ya existe
    int existing = grammar_find_terminal(g, name);
    if (existing >= 0) return existing;
    
    // Expandir arrays
    g->t_names = realloc(g->t_names, sizeof(char*) * (g->t_count + 1));
    g->terminals = realloc(g->terminals, sizeof(int) * (g->t_count + 1));
    
    int idx = g->t_count;
    g->t_names[idx] = str_dup(name);
    g->terminals[idx] = token_id;
    g->t_count++;
    
    return token_id;
}

int grammar_add_production(Grammar* g, int left, GrammarSymbol* right, int right_count) {
    if (!g) return -1;
    
    // Expandir array si es necesario
    if (g->prod_count >= g->prod_capacity) {
        g->prod_capacity = g->prod_capacity == 0 ? 16 : g->prod_capacity * 2;
        g->productions = realloc(g->productions, sizeof(Production) * g->prod_capacity);
    }
    
    int id = g->prod_count;
    Production* p = &g->productions[id];
    
    p->left = left;
    p->right_count = right_count;
    p->production_id = id;
    
    if (right_count > 0 && right) {
        p->right = malloc(sizeof(GrammarSymbol) * right_count);
        memcpy(p->right, right, sizeof(GrammarSymbol) * right_count);
    } else {
        p->right = NULL;
    }
    
    g->prod_count++;
    return id;
}

// ============== BÚSQUEDA ==============

int grammar_find_nonterminal(Grammar* g, const char* name) {
    if (!g || !name) return -1;
    for (int i = 0; i < g->nt_count; i++) {
        if (g->nt_names[i] && strcmp(g->nt_names[i], name) == 0)
            return i;
    }
    return -1;
}

int grammar_find_terminal(Grammar* g, const char* name) {
    if (!g || !name) return -1;
    for (int i = 0; i < g->t_count; i++) {
        if (g->t_names[i] && strcmp(g->t_names[i], name) == 0)
            return g->terminals[i];
    }
    return -1;
}

// ============== CARGA DESDE ARCHIVO ==============

// Formato del archivo .ll1:
// NonTerminal -> Symbol1 Symbol2 ... | Alternative1 | Alternative2
// Terminales en MAYÚSCULAS, No terminales en CamelCase
// ε representa producción vacía

int grammar_load_from_file(Grammar* g, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", filename);
        return 0;
    }
    
    char line[1024];
    int line_num = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        char* trimmed = str_trim(line);
        
        // Ignorar líneas vacías y comentarios
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == '/') 
            continue;
        
        // Buscar ->
        char* arrow = strstr(trimmed, "->");
        if (!arrow) {
            fprintf(stderr, "Error línea %d: falta '->'\n", line_num);
            continue;
        }
        
        // Extraer lado izquierdo
        *arrow = '\0';
        char* left_str = str_trim(trimmed);
        
        int left_nt = grammar_find_nonterminal(g, left_str);
        if (left_nt < 0) {
            left_nt = grammar_add_nonterminal(g, left_str);
        }
        
        // Extraer lado derecho (puede tener múltiples alternativas con |)
        char* right_str = str_trim(arrow + 2);
        
        // Dividir por |
        char* saveptr_alt;
        char* alternative = strtok_r(right_str, "|", &saveptr_alt);
        
        while (alternative) {
            alternative = str_trim(alternative);
            
            GrammarSymbol symbols[64];
            int sym_count = 0;
            
            // Parsear símbolos separados por espacios
            char* saveptr_sym;
            char* token = strtok_r(alternative, " \t", &saveptr_sym);
            
            while (token && sym_count < 64) {
                token = str_trim(token);
                
                if (strcmp(token, "ε") == 0 || strcmp(token, "epsilon") == 0) {
                    // Producción vacía - no agregar símbolos
                } else if (isupper(token[0]) && strchr(token, '_') != NULL) {
                    // Terminal (MAYÚSCULAS con _)
                    int t_id = grammar_find_terminal(g, token);
                    if (t_id < 0) {
                        // Auto-agregar terminal con ID basado en posición
                        t_id = grammar_add_terminal(g, token, g->t_count);
                    }
                    symbols[sym_count].type = SYMBOL_TERMINAL;
                    symbols[sym_count].id = t_id;
                    sym_count++;
                } else if (isupper(token[0])) {
                    // Podría ser terminal sin _ o no terminal
                    // Si está todo en mayúsculas, es terminal
                    int all_upper = 1;
                    for (int i = 0; token[i]; i++) {
                        if (islower(token[i])) { all_upper = 0; break; }
                    }
                    
                    if (all_upper) {
                        int t_id = grammar_find_terminal(g, token);
                        if (t_id < 0) {
                            t_id = grammar_add_terminal(g, token, g->t_count);
                        }
                        symbols[sym_count].type = SYMBOL_TERMINAL;
                        symbols[sym_count].id = t_id;
                    } else {
                        // No terminal (CamelCase)
                        int nt_id = grammar_find_nonterminal(g, token);
                        if (nt_id < 0) {
                            nt_id = grammar_add_nonterminal(g, token);
                        }
                        symbols[sym_count].type = SYMBOL_NON_TERMINAL;
                        symbols[sym_count].id = nt_id;
                    }
                    sym_count++;
                } else {
                    // No terminal
                    int nt_id = grammar_find_nonterminal(g, token);
                    if (nt_id < 0) {
                        nt_id = grammar_add_nonterminal(g, token);
                    }
                    symbols[sym_count].type = SYMBOL_NON_TERMINAL;
                    symbols[sym_count].id = nt_id;
                    sym_count++;
                }
                
                token = strtok_r(NULL, " \t", &saveptr_sym);
            }
            
            grammar_add_production(g, left_nt, symbols, sym_count);
            
            alternative = strtok_r(NULL, "|", &saveptr_alt);
        }
    }
    
    fclose(f);
    return 1;
}

// ============== DEBUGGING ==============

void grammar_print(Grammar* g) {
    if (!g) return;
    
    printf("=== Gramática: %s ===\n", g->name);
    printf("Símbolo inicial: %s\n", 
           g->start_symbol >= 0 ? g->nt_names[g->start_symbol] : "(none)");
    
    printf("\nNo terminales (%d):\n", g->nt_count);
    for (int i = 0; i < g->nt_count; i++)
        printf("  [%d] %s\n", i, g->nt_names[i]);
    
    printf("\nTerminales (%d):\n", g->t_count);
    for (int i = 0; i < g->t_count; i++)
        printf("  [%d] %s (token=%d)\n", i, g->t_names[i], g->terminals[i]);
    
    printf("\nProducciones (%d):\n", g->prod_count);
    for (int i = 0; i < g->prod_count; i++) {
        Production* p = &g->productions[i];
        printf("  [%d] %s ->", i, g->nt_names[p->left]);
        
        if (p->right_count == 0) {
            printf(" ε");
        } else {
            for (int j = 0; j < p->right_count; j++) {
                GrammarSymbol* s = &p->right[j];
                if (s->type == SYMBOL_TERMINAL) {
                    // Buscar nombre del terminal
                    const char* name = "?";
                    for (int k = 0; k < g->t_count; k++) {
                        if (g->terminals[k] == s->id) {
                            name = g->t_names[k];
                            break;
                        }
                    }
                    printf(" %s", name);
                } else {
                    printf(" %s", g->nt_names[s->id]);
                }
            }
        }
        printf("\n");
    }
}

// ============== GRAMÁTICA DE EXPRESIONES REGULARES ==============
// Esta gramática es LL(1) para parsear expresiones regulares

void grammar_init_regex(Grammar* g) {
    grammar_init(g, "regex");
    
    // Terminales para regex
    int T_CHAR     = grammar_add_terminal(g, "CHAR", 0);       // cualquier carácter literal
    int T_OR       = grammar_add_terminal(g, "OR", 1);         // |
    int T_STAR     = grammar_add_terminal(g, "STAR", 2);       // *
    int T_PLUS     = grammar_add_terminal(g, "PLUS", 3);       // +
    int T_QUESTION = grammar_add_terminal(g, "QUESTION", 4);   // ?
    int T_LPAREN   = grammar_add_terminal(g, "LPAREN", 5);     // (
    int T_RPAREN   = grammar_add_terminal(g, "RPAREN", 6);     // )
    int T_LBRACKET = grammar_add_terminal(g, "LBRACKET", 7);   // [
    int T_RBRACKET = grammar_add_terminal(g, "RBRACKET", 8);   // ]
    int T_DOT      = grammar_add_terminal(g, "DOT", 9);        // .
    int T_CARET    = grammar_add_terminal(g, "CARET", 10);     // ^
    int T_DASH     = grammar_add_terminal(g, "DASH", 11);      // -
    int T_ESCAPE   = grammar_add_terminal(g, "ESCAPE", 12);    // \x
    
    // No terminales
    int NT_Regex         = grammar_add_nonterminal(g, "Regex");
    int NT_Concat        = grammar_add_nonterminal(g, "Concat");
    int NT_ConcatTail    = grammar_add_nonterminal(g, "ConcatTail");
    int NT_Repeat        = grammar_add_nonterminal(g, "Repeat");
    int NT_Postfix       = grammar_add_nonterminal(g, "Postfix");
    int NT_Atom          = grammar_add_nonterminal(g, "Atom");
    int NT_CharClass     = grammar_add_nonterminal(g, "CharClass");
    int NT_CCItems       = grammar_add_nonterminal(g, "CCItems");
    int NT_CCItem        = grammar_add_nonterminal(g, "CCItem");
    int NT_RangeOpt      = grammar_add_nonterminal(g, "RangeOpt");
    
    GrammarSymbol s[16];
    
    // Regex -> Concat ConcatTail
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ConcatTail};
    grammar_add_production(g, NT_Regex, s, 2);
    
    // ConcatTail -> OR Concat ConcatTail | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_OR};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    s[2] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ConcatTail};
    grammar_add_production(g, NT_ConcatTail, s, 3);
    grammar_add_production(g, NT_ConcatTail, NULL, 0);  // ε
    
    // Concat -> Repeat Concat | ε
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Repeat};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    grammar_add_production(g, NT_Concat, s, 2);
    grammar_add_production(g, NT_Concat, NULL, 0);  // ε
    
    // Repeat -> Atom Postfix
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Atom};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Postfix};
    grammar_add_production(g, NT_Repeat, s, 2);
    
    // Postfix -> STAR | PLUS | QUESTION | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_STAR};
    grammar_add_production(g, NT_Postfix, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_PLUS};
    grammar_add_production(g, NT_Postfix, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_QUESTION};
    grammar_add_production(g, NT_Postfix, s, 1);
    grammar_add_production(g, NT_Postfix, NULL, 0);  // ε
    
    // Atom -> CHAR | ESCAPE | LPAREN Regex RPAREN | LBRACKET CharClass RBRACKET | DOT
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    grammar_add_production(g, NT_Atom, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_ESCAPE};
    grammar_add_production(g, NT_Atom, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_LPAREN};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Regex};
    s[2] = (GrammarSymbol){SYMBOL_TERMINAL, T_RPAREN};
    grammar_add_production(g, NT_Atom, s, 3);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_LBRACKET};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CharClass};
    s[2] = (GrammarSymbol){SYMBOL_TERMINAL, T_RBRACKET};
    grammar_add_production(g, NT_Atom, s, 3);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_DOT};
    grammar_add_production(g, NT_Atom, s, 1);
    
    // CharClass -> CARET CCItems | CCItems
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CARET};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CharClass, s, 2);
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CharClass, s, 1);
    
    // CCItems -> CCItem CCItems | ε
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItem};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CCItems, s, 2);
    grammar_add_production(g, NT_CCItems, NULL, 0);  // ε
    
    // CCItem -> CHAR RangeOpt | ESCAPE
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_RangeOpt};
    grammar_add_production(g, NT_CCItem, s, 2);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_ESCAPE};
    grammar_add_production(g, NT_CCItem, s, 1);
    
    // RangeOpt -> DASH CHAR | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_DASH};
    s[1] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    grammar_add_production(g, NT_RangeOpt, s, 2);
    grammar_add_production(g, NT_RangeOpt, NULL, 0);  // ε
}

// ============== INICIALIZACIÓN HULK ==============
// Mapeo de nombres de terminales en grammar.ll1 a TokenType

typedef struct {
    const char* name;
    int token_id;
} TokenMapping;

static TokenMapping hulk_terminal_map[] = {
    // Palabras clave
    { "FUNCTION", TOKEN_FUNCTION },
    { "TYPE", TOKEN_TYPE },
    { "INHERITS", TOKEN_INHERITS },
    { "WHILE", TOKEN_WHILE },
    { "FOR", TOKEN_FOR },
    { "IN", TOKEN_IN },
    { "IF", TOKEN_IF },
    { "ELIF", TOKEN_ELIF },
    { "ELSE", TOKEN_ELSE },
    { "LET", TOKEN_LET },
    { "TRUE", TOKEN_TRUE },
    { "FALSE", TOKEN_FALSE },
    { "NEW", TOKEN_NEW },
    { "SELF", TOKEN_SELF },
    { "BASE", TOKEN_BASE },
    { "AS", TOKEN_AS },
    { "IS", TOKEN_IS },
    
    // Símbolos y operadores
    { "SEMICOLON", TOKEN_SEMICOLON },
    { "LPAREN", TOKEN_LPAREN },
    { "RPAREN", TOKEN_RPAREN },
    { "LBRACE", TOKEN_LBRACE },
    { "RBRACE", TOKEN_RBRACE },
    { "COMMA", TOKEN_COMMA },
    { "COLON", TOKEN_COLON },
    { "DOT", TOKEN_DOT },
    { "ASSIGN", TOKEN_ASSIGN },
    { "ASSIGN_DESTRUCT", TOKEN_ASSIGN_DESTRUCT },
    { "PLUS", TOKEN_PLUS },
    { "MINUS", TOKEN_MINUS },
    { "MULT", TOKEN_MULT },
    { "DIV", TOKEN_DIV },
    { "MOD", TOKEN_MOD },
    { "POW", TOKEN_POW },
    { "LT", TOKEN_LT },
    { "GT", TOKEN_GT },
    { "LE", TOKEN_LE },
    { "GE", TOKEN_GE },
    { "EQ", TOKEN_EQ },
    { "NEQ", TOKEN_NEQ },
    { "OR", TOKEN_OR },
    { "AND", TOKEN_AND },
    { "CONCAT", TOKEN_CONCAT },
    { "CONCAT_WS", TOKEN_CONCAT_WS },
    { "ARROW", TOKEN_ARROW },
    
    // Literales
    { "IDENT", TOKEN_IDENT },
    { "NUMBER", TOKEN_NUMBER },
    { "STRING", TOKEN_STRING },
    
    { NULL, 0 }  // Terminador
};

static int get_hulk_token_id(const char* name) {
    for (int i = 0; hulk_terminal_map[i].name != NULL; i++) {
        if (strcmp(hulk_terminal_map[i].name, name) == 0) {
            return hulk_terminal_map[i].token_id;
        }
    }
    return -1;  // No encontrado
}

int grammar_load_hulk(Grammar* g, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: no se pudo abrir %s\n", filename);
        return 0;
    }
    
    // Primero, leer todo el archivo y unir líneas de continuación
    size_t content_cap = 65536;
    char* full_content = calloc(content_cap, 1);
    if (!full_content) {
        fprintf(stderr, "Error: sin memoria para buffer de gramática\n");
        fclose(f);
        return 0;
    }
    char line[1024];
    
    while (fgets(line, sizeof(line), f)) {
        char* trimmed = str_trim(line);
        
        // Ignorar líneas vacías y comentarios
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == '/') 
            continue;
        
        // Verificar si es línea de continuación (empieza con |)
        if (trimmed[0] == '|') {
            // Es continuación - agregar al contenido actual
            strcat(full_content, " ");
            strcat(full_content, trimmed);
        } else if (strstr(trimmed, "->") != NULL) {
            // Nueva producción - agregar newline si había contenido previo
            if (full_content[0] != '\0') {
                strcat(full_content, "\n");
            }
            strcat(full_content, trimmed);
        } else {
            // Línea desconocida, probablemente continuación sin |
            // (espacios al inicio, luego contenido)
            // Esto no debería pasar en una gramática bien formada
        }
    }
    fclose(f);
    
    // Ahora parsear el contenido unido
    char* content = full_content;
    
    while (*content) {
        // Buscar fin de línea o fin de string
        char* line_end = strchr(content, '\n');
        if (line_end) {
            *line_end = '\0';
        }
        
        char* trimmed = str_trim(content);
        
        if (trimmed[0] != '\0') {
            // Buscar ->
            char* arrow = strstr(trimmed, "->");
            if (arrow) {
                // Extraer lado izquierdo
                *arrow = '\0';
                char* left_str = str_trim(trimmed);
                
                int left_nt = grammar_find_nonterminal(g, left_str);
                if (left_nt < 0) {
                    left_nt = grammar_add_nonterminal(g, left_str);
                }
                
                // Extraer lado derecho
                char* right_str = str_trim(arrow + 2);
                
                // Dividir por |
                char right_copy[4096];
                strncpy(right_copy, right_str, sizeof(right_copy) - 1);
                right_copy[sizeof(right_copy) - 1] = '\0';
                
                char* saveptr_alt;
                char* alternative = strtok_r(right_copy, "|", &saveptr_alt);
                
                while (alternative) {
                    alternative = str_trim(alternative);
                    
                    GrammarSymbol symbols[64];
                    int sym_count = 0;
                    
                    // Parsear símbolos
                    char alt_copy[512];
                    strncpy(alt_copy, alternative, sizeof(alt_copy) - 1);
                    alt_copy[sizeof(alt_copy) - 1] = '\0';
                    
                    char* saveptr_sym;
                    char* token = strtok_r(alt_copy, " \t", &saveptr_sym);
                    
                    while (token && sym_count < 64) {
                        token = str_trim(token);
                        
                        if (strlen(token) == 0) {
                            token = strtok_r(NULL, " \t", &saveptr_sym);
                            continue;
                        }
                        
                        if (strcmp(token, "ε") == 0 || strcmp(token, "epsilon") == 0) {
                            // Producción vacía
                        } else {
                            // Verificar si es terminal
                            int is_terminal = 1;
                            for (int i = 0; token[i]; i++) {
                                if (islower(token[i])) {
                                    is_terminal = 0;
                                    break;
                                }
                            }
                            
                            if (is_terminal) {
                                int t_id = get_hulk_token_id(token);
                                if (t_id < 0) {
                                    fprintf(stderr, "Advertencia: terminal '%s' no mapeado\n", token);
                                    t_id = grammar_add_terminal(g, token, g->t_count + 100);
                                } else {
                                    if (grammar_find_terminal(g, token) < 0) {
                                        grammar_add_terminal(g, token, t_id);
                                    }
                                }
                                symbols[sym_count].type = SYMBOL_TERMINAL;
                                symbols[sym_count].id = t_id;
                            } else {
                                int nt_id = grammar_find_nonterminal(g, token);
                                if (nt_id < 0) {
                                    nt_id = grammar_add_nonterminal(g, token);
                                }
                                symbols[sym_count].type = SYMBOL_NON_TERMINAL;
                                symbols[sym_count].id = nt_id;
                            }
                            sym_count++;
                        }
                        
                        token = strtok_r(NULL, " \t", &saveptr_sym);
                    }
                    
                    grammar_add_production(g, left_nt, symbols, sym_count);
                    alternative = strtok_r(NULL, "|", &saveptr_alt);
                }
            }
        }
        
        if (line_end) {
            content = line_end + 1;
        } else {
            break;
        }
    }
    
    printf("Gramática HULK cargada: %d no-terminales, %d terminales, %d producciones\n",
           g->nt_count, g->t_count, g->prod_count);
    free(full_content);
    return 1;
}

void grammar_init_hulk(Grammar* g) {
    grammar_init(g, "hulk");
    // La gramática se cargará desde grammar.ll1 usando grammar_load_hulk()
}