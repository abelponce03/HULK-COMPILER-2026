#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "generador_analizadores_lexicos/lexer.h"
#include "generador_analizadores_lexicos/afd.h"
#include "generador_analizadores_lexicos/ast.h"
#include "generador_analizadores_lexicos/regex_parser.h"
#include "generador_parser_ll1/parser.h"
#include "generador_parser_ll1/grammar.h"
#include "generador_parser_ll1/first_&_follow.h"

// ============== DEFINICIÓN DE TOKENS PARA HULK ==============

TokenRegex hulk_tokens[] = {
    // ===== PALABRAS CLAVE (deben ir ANTES de identificadores) =====
    { TOKEN_FUNCTION,   "function" },
    { TOKEN_TYPE,       "type" },
    { TOKEN_INHERITS,   "inherits" },
    { TOKEN_WHILE,      "while" },
    { TOKEN_FOR,        "for" },
    { TOKEN_IN,         "in" },
    { TOKEN_IF,         "if" },
    { TOKEN_ELIF,       "elif" },
    { TOKEN_ELSE,       "else" },
    { TOKEN_LET,        "let" },
    { TOKEN_TRUE,       "true" },
    { TOKEN_FALSE,      "false" },
    { TOKEN_NEW,        "new" },
    { TOKEN_SELF,       "self" },
    { TOKEN_BASE,       "base" },
    { TOKEN_AS,         "as" },
    { TOKEN_IS,         "is" },
    
    // ===== OPERADORES MULTI-CARÁCTER (antes de simples) =====
    { TOKEN_ARROW,          "=>" },
    { TOKEN_ASSIGN_DESTRUCT,":=" },
    { TOKEN_LE,             "<=" },
    { TOKEN_GE,             ">=" },
    { TOKEN_EQ,             "==" },
    { TOKEN_NEQ,            "!=" },
    { TOKEN_OR,             "\\|\\|" },
    { TOKEN_AND,            "&&" },
    { TOKEN_CONCAT_WS,      "@@" },
    { TOKEN_CONCAT,         "@" },
    { TOKEN_POW,            "\\*\\*" },
    
    // ===== OPERADORES SIMPLES =====
    { TOKEN_SEMICOLON,  ";" },
    { TOKEN_LPAREN,     "\\(" },
    { TOKEN_RPAREN,     "\\)" },
    { TOKEN_LBRACE,     "\\{" },
    { TOKEN_RBRACE,     "\\}" },
    { TOKEN_COMMA,      "," },
    { TOKEN_COLON,      ":" },
    { TOKEN_DOT,        "\\." },
    { TOKEN_ASSIGN,     "=" },
    { TOKEN_PLUS,       "\\+" },
    { TOKEN_MINUS,      "\\-" },
    { TOKEN_MULT,       "\\*" },
    { TOKEN_DIV,        "/" },
    { TOKEN_MOD,        "%" },
    { TOKEN_LT,         "<" },
    { TOKEN_GT,         ">" },
    
    // ===== LITERALES =====
    { TOKEN_NUMBER,     "[0-9]+(\\.[0-9]+)?" },
    { TOKEN_STRING,     "\"[a-zA-Z0-9 ]*\"" },
    
    // ===== IDENTIFICADORES (debe ir DESPUÉS de palabras clave) =====
    { TOKEN_IDENT,      "[a-zA-Z_][a-zA-Z0-9_]*" },
    
    // ===== WHITESPACE (será ignorado) =====
    { TOKEN_WS,         "[ \\t\\n\\r]+" },
    
    // ===== COMENTARIOS =====
    { TOKEN_COMMENT,    "//.*" },
};

int hulk_token_count = sizeof(hulk_tokens) / sizeof(hulk_tokens[0]);

// Nombres de tokens para imprimir
const char* token_names[] = {
    "EOF", "WS", "COMMENT",
    "FUNCTION", "TYPE", "INHERITS", "WHILE", "FOR", "IN", "IF", "ELIF", "ELSE",
    "LET", "TRUE", "FALSE", "NEW", "SELF", "BASE", "AS", "IS",
    "SEMICOLON", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "COMMA", "COLON", "DOT",
    "ASSIGN", "ASSIGN_DESTRUCT", "PLUS", "MINUS", "MULT", "DIV", "MOD", "POW",
    "LT", "GT", "LE", "GE", "EQ", "NEQ", "OR", "AND", "CONCAT", "CONCAT_WS", "ARROW",
    "IDENT", "NUMBER", "STRING", "ERROR"
};

const char* get_token_name(int type) {
    if (type >= 0 && type < (int)(sizeof(token_names)/sizeof(token_names[0])))
        return token_names[type];
    return "UNKNOWN";
}

// ============== VARIABLES GLOBALES PARA EL PARSER ==============

static DFA* g_dfa = NULL;

// Callback para el parser: obtiene el siguiente token (ignorando WS y COMMENT)
Token parser_get_token(void* ctx) {
    (void)ctx;
    while (1) {
        Token t = lexer_next_token();
        // Ignorar whitespace y comentarios
        if (t.type != TOKEN_WS && t.type != TOKEN_COMMENT) {
            return t;
        }
        if (t.lexeme) free(t.lexeme);
    }
}

// ============== CONSTRUCCIÓN DEL LEXER ==============

DFA* build_hulk_lexer(void) {
    printf("\n========== CONSTRUCCIÓN DEL LEXER ==========\n");
    
    // Inicializaciones
    init_pos_to_token();
    followpos_init_all();
    reset_position_counter();
    
    printf("Construyendo AST del lexer...\n");
    ASTNode *ast = build_lexer_ast(hulk_tokens, hulk_token_count);
    
    if (!ast) {
        printf("Error: no se pudo construir el AST\n");
        return NULL;
    }
    
    printf("Calculando funciones del AST...\n");
    ast_compute_functions(ast);
    ast_compute_followpos(ast);
    
    // Alfabeto ASCII imprimible + whitespace
    char alphabet[128];
    int alphabet_size = 0;
    for (int c = 32; c < 127; c++) {
        alphabet[alphabet_size++] = (char)c;
    }
    alphabet[alphabet_size++] = '\t';
    alphabet[alphabet_size++] = '\n';
    alphabet[alphabet_size++] = '\r';
    
    printf("Construyendo DFA...\n");
    DFA *dfa = dfa_create(alphabet, alphabet_size);
    dfa_build(dfa, ast);
    
    printf("DFA construido con %d estados\n", dfa->count);
    
    // Exportar DFA para visualización
    dfa_save_dot(dfa, "output/lexer_dfa.dot", token_names);
    dfa_save_csv(dfa, "output/lexer_dfa.csv", token_names);
    
    ast_free(ast);
    
    return dfa;
}

// ============== TEST SOLO LEXER ==============

void test_lexer(DFA* dfa, const char* input) {
    printf("\n========== TEST LEXER ==========\n");
    printf("\n--- INPUT ---\n%s\n", input);
    printf("\n--- TOKENS ---\n");
    
    // Construir tabla si no existe
    if (dfa->next_state == NULL) {
        dfa_build_table(dfa);
    }
    
    lexer_init(dfa, input);
    
    while (1) {
        Token t = lexer_next_token();
        if (t.type == TOKEN_EOF) {
            printf("[EOF]\n");
            break;
        }
        
        // No imprimir whitespace
        if (t.type != TOKEN_WS && t.type != TOKEN_COMMENT) {
            printf("%-12s \"%s\"\n", get_token_name(t.type), t.lexeme);
        }
        free(t.lexeme);
    }
    
    printf("\n========== FIN TEST LEXER ==========\n");
}

// ============== TEST LEXER + PARSER ==============

void test_parser(DFA* dfa, const char* input, const char* grammar_file) {
    printf("\n========== TEST PARSER SINTÁCTICO ==========\n");
    
    // 1. Cargar y preparar gramática
    printf("\n--- Cargando gramática desde %s ---\n", grammar_file);
    
    Grammar grammar;
    grammar_init_hulk(&grammar);
    
    if (!grammar_load_hulk(&grammar, grammar_file)) {
        fprintf(stderr, "Error: no se pudo cargar la gramática\n");
        return;
    }
    
    grammar_print(&grammar);
    
    // 2. Calcular FIRST y FOLLOW
    printf("\n--- Calculando FIRST y FOLLOW ---\n");
    
    First_Table first;
    Follow_Table follow;
    
    compute_first_sets(&grammar, &first);
    compute_follow_sets(&grammar, &first, &follow);
    
    // Imprimir FIRST y FOLLOW
    printf("\nConjuntos FIRST:\n");
    for (int i = 0; i < grammar.nt_count; i++) {
        printf("  FIRST(%s) = { ", grammar.nt_names[i]);
        for (int j = 0; j < first.first[i].count; j++) {
            int t = first.first[i].elements[j];
            // Buscar nombre del terminal
            const char* tname = "?";
            for (int k = 0; k < grammar.t_count; k++) {
                if (grammar.terminals[k] == t) {
                    tname = grammar.t_names[k];
                    break;
                }
            }
            printf("%s ", tname);
        }
        if (first.first[i].has_epsilon) printf("ε ");
        printf("}\n");
    }
    
    printf("\nConjuntos FOLLOW:\n");
    for (int i = 0; i < grammar.nt_count; i++) {
        printf("  FOLLOW(%s) = { ", grammar.nt_names[i]);
        for (int j = 0; j < follow.follow[i].count; j++) {
            int t = follow.follow[i].elements[j];
            if (t == END_MARKER) {
                printf("$ ");
            } else {
                const char* tname = "?";
                for (int k = 0; k < grammar.t_count; k++) {
                    if (grammar.terminals[k] == t) {
                        tname = grammar.t_names[k];
                        break;
                    }
                }
                printf("%s ", tname);
            }
        }
        printf("}\n");
    }
    
    // 3. Construir tabla LL(1)
    printf("\n--- Construyendo tabla LL(1) ---\n");
    
    LL1_Table ll1;
    int is_ll1 = build_ll1_table(&grammar, &first, &follow, &ll1);
    
    if (!is_ll1) {
        fprintf(stderr, "ADVERTENCIA: La gramática NO es LL(1). Hay conflictos.\n");
    } else {
        printf("La gramática es LL(1) ✓\n");
    }
    
    // Guardar tabla en CSV
    ll1_table_save_csv(&ll1, &grammar, "output/hulk_ll1_table.csv");
    
    // 4. Inicializar parser
    printf("\n--- Analizando entrada ---\n");
    printf("INPUT: %s\n", input);
    
    ParserContext ctx;
    parser_init(&ctx, &grammar, &ll1);
    parser_set_lexer(&ctx, parser_get_token, NULL);
    
    // Inicializar lexer con la entrada
    lexer_init(dfa, input);
    
    // 5. Ejecutar parsing
    int result = parser_parse(&ctx);
    
    if (result) {
        printf("\n✓ ANÁLISIS SINTÁCTICO EXITOSO\n");
    } else {
        printf("\n✗ ERRORES EN EL ANÁLISIS SINTÁCTICO (%d errores)\n", ctx.error_count);
    }
    
    // Limpiar
    ll1_table_free(&ll1);
    grammar_free(&grammar);
    
    printf("\n========== FIN TEST PARSER ==========\n");
}

// ============== MAIN ==============

int main(int argc, char* argv[]) {
    printf("===========================================\n");
    printf("   HULK Compiler - Lexer + Parser LL(1)\n");
    printf("===========================================\n");
    
    // Crear directorio de salida
    system("mkdir -p output");
    
    // Construir el DFA del lexer
    g_dfa = build_hulk_lexer();
    if (!g_dfa) {
        fprintf(stderr, "Error fatal: no se pudo construir el lexer\n");
        return 1;
    }
    
    const char *default_input = "let x = 5;";
    const char *grammar_file = "grammar.ll1";
    char *input = NULL;
    int free_input = 0;
    
    // Procesar argumentos
    if (argc > 1) {
        // Verificar si es un archivo
        FILE *f = fopen(argv[1], "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            input = malloc(size + 1);
            fread(input, 1, size, f);
            input[size] = '\0';
            fclose(f);
            free_input = 1;
        } else {
            // Usar como entrada directa
            input = argv[1];
        }
    } else {
        input = (char*)default_input;
    }
    
    // Primero mostrar tokens (solo lexer)
    test_lexer(g_dfa, input);
    
    // Luego análisis sintáctico completo
    test_parser(g_dfa, input, grammar_file);
    
    // Limpiar
    if (free_input) free(input);
    dfa_free(g_dfa);
    
    return 0;
}
