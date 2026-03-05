/*
 * parse_statements.c — Parsing de sentencias del AST builder
 *
 * Sentencias de control de flujo que no son expresiones:
 *   - WhileStmt: while cond body
 *   - ForStmt:   for (var in iterable) body
 *   - BlockStmt: { stmt; stmt; ... }
 *
 * SRP: Solo parsing de sentencias de control de flujo.
 */

#include "hulk_ast_builder_internal.h"

// ============================================================
//  WhileStmt
//  WHILE Expr WhileBody
//  WhileBody → BlockStmt | Expr
// ============================================================

HulkNode* parse_while_stmt(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_WHILE);

    HulkNode *cond = parse_expr(b);

    WhileStmtNode *ws = hulk_ast_while_stmt(b->ctx, line, col);
    ws->condition = cond;

    if (check(b, TOKEN_LBRACE)) {
        ws->body = parse_block_stmt(b);
    } else {
        ws->body = parse_expr(b);
    }
    return (HulkNode*)ws;
}

// ============================================================
//  ForStmt
//  FOR LPAREN IDENT IN Expr RPAREN ForBody
// ============================================================

HulkNode* parse_for_stmt(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_FOR);
    expect(b, TOKEN_LPAREN);

    char *var_name = expect_ident(b);
    expect(b, TOKEN_IN);
    HulkNode *iter = parse_expr(b);
    expect(b, TOKEN_RPAREN);

    ForStmtNode *fs = hulk_ast_for_stmt(b->ctx, var_name, line, col);
    fs->iterable = iter;

    if (check(b, TOKEN_LBRACE)) {
        fs->body = parse_block_stmt(b);
    } else {
        fs->body = parse_expr(b);
    }
    return (HulkNode*)fs;
}

// ============================================================
//  BlockStmt
//  LBRACE StmtList RBRACE
//  StmtList → TerminatedStmt StmtList | ε
// ============================================================

HulkNode* parse_block_stmt(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_LBRACE);

    BlockStmtNode *blk = hulk_ast_block_stmt(b->ctx, line, col);

    while (!check(b, TOKEN_RBRACE) && !check(b, TOKEN_EOF)) {
        HulkNode *s = parse_stmt(b);
        if (s) {
            hulk_node_list_push(&blk->statements, s);
            if (!b->panic) expect(b, TOKEN_SEMICOLON);
        }
        if (b->panic) synchronize(b);
    }

    expect(b, TOKEN_RBRACE);
    return (HulkNode*)blk;
}
