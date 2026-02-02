#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


// Tipo de nodo en el AST
typedef enum {
    NODE_LEAF,
    NODE_CONCAT,
    NODE_OR,
    NODE_STAR,
    NODE_PLUS,      // cerradura positiva (uno o más)
    NODE_QUESTION   // opcional (cero o uno)
} NodeType;


// Representación de un conjunto de posiciones
// Necesitamos una forma de representar conjuntos de posiciones 
// (firstpos, lastpos, followpos).

#define MAX_POSITIONS 512

typedef struct 
{
    unsigned long bits[(MAX_POSITIONS + (sizeof(unsigned long)*8 - 1)) / (sizeof(unsigned long)*8)];
} PositionSet;

// Funciones para manipular conjuntos de posiciones
void set_init(PositionSet *s);
void set_add(PositionSet *s, int pos);
void set_union(PositionSet *dest, PositionSet *a, PositionSet *b);
int  set_contains(PositionSet *s, int pos);
int  set_is_empty(PositionSet *s);

// Estructura para nodos de AST
// Cada nodo del AST tendrá:
// - Tipo de nodo
// - Punteros a hijos
// - Para hojas: el símbolo y su posición
// - Cálculos auxiliares: anulable, firstpos, lastpos

typedef struct ASTNode {
    NodeType type;

    struct ASTNode *left;
    struct ASTNode *right;

    // Para hojas:
    char symbol;     // carácter terminal (o marca como '#')
    int  pos;        // posición única si es hoja

    // Cálculo de funciones:
    int         nullable;
    PositionSet firstpos;
    PositionSet lastpos;
} ASTNode;

// Variable global followpos - declarada como extern
extern PositionSet followpos[MAX_POSITIONS];

// Contador de posiciones (para asignar posiciones únicas a hojas)
extern int next_position;

// Funciones de creación de nodos AST
ASTNode* ast_create_leaf(char symbol, int pos);
ASTNode* ast_create_concat(ASTNode *left, ASTNode *right);
ASTNode* ast_create_or(ASTNode *left, ASTNode *right);
ASTNode* ast_create_star(ASTNode *child);
ASTNode* ast_create_plus(ASTNode *child);
ASTNode* ast_create_question(ASTNode *child);

// Obtener siguiente posición única
int get_next_position(void);

// Reiniciar contador de posiciones
void reset_position_counter(void);

// Función que recorre el AST post-orden, calcula y almacena:
// nullable, firstpos, lastpos para cada nodo
void ast_compute_functions(ASTNode *root);

// Preparar followpos antes de calcular 
void followpos_init_all(void);

// Función para recorrer el AST y llenar followpos
void ast_compute_followpos(ASTNode *root);

// Función que recorre el AST y devuelve el nodo hoja con la posición pos
ASTNode* find_leaf_by_pos(ASTNode *root, int pos);

// Liberar memoria del AST
void ast_free(ASTNode *node);

#endif // AST_H