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
    NODE_STAR
} NodeType;



//Representación de un conjunto de posiciones
//Necesitamos una forma de representar conjuntos de posiciones 
//(firstpos, lastpos, followpos).

#define MAX_POSITIONS 256

typedef struct 
{
    unsigned long bits[(MAX_POSITIONS + (sizeof(unsigned long)*8 - 1)) / (sizeof(unsigned long)*8)];

} PositionSet;

//Funciones para manipular conjuntos de posiciones

void set_init(PositionSet *s);
void set_add(PositionSet *s, int pos);
void set_union(PositionSet *dest, PositionSet *a, PositionSet *b);
int  set_contains(PositionSet *s, int pos);

//Estructura para nodos de AST
//Cada nodo del AST tendrá:
//Tipo de nodo
//Punteros a hijos
//Para hojas: el símbolo y su posición
//Cálculos auxiliares: anulable, firstpos, lastpos

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

PositionSet followpos[MAX_POSITIONS];


//FUNCION QUE RECORRE EL AST POST-ORDEN, CALCULA Y ALMACENA:
//NULLABLE, FIRSTPOS, LASTPOS PARA CADA NODO

void ast_compute_functions(ASTNode *root);

//PREPARAR FOLLOWPOS ANTES DE CALCULAR 
void followpos_init_all();

//FUNCION PARA RECORRER EL AST Y LLENAR FOLLOWPOS
void ast_compute_followpos(ASTNode *root);

//Funcion que recorre el AST y devuelve el nodo hoja con la posicion pos
ASTNode* find_leaf_by_pos(ASTNode *root, int pos);



#endif // AST_H