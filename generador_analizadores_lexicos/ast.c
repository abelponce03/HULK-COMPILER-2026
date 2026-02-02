#include "ast.h"

// Definición de variables globales
PositionSet followpos[MAX_POSITIONS];
int next_position = 1;

// Inicializa un conjunto vacío
void set_init(PositionSet *s) 
{
    memset(s->bits, 0, sizeof(s->bits));
}

// Agrega una posición al conjunto
void set_add(PositionSet *s, int pos) 
{
    if (pos < 0 || pos >= MAX_POSITIONS) return;
    int idx = pos / (sizeof(unsigned long) * 8);
    int bit = pos % (sizeof(unsigned long) * 8);
    s->bits[idx] |= (1UL << bit);
}

// Verifica si el conjunto contiene pos
int set_contains(PositionSet *s, int pos) 
{
    if (pos < 0 || pos >= MAX_POSITIONS) return 0;
    int idx = pos / (sizeof(unsigned long) * 8);
    int bit = pos % (sizeof(unsigned long) * 8);
    return (s->bits[idx] >> bit) & 1UL;
}

// Unión de conjuntos: dest = a ∪ b
void set_union(PositionSet *dest, PositionSet *a, PositionSet *b)
{
    for (int i = 0; i < (int)(sizeof(dest->bits)/sizeof(dest->bits[0])); i++) 
    {
        dest->bits[i] = a->bits[i] | b->bits[i];
    }
}

// Verifica si el conjunto está vacío
int set_is_empty(PositionSet *s)
{
    for (int i = 0; i < (int)(sizeof(s->bits)/sizeof(s->bits[0])); i++) 
    {
        if (s->bits[i] != 0) return 0;
    }
    return 1;
}


// ============ CREACIÓN DE NODOS AST ============

// NODO HOJA
ASTNode* ast_create_leaf(char symbol, int pos) 
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_LEAF;
    node->left = node->right = NULL;
    node->symbol = symbol;
    node->pos = pos;

    node->nullable = 0;  // hojas no son nullable
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    // para hojas: firstpos y lastpos contienen su propia posición
    set_add(&node->firstpos, pos);
    set_add(&node->lastpos, pos);

    return node;
}

// NODO CONCATENACIÓN
ASTNode* ast_create_concat(ASTNode *left, ASTNode *right) 
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_CONCAT;
    node->left = left;
    node->right = right;

    node->symbol = 0;
    node->pos = -1;

    node->nullable = 0;
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    return node;
}

// NODO ALTERNANCIA (OR)
ASTNode* ast_create_or(ASTNode *left, ASTNode *right) 
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_OR;
    node->left = left;
    node->right = right;

    node->symbol = 0;
    node->pos = -1;

    node->nullable = 0;
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    return node;
}

// NODO CERRADURA (STAR) - cero o más
ASTNode* ast_create_star(ASTNode *child)
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_STAR;
    node->left = child;
    node->right = NULL;

    node->symbol = 0;
    node->pos = -1;

    node->nullable = 1;  // por definición la estrella puede ser nullable
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    return node;
}

// NODO CERRADURA POSITIVA (PLUS) - uno o más
// a+ equivale a aa*
ASTNode* ast_create_plus(ASTNode *child)
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_PLUS;
    node->left = child;
    node->right = NULL;

    node->symbol = 0;
    node->pos = -1;

    node->nullable = 0;  // plus no es nullable (requiere al menos uno)
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    return node;
}

// NODO OPCIONAL (QUESTION) - cero o uno
// a? equivale a (a|ε)
ASTNode* ast_create_question(ASTNode *child)
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_QUESTION;
    node->left = child;
    node->right = NULL;

    node->symbol = 0;
    node->pos = -1;

    node->nullable = 1;  // question es nullable (puede ser cero)
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    return node;
}

// ============ MANEJO DE POSICIONES ============

int get_next_position(void) {
    return next_position++;
}

void reset_position_counter(void) {
    next_position = 1;
}


// ============ CÁLCULO DE FUNCIONES DEL AST ============

// Función principal para calcular nullable, firstpos y lastpos
void ast_compute_functions(ASTNode *node) 
{
    if (node == NULL) {
        return;
    }

    // Recorrer hijos primero (post-orden)
    ast_compute_functions(node->left);
    ast_compute_functions(node->right);

    // Inicializar conjuntos a vacío antes de calcular
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    switch (node->type) {
        case NODE_LEAF:
            // Las hojas: firstpos y lastpos son su propia posición
            node->nullable = 0;
            set_add(&node->firstpos, node->pos);
            set_add(&node->lastpos, node->pos);
            break;

        case NODE_OR: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable || c2->nullable;
            set_union(&node->firstpos, &c1->firstpos, &c2->firstpos);
            set_union(&node->lastpos, &c1->lastpos, &c2->lastpos);
        } break;

        case NODE_CONCAT: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable && c2->nullable;

            // firstpos
            if (c1->nullable) {
                set_union(&node->firstpos, &c1->firstpos, &c2->firstpos);
            } else {
                node->firstpos = c1->firstpos;
            }

            // lastpos
            if (c2->nullable) {
                set_union(&node->lastpos, &c1->lastpos, &c2->lastpos);
            } else {
                node->lastpos = c2->lastpos;
            }
        } break;

        case NODE_STAR: {
            ASTNode *c = node->left;
            node->nullable = 1;
            node->firstpos = c->firstpos;
            node->lastpos = c->lastpos;
        } break;

        case NODE_PLUS: {
            // a+ = aa*, firstpos = firstpos(a), lastpos = lastpos(a)
            ASTNode *c = node->left;
            node->nullable = c->nullable;  // plus es nullable solo si hijo es nullable
            node->firstpos = c->firstpos;
            node->lastpos = c->lastpos;
        } break;

        case NODE_QUESTION: {
            // a? = (a|ε), siempre nullable
            ASTNode *c = node->left;
            node->nullable = 1;
            node->firstpos = c->firstpos;
            node->lastpos = c->lastpos;
        } break;
    }
}


void followpos_init_all(void) 
{
    for (int i = 0; i < MAX_POSITIONS; i++) 
    {
        set_init(&followpos[i]);
    }
}


// Recorre el AST y actualiza followpos
void ast_compute_followpos(ASTNode *node)
{
    if (node == NULL) {
        return;
    }

    // Recorremos hijos primero
    ast_compute_followpos(node->left);
    ast_compute_followpos(node->right);

    if (node->type == NODE_CONCAT) {
        // Concatenación: para cada posición i en lastpos(c1),
        // agregamos firstpos(c2) en followpos[i].
        ASTNode *c1 = node->left;
        ASTNode *c2 = node->right;

        for (int i = 0; i < MAX_POSITIONS; i++) {
            if (set_contains(&c1->lastpos, i)) {
                set_union(&followpos[i], &followpos[i], &c2->firstpos);
            }
        }

    } else if (node->type == NODE_STAR || node->type == NODE_PLUS) {
        // Estrella/Plus: para cada posición i en lastpos(c),
        // agregamos firstpos(c) en followpos[i].
        ASTNode *c = node->left;

        for (int i = 0; i < MAX_POSITIONS; i++) {
            if (set_contains(&c->lastpos, i)) {
                set_union(&followpos[i], &followpos[i], &c->firstpos);
            }
        }
    }
    // NODE_OR y NODE_QUESTION no afectan followpos directamente
    // NODE_LEAF tampoco
}


// Función que recorre el AST para devolver el nodo hoja con la posición pos
ASTNode* find_leaf_by_pos(ASTNode *root, int pos) {
    if (root == NULL) return NULL;

    if (root->type == NODE_LEAF && root->pos == pos)
        return root;

    ASTNode *found = find_leaf_by_pos(root->left, pos);
    if (found) return found;
    return find_leaf_by_pos(root->right, pos);
}

// Liberar memoria del AST
void ast_free(ASTNode *node)
{
    if (node == NULL) return;
    ast_free(node->left);
    ast_free(node->right);
    free(node);
}
