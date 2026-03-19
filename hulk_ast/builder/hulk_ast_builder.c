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
        int is_named = (next.type == TOKEN_IDENT);
        free(next.lexeme);
        return is_named ? parse_function_def(b) : parse_stmt(b);
    }
    if (check(b, TOKEN_TYPE))     return parse_type_def(b);
    if (check(b, TOKEN_DECOR))    return parse_decor_block(b);

    // TerminatedStmt → Stmt SEMICOLON
    HulkNode *stmt = parse_stmt(b);
    if (stmt && !b->panic) expect(b, TOKEN_SEMICOLON);
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
    if (!ctx || !dfa || !input) return NULL;

    ASTBuilder b;
    b.ctx       = ctx;
    b.had_error = 0;
    b.panic     = 0;

    // Inicializar lexer
    lexer_init(&b.lexer, dfa, input);

    // Primer token (lookahead)
    b.current = lexer_next_token(&b.lexer);

    // Parsear programa completo
    HulkNode *ast = parse_program(&b);

    // Limpiar último token si quedó
    if (b.current.lexeme) {
        free(b.current.lexeme);
        b.current.lexeme = NULL;
    }

    if (b.had_error) {
        LOG_WARN_MSG("ast_builder", "AST construido con %s",
                     "errores (puede estar incompleto)");
    }

    return b.had_error ? NULL : ast;
}
