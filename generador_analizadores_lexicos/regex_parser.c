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
#include "../generador_parser_ll1/grammar.h"
#include "../generador_parser_ll1/parser.h"
#include "../generador_parser_ll1/first_follow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../error_handler.h"

// ============== CONTEXTO OPACO DEL PARSER ==============
//
// Encapsula la gramática de regex, las tablas FIRST/FOLLOW y la tabla LL(1).
// Reemplaza las 7 variables estáticas anteriores, haciendo el módulo
// reentrante y eliminando estado global.

struct RegexParserContext {
    Grammar grammar;
    First_Table first;
    Follow_Table follow;
    LL1_Table ll1;
    int initialized;
};

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

// Usa GrammarSymbol como símbolo unificado de la pila del parser
// (SYMBOL_TERMINAL, SYMBOL_NON_TERMINAL, SYMBOL_ACTION, SYMBOL_END de grammar.h)

#define RSTACK_MAX 2048
#define SEM_STACK_MAX 512

// IDs de no-terminales (deben coincidir con grammar_init_regex())
enum {
    RNT_Regex = 0, RNT_Concat = 1, RNT_ConcatTail = 2, RNT_Repeat = 3,
    RNT_Postfix = 4, RNT_Atom = 5, RNT_CharClass = 6, RNT_CCItems = 7,
    RNT_CCItem = 8, RNT_RangeOpt = 9
};

// ============== WRAPPER PARA EL LEXER FLEX ==============

static void regex_advance(int *token_type, char *char_value) {
    int type = regex_lex();
    *token_type = type;
    *char_value = regex_char_value;
}

// ============== CREACIÓN Y DESTRUCCIÓN ==============

RegexParserContext* regex_parser_create(void) {
    RegexParserContext *rctx = malloc(sizeof(RegexParserContext));
    if (!rctx) return NULL;
    rctx->initialized = 0;
    return rctx;
}

// Inicialización lazy: construye gramática + tablas la primera vez
static void regex_parser_ensure_init(RegexParserContext *rctx) {
    if (rctx->initialized) return;
    
    // Inicializar gramática de regex
    grammar_init_regex(&rctx->grammar);
    
    // Calcular FIRST y FOLLOW
    compute_first_sets(&rctx->grammar, &rctx->first);
    compute_follow_sets(&rctx->grammar, &rctx->first, &rctx->follow);
    
    // Construir tabla LL(1)
    if (!build_ll1_table(&rctx->grammar, &rctx->first, &rctx->follow, &rctx->ll1)) {
        LOG_WARN_MSG("regex", "la gramática de regex tiene conflictos LL(1)");
    }
    
    // Exportar tabla LL(1) de regex a CSV
    ll1_table_save_csv(&rctx->ll1, &rctx->grammar, "output/regex_ll1_table.csv");
    
    rctx->initialized = 1;
}

void regex_parser_destroy(RegexParserContext *rctx) {
    if (!rctx) return;
    if (rctx->initialized) {
        grammar_free(&rctx->grammar);
        ll1_table_free(&rctx->ll1);
    }
    free(rctx);
}

// ============== CONSULTA TABLA LL(1) ==============

static int ll1_lookup(RegexParserContext *rctx, int nt_id, int terminal_id) {
    int col = -1;
    // REGEX_T_EOF == -1, no confundir con TOKEN_EOF == 0 == REGEX_T_CHAR
    if (terminal_id == REGEX_T_EOF) {
        col = rctx->grammar.t_count; // columna $
    } else if (terminal_id >= 0 && terminal_id < rctx->ll1.t_map_size) {
        col = rctx->ll1.t_map[terminal_id];
    }
    return (col < 0) ? NO_PRODUCTION : rctx->ll1.table[nt_id][col];
}

// ============== PUSH DE PRODUCCIONES ==============
//
// Para cada producción, se empujan los símbolos del lado derecho en orden
// INVERSO (para que se procesen de izquierda a derecha), intercalando
// marcadores de acción semántica donde corresponda.

static void rpush(GrammarSymbol* stack, int* top, SymbolType type, int id) {
    if (*top < RSTACK_MAX)
        stack[(*top)++] = (GrammarSymbol){type, id};
}

static void push_production(int prod_id, GrammarSymbol* stack, int* top) {
    switch (prod_id) {
    // [0] Regex -> Concat ConcatTail
    case 0:
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_ConcatTail);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Concat);
        break;
    // [1] ConcatTail -> OR Concat ConcatTail
    case 1:
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_ConcatTail);
        rpush(stack, top, SYMBOL_ACTION, ACT_OR);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Concat);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_OR);
        break;
    // [2] ConcatTail -> ε
    case 2:
        break;
    // [3] Concat -> Repeat Concat
    case 3:
        rpush(stack, top, SYMBOL_ACTION, ACT_CONCAT);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Concat);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Repeat);
        break;
    // [4] Concat -> ε
    case 4:
        rpush(stack, top, SYMBOL_ACTION, ACT_PUSH_NULL);
        break;
    // [5] Repeat -> Atom Postfix
    case 5:
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Postfix);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Atom);
        break;
    // [6] Postfix -> STAR
    case 6:
        rpush(stack, top, SYMBOL_ACTION, ACT_STAR);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_STAR);
        break;
    // [7] Postfix -> PLUS
    case 7:
        rpush(stack, top, SYMBOL_ACTION, ACT_PLUS_OP);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_PLUS);
        break;
    // [8] Postfix -> QUESTION
    case 8:
        rpush(stack, top, SYMBOL_ACTION, ACT_QUESTION_OP);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_QUESTION);
        break;
    // [9] Postfix -> ε
    case 9:
        break;
    // [10] Atom -> CHAR
    case 10:
        rpush(stack, top, SYMBOL_ACTION, ACT_LEAF);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_CHAR);
        break;
    // [11] Atom -> ESCAPE
    case 11:
        rpush(stack, top, SYMBOL_ACTION, ACT_LEAF);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_ESCAPE);
        break;
    // [12] Atom -> LPAREN Regex RPAREN
    case 12:
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_RPAREN);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_Regex);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_LPAREN);
        break;
    // [13] Atom -> LBRACKET CharClass RBRACKET
    case 13:
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_RBRACKET);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_CharClass);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_LBRACKET);
        break;
    // [14] Atom -> DOT
    case 14:
        rpush(stack, top, SYMBOL_ACTION, ACT_DOT);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_DOT);
        break;
    // [15] CharClass -> CARET CCItems
    case 15:
        rpush(stack, top, SYMBOL_ACTION, ACT_NEGATE);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_CCItems);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_CARET);
        break;
    // [16] CharClass -> CCItems
    case 16:
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_CCItems);
        break;
    // [17] CCItems -> CCItem CCItems
    case 17:
        rpush(stack, top, SYMBOL_ACTION, ACT_OR_OPT);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_CCItems);
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_CCItem);
        break;
    // [18] CCItems -> ε
    case 18:
        rpush(stack, top, SYMBOL_ACTION, ACT_PUSH_NULL);
        break;
    // [19] CCItem -> CHAR RangeOpt
    case 19:
        rpush(stack, top, SYMBOL_NON_TERMINAL, RNT_RangeOpt);
        rpush(stack, top, SYMBOL_ACTION, ACT_SAVE_RANGE_START);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_CHAR);
        break;
    // [20] CCItem -> ESCAPE
    case 20:
        rpush(stack, top, SYMBOL_ACTION, ACT_LEAF);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_ESCAPE);
        break;
    // [21] RangeOpt -> DASH CHAR
    case 21:
        rpush(stack, top, SYMBOL_ACTION, ACT_RANGE);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_CHAR);
        rpush(stack, top, SYMBOL_TERMINAL, REGEX_T_DASH);
        break;
    // [22] RangeOpt -> ε (solo carácter individual)
    case 22:
        rpush(stack, top, SYMBOL_ACTION, ACT_LEAF_RANGE_START);
        break;
    default:
        LOG_ERROR_MSG("regex", "producción desconocida %d", prod_id);
        break;
    }
}

// ============== EJECUCIÓN DE ACCIONES SEMÁNTICAS ==============

static void exec_action(int act, ASTNode** sem, int* sem_top,
                        char saved_char, char range_start_char,
                        ASTContext *ctx) {
    switch (act) {
    case ACT_LEAF:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(ctx, saved_char, get_next_position(ctx));
        break;

    case ACT_DOT: {
        ASTNode* result = NULL;
        for (int c = 32; c < 127; c++) {
            ASTNode* leaf = ast_create_leaf(ctx, (char)c, get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_STAR:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_star(ctx, sem[*sem_top - 1]);
        break;

    case ACT_PLUS_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_plus(ctx, sem[*sem_top - 1]);
        break;

    case ACT_QUESTION_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_question(ctx, sem[*sem_top - 1]);
        break;

    case ACT_OR: {
        ASTNode* right = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* left  = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_or(ctx, left, right);
        break;
    }

    case ACT_CONCAT: {
        ASTNode* rest = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* item = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = (rest == NULL) ? item
                                               : ast_create_concat(ctx, item, rest);
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
                                               : ast_create_or(ctx, item, rest);
        break;
    }

    case ACT_SAVE_RANGE_START:
        // Manejo especial: no se usa exec_action para esto
        break;

    case ACT_LEAF_RANGE_START:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(ctx, range_start_char, get_next_position(ctx));
        break;

    case ACT_RANGE: {
        ASTNode* result = NULL;
        for (char c = range_start_char; c <= saved_char; c++) {
            ASTNode* leaf = ast_create_leaf(ctx, c, get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_NEGATE:
        // TODO: implementar negación de clase de caracteres
        LOG_WARN_MSG("regex", "clases negadas [^...] no soportadas completamente");
        break;
    }
}

// ============== FUNCIÓN PRINCIPAL ==============
//
// Analizador predictivo basado en pila (Algoritmo 4.34, Dragon Book)
// con acciones semánticas para construir el AST.

ASTNode* regex_parse(const char* regex_str, ASTContext *ctx,
                     RegexParserContext *rctx) {
    if (!regex_str || !regex_str[0]) return NULL;

    regex_parser_ensure_init(rctx);

    // Pila del parser
    GrammarSymbol pstack[RSTACK_MAX];
    int ptop = 0;

    // Pila semántica
    ASTNode* sem[SEM_STACK_MAX];
    int sem_top = 0;

    // Valores auxiliares para terminales
    char saved_char = 0;
    char range_start_char = 0;

    // Lookahead local (ya no es estado estático)
    int current_token_type;
    char current_char_value;

    // Inicializar pila: $ y símbolo inicial (Regex)
    pstack[ptop++] = (GrammarSymbol){SYMBOL_END, 0};
    pstack[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, RNT_Regex};

    // Iniciar lexer flex y obtener primer token
    regex_lexer_set_string(regex_str);
    regex_advance(&current_token_type, &current_char_value);

    int error = 0;

    while (ptop > 0 && !error) {
        GrammarSymbol top = pstack[--ptop];

        switch (top.type) {
        case SYMBOL_EPSILON:
            break; // no debería ocurrir
        case SYMBOL_END:
            if (current_token_type != REGEX_T_EOF) {
                LOG_ERROR_MSG("regex", "entrada extra después del parse");
                error = 1;
            }
            goto done;

        case SYMBOL_TERMINAL:
            if (top.id == current_token_type) {
                saved_char = current_char_value;
                regex_advance(&current_token_type, &current_char_value);
            } else {
                LOG_ERROR_MSG("regex", "se esperaba token %d, se encontró %d",
                              top.id, current_token_type);
                error = 1;
            }
            break;

        case SYMBOL_NON_TERMINAL: {
            int prod = ll1_lookup(rctx, top.id, current_token_type);
            if (prod == NO_PRODUCTION) {
                LOG_ERROR_MSG("regex", "sin producción para NT=%d, token=%d",
                              top.id, current_token_type);
                error = 1;
            } else {
                push_production(prod, pstack, &ptop);
            }
            break;
        }

        case SYMBOL_ACTION:
            if (top.id == ACT_SAVE_RANGE_START) {
                // Caso especial: actualizar variable local, no la pila semántica
                range_start_char = saved_char;
            } else {
                exec_action(top.id, sem, &sem_top, saved_char, range_start_char, ctx);
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

ASTNode* build_lexer_ast(TokenRegex* tokens, int token_count,
                         ASTContext *ctx, RegexParserContext *rctx) {
    if (!tokens || token_count <= 0) {
        return NULL;
    }
    
    ASTNode* combined = NULL;
    
    for (int i = 0; i < token_count; i++) {
        ASTNode* ast = regex_parse(tokens[i].regex, ctx, rctx);
        
        if (ast == NULL) {
            LOG_ERROR_MSG("regex", "Error parseando regex para token %d: '%s'",
                          tokens[i].token_id, tokens[i].regex);
            continue;
        }
        
        // Agregar marcador de fin (#) y asociar token_id
        int end_pos = get_next_position(ctx);
        ASTNode* end_marker = ast_create_leaf(ctx, '#', end_pos);
        
        // Registrar qué token corresponde a esta posición
        ctx->pos_to_token[end_pos] = tokens[i].token_id;
        
        // regex#
        ASTNode* marked = ast_create_concat(ctx, ast, end_marker);
        
        // Combinar con OR
        if (combined == NULL) {
            combined = marked;
        } else {
            combined = ast_create_or(ctx, combined, marked);
        }
    }
    
    return combined;
}
