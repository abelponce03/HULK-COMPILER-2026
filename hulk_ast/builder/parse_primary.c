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

static char* primary_join3(ASTBuilder *b, const char *a, const char *mid,
                           const char *c) {
    const char *sa = a ? a : "";
    const char *sm = mid ? mid : "";
    const char *sc = c ? c : "";
    size_t la = strlen(sa), lm = strlen(sm), lc = strlen(sc);
    char *out = hulk_ast_alloc(b->ctx, la + lm + lc + 1);
    memcpy(out, sa, la);
    memcpy(out + la, sm, lm);
    memcpy(out + la + lm, sc, lc);
    out[la + lm + lc] = '\0';
    return out;
}

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

        if (check(b, TOKEN_LBRACKET)) {
            // Vector indexing
            advance(b);
            HulkNode *idx = parse_expr(b);
            expect(b, TOKEN_RBRACKET);
            left = (HulkNode*)hulk_ast_index_expr(b->ctx, left, idx, line, col);
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
//  lambda  (args) -> body  o  (args): RetType -> body
//  Sin avanzar el estado del builder/lexer.
// ============================================================

static int skip_type_ref_lookahead(ASTBuilder *b) {
    if (is_ident_like(b->current.type)) {
        advance(b);
    } else if (check(b, TOKEN_LPAREN)) {
        advance(b);
        if (!check(b, TOKEN_RPAREN)) {
            if (!skip_type_ref_lookahead(b)) return 0;
            while (check(b, TOKEN_COMMA)) {
                advance(b);
                if (!skip_type_ref_lookahead(b)) return 0;
            }
        }
        if (!check(b, TOKEN_RPAREN)) return 0;
        advance(b);
        if (!check(b, TOKEN_ARROW)) return 0;
        advance(b);
        if (!skip_type_ref_lookahead(b)) return 0;
    } else {
        return 0;
    }

    while (check(b, TOKEN_LBRACKET) || check(b, TOKEN_MULT)) {
        if (check(b, TOKEN_MULT)) {
            advance(b);
        } else {
            advance(b);
            if (!check(b, TOKEN_RBRACKET)) return 0;
            advance(b);
        }
    }
    return 1;
}

static int peek_is_lambda_start_v2(ASTBuilder *b) {
    if (!check(b, TOKEN_LPAREN)) return 0;

    LexerContext saved_lexer = b->lexer;
    Token        saved_current = b->current;
    int result = 0;

    advance(b);
    if (!check(b, TOKEN_RPAREN)) {
        for (;;) {
            if (!is_ident_like(b->current.type)) goto restore;
            advance(b);
            if (check(b, TOKEN_COLON)) {
                advance(b);
                if (!skip_type_ref_lookahead(b)) goto restore;
            }
            if (!check(b, TOKEN_COMMA)) break;
            advance(b);
        }
    }

    if (!check(b, TOKEN_RPAREN)) goto restore;
    advance(b);

    if (check(b, TOKEN_COLON)) {
        advance(b);
        if (!skip_type_ref_lookahead(b)) goto restore;
    }

    result = check(b, TOKEN_ARROW);

restore:
    b->lexer = saved_lexer;
    b->current = saved_current;
    return result;
}

/* Parsea una lambda (args) -> body  o  (args): T -> body.
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

    if (check(b, TOKEN_IF)) {
        HulkNode *node = parse_if_expr(b);
        return parse_primary_tail(b, node);
    }

    if (check(b, TOKEN_LET)) {
        HulkNode *node = parse_let_expr(b);
        return parse_primary_tail(b, node);
    }

    // FUNCTION expr anónima
    if (check(b, TOKEN_FUNCTION)) {
        return parse_function_expr(b);
    }

    // LPAREN: o lambda  (args) -> body  o expr parentizada
    if (check(b, TOKEN_LPAREN)) {
        if (peek_is_lambda_start_v2(b)) {
            HulkNode *lam = parse_lambda_expr(b);
            return parse_primary_tail(b, lam);
        }
        advance(b);
        HulkNode *expr = parse_expr(b);
        expect(b, TOKEN_RPAREN);
        return parse_primary_tail(b, expr);
    }

    // NEW IDENT LPAREN ArgList RPAREN
    // NEW TypeRefWithoutFunction LBRACKET Expr RBRACKET [ { i -> expr } ]
    if (check(b, TOKEN_NEW)) {
        advance(b);
        char *type_name = expect_ident(b);
        while (check(b, TOKEN_LBRACKET)) {
            LexerContext saved_lexer = b->lexer;
            Token saved_current = b->current;
            advance(b);
            if (!check(b, TOKEN_RBRACKET)) {
                b->lexer = saved_lexer;
                b->current = saved_current;
                break;
            }
            advance(b);
            type_name = primary_join3(b, type_name, "", "[]");
        }
        if (check(b, TOKEN_LBRACKET)) {
            advance(b);
            HulkNode *size = parse_expr(b);
            expect(b, TOKEN_RBRACKET);
            CallExprNode *call = hulk_ast_call_expr(
                b->ctx,
                (HulkNode*)hulk_ast_ident(b->ctx, "__array_new", line, col),
                line, col);
            hulk_node_list_push(&call->args, size);
            if (check(b, TOKEN_LBRACE)) {
                advance(b);
                int pl = cur_line(b), pc = cur_col(b);
                char *idx_name = expect_ident(b);
                expect(b, TOKEN_ARROW);
                FunctionExprNode *fn = hulk_ast_function_expr(b->ctx, "Number", pl, pc);
                VarBindingNode *p = hulk_ast_var_binding(b->ctx, idx_name, "Number", pl, pc);
                hulk_node_list_push(&fn->params, (HulkNode*)p);
                fn->body = parse_expr(b);
                expect(b, TOKEN_RBRACE);
                ((IdentNode*)call->callee)->name = hulk_ast_strdup(b->ctx, "__array_init");
                hulk_node_list_push(&call->args, (HulkNode*)fn);
            }
            (void)type_name;
            return parse_primary_tail(b, (HulkNode*)call);
        }
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

    // LBRACKET vector_lit RBRACKET  →  [a, b, c]
    if (check(b, TOKEN_LBRACKET)) {
        int vline = cur_line(b), vcol = cur_col(b);
        advance(b);
        VectorLitNode *v = hulk_ast_vector_lit(b->ctx, vline, vcol);
        if (!check(b, TOKEN_RBRACKET)) {
            for (;;) {
                HulkNode *item = parse_expr(b);
                if (item) hulk_node_list_push(&v->items, item);
                if (!match(b, TOKEN_COMMA)) break;
            }
        }
        expect(b, TOKEN_RBRACKET);
        return parse_primary_tail(b, (HulkNode*)v);
    }

    // LBRACE array_lit RBRACE  →  {a, b, c}  (compatibilidad PIAD)
    if (check(b, TOKEN_LBRACE)) {
        int vline = cur_line(b), vcol = cur_col(b);
        advance(b);
        VectorLitNode *v = hulk_ast_vector_lit(b->ctx, vline, vcol);
        if (!check(b, TOKEN_RBRACE)) {
            for (;;) {
                HulkNode *item = parse_expr(b);
                if (item) hulk_node_list_push(&v->items, item);
                if (!match(b, TOKEN_COMMA)) break;
            }
        }
        expect(b, TOKEN_RBRACE);
        return parse_primary_tail(b, (HulkNode*)v);
    }

    // BASE LPAREN ArgList RPAREN
    if (check(b, TOKEN_BASE)) {
        LexerContext lookahead = b->lexer;
        Token next = lexer_next_token(&lookahead);
        int is_base_call = (next.type == TOKEN_LPAREN);
        if (next.lexeme) free(next.lexeme);
        if (!is_base_call) {
            char *name = save_lexeme(b);
            advance(b);
            HulkNode *node = (HulkNode*)hulk_ast_ident(b->ctx, name, line, col);
            return parse_primary_tail(b, node);
        }
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
