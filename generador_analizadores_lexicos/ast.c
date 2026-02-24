#include "ast.h"
#include "../error_handler.h"

// ============ POOL / ARENA DE NODOS ============

#define AST_POOL_INITIAL_CAPACITY 4096

// Inicializa un ASTContext (reemplaza init_pos_to_token +
// followpos_init_all + reset_position_counter + pool_init)
void ast_context_init(ASTContext *ctx) {
    memset(ctx->followpos, 0, sizeof(ctx->followpos));
    memset(ctx->leaf_at,   0, sizeof(ctx->leaf_at));
    for (int i = 0; i < MAX_POSITIONS; i++)
        ctx->pos_to_token[i] = -1;
    ctx->next_position = 1;
    ctx->max_position  = 0;

    // Inicializar pool
    ctx->pool_nodes    = (ASTNode*)malloc(sizeof(ASTNode) * AST_POOL_INITIAL_CAPACITY);
    ctx->pool_count    = 0;
    ctx->pool_capacity = ctx->pool_nodes ? AST_POOL_INITIAL_CAPACITY : 0;
}

// Libera la arena completa (sustituye el ast_free recursivo)
void ast_context_free(ASTContext *ctx) {
    if (ctx->pool_nodes) {
        free(ctx->pool_nodes);
        ctx->pool_nodes    = NULL;
        ctx->pool_count    = 0;
        ctx->pool_capacity = 0;
    }
}

// Obtiene un nodo del pool (crece automáticamente)
static ASTNode* pool_alloc(ASTContext *ctx) {
    if (ctx->pool_count >= ctx->pool_capacity) {
        int new_cap = ctx->pool_capacity * 2;
        ASTNode *tmp = (ASTNode*)realloc(ctx->pool_nodes,
                                         sizeof(ASTNode) * new_cap);
        if (!tmp) {
            LOG_FATAL_MSG("ast", "sin memoria expandiendo pool (%d nodos)", new_cap);
            return NULL;
        }
        ctx->pool_nodes    = tmp;
        ctx->pool_capacity = new_cap;
    }
    return &ctx->pool_nodes[ctx->pool_count++];
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


// ============ CREACIÓN DE NODOS AST (via Pool) ============

// Inicializa los campos comunes de un nodo interno
static void node_init_internal(ASTNode *node, NodeType type,
                               ASTNode *left, ASTNode *right) {
    node->type     = type;
    node->left     = left;
    node->right    = right;
    node->symbol   = 0;
    node->pos      = -1;
    node->nullable = 0;
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);
}

// NODO HOJA
ASTNode* ast_create_leaf(ASTContext *ctx, char symbol, int pos) 
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node->type = NODE_LEAF;
    node->left = node->right = NULL;
    node->symbol = symbol;
    node->pos = pos;

    node->nullable = 0;
    posset_init(&node->firstpos);
    posset_init(&node->lastpos);

    // para hojas: firstpos y lastpos contienen su propia posición
    posset_add(&node->firstpos, pos);
    posset_add(&node->lastpos, pos);

    return node;
}

// NODO CONCATENACIÓN
ASTNode* ast_create_concat(ASTContext *ctx, ASTNode *left, ASTNode *right) 
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node_init_internal(node, NODE_CONCAT, left, right);
    return node;
}

// NODO ALTERNANCIA (OR)
ASTNode* ast_create_or(ASTContext *ctx, ASTNode *left, ASTNode *right) 
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node_init_internal(node, NODE_OR, left, right);
    return node;
}

// NODO CERRADURA (STAR) - cero o más
ASTNode* ast_create_star(ASTContext *ctx, ASTNode *child)
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node_init_internal(node, NODE_STAR, child, NULL);
    node->nullable = 1;  // por definición la estrella puede ser nullable
    return node;
}

// NODO CERRADURA POSITIVA (PLUS) - uno o más
// a+ equivale a aa*
ASTNode* ast_create_plus(ASTContext *ctx, ASTNode *child)
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node_init_internal(node, NODE_PLUS, child, NULL);
    // plus no es nullable (requiere al menos uno)
    return node;
}

// NODO OPCIONAL (QUESTION) - cero o uno
// a? equivale a (a|ε)
ASTNode* ast_create_question(ASTContext *ctx, ASTNode *child)
{
    ASTNode *node = pool_alloc(ctx);
    if (!node) return NULL;
    node_init_internal(node, NODE_QUESTION, child, NULL);
    node->nullable = 1;  // question es nullable (puede ser cero)
    return node;
}

// ============ MANEJO DE POSICIONES ============

int get_next_position(ASTContext *ctx) {
    int pos = ctx->next_position++;
    if (pos > ctx->max_position)
        ctx->max_position = pos;
    return pos;
}


// ============ PATRÓN VISITOR — WALKERS GENÉRICOS ============

// Dispatch: llama al callback correspondiente al tipo del nodo
static void visitor_dispatch(ASTNode *node, const ASTVisitor *v, void *data) {
    static const int type_to_index[] = {0, 1, 2, 3, 4, 5};
    // NODE_LEAF=0, NODE_CONCAT=1, NODE_OR=2, NODE_STAR=3, NODE_PLUS=4, NODE_QUESTION=5
    ASTVisitFn callbacks[] = {
        v->visit_leaf, v->visit_concat, v->visit_or,
        v->visit_star, v->visit_plus,   v->visit_question
    };
    (void)type_to_index;
    ASTVisitFn fn = callbacks[node->type];
    if (fn) fn(node, data);
}

void ast_walk_postorder(ASTNode *node, const ASTVisitor *v, void *data) {
    if (!node) return;
    ast_walk_postorder(node->left, v, data);
    ast_walk_postorder(node->right, v, data);
    visitor_dispatch(node, v, data);
}

void ast_walk_preorder(ASTNode *node, const ASTVisitor *v, void *data) {
    if (!node) return;
    visitor_dispatch(node, v, data);
    ast_walk_preorder(node->left, v, data);
    ast_walk_preorder(node->right, v, data);
}

// ============ VISITORS CONCRETOS ============

// --- compute_functions: calcula nullable, firstpos, lastpos (post-orden) ---

static void visit_compute_leaf(ASTNode *n, void *data) {
    (void)data;
    posset_init(&n->firstpos);
    posset_init(&n->lastpos);
    n->nullable = 0;
    posset_add(&n->firstpos, n->pos);
    posset_add(&n->lastpos, n->pos);
}

static void visit_compute_or(ASTNode *n, void *data) {
    (void)data;
    posset_init(&n->firstpos);
    posset_init(&n->lastpos);
    n->nullable = n->left->nullable || n->right->nullable;
    posset_union(&n->firstpos, &n->left->firstpos, &n->right->firstpos);
    posset_union(&n->lastpos, &n->left->lastpos, &n->right->lastpos);
}

static void visit_compute_concat(ASTNode *n, void *data) {
    (void)data;
    ASTNode *c1 = n->left, *c2 = n->right;
    posset_init(&n->firstpos);
    posset_init(&n->lastpos);
    n->nullable = c1->nullable && c2->nullable;
    if (c1->nullable)
        posset_union(&n->firstpos, &c1->firstpos, &c2->firstpos);
    else
        n->firstpos = c1->firstpos;
    if (c2->nullable)
        posset_union(&n->lastpos, &c1->lastpos, &c2->lastpos);
    else
        n->lastpos = c2->lastpos;
}

static void visit_compute_star(ASTNode *n, void *data) {
    (void)data;
    n->nullable = 1;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}

static void visit_compute_plus(ASTNode *n, void *data) {
    (void)data;
    n->nullable = n->left->nullable;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}

static void visit_compute_question(ASTNode *n, void *data) {
    (void)data;
    n->nullable = 1;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}

void ast_compute_functions(ASTNode *root) {
    static const ASTVisitor compute_visitor = {
        .visit_leaf     = visit_compute_leaf,
        .visit_concat   = visit_compute_concat,
        .visit_or       = visit_compute_or,
        .visit_star     = visit_compute_star,
        .visit_plus     = visit_compute_plus,
        .visit_question = visit_compute_question,
    };
    ast_walk_postorder(root, &compute_visitor, NULL);
}

// --- compute_followpos (post-orden, solo concat/star/plus importan) ---

static void visit_followpos_concat(ASTNode *n, void *data) {
    ASTContext *ctx = (ASTContext*)data;
    int limit = ctx->max_position + 1;
    for (int i = 0; i < limit; i++) {
        if (posset_contains(&n->left->lastpos, i))
            posset_union(&ctx->followpos[i], &ctx->followpos[i], &n->right->firstpos);
    }
}

static void visit_followpos_repeat(ASTNode *n, void *data) {
    ASTContext *ctx = (ASTContext*)data;
    int limit = ctx->max_position + 1;
    for (int i = 0; i < limit; i++) {
        if (posset_contains(&n->left->lastpos, i))
            posset_union(&ctx->followpos[i], &ctx->followpos[i], &n->left->firstpos);
    }
}

void ast_compute_followpos(ASTNode *root, ASTContext *ctx) {
    static const ASTVisitor followpos_visitor = {
        .visit_leaf     = NULL,
        .visit_concat   = visit_followpos_concat,
        .visit_or       = NULL,
        .visit_star     = visit_followpos_repeat,
        .visit_plus     = visit_followpos_repeat,
        .visit_question = NULL,
    };
    ast_walk_postorder(root, &followpos_visitor, ctx);
}

// --- build_leaf_index (pre-orden, solo hojas importan) ---

static void visit_leaf_index(ASTNode *n, void *data) {
    ASTContext *ctx = (ASTContext*)data;
    if (n->pos >= 0 && n->pos < MAX_POSITIONS)
        ctx->leaf_at[n->pos] = n;
}

void ast_build_leaf_index(ASTNode *root, ASTContext *ctx) {
    memset(ctx->leaf_at, 0, sizeof(ctx->leaf_at));
    static const ASTVisitor leaf_visitor = {
        .visit_leaf     = visit_leaf_index,
        .visit_concat   = NULL,
        .visit_or       = NULL,
        .visit_star     = NULL,
        .visit_plus     = NULL,
        .visit_question = NULL,
    };
    ast_walk_preorder(root, &leaf_visitor, ctx);
}

// Liberar memoria del AST — noop cuando se usa pool.
// Se mantiene por compatibilidad; usar ast_context_free() para liberar todo.
void ast_free(ASTNode *node)
{
    (void)node;  // Los nodos viven en la arena del ASTContext
}

// --- ast_print (pre-orden) ---

static void visit_print_leaf(ASTNode *n, void *data) {
    int depth = *(int*)data;
    for (int i = 0; i < depth; i++) printf("  ");
    if (n->symbol == '#')
        printf("LEAF(#, pos=%d)\n", n->pos);
    else if (n->symbol >= 32 && n->symbol < 127)
        printf("LEAF('%c', pos=%d)\n", n->symbol, n->pos);
    else
        printf("LEAF(0x%02x, pos=%d)\n", (unsigned char)n->symbol, n->pos);
}

static void visit_print_internal(ASTNode *n, void *data) {
    int *depth = (int*)data;
    for (int i = 0; i < *depth; i++) printf("  ");
    const char *names[] = {"LEAF","CONCAT","OR","STAR","PLUS","QUESTION"};
    printf("%s\n", names[n->type]);
    // Recurse children manually with depth+1
    int child_depth = *depth + 1;
    ast_print(n->left, child_depth);
    ast_print(n->right, child_depth);
}

// ast_print uses manual recursion because depth changes per level
void ast_print(ASTNode* node, int depth) {
    if (!node) return;
    if (node->type == NODE_LEAF) {
        visit_print_leaf(node, &depth);
    } else {
        visit_print_internal(node, &depth);
    }
}
