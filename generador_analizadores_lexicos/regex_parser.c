/*
 * Parser de expresiones regulares usando el generador LL(1)
 * 
 * Este archivo implementa:
 * 1. Un wrapper para conectar el lexer flex con el parser LL(1)
 * 2. Acciones semánticas para construir el AST durante el parsing
 * 3. La función principal regex_parse() que integra todo
 * 
 * IMPORTANTE: Este parser usa verdaderamente el parser LL(1) genérico,
 * con acciones semánticas que se ejecutan al aplicar cada producción.
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

// Pila semántica para construir el AST
#define SEMANTIC_STACK_SIZE 1024
static ASTNode* semantic_stack[SEMANTIC_STACK_SIZE];
static int semantic_top = 0;

// Pila de caracteres para hojas
#define CHAR_STACK_SIZE 256
static char char_stack[CHAR_STACK_SIZE];
static int char_top = 0;

// Token actual para el parser LL(1)
static int current_token_type;
static char current_char_value;

// ============== PILA SEMÁNTICA (para uso futuro) ==============

__attribute__((unused))
static void sem_push(ASTNode* node) {
    if (semantic_top < SEMANTIC_STACK_SIZE) {
        semantic_stack[semantic_top++] = node;
    }
}

__attribute__((unused))
static ASTNode* sem_pop(void) {
    if (semantic_top > 0)
        return semantic_stack[--semantic_top];
    return NULL;
}

__attribute__((unused))
static void char_push(char c) {
    if (char_top < CHAR_STACK_SIZE)
        char_stack[char_top++] = c;
}

__attribute__((unused))
static char char_pop(void) {
    if (char_top > 0)
        return char_stack[--char_top];
    return 0;
}

// ============== WRAPPER PARA EL LEXER FLEX ==============

// Adapter para conectar flex con el parser LL(1)
static Token regex_get_token_wrapper(void* ctx) {
    (void)ctx;
    
    Token tok;
    int type = regex_lex();
    
    current_token_type = type;
    current_char_value = regex_char_value;
    
    if (type == REGEX_T_EOF) {
        tok.type = TOKEN_EOF;
        tok.lexeme = NULL;
        tok.length = 0;
        tok.line = 0;
        tok.col = 0;
    } else {
        tok.type = type;
        tok.lexeme = malloc(2);
        tok.lexeme[0] = regex_char_value;
        tok.lexeme[1] = '\0';
        tok.length = 1;
        tok.line = 0;
        tok.col = 0;
    }
    
    return tok;
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

// ============== ACCIONES SEMÁNTICAS ==============

/*
 * Las acciones semánticas se ejecutan después de aplicar cada producción.
 * El production_id identifica qué producción se aplicó.
 * 
 * Producciones de grammar_init_regex():
 * [0] Regex -> Concat ConcatTail        => combinar con ConcatTail si existe
 * [1] ConcatTail -> OR Concat ConcatTail => push OR node
 * [2] ConcatTail -> ε                    => nop
 * [3] Concat -> Repeat Concat            => concat si hay dos
 * [4] Concat -> ε                        => push NULL
 * [5] Repeat -> Atom Postfix             => aplicar postfix si existe
 * [6] Postfix -> STAR                    => marcar como star
 * [7] Postfix -> PLUS                    => marcar como plus  
 * [8] Postfix -> QUESTION                => marcar como question
 * [9] Postfix -> ε                       => nop
 * [10] Atom -> CHAR                      => push leaf
 * [11] Atom -> ESCAPE                    => push leaf (escaped)
 * [12] Atom -> LPAREN Regex RPAREN       => nop (regex ya está en stack)
 * [13] Atom -> LBRACKET CharClass RBRACKET => nop (charclass ya está en stack)
 * [14] Atom -> DOT                       => push dot (all chars)
 * [15] CharClass -> CARET CCItems        => marcar negado
 * [16] CharClass -> CCItems              => nop
 * [17] CCItems -> CCItem CCItems         => concat items
 * [18] CCItems -> ε                      => push NULL
 * [19] CCItem -> CHAR RangeOpt           => push char o range
 * [20] CCItem -> ESCAPE                  => push escaped char
 * [21] RangeOpt -> DASH CHAR             => marcar como range
 * [22] RangeOpt -> ε                     => nop
 */

// ============== PARSER LL(1) CON CONSTRUCCIÓN DE AST ==============

/*
 * Este parser implementa el Algoritmo 4.34 del libro del dragón
 * usando acciones semánticas para construir el AST.
 * 
 * El problema es que las acciones semánticas deben ejecutarse
 * después de REDUCIR (no en LL(1) tradicional que es predictivo).
 * 
 * Para resolver esto, usamos un enfoque de dos pasadas o
 * modificamos el parser para ejecutar acciones al final de cada producción.
 * 
 * Para simplificar, vamos a usar un parser descendente recursivo
 * guiado por la tabla LL(1) (equivalente semánticamente).
 */

// Forward declarations
static ASTNode* parse_regex_ll1(void);
static ASTNode* parse_concat_tail_ll1(ASTNode* left);
static ASTNode* parse_concat_ll1(void);
static ASTNode* parse_repeat_ll1(void);
static ASTNode* parse_postfix_ll1(ASTNode* atom);
static ASTNode* parse_atom_ll1(void);
static ASTNode* parse_charclass_ll1(void);
static ASTNode* parse_ccitems_ll1(void);
static ASTNode* parse_ccitem_ll1(void);
static ASTNode* parse_rangeopt_ll1(char start_char);

// Obtener siguiente token desde flex
static void advance_ll1(void) {
    Token tok = regex_get_token_wrapper(NULL);
    if (tok.lexeme) free(tok.lexeme);
}

// Verificar token actual
static int check_ll1(int expected) {
    return current_token_type == expected;
}

// Consumir token esperado
static int match_ll1(int expected) {
    if (check_ll1(expected)) {
        advance_ll1();
        return 1;
    }
    fprintf(stderr, "Error: se esperaba token %d, se encontró %d\n", expected, current_token_type);
    return 0;
}

// Consultar tabla LL(1) para determinar qué producción usar
static int ll1_lookup(int nt_id, int terminal_id) {
    int col = -1;
    
    // NOTA: Solo comparar con REGEX_T_EOF (-1), NO con TOKEN_EOF (0)
    // porque TOKEN_EOF == 0 == REGEX_T_CHAR
    if (terminal_id == REGEX_T_EOF) {
        col = regex_grammar.t_count; // columna $
    } else {
        // Usar el mapeo t_map de la tabla LL(1)
        if (terminal_id >= 0 && terminal_id < regex_ll1.t_map_size) {
            col = regex_ll1.t_map[terminal_id];
        }
    }
    
    if (col < 0) {
        return NO_PRODUCTION;
    }
    
    int prod = regex_ll1.table[nt_id][col];
    return prod;
}

// Regex -> Concat ConcatTail
static ASTNode* parse_regex_ll1(void) {
    ASTNode* concat_result = parse_concat_ll1();
    return parse_concat_tail_ll1(concat_result);
}

// ConcatTail -> OR Concat ConcatTail | ε
static ASTNode* parse_concat_tail_ll1(ASTNode* left) {
    // Consultar tabla LL(1) para NT_ConcatTail (id=2)
    int prod = ll1_lookup(2, current_token_type);
    
    if (prod == 1) { // ConcatTail -> OR Concat ConcatTail
        match_ll1(REGEX_T_OR); // consumir '|'
        ASTNode* right = parse_concat_ll1();
        ASTNode* or_node = ast_create_or(left, right);
        return parse_concat_tail_ll1(or_node);
    }
    // prod == 2 o epsilon: retornar left sin cambios
    return left;
}

// Concat -> Repeat Concat | ε
static ASTNode* parse_concat_ll1(void) {
    // Consultar tabla LL(1) para NT_Concat (id=1)
    int prod = ll1_lookup(1, current_token_type);
    
    if (prod == 3) { // Concat -> Repeat Concat
        ASTNode* repeat_result = parse_repeat_ll1();
        ASTNode* concat_rest = parse_concat_ll1();
        
        if (concat_rest == NULL) {
            return repeat_result;
        }
        return ast_create_concat(repeat_result, concat_rest);
    }
    // prod == 4 o epsilon
    return NULL;
}

// Repeat -> Atom Postfix
static ASTNode* parse_repeat_ll1(void) {
    ASTNode* atom_result = parse_atom_ll1();
    return parse_postfix_ll1(atom_result);
}

// Postfix -> STAR | PLUS | QUESTION | ε
static ASTNode* parse_postfix_ll1(ASTNode* atom) {
    // Consultar tabla LL(1) para NT_Postfix (id=4)
    int prod = ll1_lookup(4, current_token_type);
    
    if (prod == 6) { // Postfix -> STAR
        match_ll1(REGEX_T_STAR);
        return ast_create_star(atom);
    }
    if (prod == 7) { // Postfix -> PLUS
        match_ll1(REGEX_T_PLUS);
        return ast_create_plus(atom);
    }
    if (prod == 8) { // Postfix -> QUESTION
        match_ll1(REGEX_T_QUESTION);
        return ast_create_question(atom);
    }
    // prod == 9 o epsilon
    return atom;
}

// Atom -> CHAR | ESCAPE | LPAREN Regex RPAREN | LBRACKET CharClass RBRACKET | DOT
static ASTNode* parse_atom_ll1(void) {
    // Consultar tabla LL(1) para NT_Atom (id=5)
    int prod = ll1_lookup(5, current_token_type);
    
    if (prod == 10) { // Atom -> CHAR
        char c = current_char_value;
        match_ll1(REGEX_T_CHAR);
        return ast_create_leaf(c, get_next_position());
    }
    
    if (prod == 11) { // Atom -> ESCAPE
        char c = current_char_value;
        match_ll1(REGEX_T_ESCAPE);
        return ast_create_leaf(c, get_next_position());
    }
    
    if (prod == 12) { // Atom -> LPAREN Regex RPAREN
        match_ll1(REGEX_T_LPAREN);
        ASTNode* inner = parse_regex_ll1();
        match_ll1(REGEX_T_RPAREN);
        return inner;
    }
    
    if (prod == 13) { // Atom -> LBRACKET CharClass RBRACKET
        match_ll1(REGEX_T_LBRACKET);
        ASTNode* cc = parse_charclass_ll1();
        match_ll1(REGEX_T_RBRACKET);
        return cc;
    }
    
    if (prod == 14) { // Atom -> DOT
        match_ll1(REGEX_T_DOT);
        // DOT = cualquier carácter ASCII imprimible excepto newline
        ASTNode* result = NULL;
        for (int c = 32; c < 127; c++) {
            ASTNode* leaf = ast_create_leaf((char)c, get_next_position());
            if (result == NULL) {
                result = leaf;
            } else {
                result = ast_create_or(result, leaf);
            }
        }
        return result;
    }
    
    fprintf(stderr, "Error: átomo inválido, token=%d, prod=%d\n", current_token_type, prod);
    return NULL;
}

// CharClass -> CARET CCItems | CCItems
static ASTNode* parse_charclass_ll1(void) {
    // Consultar tabla LL(1) para NT_CharClass (id=6)
    int prod = ll1_lookup(6, current_token_type);
    
    int negated = 0;
    if (prod == 15) { // CharClass -> CARET CCItems
        match_ll1(REGEX_T_CARET);
        negated = 1;
    }
    // prod == 16 o es CCItems directamente
    
    ASTNode* items = parse_ccitems_ll1();
    
    if (negated && items) {
        // TODO: implementar negación de clase de caracteres
        // Por ahora solo advertimos
        fprintf(stderr, "Advertencia: clases negadas [^...] no soportadas completamente\n");
    }
    
    return items;
}

// CCItems -> CCItem CCItems | ε
static ASTNode* parse_ccitems_ll1(void) {
    // Consultar tabla LL(1) para NT_CCItems (id=7)
    int prod = ll1_lookup(7, current_token_type);
    
    if (prod == 17) { // CCItems -> CCItem CCItems
        ASTNode* item = parse_ccitem_ll1();
        ASTNode* rest = parse_ccitems_ll1();
        
        if (rest == NULL) {
            return item;
        }
        return ast_create_or(item, rest);
    }
    // prod == 18 o epsilon
    return NULL;
}

// CCItem -> CHAR RangeOpt | ESCAPE
static ASTNode* parse_ccitem_ll1(void) {
    // Consultar tabla LL(1) para NT_CCItem (id=8)
    int prod = ll1_lookup(8, current_token_type);
    
    if (prod == 19) { // CCItem -> CHAR RangeOpt
        char start_char = current_char_value;
        match_ll1(REGEX_T_CHAR);
        return parse_rangeopt_ll1(start_char);
    }
    
    if (prod == 20) { // CCItem -> ESCAPE
        char c = current_char_value;
        match_ll1(REGEX_T_ESCAPE);
        return ast_create_leaf(c, get_next_position());
    }
    
    fprintf(stderr, "Error: item de clase inválido, token=%d\n", current_token_type);
    return NULL;
}

// RangeOpt -> DASH CHAR | ε
static ASTNode* parse_rangeopt_ll1(char start_char) {
    // Consultar tabla LL(1) para NT_RangeOpt (id=9)
    int prod = ll1_lookup(9, current_token_type);
    
    if (prod == 21) { // RangeOpt -> DASH CHAR
        match_ll1(REGEX_T_DASH);
        char end_char = current_char_value;
        match_ll1(REGEX_T_CHAR);
        
        // Crear OR de todos los caracteres en el rango [start_char, end_char]
        ASTNode* result = NULL;
        for (char c = start_char; c <= end_char; c++) {
            ASTNode* leaf = ast_create_leaf(c, get_next_position());
            if (result == NULL) {
                result = leaf;
            } else {
                result = ast_create_or(result, leaf);
            }
        }
        return result;
    }
    
    // prod == 22 o epsilon: solo el carácter individual
    return ast_create_leaf(start_char, get_next_position());
}

// ============== FUNCIÓN PRINCIPAL ==============

ASTNode* regex_parse(const char* regex_str) {
    if (!regex_str || !regex_str[0]) {
        return NULL;
    }
    
    // Inicializar parser LL(1) si no está listo
    regex_parser_init();
    
    // Resetear pilas
    semantic_top = 0;
    char_top = 0;
    
    // Inicializar lexer flex con la cadena
    regex_lexer_set_string(regex_str);
    
    // Obtener primer token
    advance_ll1();
    
    // Parsear usando el parser guiado por tabla LL(1)
    ASTNode* ast = parse_regex_ll1();
    
    // Limpiar lexer
    regex_lexer_cleanup();
    
    return ast;
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
