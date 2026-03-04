/*
 * test_hulk_ast.c — Tests unitarios para el AST de HULK
 *
 * Cubre:
 *   - Arena (HulkASTContext): asignación y liberación
 *   - Creación de cada tipo de nodo
 *   - HulkNodeList: init, push, free
 *   - Visitor dispatch y traversal
 *   - AST printer (verificación de salida)
 *   - Nombres de debugging
 */

#include "test_framework.h"
#include "../hulk_ast/hulk_ast.h"
#include "../hulk_ast/hulk_ast_printer.h"

// ============================================
//  Suite 1: Arena / Object Pool
// ============================================

TEST(arena_init_and_free) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    ASSERT_EQ(0, ctx.block_count);
    ASSERT_EQ(0, ctx.block_capacity);
    hulk_ast_context_free(&ctx);
    ASSERT_EQ(0, ctx.block_count);
}

TEST(arena_alloc_registers_blocks) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    void *p1 = hulk_ast_alloc(&ctx, 64);
    void *p2 = hulk_ast_alloc(&ctx, 128);
    ASSERT_NOT_NULL(p1);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ(2, ctx.block_count);
    hulk_ast_context_free(&ctx);
}

TEST(arena_strdup_copies_string) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    char *s = hulk_ast_strdup(&ctx, "hello");
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ("hello", s);
    ASSERT_EQ(1, ctx.block_count);
    hulk_ast_context_free(&ctx);
}

TEST(arena_strdup_null_returns_null) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    char *s = hulk_ast_strdup(&ctx, NULL);
    ASSERT_NULL(s);
    ASSERT_EQ(0, ctx.block_count);
    hulk_ast_context_free(&ctx);
}

TEST(arena_many_allocs) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    for (int i = 0; i < 500; i++) {
        void *p = hulk_ast_alloc(&ctx, 32);
        ASSERT_NOT_NULL(p);
    }
    ASSERT_EQ(500, ctx.block_count);
    hulk_ast_context_free(&ctx);
    ASSERT_EQ(0, ctx.block_count);
}

// ============================================
//  Suite 2: HulkNodeList
// ============================================

TEST(node_list_init_empty) {
    HulkNodeList list;
    hulk_node_list_init(&list);
    ASSERT_EQ(0, list.count);
    ASSERT_EQ(0, list.capacity);
    ASSERT_NULL(list.items);
    hulk_node_list_free(&list);
}

TEST(node_list_push_and_count) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNodeList list;
    hulk_node_list_init(&list);

    HulkNode *n1 = (HulkNode*)hulk_ast_number_lit(&ctx, "1", 1, 1);
    HulkNode *n2 = (HulkNode*)hulk_ast_number_lit(&ctx, "2", 1, 2);
    hulk_node_list_push(&list, n1);
    hulk_node_list_push(&list, n2);

    ASSERT_EQ(2, list.count);
    ASSERT(list.items[0] == n1);
    ASSERT(list.items[1] == n2);

    hulk_node_list_free(&list);
    hulk_ast_context_free(&ctx);
}

TEST(node_list_grows_capacity) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNodeList list;
    hulk_node_list_init(&list);

    for (int i = 0; i < 20; i++) {
        HulkNode *n = (HulkNode*)hulk_ast_ident(&ctx, "x", 1, i);
        hulk_node_list_push(&list, n);
    }
    ASSERT_EQ(20, list.count);
    ASSERT(list.capacity >= 20);

    hulk_node_list_free(&list);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 3: Creación de nodos
// ============================================

TEST(create_program_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    ProgramNode *p = hulk_ast_program(&ctx, 1, 1);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(NODE_PROGRAM, p->base.type);
    ASSERT_EQ(1, p->base.line);
    ASSERT_EQ(0, p->declarations.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_function_def_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    FunctionDefNode *f = hulk_ast_function_def(&ctx, "factorial", "Number", 5, 1);
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(NODE_FUNCTION_DEF, f->base.type);
    ASSERT_STR_EQ("factorial", f->name);
    ASSERT_STR_EQ("Number", f->return_type);
    ASSERT_EQ(5, f->base.line);
    ASSERT_NULL(f->body);
    ASSERT_EQ(0, f->params.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_function_def_no_return_type) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    FunctionDefNode *f = hulk_ast_function_def(&ctx, "greet", NULL, 1, 1);
    ASSERT_NOT_NULL(f);
    ASSERT_NULL(f->return_type);
    hulk_ast_context_free(&ctx);
}

TEST(create_type_def_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    TypeDefNode *t = hulk_ast_type_def(&ctx, "Dog", "Animal", 10, 1);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(NODE_TYPE_DEF, t->base.type);
    ASSERT_STR_EQ("Dog", t->name);
    ASSERT_STR_EQ("Animal", t->parent);
    ASSERT_EQ(0, t->members.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_type_def_no_parent) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    TypeDefNode *t = hulk_ast_type_def(&ctx, "Point", NULL, 1, 1);
    ASSERT_NOT_NULL(t);
    ASSERT_NULL(t->parent);
    hulk_ast_context_free(&ctx);
}

TEST(create_method_def_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    MethodDefNode *m = hulk_ast_method_def(&ctx, "speak", "String", 15, 5);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(NODE_METHOD_DEF, m->base.type);
    ASSERT_STR_EQ("speak", m->name);
    ASSERT_STR_EQ("String", m->return_type);
    hulk_ast_context_free(&ctx);
}

TEST(create_attribute_def_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    AttributeDefNode *a = hulk_ast_attribute_def(&ctx, "x", "Number", 3, 5);
    ASSERT_NOT_NULL(a);
    ASSERT_EQ(NODE_ATTRIBUTE_DEF, a->base.type);
    ASSERT_STR_EQ("x", a->name);
    ASSERT_STR_EQ("Number", a->type_annotation);
    ASSERT_NULL(a->init_expr);
    hulk_ast_context_free(&ctx);
}

TEST(create_let_expr_with_bindings) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    LetExprNode *l = hulk_ast_let_expr(&ctx, 1, 1);
    VarBindingNode *vb = hulk_ast_var_binding(&ctx, "x", "Number", 1, 5);
    vb->init_expr = (HulkNode*)hulk_ast_number_lit(&ctx, "42", 1, 10);
    hulk_node_list_push(&l->bindings, (HulkNode*)vb);
    l->body = (HulkNode*)hulk_ast_ident(&ctx, "x", 1, 20);

    ASSERT_EQ(NODE_LET_EXPR, l->base.type);
    ASSERT_EQ(1, l->bindings.count);
    ASSERT_NOT_NULL(l->body);
    VarBindingNode *check = (VarBindingNode*)l->bindings.items[0];
    ASSERT_STR_EQ("x", check->name);
    hulk_ast_context_free(&ctx);
}

TEST(create_if_expr_with_elifs) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    IfExprNode *ie = hulk_ast_if_expr(&ctx, 1, 1);
    ie->condition = (HulkNode*)hulk_ast_bool_lit(&ctx, 1, 1, 4);
    ie->then_body = (HulkNode*)hulk_ast_number_lit(&ctx, "1", 1, 10);
    ie->else_body = (HulkNode*)hulk_ast_number_lit(&ctx, "3", 3, 10);

    ElifBranchNode *elif = hulk_ast_elif_branch(&ctx, 2, 1);
    elif->condition = (HulkNode*)hulk_ast_bool_lit(&ctx, 0, 2, 6);
    elif->body = (HulkNode*)hulk_ast_number_lit(&ctx, "2", 2, 10);
    hulk_node_list_push(&ie->elifs, (HulkNode*)elif);

    ASSERT_EQ(NODE_IF_EXPR, ie->base.type);
    ASSERT_EQ(1, ie->elifs.count);
    ASSERT_NOT_NULL(ie->condition);
    ASSERT_NOT_NULL(ie->else_body);
    hulk_ast_context_free(&ctx);
}

TEST(create_while_stmt_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    WhileStmtNode *w = hulk_ast_while_stmt(&ctx, 1, 1);
    w->condition = (HulkNode*)hulk_ast_bool_lit(&ctx, 1, 1, 7);
    w->body = (HulkNode*)hulk_ast_number_lit(&ctx, "0", 1, 15);
    ASSERT_EQ(NODE_WHILE_STMT, w->base.type);
    ASSERT_NOT_NULL(w->condition);
    ASSERT_NOT_NULL(w->body);
    hulk_ast_context_free(&ctx);
}

TEST(create_for_stmt_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    ForStmtNode *f = hulk_ast_for_stmt(&ctx, "item", 1, 1);
    ASSERT_EQ(NODE_FOR_STMT, f->base.type);
    ASSERT_STR_EQ("item", f->var_name);
    ASSERT_NULL(f->iterable);
    ASSERT_NULL(f->body);
    hulk_ast_context_free(&ctx);
}

TEST(create_block_stmt_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    BlockStmtNode *b = hulk_ast_block_stmt(&ctx, 1, 1);
    hulk_node_list_push(&b->statements, (HulkNode*)hulk_ast_number_lit(&ctx, "1", 1, 3));
    hulk_node_list_push(&b->statements, (HulkNode*)hulk_ast_number_lit(&ctx, "2", 2, 3));
    ASSERT_EQ(NODE_BLOCK_STMT, b->base.type);
    ASSERT_EQ(2, b->statements.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_binary_op_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *left = (HulkNode*)hulk_ast_number_lit(&ctx, "3", 1, 1);
    HulkNode *right = (HulkNode*)hulk_ast_number_lit(&ctx, "4", 1, 5);
    BinaryOpNode *bin = hulk_ast_binary_op(&ctx, OP_ADD, left, right, 1, 3);
    ASSERT_EQ(NODE_BINARY_OP, bin->base.type);
    ASSERT_EQ(OP_ADD, bin->op);
    ASSERT(bin->left == left);
    ASSERT(bin->right == right);
    hulk_ast_context_free(&ctx);
}

TEST(create_unary_op_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *operand = (HulkNode*)hulk_ast_number_lit(&ctx, "5", 1, 2);
    UnaryOpNode *u = hulk_ast_unary_op(&ctx, operand, 1, 1);
    ASSERT_EQ(NODE_UNARY_OP, u->base.type);
    ASSERT(u->operand == operand);
    hulk_ast_context_free(&ctx);
}

TEST(create_number_lit_value) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    NumberLitNode *n = hulk_ast_number_lit(&ctx, "3.14", 1, 1);
    ASSERT_EQ(NODE_NUMBER_LIT, n->base.type);
    ASSERT_STR_EQ("3.14", n->raw);
    ASSERT(n->value > 3.13 && n->value < 3.15);
    hulk_ast_context_free(&ctx);
}

TEST(create_string_lit_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    StringLitNode *s = hulk_ast_string_lit(&ctx, "hello world", 1, 1);
    ASSERT_EQ(NODE_STRING_LIT, s->base.type);
    ASSERT_STR_EQ("hello world", s->value);
    hulk_ast_context_free(&ctx);
}

TEST(create_bool_lit_true) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    BoolLitNode *b = hulk_ast_bool_lit(&ctx, 1, 1, 1);
    ASSERT_EQ(NODE_BOOL_LIT, b->base.type);
    ASSERT_EQ(1, b->value);
    hulk_ast_context_free(&ctx);
}

TEST(create_bool_lit_false) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    BoolLitNode *b = hulk_ast_bool_lit(&ctx, 0, 1, 1);
    ASSERT_EQ(0, b->value);
    hulk_ast_context_free(&ctx);
}

TEST(create_ident_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    IdentNode *id = hulk_ast_ident(&ctx, "myVar", 5, 10);
    ASSERT_EQ(NODE_IDENT, id->base.type);
    ASSERT_STR_EQ("myVar", id->name);
    ASSERT_EQ(5, id->base.line);
    ASSERT_EQ(10, id->base.col);
    hulk_ast_context_free(&ctx);
}

TEST(create_call_expr_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *callee = (HulkNode*)hulk_ast_ident(&ctx, "print", 1, 1);
    CallExprNode *c = hulk_ast_call_expr(&ctx, callee, 1, 1);
    hulk_node_list_push(&c->args, (HulkNode*)hulk_ast_string_lit(&ctx, "hi", 1, 7));
    ASSERT_EQ(NODE_CALL_EXPR, c->base.type);
    ASSERT(c->callee == callee);
    ASSERT_EQ(1, c->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_member_access_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *obj = (HulkNode*)hulk_ast_ident(&ctx, "point", 1, 1);
    MemberAccessNode *m = hulk_ast_member_access(&ctx, obj, "x", 1, 6);
    ASSERT_EQ(NODE_MEMBER_ACCESS, m->base.type);
    ASSERT_STR_EQ("x", m->member);
    ASSERT(m->object == obj);
    hulk_ast_context_free(&ctx);
}

TEST(create_new_expr_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    NewExprNode *ne = hulk_ast_new_expr(&ctx, "Point", 1, 1);
    hulk_node_list_push(&ne->args, (HulkNode*)hulk_ast_number_lit(&ctx, "1", 1, 11));
    hulk_node_list_push(&ne->args, (HulkNode*)hulk_ast_number_lit(&ctx, "2", 1, 14));
    ASSERT_EQ(NODE_NEW_EXPR, ne->base.type);
    ASSERT_STR_EQ("Point", ne->type_name);
    ASSERT_EQ(2, ne->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_assign_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *target = (HulkNode*)hulk_ast_ident(&ctx, "x", 1, 1);
    HulkNode *value = (HulkNode*)hulk_ast_number_lit(&ctx, "10", 1, 5);
    AssignNode *a = hulk_ast_assign(&ctx, target, value, 1, 3);
    ASSERT_EQ(NODE_ASSIGN, a->base.type);
    ASSERT(a->target == target);
    ASSERT(a->value == value);
    hulk_ast_context_free(&ctx);
}

TEST(create_destruct_assign_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *target = (HulkNode*)hulk_ast_ident(&ctx, "y", 1, 1);
    HulkNode *value = (HulkNode*)hulk_ast_number_lit(&ctx, "20", 1, 6);
    DestructAssignNode *da = hulk_ast_destruct_assign(&ctx, target, value, 1, 3);
    ASSERT_EQ(NODE_DESTRUCT_ASSIGN, da->base.type);
    hulk_ast_context_free(&ctx);
}

TEST(create_as_expr_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *expr = (HulkNode*)hulk_ast_ident(&ctx, "obj", 1, 1);
    AsExprNode *a = hulk_ast_as_expr(&ctx, expr, "Dog", 1, 5);
    ASSERT_EQ(NODE_AS_EXPR, a->base.type);
    ASSERT_STR_EQ("Dog", a->type_name);
    hulk_ast_context_free(&ctx);
}

TEST(create_is_expr_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *expr = (HulkNode*)hulk_ast_ident(&ctx, "animal", 1, 1);
    IsExprNode *is = hulk_ast_is_expr(&ctx, expr, "Cat", 1, 8);
    ASSERT_EQ(NODE_IS_EXPR, is->base.type);
    ASSERT_STR_EQ("Cat", is->type_name);
    hulk_ast_context_free(&ctx);
}

TEST(create_self_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    SelfNode *s = hulk_ast_self(&ctx, 5, 10);
    ASSERT_EQ(NODE_SELF, s->base.type);
    ASSERT_EQ(5, s->base.line);
    hulk_ast_context_free(&ctx);
}

TEST(create_base_call_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    BaseCallNode *b = hulk_ast_base_call(&ctx, 3, 5);
    hulk_node_list_push(&b->args, (HulkNode*)hulk_ast_number_lit(&ctx, "1", 3, 10));
    ASSERT_EQ(NODE_BASE_CALL, b->base.type);
    ASSERT_EQ(1, b->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_decor_block_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    DecorBlockNode *db = hulk_ast_decor_block(&ctx, 1, 1);
    DecorItemNode *di = hulk_ast_decor_item(&ctx, "log", 1, 7);
    hulk_node_list_push(&db->decorators, (HulkNode*)di);
    db->target = (HulkNode*)hulk_ast_function_def(&ctx, "foo", NULL, 2, 1);

    ASSERT_EQ(NODE_DECOR_BLOCK, db->base.type);
    ASSERT_EQ(1, db->decorators.count);
    ASSERT_NOT_NULL(db->target);
    hulk_ast_context_free(&ctx);
}

TEST(create_decor_item_with_args) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    DecorItemNode *di = hulk_ast_decor_item(&ctx, "memoize", 1, 1);
    hulk_node_list_push(&di->args, (HulkNode*)hulk_ast_number_lit(&ctx, "100", 1, 9));
    ASSERT_EQ(NODE_DECOR_ITEM, di->base.type);
    ASSERT_STR_EQ("memoize", di->name);
    ASSERT_EQ(1, di->args.count);
    hulk_ast_context_free(&ctx);
}

TEST(create_concat_expr_node) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *left = (HulkNode*)hulk_ast_string_lit(&ctx, "hello", 1, 1);
    HulkNode *right = (HulkNode*)hulk_ast_string_lit(&ctx, "world", 1, 10);
    ConcatExprNode *c = hulk_ast_concat_expr(&ctx, OP_CONCAT, left, right, 1, 7);
    ASSERT_EQ(NODE_CONCAT_EXPR, c->base.type);
    ASSERT_EQ(OP_CONCAT, c->op);
    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 4: Visitor dispatch
// ============================================

static int visitor_call_count = 0;

static void counting_visitor(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)n; (void)v; (void)data;
    visitor_call_count++;
}

TEST(visitor_dispatches_correct_type) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkASTVisitor v;
    hulk_visitor_init(&v);
    visitor_call_count = 0;
    v.visit[NODE_NUMBER_LIT] = counting_visitor;

    HulkNode *n = (HulkNode*)hulk_ast_number_lit(&ctx, "7", 1, 1);
    hulk_ast_accept(n, &v, NULL);
    ASSERT_EQ(1, visitor_call_count);

    // Ident no tiene callback, no debe incrementar
    HulkNode *id = (HulkNode*)hulk_ast_ident(&ctx, "x", 1, 1);
    hulk_ast_accept(id, &v, NULL);
    ASSERT_EQ(1, visitor_call_count);

    hulk_ast_context_free(&ctx);
}

TEST(visitor_accept_null_safe) {
    HulkASTVisitor v;
    hulk_visitor_init(&v);
    // No debe crashear
    hulk_ast_accept(NULL, &v, NULL);
    hulk_ast_accept(NULL, NULL, NULL);
    hulk_ast_accept_list(NULL, &v, NULL);
}

TEST(visitor_accept_list_visits_all) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkASTVisitor v;
    hulk_visitor_init(&v);
    visitor_call_count = 0;
    v.visit[NODE_IDENT] = counting_visitor;

    HulkNodeList list;
    hulk_node_list_init(&list);
    for (int i = 0; i < 5; i++)
        hulk_node_list_push(&list, (HulkNode*)hulk_ast_ident(&ctx, "a", 1, i));

    hulk_ast_accept_list(&list, &v, NULL);
    ASSERT_EQ(5, visitor_call_count);

    hulk_node_list_free(&list);
    hulk_ast_context_free(&ctx);
}

// Visitor that receives data pointer
static void data_visitor(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)n; (void)v;
    int *counter = (int*)data;
    (*counter)++;
}

TEST(visitor_passes_data) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkASTVisitor v;
    hulk_visitor_init(&v);
    v.visit[NODE_STRING_LIT] = data_visitor;

    int my_counter = 0;
    HulkNode *s = (HulkNode*)hulk_ast_string_lit(&ctx, "test", 1, 1);
    hulk_ast_accept(s, &v, &my_counter);
    ASSERT_EQ(1, my_counter);

    hulk_ast_context_free(&ctx);
}

// ============================================
//  Suite 5: AST Printer (smoke test)
// ============================================

TEST(printer_outputs_program) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    ProgramNode *prog = hulk_ast_program(&ctx, 1, 1);
    FunctionDefNode *fn = hulk_ast_function_def(&ctx, "greet", "String", 2, 1);
    fn->body = (HulkNode*)hulk_ast_string_lit(&ctx, "hello", 2, 25);
    hulk_node_list_push(&prog->declarations, (HulkNode*)fn);

    // Print to a temporary buffer via tmpfile
    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    hulk_ast_print((HulkNode*)prog, tmp);

    // Read back
    fseek(tmp, 0, SEEK_END);
    long len = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, tmp);
    buf[len] = '\0';
    fclose(tmp);

    // Should contain key strings
    ASSERT(strstr(buf, "Program") != NULL);
    ASSERT(strstr(buf, "FunctionDef 'greet'") != NULL);
    ASSERT(strstr(buf, "StringLit") != NULL);

    free(buf);
    hulk_ast_context_free(&ctx);
}

TEST(printer_handles_binary_op) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    HulkNode *left = (HulkNode*)hulk_ast_number_lit(&ctx, "3", 1, 1);
    HulkNode *right = (HulkNode*)hulk_ast_number_lit(&ctx, "4", 1, 5);
    BinaryOpNode *add = hulk_ast_binary_op(&ctx, OP_ADD, left, right, 1, 3);

    FILE *tmp = tmpfile();
    ASSERT_NOT_NULL(tmp);
    hulk_ast_print((HulkNode*)add, tmp);

    fseek(tmp, 0, SEEK_END);
    long len = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    fread(buf, 1, len, tmp);
    buf[len] = '\0';
    fclose(tmp);

    ASSERT(strstr(buf, "BinaryOp '+'") != NULL);
    ASSERT(strstr(buf, "NumberLit 3") != NULL);
    ASSERT(strstr(buf, "NumberLit 4") != NULL);

    free(buf);
    hulk_ast_context_free(&ctx);
}

TEST(printer_handles_null) {
    // Should not crash
    hulk_ast_print(NULL, stdout);
    hulk_ast_print(NULL, NULL);
}

// ============================================
//  Suite 6: Debugging names
// ============================================

TEST(node_type_names_correct) {
    ASSERT_STR_EQ("Program", hulk_node_type_name(NODE_PROGRAM));
    ASSERT_STR_EQ("FunctionDef", hulk_node_type_name(NODE_FUNCTION_DEF));
    ASSERT_STR_EQ("TypeDef", hulk_node_type_name(NODE_TYPE_DEF));
    ASSERT_STR_EQ("NumberLit", hulk_node_type_name(NODE_NUMBER_LIT));
    ASSERT_STR_EQ("DecorBlock", hulk_node_type_name(NODE_DECOR_BLOCK));
    ASSERT_STR_EQ("ConcatExpr", hulk_node_type_name(NODE_CONCAT_EXPR));
}

TEST(node_type_name_unknown) {
    ASSERT_STR_EQ("Unknown", hulk_node_type_name(NODE_HULK_COUNT));
    ASSERT_STR_EQ("Unknown", hulk_node_type_name((HulkNodeType)99));
}

TEST(binary_op_names_correct) {
    ASSERT_STR_EQ("+", hulk_binary_op_name(OP_ADD));
    ASSERT_STR_EQ("-", hulk_binary_op_name(OP_SUB));
    ASSERT_STR_EQ("*", hulk_binary_op_name(OP_MUL));
    ASSERT_STR_EQ("/", hulk_binary_op_name(OP_DIV));
    ASSERT_STR_EQ("**", hulk_binary_op_name(OP_POW));
    ASSERT_STR_EQ("==", hulk_binary_op_name(OP_EQ));
    ASSERT_STR_EQ("@", hulk_binary_op_name(OP_CONCAT));
    ASSERT_STR_EQ("@@", hulk_binary_op_name(OP_CONCAT_WS));
}

// ============================================
//  Suite 7: Composite AST tree
// ============================================

TEST(composite_tree_let_with_binary) {
    // let x = 3 + 4 in x
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    LetExprNode *let = hulk_ast_let_expr(&ctx, 1, 1);
    VarBindingNode *vb = hulk_ast_var_binding(&ctx, "x", NULL, 1, 5);
    HulkNode *three = (HulkNode*)hulk_ast_number_lit(&ctx, "3", 1, 9);
    HulkNode *four = (HulkNode*)hulk_ast_number_lit(&ctx, "4", 1, 13);
    vb->init_expr = (HulkNode*)hulk_ast_binary_op(&ctx, OP_ADD, three, four, 1, 11);
    hulk_node_list_push(&let->bindings, (HulkNode*)vb);
    let->body = (HulkNode*)hulk_ast_ident(&ctx, "x", 1, 18);

    // Verify structure
    ASSERT_EQ(1, let->bindings.count);
    VarBindingNode *check = (VarBindingNode*)let->bindings.items[0];
    ASSERT_EQ(NODE_BINARY_OP, check->init_expr->type);
    BinaryOpNode *bin = (BinaryOpNode*)check->init_expr;
    ASSERT_EQ(OP_ADD, bin->op);
    ASSERT_EQ(NODE_IDENT, let->body->type);

    hulk_ast_context_free(&ctx);
}

TEST(composite_tree_type_with_methods) {
    // type Point { x = 0; y = 0; dist() => ... }
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    TypeDefNode *t = hulk_ast_type_def(&ctx, "Point", NULL, 1, 1);

    AttributeDefNode *ax = hulk_ast_attribute_def(&ctx, "x", "Number", 2, 3);
    ax->init_expr = (HulkNode*)hulk_ast_number_lit(&ctx, "0", 2, 18);
    hulk_node_list_push(&t->members, (HulkNode*)ax);

    AttributeDefNode *ay = hulk_ast_attribute_def(&ctx, "y", "Number", 3, 3);
    ay->init_expr = (HulkNode*)hulk_ast_number_lit(&ctx, "0", 3, 18);
    hulk_node_list_push(&t->members, (HulkNode*)ay);

    MethodDefNode *dist = hulk_ast_method_def(&ctx, "dist", "Number", 4, 3);
    dist->body = (HulkNode*)hulk_ast_number_lit(&ctx, "0", 4, 25);
    hulk_node_list_push(&t->members, (HulkNode*)dist);

    ASSERT_EQ(3, t->members.count);
    ASSERT_EQ(NODE_ATTRIBUTE_DEF, t->members.items[0]->type);
    ASSERT_EQ(NODE_ATTRIBUTE_DEF, t->members.items[1]->type);
    ASSERT_EQ(NODE_METHOD_DEF, t->members.items[2]->type);

    hulk_ast_context_free(&ctx);
}

// ============================================
//  main
// ============================================

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║    HULK AST — Tests Unitarios         ║\n");
    printf("╚═══════════════════════════════════════╝\n");

    TEST_SUITE("Arena / Object Pool");
    RUN_TEST(arena_init_and_free);
    RUN_TEST(arena_alloc_registers_blocks);
    RUN_TEST(arena_strdup_copies_string);
    RUN_TEST(arena_strdup_null_returns_null);
    RUN_TEST(arena_many_allocs);

    TEST_SUITE("HulkNodeList");
    RUN_TEST(node_list_init_empty);
    RUN_TEST(node_list_push_and_count);
    RUN_TEST(node_list_grows_capacity);

    TEST_SUITE("Creación de nodos");
    RUN_TEST(create_program_node);
    RUN_TEST(create_function_def_node);
    RUN_TEST(create_function_def_no_return_type);
    RUN_TEST(create_type_def_node);
    RUN_TEST(create_type_def_no_parent);
    RUN_TEST(create_method_def_node);
    RUN_TEST(create_attribute_def_node);
    RUN_TEST(create_let_expr_with_bindings);
    RUN_TEST(create_if_expr_with_elifs);
    RUN_TEST(create_while_stmt_node);
    RUN_TEST(create_for_stmt_node);
    RUN_TEST(create_block_stmt_node);
    RUN_TEST(create_binary_op_node);
    RUN_TEST(create_unary_op_node);
    RUN_TEST(create_number_lit_value);
    RUN_TEST(create_string_lit_node);
    RUN_TEST(create_bool_lit_true);
    RUN_TEST(create_bool_lit_false);
    RUN_TEST(create_ident_node);
    RUN_TEST(create_call_expr_node);
    RUN_TEST(create_member_access_node);
    RUN_TEST(create_new_expr_node);
    RUN_TEST(create_assign_node);
    RUN_TEST(create_destruct_assign_node);
    RUN_TEST(create_as_expr_node);
    RUN_TEST(create_is_expr_node);
    RUN_TEST(create_self_node);
    RUN_TEST(create_base_call_node);
    RUN_TEST(create_decor_block_node);
    RUN_TEST(create_decor_item_with_args);
    RUN_TEST(create_concat_expr_node);

    TEST_SUITE("Visitor dispatch");
    RUN_TEST(visitor_dispatches_correct_type);
    RUN_TEST(visitor_accept_null_safe);
    RUN_TEST(visitor_accept_list_visits_all);
    RUN_TEST(visitor_passes_data);

    TEST_SUITE("AST Printer");
    RUN_TEST(printer_outputs_program);
    RUN_TEST(printer_handles_binary_op);
    RUN_TEST(printer_handles_null);

    TEST_SUITE("Debugging names");
    RUN_TEST(node_type_names_correct);
    RUN_TEST(node_type_name_unknown);
    RUN_TEST(binary_op_names_correct);

    TEST_SUITE("Composite AST Trees");
    RUN_TEST(composite_tree_let_with_binary);
    RUN_TEST(composite_tree_type_with_methods);

    TEST_REPORT();
    return TEST_EXIT_CODE();
}
