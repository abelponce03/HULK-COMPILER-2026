/*
 * Parser de expresiones regulares — Analizador Predictivo Basado en Pila
 * (Table-Driven LL(1) Parser, Algoritmo 4.34, Dragon Book)
 *
 * Usa la tabla LL(1) generada por el generador de parsers para dirigir
 * el análisis sintáctico. Acciones semánticas integradas en la pila del
 * parser construyen el AST durante el análisis.
 *
 * Componentes:
 *  1. Pila del parser: símbolos terminales, no-terminales y marcadores de acción
 *  2. Pila semántica: nodos ASTNode* para construir el árbol
 *  3. Tabla LL(1): determina qué producción aplicar para cada (NT, terminal)
 *  4. Acciones: se ejecutan cuando un marcador ACT_xxx es extraído de la pila
 *
 * Las producciones y sus acciones están acopladas a grammar_init_regex().
 * Si se modifica la gramática, se deben actualizar las acciones en push_production().
 */

#include "regex_parser.h"
#include "regex_tokens.h"
#include "../generador_parser_ll1/first_&_follow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============== ESTADO GLOBAL DEL PARSER ==============

static Grammar regex_grammar;
static First_Table regex_first;
static Follow_Table regex_follow;
static LL1_Table regex_ll1;
static int regex_parser_initialized = 0;

// Token actual (lookahead)
static int current_token_type;
static char current_char_value;

// ============== ACCIONES SEMÁNTICAS ==============
//
// Marcadores de acción que se insertan en la pila del parser.
// Cuando se extraen, modifican la pila semántica para construir el AST.
//
// Correspondencia con producciones de grammar_init_regex():
// [0]  Regex     -> Concat ConcatTail
// [1]  ConcatTail-> OR Concat ConcatTail   → ACT_OR
// [2]  ConcatTail-> ε
// [3]  Concat    -> Repeat Concat           → ACT_CONCAT
// [4]  Concat    -> ε                       → ACT_PUSH_NULL
// [5]  Repeat    -> Atom Postfix
// [6]  Postfix   -> STAR                    → ACT_STAR
// [7]  Postfix   -> PLUS                    → ACT_PLUS_OP
// [8]  Postfix   -> QUESTION                → ACT_QUESTION_OP
// [9]  Postfix   -> ε
// [10] Atom      -> CHAR                    → ACT_LEAF
// [11] Atom      -> ESCAPE                  → ACT_LEAF
// [12] Atom      -> LPAREN Regex RPAREN
// [13] Atom      -> LBRACKET CharClass RBRACKET
// [14] Atom      -> DOT                     → ACT_DOT
// [15] CharClass -> CARET CCItems           → ACT_NEGATE
// [16] CharClass -> CCItems
// [17] CCItems   -> CCItem CCItems          → ACT_OR_OPT
// [18] CCItems   -> ε                       → ACT_PUSH_NULL
// [19] CCItem    -> CHAR RangeOpt           → ACT_SAVE_RANGE_START
// [20] CCItem    -> ESCAPE                  → ACT_LEAF
// [21] RangeOpt  -> DASH CHAR               → ACT_RANGE
// [22] RangeOpt  -> ε                       → ACT_LEAF_RANGE_START

typedef enum {
    ACT_LEAF,            // Crea leaf(saved_char, next_pos), push
    ACT_DOT,             // Crea OR de printables (32..126), push
    ACT_STAR,            // Pop nodo, push star(nodo)
    ACT_PLUS_OP,         // Pop nodo, push plus(nodo)
    ACT_QUESTION_OP,     // Pop nodo, push question(nodo)
    ACT_OR,              // Pop right, pop left, push or(left, right)
    ACT_CONCAT,          // Pop rest, pop item: si rest==NULL push item, sino concat
    ACT_PUSH_NULL,       // Push NULL al sem_stack
    ACT_OR_OPT,          // Pop rest, pop item: si rest==NULL push item, sino or
    ACT_SAVE_RANGE_START,// Guarda saved_char en range_start_char (sin tocar sem_stack)
    ACT_LEAF_RANGE_START,// Crea leaf(range_start_char, next_pos), push
    ACT_RANGE,           // Crea OR chain range_start_char..saved_char, push
    ACT_NEGATE,          // (futuro) negación de clase de caracteres
} SemanticAction;

// ============== PILA DEL PARSER (3 tipos de símbolo) ==============

typedef enum {
    RSYM_TERMINAL,
    RSYM_NON_TERMINAL,
    RSYM_ACTION,
    RSYM_END
} RSymType;

typedef struct {
    RSymType type;
    int id;
} RSym;

#define RSTACK_MAX 2048
#define SEM_STACK_MAX 512

// IDs de no-terminales (deben coincidir con grammar_init_regex())
enum {
    RNT_Regex = 0, RNT_Concat = 1, RNT_ConcatTail = 2, RNT_Repeat = 3,
    RNT_Postfix = 4, RNT_Atom = 5, RNT_CharClass = 6, RNT_CCItems = 7,
    RNT_CCItem = 8, RNT_RangeOpt = 9
};

// ============== WRAPPER PARA EL LEXER FLEX ==============

static void regex_advance(void) {
    int type = regex_lex();
    current_token_type = type;
    current_char_value = regex_char_value;
}

// ============== INICIALIZACIÓN ==============

void regex_parser_init(void) {
    if (regex_parser_initialized) return;
    
    // Inicializar gramática de regex
    grammar_init_regex(&regex_grammar);
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&regex_grammar, &regex_first);
    compute_follow_sets(&regex_grammar, &regex_first, &regex_follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&regex_grammar, &regex_first, &regex_follow, &regex_ll1)) {
        fprintf(stderr, "Advertencia: la gramática de regex tiene conflictos LL(1)\n");
    }
    
    // Exportar tabla LL(1) de regex a CSV
    ll1_table_save_csv(&regex_ll1, &regex_grammar, "output/regex_ll1_table.csv");
    
    regex_parser_initialized = 1;
}

void regex_parser_cleanup(void) {
    if (regex_parser_initialized) {
        grammar_free(&regex_grammar);
        ll1_table_free(&regex_ll1);
        regex_parser_initialized = 0;
    }
}

// ============== CONSULTA TABLA LL(1) ==============

static int ll1_lookup(int nt_id, int terminal_id) {
    int col = -1;
    // REGEX_T_EOF == -1, no confundir con TOKEN_EOF == 0 == REGEX_T_CHAR
    if (terminal_id == REGEX_T_EOF) {
        col = regex_grammar.t_count; // columna $
    } else if (terminal_id >= 0 && terminal_id < regex_ll1.t_map_size) {
        col = regex_ll1.t_map[terminal_id];
    }
    return (col < 0) ? NO_PRODUCTION : regex_ll1.table[nt_id][col];
}

// ============== PUSH DE PRODUCCIONES ==============
//
// Para cada producción, se empujan los símbolos del lado derecho en orden
// INVERSO (para que se procesen de izquierda a derecha), intercalando
// marcadores de acción semántica donde corresponda.

static void rpush(RSym* stack, int* top, RSymType type, int id) {
    if (*top < RSTACK_MAX)
        stack[(*top)++] = (RSym){type, id};
}

static void push_production(int prod_id, RSym* stack, int* top) {
    switch (prod_id) {
    // [0] Regex -> Concat ConcatTail
    case 0:
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_ConcatTail);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Concat);
        break;
    // [1] ConcatTail -> OR Concat ConcatTail
    case 1:
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_ConcatTail);
        rpush(stack, top, RSYM_ACTION, ACT_OR);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Concat);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_OR);
        break;
    // [2] ConcatTail -> ε
    case 2:
        break;
    // [3] Concat -> Repeat Concat
    case 3:
        rpush(stack, top, RSYM_ACTION, ACT_CONCAT);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Concat);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Repeat);
        break;
    // [4] Concat -> ε
    case 4:
        rpush(stack, top, RSYM_ACTION, ACT_PUSH_NULL);
        break;
    // [5] Repeat -> Atom Postfix
    case 5:
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Postfix);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Atom);
        break;
    // [6] Postfix -> STAR
    case 6:
        rpush(stack, top, RSYM_ACTION, ACT_STAR);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_STAR);
        break;
    // [7] Postfix -> PLUS
    case 7:
        rpush(stack, top, RSYM_ACTION, ACT_PLUS_OP);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_PLUS);
        break;
    // [8] Postfix -> QUESTION
    case 8:
        rpush(stack, top, RSYM_ACTION, ACT_QUESTION_OP);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_QUESTION);
        break;
    // [9] Postfix -> ε
    case 9:
        break;
    // [10] Atom -> CHAR
    case 10:
        rpush(stack, top, RSYM_ACTION, ACT_LEAF);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_CHAR);
        break;
    // [11] Atom -> ESCAPE
    case 11:
        rpush(stack, top, RSYM_ACTION, ACT_LEAF);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_ESCAPE);
        break;
    // [12] Atom -> LPAREN Regex RPAREN
    case 12:
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_RPAREN);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_Regex);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_LPAREN);
        break;
    // [13] Atom -> LBRACKET CharClass RBRACKET
    case 13:
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_RBRACKET);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_CharClass);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_LBRACKET);
        break;
    // [14] Atom -> DOT
    case 14:
        rpush(stack, top, RSYM_ACTION, ACT_DOT);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_DOT);
        break;
    // [15] CharClass -> CARET CCItems
    case 15:
        rpush(stack, top, RSYM_ACTION, ACT_NEGATE);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_CCItems);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_CARET);
        break;
    // [16] CharClass -> CCItems
    case 16:
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_CCItems);
        break;
    // [17] CCItems -> CCItem CCItems
    case 17:
        rpush(stack, top, RSYM_ACTION, ACT_OR_OPT);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_CCItems);
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_CCItem);
        break;
    // [18] CCItems -> ε
    case 18:
        rpush(stack, top, RSYM_ACTION, ACT_PUSH_NULL);
        break;
    // [19] CCItem -> CHAR RangeOpt
    case 19:
        rpush(stack, top, RSYM_NON_TERMINAL, RNT_RangeOpt);
        rpush(stack, top, RSYM_ACTION, ACT_SAVE_RANGE_START);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_CHAR);
        break;
    // [20] CCItem -> ESCAPE
    case 20:
        rpush(stack, top, RSYM_ACTION, ACT_LEAF);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_ESCAPE);
        break;
    // [21] RangeOpt -> DASH CHAR
    case 21:
        rpush(stack, top, RSYM_ACTION, ACT_RANGE);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_CHAR);
        rpush(stack, top, RSYM_TERMINAL, REGEX_T_DASH);
        break;
    // [22] RangeOpt -> ε (solo carácter individual)
    case 22:
        rpush(stack, top, RSYM_ACTION, ACT_LEAF_RANGE_START);
        break;
    default:
        fprintf(stderr, "Error regex: producción desconocida %d\n", prod_id);
        break;
    }
}

// ============== EJECUCIÓN DE ACCIONES SEMÁNTICAS ==============

static void exec_action(int act, ASTNode** sem, int* sem_top,
                        char saved_char, char range_start_char) {
    switch (act) {
    case ACT_LEAF:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(saved_char, get_next_position());
        break;

    case ACT_DOT: {
        ASTNode* result = NULL;
        for (int c = 32; c < 127; c++) {
            ASTNode* leaf = ast_create_leaf((char)c, get_next_position());
            result = result ? ast_create_or(result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_STAR:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_star(sem[*sem_top - 1]);
        break;

    case ACT_PLUS_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_plus(sem[*sem_top - 1]);
        break;

    case ACT_QUESTION_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_question(sem[*sem_top - 1]);
        break;

    case ACT_OR: {
        ASTNode* right = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* left  = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_or(left, right);
        break;
    }

    case ACT_CONCAT: {
        ASTNode* rest = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* item = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = (rest == NULL) ? item
                                               : ast_create_concat(item, rest);
        break;
    }

    case ACT_PUSH_NULL:
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = NULL;
        break;

    case ACT_OR_OPT: {
        ASTNode* rest = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* item = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = (rest == NULL) ? item
                                               : ast_create_or(item, rest);
        break;
    }

    case ACT_SAVE_RANGE_START:
        // Manejo especial: no se usa exec_action para esto
        break;

    case ACT_LEAF_RANGE_START:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(range_start_char, get_next_position());
        break;

    case ACT_RANGE: {
        ASTNode* result = NULL;
        for (char c = range_start_char; c <= saved_char; c++) {
            ASTNode* leaf = ast_create_leaf(c, get_next_position());
            result = result ? ast_create_or(result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_NEGATE:
        // TODO: implementar negación de clase de caracteres
        fprintf(stderr, "Advertencia: clases negadas [^...] no soportadas completamente\n");
        break;
    }
}

// ============== FUNCIÓN PRINCIPAL ==============
//
// Analizador predictivo basado en pila (Algoritmo 4.34, Dragon Book)
// con acciones semánticas para construir el AST.

ASTNode* regex_parse(const char* regex_str) {
    if (!regex_str || !regex_str[0]) return NULL;

    regex_parser_init();

    // Pila del parser
    RSym pstack[RSTACK_MAX];
    int ptop = 0;

    // Pila semántica
    ASTNode* sem[SEM_STACK_MAX];
    int sem_top = 0;

    // Valores auxiliares para terminales
    char saved_char = 0;
    char range_start_char = 0;

    // Inicializar pila: $ y símbolo inicial (Regex)
    pstack[ptop++] = (RSym){RSYM_END, 0};
    pstack[ptop++] = (RSym){RSYM_NON_TERMINAL, RNT_Regex};

    // Iniciar lexer flex y obtener primer token
    regex_lexer_set_string(regex_str);
    regex_advance();

    int error = 0;

    while (ptop > 0 && !error) {
        RSym top = pstack[--ptop];

        switch (top.type) {
        case RSYM_END:
            if (current_token_type != REGEX_T_EOF) {
                fprintf(stderr, "Error regex: entrada extra después del parse\n");
                error = 1;
            }
            goto done;

        case RSYM_TERMINAL:
            if (top.id == current_token_type) {
                saved_char = current_char_value;
                regex_advance();
            } else {
                fprintf(stderr, "Error regex: se esperaba token %d, se encontró %d\n",
                        top.id, current_token_type);
                error = 1;
            }
            break;

        case RSYM_NON_TERMINAL: {
            int prod = ll1_lookup(top.id, current_token_type);
            if (prod == NO_PRODUCTION) {
                fprintf(stderr, "Error regex: sin producción para NT=%d, token=%d\n",
                        top.id, current_token_type);
                error = 1;
            } else {
                push_production(prod, pstack, &ptop);
            }
            break;
        }

        case RSYM_ACTION:
            if (top.id == ACT_SAVE_RANGE_START) {
                // Caso especial: actualizar variable local, no la pila semántica
                range_start_char = saved_char;
            } else {
                exec_action(top.id, sem, &sem_top, saved_char, range_start_char);
            }
            break;
        }
    }

done:
    regex_lexer_cleanup();

    if (error) {
        for (int i = 0; i < sem_top; i++)
            if (sem[i]) ast_free(sem[i]);
        return NULL;
    }

    return (sem_top > 0) ? sem[--sem_top] : NULL;
}

// ============== CONSTRUCCIÓN DEL AST DEL LEXER ==============

ASTNode* build_lexer_ast(TokenRegex* tokens, int token_count) {
    if (!tokens || token_count <= 0) {
        return NULL;
    }
    
    ASTNode* combined = NULL;
    
    for (int i = 0; i < token_count; i++) {
        ASTNode* ast = regex_parse(tokens[i].regex);
        
        if (ast == NULL) {
            fprintf(stderr, "Error parseando regex para token %d: '%s'\n", 
                    tokens[i].token_id, tokens[i].regex);
            continue;
        }
        
        // Agregar marcador de fin (#) y asociar token_id
        int end_pos = get_next_position();
        ASTNode* end_marker = ast_create_leaf('#', end_pos);
        
        // Registrar qué token corresponde a esta posición
        pos_to_token[end_pos] = tokens[i].token_id;
        
        // regex#
        ASTNode* marked = ast_create_concat(ast, end_marker);
        
        // Combinar con OR
        if (combined == NULL) {
            combined = marked;
        } else {
            combined = ast_create_or(combined, marked);
        }
    }
    
    return combined;
}

// ============== DEBUGGING ==============

void ast_print(ASTNode* node, int depth) {
    if (!node) return;
    
    for (int i = 0; i < depth; i++) printf("  ");
    
    switch (node->type) {
        case NODE_LEAF:
            if (node->symbol == '#')
                printf("LEAF(#, pos=%d)\n", node->pos);
            else if (node->symbol >= 32 && node->symbol < 127)
                printf("LEAF('%c', pos=%d)\n", node->symbol, node->pos);
            else
                printf("LEAF(0x%02x, pos=%d)\n", (unsigned char)node->symbol, node->pos);
            break;
        case NODE_CONCAT:
            printf("CONCAT\n");
            ast_print(node->left, depth + 1);
            ast_print(node->right, depth + 1);
            break;
        case NODE_OR:
            printf("OR\n");
            ast_print(node->left, depth + 1);
            ast_print(node->right, depth + 1);
            break;
        case NODE_STAR:
            printf("STAR\n");
            ast_print(node->left, depth + 1);
            break;
        case NODE_PLUS:
            printf("PLUS\n");
            ast_print(node->left, depth + 1);
            break;
        case NODE_QUESTION:
            printf("QUESTION\n");
            ast_print(node->left, depth + 1);
            break;
    }
}
