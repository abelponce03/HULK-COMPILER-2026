/*
 * test_ast.c — Tests unitarios del AST de regex
 *
 * Verifica:
 *  - Creación de nodos (pool allocation)
 *  - PositionSet operations (bitset)
 *  - Cálculo de nullable, firstpos, lastpos
 *  - Visitor pattern (walkers)
 *  - Context init/free sin leaks
 */

#include "test_framework.h"
#include "../generador_analizadores_lexicos/ast.h"
#include <stdlib.h>

// ============== TESTS: POSITIONSET ==============

TEST(posset_empty) {
    PositionSet s;
    posset_init(&s);
    ASSERT(posset_is_empty(&s));
}

TEST(posset_add_contains) {
    PositionSet s;
    posset_init(&s);
    posset_add(&s, 42);
    ASSERT(posset_contains(&s, 42));
    ASSERT(!posset_contains(&s, 0));
    ASSERT(!posset_contains(&s, 41));
    ASSERT(!posset_is_empty(&s));
}

TEST(posset_multiple) {
    PositionSet s;
    posset_init(&s);
    posset_add(&s, 0);
    posset_add(&s, 63);
    posset_add(&s, 64);
    posset_add(&s, 1000);
    ASSERT(posset_contains(&s, 0));
    ASSERT(posset_contains(&s, 63));
    ASSERT(posset_contains(&s, 64));
    ASSERT(posset_contains(&s, 1000));
    ASSERT(!posset_contains(&s, 1));
}

TEST(posset_union) {
    PositionSet a, b, dest;
    posset_init(&a);
    posset_init(&b);
    posset_add(&a, 1);
    posset_add(&a, 3);
    posset_add(&b, 2);
    posset_add(&b, 3);
    posset_union(&dest, &a, &b);
    ASSERT(posset_contains(&dest, 1));
    ASSERT(posset_contains(&dest, 2));
    ASSERT(posset_contains(&dest, 3));
    ASSERT(!posset_contains(&dest, 0));
}

TEST(posset_boundary) {
    PositionSet s;
    posset_init(&s);
    // Out of bounds should not crash
    posset_add(&s, -1);
    posset_add(&s, MAX_POSITIONS + 1);
    ASSERT(!posset_contains(&s, -1));
    ASSERT(!posset_contains(&s, MAX_POSITIONS + 1));
}

// ============== TESTS: AST CONTEXT ==============

TEST(context_init_free) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASSERT_NOT_NULL(ctx.pool_nodes);
    ASSERT_EQ(0, ctx.pool_count);
    ASSERT_EQ(1, ctx.next_position);
    ASSERT_EQ(-1, ctx.pos_to_token[0]);
    ast_context_free(&ctx);
    ASSERT_NULL(ctx.pool_nodes);
}

TEST(context_positions) {
    ASTContext ctx;
    ast_context_init(&ctx);
    int p1 = get_next_position(&ctx);
    int p2 = get_next_position(&ctx);
    int p3 = get_next_position(&ctx);
    ASSERT_EQ(1, p1);
    ASSERT_EQ(2, p2);
    ASSERT_EQ(3, p3);
    ASSERT_EQ(3, ctx.max_position);
    ast_context_free(&ctx);
}

// ============== TESTS: NODE CREATION ==============

TEST(create_leaf) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *leaf = ast_create_leaf(&ctx, 'a', 1);
    ASSERT_NOT_NULL(leaf);
    ASSERT_EQ(NODE_LEAF, leaf->type);
    ASSERT_EQ('a', leaf->symbol);
    ASSERT_EQ(1, leaf->pos);
    ASSERT(posset_contains(&leaf->firstpos, 1));
    ASSERT(posset_contains(&leaf->lastpos, 1));
    ASSERT_EQ(0, leaf->nullable);
    ast_context_free(&ctx);
}

TEST(create_concat) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *b = ast_create_leaf(&ctx, 'b', 2);
    ASTNode *concat = ast_create_concat(&ctx, a, b);
    ASSERT_NOT_NULL(concat);
    ASSERT_EQ(NODE_CONCAT, concat->type);
    ASSERT_EQ(a, concat->left);
    ASSERT_EQ(b, concat->right);
    ast_context_free(&ctx);
}

TEST(create_or) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *b = ast_create_leaf(&ctx, 'b', 2);
    ASTNode *or_node = ast_create_or(&ctx, a, b);
    ASSERT_NOT_NULL(or_node);
    ASSERT_EQ(NODE_OR, or_node->type);
    ast_context_free(&ctx);
}

TEST(create_star) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *star = ast_create_star(&ctx, a);
    ASSERT_NOT_NULL(star);
    ASSERT_EQ(NODE_STAR, star->type);
    ASSERT_EQ(1, star->nullable);  // a* es nullable
    ast_context_free(&ctx);
}

TEST(create_plus) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *plus = ast_create_plus(&ctx, a);
    ASSERT_NOT_NULL(plus);
    ASSERT_EQ(NODE_PLUS, plus->type);
    ASSERT_EQ(0, plus->nullable);  // a+ NO es nullable
    ast_context_free(&ctx);
}

TEST(create_question) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *q = ast_create_question(&ctx, a);
    ASSERT_NOT_NULL(q);
    ASSERT_EQ(NODE_QUESTION, q->type);
    ASSERT_EQ(1, q->nullable);  // a? es nullable
    ast_context_free(&ctx);
}

// ============== TESTS: COMPUTE FUNCTIONS ==============

TEST(compute_concat_functions) {
    ASTContext ctx;
    ast_context_init(&ctx);
    // a.b  →  firstpos={1}, lastpos={2}, nullable=false
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *b = ast_create_leaf(&ctx, 'b', 2);
    ASTNode *concat = ast_create_concat(&ctx, a, b);
    ast_compute_functions(concat);
    ASSERT_EQ(0, concat->nullable);
    ASSERT(posset_contains(&concat->firstpos, 1));
    ASSERT(!posset_contains(&concat->firstpos, 2));
    ASSERT(!posset_contains(&concat->lastpos, 1));
    ASSERT(posset_contains(&concat->lastpos, 2));
    ast_context_free(&ctx);
}

TEST(compute_or_functions) {
    ASTContext ctx;
    ast_context_init(&ctx);
    // a|b  →  firstpos={1,2}, lastpos={1,2}, nullable=false
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *b = ast_create_leaf(&ctx, 'b', 2);
    ASTNode *or_node = ast_create_or(&ctx, a, b);
    ast_compute_functions(or_node);
    ASSERT_EQ(0, or_node->nullable);
    ASSERT(posset_contains(&or_node->firstpos, 1));
    ASSERT(posset_contains(&or_node->firstpos, 2));
    ASSERT(posset_contains(&or_node->lastpos, 1));
    ASSERT(posset_contains(&or_node->lastpos, 2));
    ast_context_free(&ctx);
}

TEST(compute_star_nullable) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *star = ast_create_star(&ctx, a);
    ast_compute_functions(star);
    ASSERT_EQ(1, star->nullable);
    ast_context_free(&ctx);
}

// ============== TESTS: VISITOR ==============

static int visitor_count = 0;
static void count_visitor(ASTNode *node, void *data) {
    (void)node; (void)data;
    visitor_count++;
}

TEST(visitor_postorder) {
    ASTContext ctx;
    ast_context_init(&ctx);
    ASTNode *a = ast_create_leaf(&ctx, 'a', 1);
    ASTNode *b = ast_create_leaf(&ctx, 'b', 2);
    ASTNode *concat = ast_create_concat(&ctx, a, b);

    visitor_count = 0;
    ASTVisitor v = {
        .visit_leaf   = count_visitor,
        .visit_concat = count_visitor,
        .visit_or     = NULL,
        .visit_star   = NULL,
        .visit_plus   = NULL,
        .visit_question = NULL,
    };
    ast_walk_postorder(concat, &v, NULL);
    ASSERT_EQ(3, visitor_count);  // a, b, concat
    ast_context_free(&ctx);
}

TEST(pool_grows) {
    ASTContext ctx;
    ast_context_init(&ctx);
    // Crear muchos nodos — el pool debe crecer automáticamente
    for (int i = 0; i < 5000; i++) {
        ASTNode *n = ast_create_leaf(&ctx, 'x', i);
        ASSERT_NOT_NULL(n);
    }
    ASSERT_EQ(5000, ctx.pool_count);
    ASSERT_GT(ctx.pool_capacity, 4096);
    ast_context_free(&ctx);
}

// ============== MAIN ==============

int main(void) {
    printf("\n🧪 HULK Compiler — AST Unit Tests\n");

    TEST_SUITE("PositionSet (bitset)");
    RUN_TEST(posset_empty);
    RUN_TEST(posset_add_contains);
    RUN_TEST(posset_multiple);
    RUN_TEST(posset_union);
    RUN_TEST(posset_boundary);

    TEST_SUITE("ASTContext");
    RUN_TEST(context_init_free);
    RUN_TEST(context_positions);

    TEST_SUITE("Node Creation");
    RUN_TEST(create_leaf);
    RUN_TEST(create_concat);
    RUN_TEST(create_or);
    RUN_TEST(create_star);
    RUN_TEST(create_plus);
    RUN_TEST(create_question);

    TEST_SUITE("Compute Functions (nullable, firstpos, lastpos)");
    RUN_TEST(compute_concat_functions);
    RUN_TEST(compute_or_functions);
    RUN_TEST(compute_star_nullable);

    TEST_SUITE("Visitor Pattern");
    RUN_TEST(visitor_postorder);

    TEST_SUITE("Object Pool");
    RUN_TEST(pool_grows);

    TEST_REPORT();
    return TEST_EXIT_CODE();
}
