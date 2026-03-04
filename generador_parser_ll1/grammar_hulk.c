/*
 * grammar_hulk.c — Gramática específica del lenguaje HULK
 *
 * Contiene el mapeo de tokens HULK, la carga de gramática desde
 * archivo .ll1, y la inicialización específica de HULK.
 * Extraído de grammar.c (SRP).
 */

#include "grammar.h"
#include "grammar_utils.h"
#include "../error_handler.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define str_trim  grammar_str_trim

// ============== MAPEO DE TOKENS HULK ==============
// Asocia nombres de terminales en grammar.ll1 a TokenType (enum en token_types.h)

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
    { "DECOR", TOKEN_DECOR },
    
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

// ============== INICIALIZACIÓN ==============

void grammar_init_hulk(Grammar* g) {
    grammar_init(g, "hulk");
    // La gramática se cargará desde grammar.ll1 usando grammar_load_hulk()
}

// ============== CARGA DESDE ARCHIVO .ll1 ==============

int grammar_load_hulk(Grammar* g, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        LOG_ERROR_MSG("grammar", "no se pudo abrir %s", filename);
        return 0;
    }
    
    // Primero, leer todo el archivo y unir líneas de continuación
    size_t content_cap = 65536;
    char* full_content = calloc(content_cap, 1);
    if (!full_content) {
        LOG_FATAL_MSG("grammar", "sin memoria para buffer de gramática");
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
                                    LOG_WARN_MSG("grammar", "terminal '%s' no mapeado", token);
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
