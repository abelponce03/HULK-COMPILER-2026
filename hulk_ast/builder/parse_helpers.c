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
#include "../../hulk_tokens.h"

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

int is_ident_like(TokenType t) {
    /* PIAD usa `base` como identificador en bindings/atributos. Léxicamente
     * sigue siendo keyword para permitir `base(...)` como expresión especial,
     * pero en posiciones declarativas lo aceptamos como nombre. */
    return t == TOKEN_IDENT || t == TOKEN_BASE;
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
    const char *expected = get_token_name((int)t);
    const char *found    = get_token_name((int)b->current.type);
    snprintf(buf, sizeof(buf), "se esperaba '%s', se encontró '%s'",
             expected ? expected : "?", found ? found : "?");
    error_at(b, buf);
    return 0;
}

char* expect_ident(ASTBuilder *b) {
    if (!is_ident_like(b->current.type)) {
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

    HulkNode *arg = parse_expr(b);
    if (arg) hulk_node_list_push(out, arg);
    while (match(b, TOKEN_COMMA)) {
        arg = parse_expr(b);
        if (arg) hulk_node_list_push(out, arg);
    }
}

// ArgIdList → ArgId (COMMA ArgId)* | ε
// ArgId → IDENT TypeAnnotation
void parse_arg_id_list(ASTBuilder *b, HulkNodeList *out) {
    if (!is_ident_like(b->current.type)) return;

    do {
        int line = cur_line(b), col = cur_col(b);
        char *pname = expect_ident(b);
        if (!pname) break;
        char *ptype = parse_type_annotation(b);
        VarBindingNode *vb = hulk_ast_var_binding(b->ctx, pname, ptype, line, col);
        if (vb) hulk_node_list_push(out, (HulkNode*)vb);
    } while (match(b, TOKEN_COMMA));
}

static char* ast_join3(ASTBuilder *b, const char *a, const char *mid,
                       const char *c) {
    const char *sa = a ? a : "";
    const char *sm = mid ? mid : "";
    const char *sc = c ? c : "";
    size_t len = strlen(sa) + strlen(sm) + strlen(sc);
    char *out = hulk_ast_alloc(b->ctx, len + 1);
    memcpy(out, sa, strlen(sa));
    memcpy(out + strlen(sa), sm, strlen(sm));
    memcpy(out + strlen(sa) + strlen(sm), sc, strlen(sc));
    out[len] = '\0';
    return out;
}

static char* parse_type_ref(ASTBuilder *b) {
    char *type = NULL;
    if (check(b, TOKEN_IDENT)) {
        type = save_lexeme(b);
        advance(b);
    } else if (check(b, TOKEN_LPAREN)) {
        advance(b);
        char *params = hulk_ast_strdup(b->ctx, "");

        if (!check(b, TOKEN_RPAREN)) {
            params = parse_type_ref(b);
            while (match(b, TOKEN_COMMA)) {
                char *next = parse_type_ref(b);
                params = ast_join3(b, params, ",", next);
            }
        }

        expect(b, TOKEN_RPAREN);
        expect(b, TOKEN_ARROW);
        char *ret = parse_type_ref(b);
        char *head = ast_join3(b, "(", params, ")->");
        type = ast_join3(b, head, "", ret);
    } else {
        error_at(b, "se esperaba un tipo");
        type = hulk_ast_strdup(b->ctx, "Object");
    }

    while (check(b, TOKEN_LBRACKET) || check(b, TOKEN_MULT)) {
        if (check(b, TOKEN_MULT)) {
            advance(b);
            type = ast_join3(b, type, "", "*");
            continue;
        }
        advance(b);
        expect(b, TOKEN_RBRACKET);
        type = ast_join3(b, type, "", "[]");
    }
    return type;
}

// TypeAnnotation → COLON TypeRef | ε
char* parse_type_annotation(ASTBuilder *b) {
    if (check(b, TOKEN_COLON)) {
        advance(b);
        return parse_type_ref(b);
    }
    return NULL;
}
