/*
 * parse_primary.c — Parsing de expresiones primarias del AST builder
 *
 * Expresiones atómicas y sus extensiones:
 *   - Literales: NUMBER, STRING, TRUE, FALSE
 *   - Identificadores + tail (calls, member access, asignación)
 *   - Agrupamiento: (expr)
 *   - Instanciación: new Type(args)
 *   - Self: self [.member ...]
 *   - Base: base(args)
 *
 * SRP: Solo parsing de expresiones primarias y sus extensiones.
 */

#include "hulk_ast_builder_internal.h"

// ============================================================
//  PrimaryTail  (calls, member access, assign)
//  PrimaryTail → LPAREN ArgList RPAREN PrimaryTail
//              | DOT IDENT PrimaryTail
//              | ASSIGN_DESTRUCT Expr
//              | ASSIGN Expr
//              | ε
// ============================================================

HulkNode* parse_primary_tail(ASTBuilder *b, HulkNode *left) {
    while (1) {
        int line = cur_line(b), col = cur_col(b);

        if (check(b, TOKEN_LPAREN)) {
            // Function call
            advance(b);
            CallExprNode *call = hulk_ast_call_expr(b->ctx, left, line, col);
            parse_arg_list(b, &call->args);
            expect(b, TOKEN_RPAREN);
            left = (HulkNode*)call;
            continue;
        }

        if (check(b, TOKEN_DOT)) {
            // Member access
            advance(b);
            char *member = expect_ident(b);
            left = (HulkNode*)hulk_ast_member_access(b->ctx, left, member, line, col);
            continue;
        }

        if (check(b, TOKEN_ASSIGN_DESTRUCT)) {
            // Destructive assignment
            advance(b);
            HulkNode *value = parse_expr(b);
            return (HulkNode*)hulk_ast_destruct_assign(b->ctx, left, value, line, col);
        }

        if (check(b, TOKEN_ASSIGN)) {
            // Regular assignment
            advance(b);
            HulkNode *value = parse_expr(b);
            return (HulkNode*)hulk_ast_assign(b->ctx, left, value, line, col);
        }

        break;  // ε
    }
    return left;
}

// ============================================================
//  Lookahead helper: detecta si estamos en el comienzo de un
//  lambda  (args) => body  o  (args): RetType => body
//  Sin avanzar el estado del builder/lexer.
// ============================================================

static int peek_is_lambda_start(ASTBuilder *b) {
    if (!check(b, TOKEN_LPAREN)) return 0;

    /* Guardamos el estado completo del lexer y el token actual */
    LexerContext saved_lexer = b->lexer;
    Token        saved_current = b->current;

    int result = 0;
    advance(b);  /* consume LPAREN */

    /* Caso trivial: () => ... */
    if (check(b, TOKEN_RPAREN)) {
        advance(b);
        if (check(b, TOKEN_ARROW)) { result = 1; }
        else if (check(b, TOKEN_COLON)) result = 1;
        goto restore;
    }

    /* Esperamos al menos un IDENT. Permitimos ',' o ':' o ')' después */
    for (;;) {
        if (!check(b, TOKEN_IDENT)) { result = 0; goto restore; }
        advance(b);
        if (check(b, TOKEN_COLON)) {
            /* Anotación de tipo: salta IDENT */
            advance(b);
            if (!check(b, TOKEN_IDENT)) { result = 0; goto restore; }
            advance(b);
        }
        if (check(b, TOKEN_COMMA)) { advance(b); continue; }
        break;
    }
    if (!check(b, TOKEN_RPAREN)) { result = 0; goto restore; }
    advance(b);

    /* Después del ): puede venir : TipoRetorno luego => */
    if (check(b, TOKEN_COLON)) {
        advance(b);
        if (!check(b, TOKEN_IDENT)) { result = 0; goto restore; }
        advance(b);
    }
    result = check(b, TOKEN_ARROW) ? 1 : 0;

restore:
    b->lexer = saved_lexer;
    b->current = saved_current;
    return result;
}

/* Parsea una lambda (args) => body  o  (args): T => body.
 * El builder ya debe apuntar a LPAREN. */
static HulkNode* parse_lambda_expr(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_LPAREN);

    HulkNodeList params;
    hulk_node_list_init(&params);
    parse_arg_id_list(b, &params);
    expect(b, TOKEN_RPAREN);

    char *ret_type = parse_type_annotation(b);
    expect(b, TOKEN_ARROW);

    FunctionExprNode *fn = hulk_ast_function_expr(b->ctx, ret_type, line, col);
    fn->params = params;
    fn->body = parse_expr(b);
    return (HulkNode*)fn;
}

// ============================================================
//  Primary
// ============================================================

HulkNode* parse_primary(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    (void)line; (void)col;

    // NUMBER
    if (check(b, TOKEN_NUMBER)) {
        char *raw = save_lexeme(b);
        advance(b);
        return (HulkNode*)hulk_ast_number_lit(b->ctx, raw, line, col);
    }

    // STRING (strip quotes)
    if (check(b, TOKEN_STRING)) {
        char *lexeme = b->current.lexeme;
        int len = b->current.length;
        char *content = hulk_ast_alloc(b->ctx, len);
        if (len >= 2) {
            memcpy(content, lexeme + 1, len - 2);
            content[len - 2] = '\0';
        } else {
            content[0] = '\0';
        }
        advance(b);
        StringLitNode *s = hulk_ast_string_lit(b->ctx, content, line, col);
        return (HulkNode*)s;
    }

    // TRUE
    if (check(b, TOKEN_TRUE)) {
        advance(b);
        return (HulkNode*)hulk_ast_bool_lit(b->ctx, 1, line, col);
    }

    // FALSE
    if (check(b, TOKEN_FALSE)) {
        advance(b);
        return (HulkNode*)hulk_ast_bool_lit(b->ctx, 0, line, col);
    }

    // IDENT + PrimaryTail
    if (check(b, TOKEN_IDENT)) {
        char *name = save_lexeme(b);
        advance(b);
        HulkNode *node = (HulkNode*)hulk_ast_ident(b->ctx, name, line, col);
        return parse_primary_tail(b, node);
    }

    // FUNCTION expr anónima
    if (check(b, TOKEN_FUNCTION)) {
        return parse_function_expr(b);
    }

    // LPAREN: o lambda  (args) => body  o expr parentizada
    if (check(b, TOKEN_LPAREN)) {
        if (peek_is_lambda_start(b)) {
            HulkNode *lam = parse_lambda_expr(b);
            return parse_primary_tail(b, lam);
        }
        advance(b);
        HulkNode *expr = parse_expr(b);
        expect(b, TOKEN_RPAREN);
        return parse_primary_tail(b, expr);
    }

    // NEW IDENT LPAREN ArgList RPAREN
    if (check(b, TOKEN_NEW)) {
        advance(b);
        char *type_name = expect_ident(b);
        expect(b, TOKEN_LPAREN);
        NewExprNode *ne = hulk_ast_new_expr(b->ctx, type_name, line, col);
        parse_arg_list(b, &ne->args);
        expect(b, TOKEN_RPAREN);
        return (HulkNode*)ne;
    }

    // SELF PrimaryTail
    if (check(b, TOKEN_SELF)) {
        advance(b);
        HulkNode *node = (HulkNode*)hulk_ast_self(b->ctx, line, col);
        return parse_primary_tail(b, node);
    }

    // BASE LPAREN ArgList RPAREN
    if (check(b, TOKEN_BASE)) {
        advance(b);
        expect(b, TOKEN_LPAREN);
        BaseCallNode *bc = hulk_ast_base_call(b->ctx, line, col);
        parse_arg_list(b, &bc->args);
        expect(b, TOKEN_RPAREN);
        return (HulkNode*)bc;
    }

    error_at(b, "expresión no válida");
    return NULL;
}
