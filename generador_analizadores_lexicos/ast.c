#include "ast.h"

// Inicializa un ASTContext (reemplaza init_pos_to_token +
// followpos_init_all + reset_position_counter)
void ast_context_init(ASTContext *ctx) {
    memset(ctx->followpos, 0, sizeof(ctx->followpos));
    memset(ctx->leaf_at,   0, sizeof(ctx->leaf_at));
    for (int i = 0; i < MAX_POSITIONS; i++)
        ctx->pos_to_token[i] = -1;
    ctx->next_position = 1;
    ctx->max_position  = 0;
}

// Inicializa un conjunto vacío
void posset_init(PositionSet *s) 
{
    memset(s->bits, 0, sizeof(s->bits));
}

// Agrega una posición al conjunto
void posset_add(PositionSet *s, int pos) 
{
    if (pos < 0 || pos >= MAX_POSITIONS) return;
    int idx = pos / (sizeof(unsigned long) * 8);
    int bit = pos % (sizeof(unsigned long) * 8);
    s->bits[idx] |= (1UL << bit);
}

// Verifica si el conjunto contiene pos
int posset_contains(PositionSet *s, int pos) 
{
    if (pos < 0 || pos >= MAX_POSITIONS) return 0;
    int idx = pos / (sizeof(unsigned long) * 8);
    int bit = pos % (sizeof(unsigned long) * 8);
    return (s->bits[idx] >> bit) & 1UL;
}

// Unión de conjuntos: dest = a ∪ b
void posset_union(PositionSet *dest, PositionSet *a, PositionSet *b)
{
    for (int i = 0; i < (int)(sizeof(dest->bits)/sizeof(dest->bits[0])); i++) 
    {
        dest->bits[i] = a->bits[i] | b->bits[i];
    }
}

// Verifica si el conjunto está vacío
int posset_is_empty(PositionSet *s)
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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

    // para hojas: firstpos y lastpos contienen su propia posición
    posset_add(&node->firstpos, pos);
    posset_add(&node->lastpos, pos);

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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

    return node;
}

// ============ MANEJO DE POSICIONES ============

int get_next_position(ASTContext *ctx) {
    int pos = ctx->next_position++;
    if (pos > ctx->max_position)
        ctx->max_position = pos;
    return pos;
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
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

    switch (node->type) {
        case NODE_LEAF:
            // Las hojas: firstpos y lastpos son su propia posición
            node->nullable = 0;
            posset_add(&node->firstpos, node->pos);
            posset_add(&node->lastpos, node->pos);
            break;

        case NODE_OR: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable || c2->nullable;
            posset_union(&node->firstpos, &c1->firstpos, &c2->firstpos);
            posset_union(&node->lastpos, &c1->lastpos, &c2->lastpos);
        } break;

        case NODE_CONCAT: {
            ASTNode *c1 = node->left;
            ASTNode *c2 = node->right;

            node->nullable = c1->nullable && c2->nullable;

            // firstpos
            if (c1->nullable) {
                posset_union(&node->firstpos, &c1->firstpos, &c2->firstpos);
            } else {
                node->firstpos = c1->firstpos;
            }

            // lastpos
            if (c2->nullable) {
                posset_union(&node->lastpos, &c1->lastpos, &c2->lastpos);
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


// Recorre el AST y actualiza ctx->followpos
void ast_compute_followpos(ASTNode *node, ASTContext *ctx)
{
    if (node == NULL) {
        return;
    }

    // Recorremos hijos primero
    ast_compute_followpos(node->left, ctx);
    ast_compute_followpos(node->right, ctx);

    int limit = ctx->max_position + 1;

    if (node->type == NODE_CONCAT) {
        ASTNode *c1 = node->left;
        ASTNode *c2 = node->right;

        for (int i = 0; i < limit; i++) {
            if (posset_contains(&c1->lastpos, i)) {
                posset_union(&ctx->followpos[i], &ctx->followpos[i], &c2->firstpos);
            }
        }

    } else if (node->type == NODE_STAR || node->type == NODE_PLUS) {
        ASTNode *c = node->left;

        for (int i = 0; i < limit; i++) {
            if (posset_contains(&c->lastpos, i)) {
                posset_union(&ctx->followpos[i], &ctx->followpos[i], &c->firstpos);
            }
        }
    }
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

// --- Índice directo posición → hoja (reemplaza find_leaf_by_pos) ---

static void leaf_index_fill(ASTNode *node, ASTContext *ctx) {
    if (!node) return;
    if (node->type == NODE_LEAF && node->pos >= 0 && node->pos < MAX_POSITIONS)
        ctx->leaf_at[node->pos] = node;
    leaf_index_fill(node->left, ctx);
    leaf_index_fill(node->right, ctx);
}

// Construye índice ctx->leaf_at[pos] → nodo hoja en una sola pasada O(n)
void ast_build_leaf_index(ASTNode *root, ASTContext *ctx) {
    memset(ctx->leaf_at, 0, sizeof(ctx->leaf_at));
    leaf_index_fill(root, ctx);
}

// Liberar memoria del AST
void ast_free(ASTNode *node)
{
    if (node == NULL) return;
    ast_free(node->left);
    ast_free(node->right);
    free(node);
}
