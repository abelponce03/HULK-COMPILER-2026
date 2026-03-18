/*
 * parse_definitions.c — Parsing de definiciones del AST builder
 *
 * Definiciones de nivel superior del lenguaje HULK:
 *   - FunctionDef: function name(params): Type => body | { body }
 *   - TypeDef:     type Name(params) inherits Parent(args) { body }
 *   - DecorBlock:  decor items function/type ...
 *
 * Incluye el parsing de miembros de tipo (métodos y atributos).
 *
 * SRP: Solo parsing de definiciones y declaraciones.
 */

#include "hulk_ast_builder_internal.h"

// ============================================================
//  FunctionDef
//  FUNCTION IDENT LPAREN ArgIdList RPAREN TypeAnnotation FunctionBody
// ============================================================

// FunctionBody → ARROW Expr SEMICOLON | BlockStmt
static HulkNode* parse_function_body(ASTBuilder *b) {
    if (check(b, TOKEN_ARROW)) {
        advance(b);   // =>
        HulkNode *body = parse_expr(b);
        expect(b, TOKEN_SEMICOLON);
        return body;
    }
    return parse_block_stmt(b);
}

HulkNode* parse_function_def(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_FUNCTION);

    char *name = expect_ident(b);
    if (!name) return NULL;

    expect(b, TOKEN_LPAREN);
    HulkNodeList params;
    hulk_node_list_init(&params);
    parse_arg_id_list(b, &params);
    expect(b, TOKEN_RPAREN);

    char *ret_type = parse_type_annotation(b);

    FunctionDefNode *fn = hulk_ast_function_def(b->ctx, name, ret_type, line, col);
    fn->params = params;
    fn->body   = parse_function_body(b);
    return (HulkNode*)fn;
}

// ============================================================
//  TypeDef
//  TYPE IDENT TypeParams TypeInheritance LBRACE TypeBody RBRACE
// ============================================================

// TypeBody → TypeMember TypeBody | ε
// TypeMember → IDENT TypeMemberTail
static void parse_type_body(ASTBuilder *b, TypeDefNode *td) {
    while (check(b, TOKEN_IDENT)) {
        int line = cur_line(b), col = cur_col(b);
        char *member_name = expect_ident(b);

        // TypeMemberTail → LPAREN ... → method
        //                → TypeAnnotation (SEMICOLON | ASSIGN ...) → attribute
        if (check(b, TOKEN_LPAREN)) {
            // MethodDef: name(params): RetType => body | { body }
            advance(b);  // (
            HulkNodeList mparams;
            hulk_node_list_init(&mparams);
            parse_arg_id_list(b, &mparams);
            expect(b, TOKEN_RPAREN);
            char *ret_type = parse_type_annotation(b);

            MethodDefNode *m = hulk_ast_method_def(b->ctx, member_name, ret_type, line, col);
            m->params = mparams;

            // MethodBody → ARROW Expr SEMICOLON | BlockStmt
            if (check(b, TOKEN_ARROW)) {
                advance(b);
                m->body = parse_expr(b);
                expect(b, TOKEN_SEMICOLON);
            } else {
                m->body = parse_block_stmt(b);
            }
            hulk_node_list_push(&td->members, (HulkNode*)m);
        } else {
            // AttributeDef: name [: Type] [= expr] ;
            char *type_ann = parse_type_annotation(b);
            AttributeDefNode *attr = hulk_ast_attribute_def(
                b->ctx, member_name, type_ann, line, col);

            if (check(b, TOKEN_ASSIGN)) {
                advance(b);
                attr->init_expr = parse_expr(b);
            }
            expect(b, TOKEN_SEMICOLON);
            hulk_node_list_push(&td->members, (HulkNode*)attr);
        }
    }
}

HulkNode* parse_type_def(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_TYPE);

    char *name = expect_ident(b);
    if (!name) return NULL;

    // TypeParams → LPAREN ArgIdList RPAREN | ε
    HulkNodeList params;
    hulk_node_list_init(&params);
    if (check(b, TOKEN_LPAREN)) {
        advance(b);
        parse_arg_id_list(b, &params);
        expect(b, TOKEN_RPAREN);
    }

    // TypeInheritance → INHERITS IDENT TypeBaseArgs | ε
    char *parent = NULL;
    HulkNodeList parent_args;
    hulk_node_list_init(&parent_args);
    if (check(b, TOKEN_INHERITS)) {
        advance(b);
        parent = expect_ident(b);
        // TypeBaseArgs → LPAREN ArgList RPAREN | ε
        if (check(b, TOKEN_LPAREN)) {
            advance(b);
            parse_arg_list(b, &parent_args);
            expect(b, TOKEN_RPAREN);
        }
    }

    TypeDefNode *td = hulk_ast_type_def(b->ctx, name, parent, line, col);
    td->params      = params;
    td->parent_args = parent_args;

    expect(b, TOKEN_LBRACE);
    parse_type_body(b, td);
    expect(b, TOKEN_RBRACE);

    return (HulkNode*)td;
}

// ============================================================
//  DecorBlock
//  CONCAT DecorItems DecorNext
//  DecorNext → CONCAT DecorItems DecorNext | FunctionDef | TypeDef
// ============================================================

static void parse_decor_items(ASTBuilder *b, HulkNodeList *decorators) {
    // DecorItems → DecorItem DecorItemsTail
    // DecorItemsTail → COMMA DecorItem DecorItemsTail | ε
    // DecorItem → IDENT DecorArgs
    // DecorArgs → LPAREN ArgList RPAREN | ε
    do {
        int line = cur_line(b), col = cur_col(b);
        char *dname = expect_ident(b);
        if (!dname) return;

        DecorItemNode *di = hulk_ast_decor_item(b->ctx, dname, line, col);
        if (check(b, TOKEN_LPAREN)) {
            advance(b);
            parse_arg_list(b, &di->args);
            expect(b, TOKEN_RPAREN);
        }
        hulk_node_list_push(decorators, (HulkNode*)di);
    } while (match(b, TOKEN_COMMA));
}

HulkNode* parse_decor_block(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    DecorBlockNode *db = hulk_ast_decor_block(b->ctx, line, col);

    // Primer DECOR + items
    expect(b, TOKEN_DECOR);
    parse_decor_items(b, &db->decorators);

    // DecorNext: más DECOR lines u objetivo final
    while (check(b, TOKEN_DECOR)) {
        advance(b);
        parse_decor_items(b, &db->decorators);
    }

    // Target: FunctionDef | TypeDef
    if (check(b, TOKEN_FUNCTION)) {
        db->target = parse_function_def(b);
    } else if (check(b, TOKEN_TYPE)) {
        db->target = parse_type_def(b);
    } else {
        error_at(b, "se esperaba 'function' o 'type' después de decoradores");
    }
    return (HulkNode*)db;
}
