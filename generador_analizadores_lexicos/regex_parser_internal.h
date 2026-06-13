/*
 * regex_parser_internal.h — Definiciones internas del parser de regex
 *
 * Compartidas entre el motor de parsing predictivo (regex_parser.c) y
 * las acciones semánticas que construyen el AST (regex_ast_actions.c):
 *   - SemanticAction: los marcadores ACT_* que se apilan y disparan
 *     construcción de nodos del AST.
 *   - RNT_*: IDs de no-terminales (deben coincidir con grammar_init_regex).
 *   - exec_action: ejecuta una acción semántica sobre la pila semántica.
 *
 * Header PRIVADO del subsistema lexer; no lo incluye código externo.
 */

#ifndef REGEX_PARSER_INTERNAL_H
#define REGEX_PARSER_INTERNAL_H

#include "ast.h"

#define RSTACK_MAX 2048
#define SEM_STACK_MAX 512

/* Acciones semánticas: marcadores que, al extraerse de la pila del
 * parser, construyen nodos del AST de la regex sobre la pila semántica. */
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
    ACT_NEGATE,          // Negación de clase de caracteres [^...]
} SemanticAction;

/* IDs de no-terminales (deben coincidir con grammar_init_regex()). */
enum {
    RNT_Regex = 0, RNT_Concat = 1, RNT_ConcatTail = 2, RNT_Repeat = 3,
    RNT_Postfix = 4, RNT_Atom = 5, RNT_CharClass = 6, RNT_CCItems = 7,
    RNT_CCItem = 8, RNT_RangeOpt = 9
};

/* Ejecuta la acción semántica `act` sobre la pila semántica `sem`. */
void exec_action(int act, ASTNode **sem, int *sem_top,
                 char saved_char, char range_start_char,
                 ASTContext *ctx);

#endif /* REGEX_PARSER_INTERNAL_H */
