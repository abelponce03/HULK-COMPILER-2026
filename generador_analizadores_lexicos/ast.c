#include "ast.h"

// Inicializa un conjunto vacío
void set_init(PositionSet *s) 
{
    memset(s->bits, 0, sizeof(s->bits));
}

// Agrega una posición al conjunto
void set_add(PositionSet *s, int pos) 
{
    int idx = pos / (sizeof(unsigned long) * 8);
    int bit = pos % (sizeof(unsigned long) * 8);
    s->bits[idx] |= (1UL << bit);
}

// Verifica si el conjunto contiene pos
int set_contains(PositionSet *s, int pos) 
{
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


//NODO HOJA

ASTNode* ast_create_leaf(char symbol, int pos) 
{
    ASTNode *node = (ASTNode*)malloc(sizeof(ASTNode));
    node->type = NODE_LEAF;
    node->left = node->right = NULL;
    node->symbol = symbol;
    node->pos = pos;

    node->nullable = 0;  // hojas no son nullable (excepto si manejas explícitamente ε)
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    // para hojas: firstpos y lastpos contienen su propia posición
    set_add(&node->firstpos, pos);
    set_add(&node->lastpos, pos);

    return node;
}

//NODO CONCATENACIÓN

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

//NODO ALTERNANCIA (OR)

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

//NODO CERRADURA (STAR)

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

//MANEJO DE POSICIONES ÚNICAS PARA HOJAS
//AL CREARSE UNA HOJA SE LE ASIGNA UNA POSICIÓN ÚNICA

int next_position = 1;

int get_next_position() {
    return next_position++;
}


//FUNCION QUE RECORRE EL AST POST-ORDEN, CALCULA Y ALMACENA:
//NULLABLE, FIRSTPOS, LASTPOS PARA CADA NODO

#include "ast.h"

// Función principal para calcular nullable, firstpos y lastpos
void ast_compute_functions(ASTNode *node) 
{
    if (node == NULL) {
        return;
    }

    // Recorrer hijos primero (post-orden)
    if (node->type == NODE_CONCAT || node->type == NODE_OR) {
        ast_compute_functions(node->left);
        ast_compute_functions(node->right);
    } else if (node->type == NODE_STAR) {
        ast_compute_functions(node->left);
    }

    // Inicializar conjuntos a vacío antes de calcular
    set_init(&node->firstpos);
    set_init(&node->lastpos);

    switch (node->type) {
        case NODE_LEAF:
            // Ya están inicializados en el constructor
            // Nullable permanece como está
            set_add(&node->firstpos, node->pos);
            set_add(&node->lastpos, node->pos);
            break;

        case NODE_OR: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable || c2->nullable;

            // firstpos = union de hijos
            set_union(&node->firstpos, &c1->firstpos, &c2->firstpos);

            // lastpos = union de hijos
            set_union(&node->lastpos, &c1->lastpos, &c2->lastpos);
        } break;

        case NODE_CONCAT: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable && c2->nullable;

            // firstpos
            if (c1->nullable) {
                PositionSet temp;
                set_union(&temp, &c1->firstpos, &c2->firstpos);
                node->firstpos = temp;
            } else {
                node->firstpos = c1->firstpos;
            }

            // lastpos
            if (c2->nullable) {
                PositionSet temp;
                set_union(&temp, &c1->lastpos, &c2->lastpos);
                node->lastpos = temp;
            } else {
                node->lastpos = c2->lastpos;
            }
        } break;

        case NODE_STAR: {
            ASTNode *c = node->left;
            node->nullable = 1; // por definición
            node->firstpos = c->firstpos;
            node->lastpos = c->lastpos;
        } break;
    }
}


void followpos_init_all() 
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

    // Recorremos el árbol en post-orden
    if (node->type == NODE_CONCAT) {
        // concatenación
        ASTNode *c1 = node->left;
        ASTNode *c2 = node->right;

        // Para cada posición i en lastpos(c1),
        // agregamos firstpos(c2) en followpos[i].
        for (int i = 0; i < MAX_POSITIONS; i++) {
            if (set_contains(&c1->lastpos, i)) {
                set_union(&followpos[i], &followpos[i], &c2->firstpos);
            }
        }

        ast_compute_followpos(c1);
        ast_compute_followpos(c2);

    } else if (node->type == NODE_STAR) {
        // estrella
        ASTNode *c = node->left;

        // Para cada posición i en lastpos(c),
        // agregamos firstpos(c) en followpos[i].
        for (int i = 0; i < MAX_POSITIONS; i++) {
            if (set_contains(&c->lastpos, i)) {
                set_union(&followpos[i], &followpos[i], &c->firstpos);
            }
        }

        ast_compute_followpos(c);

    } else if (node->type == NODE_OR) {
        // alternancia
        ast_compute_followpos(node->left);
        ast_compute_followpos(node->right);
    } else {
        // hoja: nada que hacer
        return;
    }
}

//Funcion que recorre el ast para devolver el nodo hoja con la posicion pos
ASTNode* find_leaf_by_pos(ASTNode *root, int pos) {
    if (root == NULL) return NULL;

    if (root->type == NODE_LEAF && root->pos == pos)
        return root;

    ASTNode *found = find_leaf_by_pos(root->left, pos);
    if (found) return found;
    return find_leaf_by_pos(root->right, pos);
}
