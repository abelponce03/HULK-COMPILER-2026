/*
 * parse_expressions.c — Parsing de expresiones del AST builder
 *
 * Contiene la cadena de precedencia de operadores y las expresiones
 * compuestas (let, if/elif/else).
 *
 * Cadena de precedencia (de menor a mayor):
 *   or → and → cmp/is → concat → add → term → factor → unary → primary
 *
 * Asociatividad:
 *   - Operadores aritméticos/lógicos/concat: izquierda (loop).
 *   - Potencia (**): derecha (recursión).
 *   - as: izquierda (loop).
 *
 * SRP: Solo parsing de expresiones y su precedencia.
 */

#include "hulk_ast_builder_internal.h"

// ============================================================
//  OrExpr / AndExpr  (left-assoc loops)
// ============================================================

HulkNode* parse_or_expr(ASTBuilder *b) {
    HulkNode *left = parse_and_expr(b);
    while (check(b, TOKEN_OR)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *right = parse_and_expr(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, OP_OR, left, right, line, col);
    }
    return left;
}

HulkNode* parse_and_expr(ASTBuilder *b) {
    HulkNode *left = parse_cmp_expr(b);
    while (check(b, TOKEN_AND)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *right = parse_cmp_expr(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, OP_AND, left, right, line, col);
    }
    return left;
}

// ============================================================
//  CmpExpr  (comparaciones + IS)
// ============================================================

HulkNode* parse_cmp_expr(ASTBuilder *b) {
    HulkNode *left = parse_concat_expr(b);
    while (1) {
        int line = cur_line(b), col = cur_col(b);
        BinaryOp op;
        if      (check(b, TOKEN_LT))  op = OP_LT;
        else if (check(b, TOKEN_GT))  op = OP_GT;
        else if (check(b, TOKEN_LE))  op = OP_LE;
        else if (check(b, TOKEN_GE))  op = OP_GE;
        else if (check(b, TOKEN_EQ))  op = OP_EQ;
        else if (check(b, TOKEN_NEQ)) op = OP_NEQ;
        else if (check(b, TOKEN_IS)) {
            advance(b);
            char *type_name = expect_ident(b);
            left = (HulkNode*)hulk_ast_is_expr(b->ctx, left, type_name, line, col);
            continue;
        }
        else break;

        advance(b);
        HulkNode *right = parse_concat_expr(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, op, left, right, line, col);
    }
    return left;
}

// ============================================================
//  ConcatExpr  (@ y @@)
// ============================================================

HulkNode* parse_concat_expr(ASTBuilder *b) {
    HulkNode *left = parse_add_expr(b);
    while (check(b, TOKEN_CONCAT) || check(b, TOKEN_CONCAT_WS)) {
        int line = cur_line(b), col = cur_col(b);
        BinaryOp op = check(b, TOKEN_CONCAT) ? OP_CONCAT : OP_CONCAT_WS;
        advance(b);
        HulkNode *right = parse_add_expr(b);
        left = (HulkNode*)hulk_ast_concat_expr(b->ctx, op, left, right, line, col);
    }
    return left;
}

// ============================================================
//  AddExpr  (+ -)
// ============================================================

HulkNode* parse_add_expr(ASTBuilder *b) {
    HulkNode *left = parse_term(b);
    while (check(b, TOKEN_PLUS) || check(b, TOKEN_MINUS)) {
        int line = cur_line(b), col = cur_col(b);
        BinaryOp op = check(b, TOKEN_PLUS) ? OP_ADD : OP_SUB;
        advance(b);
        HulkNode *right = parse_term(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, op, left, right, line, col);
    }
    return left;
}

// ============================================================
//  Term  (* / %)
// ============================================================

HulkNode* parse_term(ASTBuilder *b) {
    HulkNode *left = parse_factor(b);
    while (check(b, TOKEN_MULT) || check(b, TOKEN_DIV) || check(b, TOKEN_MOD)) {
        int line = cur_line(b), col = cur_col(b);
        BinaryOp op;
        if      (check(b, TOKEN_MULT)) op = OP_MUL;
        else if (check(b, TOKEN_DIV))  op = OP_DIV;
        else                           op = OP_MOD;
        advance(b);
        HulkNode *right = parse_factor(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, op, left, right, line, col);
    }
    return left;
}

// ============================================================
//  Factor  (**)  — DERECHA-asociativo
// ============================================================

HulkNode* parse_factor(ASTBuilder *b) {
    HulkNode *base = parse_unary(b);
    if (check(b, TOKEN_POW)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *exp = parse_factor(b);  // recursión = right-assoc
        return (HulkNode*)hulk_ast_binary_op(b->ctx, OP_POW, base, exp, line, col);
    }
    return base;
}

// ============================================================
//  Unary
//  Unary → MINUS Unary | Primary AsExpr
// ============================================================

HulkNode* parse_unary(ASTBuilder *b) {
    if (check(b, TOKEN_MINUS)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *operand = parse_unary(b);
        return (HulkNode*)hulk_ast_unary_op(b->ctx, operand, line, col);
    }
    HulkNode *node = parse_primary(b);
    return parse_as_chain(b, node);
}

// ============================================================
//  AsExpr → AS IDENT AsExpr | ε
// ============================================================

HulkNode* parse_as_chain(ASTBuilder *b, HulkNode *left) {
    while (check(b, TOKEN_AS)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        char *type_name = expect_ident(b);
        left = (HulkNode*)hulk_ast_as_expr(b->ctx, left, type_name, line, col);
    }
    return left;
}

// ============================================================
//  LetExpr
//  LET VarBindingList IN LetBody
// ============================================================

HulkNode* parse_let_expr(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_LET);

    LetExprNode *let = hulk_ast_let_expr(b->ctx, line, col);

    // VarBindingList → VarBinding (COMMA VarBinding)*
    // VarBinding → IDENT TypeAnnotation ASSIGN Expr
    do {
        int vl = cur_line(b), vc = cur_col(b);
        char *vname = expect_ident(b);
        if (!vname) break;
        char *vtype = parse_type_annotation(b);
        expect(b, TOKEN_ASSIGN);
        HulkNode *init = parse_expr(b);

        VarBindingNode *vb = hulk_ast_var_binding(b->ctx, vname, vtype, vl, vc);
        vb->init_expr = init;
        hulk_node_list_push(&let->bindings, (HulkNode*)vb);
    } while (match(b, TOKEN_COMMA));

    expect(b, TOKEN_IN);

    // LetBody → Stmt (incluye Expr, BlockStmt, WhileStmt, ForStmt)
    let->body = parse_stmt(b);

    return (HulkNode*)let;
}

// ============================================================
//  IfExpr
//  IF LPAREN Expr RPAREN IfBody ElifList ELSE IfBody
// ============================================================

static HulkNode* parse_if_body(ASTBuilder *b) {
    if (check(b, TOKEN_LBRACE)) return parse_block_stmt(b);
    return parse_expr(b);
}

HulkNode* parse_if_expr(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_IF);
    expect(b, TOKEN_LPAREN);
    HulkNode *cond = parse_expr(b);
    expect(b, TOKEN_RPAREN);

    IfExprNode *ie = hulk_ast_if_expr(b->ctx, line, col);
    ie->condition = cond;
    ie->then_body = parse_if_body(b);

    // ElifList → ElifBranch ElifList | ε
    while (check(b, TOKEN_ELIF)) {
        int el = cur_line(b), ec = cur_col(b);
        advance(b);  // elif
        expect(b, TOKEN_LPAREN);
        HulkNode *econd = parse_expr(b);
        expect(b, TOKEN_RPAREN);

        ElifBranchNode *elif = hulk_ast_elif_branch(b->ctx, el, ec);
        elif->condition = econd;
        elif->body      = parse_if_body(b);
        hulk_node_list_push(&ie->elifs, (HulkNode*)elif);
    }

    expect(b, TOKEN_ELSE);
    ie->else_body = parse_if_body(b);

    return (HulkNode*)ie;
}
