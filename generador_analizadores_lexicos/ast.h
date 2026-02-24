#ifndef AST_H
#define AST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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

#define MAX_POSITIONS 4096

typedef struct 
{
    unsigned long bits[(MAX_POSITIONS + (sizeof(unsigned long)*8 - 1)) / (sizeof(unsigned long)*8)];
} PositionSet;

// Funciones para manipular conjuntos de posiciones
void posset_init(PositionSet *s);
void posset_add(PositionSet *s, int pos);
void posset_union(PositionSet *dest, PositionSet *a, PositionSet *b);
int  posset_contains(PositionSet *s, int pos);
int  posset_is_empty(PositionSet *s);

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

// Contexto que agrupa todo el estado mutable del AST/DFA:
//   - followpos[]:      resultado del cálculo de followpos
//   - leaf_at[]:        índice posición → nodo hoja
//   - pos_to_token[]:   mapa posición '#' → token_id
//   - next_position:    contador de posiciones únicas
//   - max_position:     mayor posición asignada (para acotar iteraciones)
//   - pool:             arena de nodos AST (Object Pool)
typedef struct {
    PositionSet followpos[MAX_POSITIONS];
    ASTNode*    leaf_at[MAX_POSITIONS];
    int         pos_to_token[MAX_POSITIONS];
    int         next_position;
    int         max_position;   // = next_position - 1 tras construir AST

    // Object Pool: arena de nodos pre-asignada
    ASTNode*    pool_nodes;     // bloque contiguo de nodos
    int         pool_count;     // nodos asignados
    int         pool_capacity;  // capacidad total
} ASTContext;

// Inicializa todos los campos de un ASTContext (followpos, leaf_at,
// pos_to_token a -1, next_position a 1, pool vacío).
void ast_context_init(ASTContext *ctx);

// Libera la arena de nodos (sustituye ast_free recursivo)
void ast_context_free(ASTContext *ctx);

// Funciones de creación de nodos AST (usan el pool del contexto)
ASTNode* ast_create_leaf(ASTContext *ctx, char symbol, int pos);
ASTNode* ast_create_concat(ASTContext *ctx, ASTNode *left, ASTNode *right);
ASTNode* ast_create_or(ASTContext *ctx, ASTNode *left, ASTNode *right);
ASTNode* ast_create_star(ASTContext *ctx, ASTNode *child);
ASTNode* ast_create_plus(ASTContext *ctx, ASTNode *child);
ASTNode* ast_create_question(ASTContext *ctx, ASTNode *child);

// Obtener siguiente posición única
int get_next_position(ASTContext *ctx);

// ============== PATRÓN VISITOR ==============
// Función genérica de visita: recibe el nodo y datos del usuario.
typedef void (*ASTVisitFn)(ASTNode *node, void *data);

// Visitor con un callback por tipo de nodo.
// Los campos pueden ser NULL si no se necesita visitar ese tipo.
typedef struct {
    ASTVisitFn visit_leaf;
    ASTVisitFn visit_concat;
    ASTVisitFn visit_or;
    ASTVisitFn visit_star;
    ASTVisitFn visit_plus;
    ASTVisitFn visit_question;
} ASTVisitor;

// Recorre el AST en post-orden (hijos primero, luego raíz)
void ast_walk_postorder(ASTNode *node, const ASTVisitor *v, void *data);

// Recorre el AST en pre-orden (raíz primero, luego hijos)
void ast_walk_preorder(ASTNode *node, const ASTVisitor *v, void *data);

// ============== FUNCIONES DE ALTO NIVEL ==============

// Función que recorre el AST post-orden, calcula y almacena:
// nullable, firstpos, lastpos para cada nodo
void ast_compute_functions(ASTNode *root);

// Inicializar followpos y calcular (usa ctx->followpos)
void ast_compute_followpos(ASTNode *root, ASTContext *ctx);

// Construye el índice ctx->leaf_at[pos] → nodo hoja en O(n)
// Debe llamarse después de construir el AST y antes de dfa_build()
void ast_build_leaf_index(ASTNode *root, ASTContext *ctx);

// Liberar memoria del AST (noop cuando se usa pool — usar ast_context_free)
void ast_free(ASTNode *node);

// Imprime el AST de forma indentada (debugging)
void ast_print(ASTNode* node, int depth);

#endif // AST_H