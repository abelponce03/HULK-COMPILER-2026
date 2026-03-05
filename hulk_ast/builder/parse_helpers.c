/*
 * parse_helpers.c — Helpers de tokens, errores y argumentos
 *
 * Funciones utilitarias compartidas por todos los módulos del AST builder:
 *   - Manipulación de tokens (advance, check, match, save_lexeme)
 *   - Posición (cur_line, cur_col)
 *   - Reporte de errores (error_at, expect, expect_ident, synchronize)
 *   - Parsing de argumentos y anotaciones de tipo
 *
 * SRP: Solo gestión de tokens, errores y helpers de argumentos.
 */

#include "hulk_ast_builder_internal.h"

// ============================================================
//  Manipulación de tokens
// ============================================================

void advance(ASTBuilder *b) {
    if (b->current.lexeme) {
        free(b->current.lexeme);
        b->current.lexeme = NULL;
    }
    b->current = lexer_next_token(&b->lexer);
}

int check(ASTBuilder *b, TokenType t) {
    return b->current.type == t;
}

int match(ASTBuilder *b, TokenType t) {
    if (b->current.type == t) {
        advance(b);
        return 1;
    }
    return 0;
}

char* save_lexeme(ASTBuilder *b) {
    return hulk_ast_strdup(b->ctx, b->current.lexeme);
}

int cur_line(ASTBuilder *b) { return b->current.line; }
int cur_col(ASTBuilder *b)  { return b->current.col;  }

// ============================================================
//  Reporte de errores
// ============================================================

void error_at(ASTBuilder *b, const char *msg) {
    if (b->panic) return;
    b->had_error = 1;
    b->panic     = 1;
    LOG_ERROR_MSG("ast_builder", "[%d:%d] %s (encontrado '%s')",
                  b->current.line, b->current.col, msg,
                  b->current.lexeme ? b->current.lexeme : "EOF");
}

int expect(ASTBuilder *b, TokenType t) {
    if (b->current.type == t) {
        advance(b);
        return 1;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "se esperaba token %d", (int)t);
    error_at(b, buf);
    return 0;
}

char* expect_ident(ASTBuilder *b) {
    if (b->current.type != TOKEN_IDENT) {
        error_at(b, "se esperaba un identificador");
        return NULL;
    }
    char *name = save_lexeme(b);
    advance(b);
    return name;
}

void synchronize(ASTBuilder *b) {
    b->panic = 0;
    while (b->current.type != TOKEN_EOF) {
        if (b->current.type == TOKEN_SEMICOLON) {
            advance(b);
            return;
        }
        switch (b->current.type) {
            case TOKEN_FUNCTION:
            case TOKEN_TYPE:
            case TOKEN_LET:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_FOR:
            case TOKEN_DECOR:
                return;
            default:
                break;
        }
        advance(b);
    }
}

// ============================================================
//  Argumentos y anotaciones de tipo
// ============================================================

// ArgList → Expr (COMMA Expr)* | ε
void parse_arg_list(ASTBuilder *b, HulkNodeList *out) {
    if (check(b, TOKEN_RPAREN)) return;

    hulk_node_list_push(out, parse_expr(b));
    while (match(b, TOKEN_COMMA)) {
        hulk_node_list_push(out, parse_expr(b));
    }
}

// ArgIdList → ArgId (COMMA ArgId)* | ε
// ArgId → IDENT TypeAnnotation
void parse_arg_id_list(ASTBuilder *b, HulkNodeList *out) {
    if (!check(b, TOKEN_IDENT)) return;

    do {
        int line = cur_line(b), col = cur_col(b);
        char *pname = expect_ident(b);
        char *ptype = parse_type_annotation(b);
        VarBindingNode *vb = hulk_ast_var_binding(b->ctx, pname, ptype, line, col);
        hulk_node_list_push(out, (HulkNode*)vb);
    } while (match(b, TOKEN_COMMA));
}

// TypeAnnotation → COLON IDENT | ε
char* parse_type_annotation(ASTBuilder *b) {
    if (check(b, TOKEN_COLON)) {
        advance(b);
        return expect_ident(b);
    }
    return NULL;
}
