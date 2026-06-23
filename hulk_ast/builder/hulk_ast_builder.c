/*
 * hulk_ast_builder.c — Orquestador del AST Builder
 *
 * Punto de entrada público (hulk_build_ast) y dispatchers de alto nivel:
 *   - parse_program:   recorre top-level declarations
 *   - parse_top_level: despacha a función/tipo/decorador/sentencia
 *   - parse_stmt:      despacha a while/for/block/expr
 *   - parse_expr:      despacha a if/let/or_expr
 *
 * La implementación concreta de cada tipo de nodo está en su módulo:
 *   - parse_helpers.c:     tokens, errores, argumentos
 *   - parse_expressions.c: cadena de precedencia, let, if
 *   - parse_statements.c:  while, for, block
 *   - parse_definitions.c: function, type, decoradores
 *   - parse_primary.c:     literales, calls, members, new, self, base
 *
 * Principios SOLID:
 *   SRP — Solo dispatch y API pública; cada módulo tiene su responsabilidad.
 *   OCP — Nuevas producciones se agregan en su módulo sin tocar este archivo.
 *   DIP — Los módulos dependen de la abstracción ASTBuilder (internal.h).
 */

#include "hulk_ast_builder_internal.h"
#include "hulk_ll1_builder.h"

// ============================================================
//  Parse: Program
//  Program → TopLevel Program | ε
// ============================================================

HulkNode* parse_program(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    ProgramNode *prog = hulk_ast_program(b->ctx, line, col);

    while (!check(b, TOKEN_EOF)) {
        HulkNode *decl = parse_top_level(b);
        if (decl) {
            hulk_node_list_push(&prog->declarations, decl);
        } else if (b->panic) {
            synchronize(b);
        }
    }
    return (HulkNode*)prog;
}

// ============================================================
//  Parse: TopLevel
//  TopLevel → FunctionDef | TypeDef | DecorBlock | TerminatedStmt
// ============================================================

HulkNode* parse_top_level(ASTBuilder *b) {
    if (check(b, TOKEN_FUNCTION)) {
        LexerContext lookahead = b->lexer;
        Token next = lexer_next_token(&lookahead);
        int is_named = is_ident_like(next.type);
        free(next.lexeme);
        if (is_named) return parse_function_def(b);
    }
    if (check(b, TOKEN_DEFINE))   return parse_define_def(b);
    if (check(b, TOKEN_TYPE))     return parse_type_def(b);
    if (check(b, TOKEN_DECOR))    return parse_decor_block(b);
    if (check(b, TOKEN_PROTOCOL)) return parse_protocol_def(b);

    // TerminatedStmt → Stmt SEMICOLON
    // El `;` es opcional tras un bloque { ... } y al final del archivo
    // (spec A.2.4); obligatorio entre statements top-level consecutivos.
    HulkNode *stmt = parse_stmt(b);
    if (stmt && !b->panic) {
        if (stmt->type == NODE_BLOCK_STMT || check(b, TOKEN_EOF))
            match(b, TOKEN_SEMICOLON);   // opcional
        else
            expect(b, TOKEN_SEMICOLON);  // obligatorio
    }
    return stmt;
}

// ============================================================
//  Parse: Stmt
//  Stmt → WhileStmt | ForStmt | BlockStmt | Expr
// ============================================================

HulkNode* parse_stmt(ASTBuilder *b) {
    if (check(b, TOKEN_WHILE))  return parse_while_stmt(b);
    if (check(b, TOKEN_FOR))    return parse_for_stmt(b);
    if (check(b, TOKEN_LBRACE)) return parse_block_stmt(b);
    return parse_expr(b);
}

// ============================================================
//  Parse: Expr
//  Expr → IfExpr | LetExpr | OrExpr
// ============================================================

HulkNode* parse_expr(ASTBuilder *b) {
    if (check(b, TOKEN_IF))  return parse_if_expr(b);
    if (check(b, TOKEN_LET)) return parse_let_expr(b);
    return parse_or_expr(b);
}

// ============================================================
//  API PÚBLICA
// ============================================================

HulkNode* hulk_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input) {
    return hulk_ll1_build_ast(ctx, dfa, input);
}
