/*
 * hulk_ast_builder.c — Parser de descenso recursivo que construye el AST
 *
 * Sigue la gramática LL(1) de grammar.ll1, usando un token de lookahead
 * para decidir qué producción aplicar.  Cada función parse_X retorna
 * el HulkNode* correspondiente, asignado desde el HulkASTContext (arena).
 *
 * Asociatividad:
 *   - Operadores aritméticos/lógicos: izquierda (loop).
 *   - Potencia (**): derecha (recursión).
 *   - as/is: izquierda (loop).
 */

#include "hulk_ast_builder.h"
#include "../generador_analizadores_lexicos/lexer.h"
#include "../error_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
//  Contexto interno del builder
// ============================================================

typedef struct {
    HulkASTContext *ctx;      // arena (propietaria de todos los nodos)
    LexerContext    lexer;    // contexto del lexer
    Token           current;  // token lookahead (no consumido aún)
    int             had_error;
    int             panic;    // en modo pánico (saltando tokens)
} ASTBuilder;

// ============================================================
//  Prototipos internos (orden de precedencia ascendente)
// ============================================================

static HulkNode* parse_program(ASTBuilder *b);
static HulkNode* parse_top_level(ASTBuilder *b);
static HulkNode* parse_function_def(ASTBuilder *b);
static HulkNode* parse_type_def(ASTBuilder *b);
static HulkNode* parse_decor_block(ASTBuilder *b);
static HulkNode* parse_stmt(ASTBuilder *b);
static HulkNode* parse_expr(ASTBuilder *b);
static HulkNode* parse_let_expr(ASTBuilder *b);
static HulkNode* parse_if_expr(ASTBuilder *b);
static HulkNode* parse_or_expr(ASTBuilder *b);
static HulkNode* parse_and_expr(ASTBuilder *b);
static HulkNode* parse_cmp_expr(ASTBuilder *b);
static HulkNode* parse_concat_expr(ASTBuilder *b);
static HulkNode* parse_add_expr(ASTBuilder *b);
static HulkNode* parse_term(ASTBuilder *b);
static HulkNode* parse_factor(ASTBuilder *b);
static HulkNode* parse_unary(ASTBuilder *b);
static HulkNode* parse_primary(ASTBuilder *b);
static HulkNode* parse_primary_tail(ASTBuilder *b, HulkNode *left);
static HulkNode* parse_as_chain(ASTBuilder *b, HulkNode *left);
static HulkNode* parse_block_stmt(ASTBuilder *b);
static HulkNode* parse_while_stmt(ASTBuilder *b);
static HulkNode* parse_for_stmt(ASTBuilder *b);
static void      parse_arg_list(ASTBuilder *b, HulkNodeList *out);
static void      parse_arg_id_list(ASTBuilder *b, HulkNodeList *out);
static char*     parse_type_annotation(ASTBuilder *b);

// ============================================================
//  Helpers de tokens
// ============================================================

// Avanza al siguiente token (libera el lexeme anterior)
static void advance(ASTBuilder *b) {
    if (b->current.lexeme) {
        free(b->current.lexeme);
        b->current.lexeme = NULL;
    }
    b->current = lexer_next_token(&b->lexer);
}

// ¿El token actual es de este tipo?
static int check(ASTBuilder *b, TokenType t) {
    return b->current.type == t;
}

// Si coincide, consume y retorna 1; si no, retorna 0
static int match(ASTBuilder *b, TokenType t) {
    if (b->current.type == t) {
        advance(b);
        return 1;
    }
    return 0;
}

// Copia el lexeme actual a la arena ANTES de avanzar
static char* save_lexeme(ASTBuilder *b) {
    return hulk_ast_strdup(b->ctx, b->current.lexeme);
}

// Posición actual para nodos
static int cur_line(ASTBuilder *b) { return b->current.line; }
static int cur_col(ASTBuilder *b)  { return b->current.col;  }

// ============================================================
//  Reporte de errores
// ============================================================

static void error_at(ASTBuilder *b, const char *msg) {
    if (b->panic) return;          // ya estamos en recuperación
    b->had_error = 1;
    b->panic     = 1;
    LOG_ERROR_MSG("ast_builder", "[%d:%d] %s (encontrado '%s')",
                  b->current.line, b->current.col, msg,
                  b->current.lexeme ? b->current.lexeme : "EOF");
}

// Consume el token esperado o reporta error
static int expect(ASTBuilder *b, TokenType t) {
    if (b->current.type == t) {
        advance(b);
        return 1;
    }
    // Mensaje descriptivo
    char buf[128];
    snprintf(buf, sizeof(buf), "se esperaba token %d", (int)t);
    error_at(b, buf);
    return 0;
}

// Espera un IDENT y copia su nombre a la arena
static char* expect_ident(ASTBuilder *b) {
    if (b->current.type != TOKEN_IDENT) {
        error_at(b, "se esperaba un identificador");
        return NULL;
    }
    char *name = save_lexeme(b);
    advance(b);
    return name;
}

// Sincroniza saltando tokens hasta encontrar un punto seguro
static void synchronize(ASTBuilder *b) {
    b->panic = 0;
    while (b->current.type != TOKEN_EOF) {
        // Después de un SEMICOLON es buen punto de re-sincronización
        if (b->current.type == TOKEN_SEMICOLON) {
            advance(b);
            return;
        }
        // Inicio de nueva declaración
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
//  Parse: Program
//  Program → TopLevel Program | ε
// ============================================================

static HulkNode* parse_program(ASTBuilder *b) {
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

static HulkNode* parse_top_level(ASTBuilder *b) {
    if (check(b, TOKEN_FUNCTION)) return parse_function_def(b);
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

static HulkNode* parse_stmt(ASTBuilder *b) {
    if (check(b, TOKEN_WHILE))  return parse_while_stmt(b);
    if (check(b, TOKEN_FOR))    return parse_for_stmt(b);
    if (check(b, TOKEN_LBRACE)) return parse_block_stmt(b);
    return parse_expr(b);
}

// ============================================================
//  Parse: Expr
//  Expr → IfExpr | LetExpr | OrExpr
// ============================================================

static HulkNode* parse_expr(ASTBuilder *b) {
    if (check(b, TOKEN_IF))  return parse_if_expr(b);
    if (check(b, TOKEN_LET)) return parse_let_expr(b);
    return parse_or_expr(b);
}

// ============================================================
//  Parse: FunctionDef
//  FUNCTION IDENT LPAREN ArgIdList RPAREN TypeAnnotation FunctionBody
// ============================================================

static HulkNode* parse_function_body(ASTBuilder *b);

static HulkNode* parse_function_def(ASTBuilder *b) {
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

// ============================================================
//  Parse: TypeDef
//  TYPE IDENT TypeParams TypeInheritance LBRACE TypeBody RBRACE
// ============================================================

static void parse_type_body(ASTBuilder *b, TypeDefNode *td);

static HulkNode* parse_type_def(ASTBuilder *b) {
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
            AttributeDefNode *attr = hulk_ast_attribute_def(b->ctx, member_name, type_ann, line, col);

            if (check(b, TOKEN_ASSIGN)) {
                advance(b);
                attr->init_expr = parse_expr(b);
            }
            expect(b, TOKEN_SEMICOLON);
            hulk_node_list_push(&td->members, (HulkNode*)attr);
        }
    }
}

// ============================================================
//  Parse: DecorBlock
//  DECOR DecorItems DecorNext
//  DecorNext → DECOR DecorItems DecorNext | FunctionDef | TypeDef
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

static HulkNode* parse_decor_block(ASTBuilder *b) {
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

// ============================================================
//  Parse: LetExpr
//  LET VarBindingList IN LetBody
// ============================================================

static HulkNode* parse_let_expr(ASTBuilder *b) {
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

    // LetBody → Expr | WhileStmt | ForStmt | BlockStmt
    let->body = parse_stmt(b);

    return (HulkNode*)let;
}

// ============================================================
//  Parse: IfExpr
//  IF LPAREN Expr RPAREN IfBody ElifList ELSE IfBody
// ============================================================

static HulkNode* parse_if_body(ASTBuilder *b) {
    if (check(b, TOKEN_LBRACE)) return parse_block_stmt(b);
    return parse_expr(b);
}

static HulkNode* parse_if_expr(ASTBuilder *b) {
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

// ============================================================
//  Parse: WhileStmt
//  WHILE Expr WhileBody
//  WhileBody → BlockStmt | Expr
// ============================================================

static HulkNode* parse_while_stmt(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);
    expect(b, TOKEN_WHILE);

    // La condición puede estar entre paréntesis o no, según la gramática
    // La gramática dice: WHILE Expr WhileBody
    HulkNode *cond = parse_expr(b);

    WhileStmtNode *ws = hulk_ast_while_stmt(b->ctx, line, col);
    ws->condition = cond;

    // WhileBody → BlockStmt | Expr
    if (check(b, TOKEN_LBRACE)) {
        ws->body = parse_block_stmt(b);
    } else {
        ws->body = parse_expr(b);
    }
    return (HulkNode*)ws;
}

// ============================================================
//  Parse: ForStmt
//  FOR LPAREN IDENT IN Expr RPAREN ForBody
// ============================================================

static HulkNode* parse_for_stmt(ASTBuilder *b) {
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
//  Parse: BlockStmt
//  LBRACE StmtList RBRACE
//  StmtList → TerminatedStmt StmtList | ε
// ============================================================

static HulkNode* parse_block_stmt(ASTBuilder *b) {
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

// ============================================================
//  Parse: OrExpr / AndExpr  (left-assoc loops)
// ============================================================

static HulkNode* parse_or_expr(ASTBuilder *b) {
    HulkNode *left = parse_and_expr(b);
    while (check(b, TOKEN_OR)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *right = parse_and_expr(b);
        left = (HulkNode*)hulk_ast_binary_op(b->ctx, OP_OR, left, right, line, col);
    }
    return left;
}

static HulkNode* parse_and_expr(ASTBuilder *b) {
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
//  Parse: CmpExpr  (comparaciones + IS)
// ============================================================

static HulkNode* parse_cmp_expr(ASTBuilder *b) {
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
//  Parse: ConcatExpr  (@ y @@)
// ============================================================

static HulkNode* parse_concat_expr(ASTBuilder *b) {
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
//  Parse: AddExpr  (+ -)
// ============================================================

static HulkNode* parse_add_expr(ASTBuilder *b) {
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
//  Parse: Term  (* / %)
// ============================================================

static HulkNode* parse_term(ASTBuilder *b) {
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
//  Parse: Factor  (**)  — DERECHA-asociativo
// ============================================================

static HulkNode* parse_factor(ASTBuilder *b) {
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
//  Parse: Unary
//  Unary → MINUS Unary | Primary AsExpr
// ============================================================

static HulkNode* parse_unary(ASTBuilder *b) {
    if (check(b, TOKEN_MINUS)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        HulkNode *operand = parse_unary(b);
        return (HulkNode*)hulk_ast_unary_op(b->ctx, operand, line, col);
    }
    HulkNode *node = parse_primary(b);
    return parse_as_chain(b, node);
}

// AsExpr → AS IDENT AsExpr | ε
static HulkNode* parse_as_chain(ASTBuilder *b, HulkNode *left) {
    while (check(b, TOKEN_AS)) {
        int line = cur_line(b), col = cur_col(b);
        advance(b);
        char *type_name = expect_ident(b);
        left = (HulkNode*)hulk_ast_as_expr(b->ctx, left, type_name, line, col);
    }
    return left;
}

// ============================================================
//  Parse: Primary
// ============================================================

static HulkNode* parse_primary(ASTBuilder *b) {
    int line = cur_line(b), col = cur_col(b);

    // NUMBER
    if (check(b, TOKEN_NUMBER)) {
        char *raw = save_lexeme(b);
        advance(b);
        return (HulkNode*)hulk_ast_number_lit(b->ctx, raw, line, col);
    }

    // STRING (strip quotes)
    if (check(b, TOKEN_STRING)) {
        char *lexeme = b->current.lexeme;
        // El lexer incluye las comillas; extraer el contenido
        int len = b->current.length;
        char *content = hulk_ast_alloc(b->ctx, len); // len incluye comillas
        if (len >= 2) {
            memcpy(content, lexeme + 1, len - 2);
            content[len - 2] = '\0';
        } else {
            content[0] = '\0';
        }
        advance(b);
        StringLitNode *s = hulk_ast_string_lit(b->ctx, content, line, col);
        // string_lit ya hizo strdup; pero content ya está en arena, safe
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

    // LPAREN Expr RPAREN
    if (check(b, TOKEN_LPAREN)) {
        advance(b);
        HulkNode *expr = parse_expr(b);
        expect(b, TOKEN_RPAREN);
        return expr;
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

// ============================================================
//  Parse: PrimaryTail  (calls, member access, assign)
//  PrimaryTail → LPAREN ArgList RPAREN PrimaryTail
//              | DOT IDENT PrimaryTail
//              | ASSIGN_DESTRUCT Expr
//              | ASSIGN Expr
//              | ε
// ============================================================

static HulkNode* parse_primary_tail(ASTBuilder *b, HulkNode *left) {
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
//  Parse: ArgList / ArgIdList
// ============================================================

// ArgList → Expr (COMMA Expr)* | ε
static void parse_arg_list(ASTBuilder *b, HulkNodeList *out) {
    // ε: si el siguiente token cierra la lista
    if (check(b, TOKEN_RPAREN)) return;

    hulk_node_list_push(out, parse_expr(b));
    while (match(b, TOKEN_COMMA)) {
        hulk_node_list_push(out, parse_expr(b));
    }
}

// ArgIdList → ArgId (COMMA ArgId)* | ε
// ArgId → IDENT TypeAnnotation
static void parse_arg_id_list(ASTBuilder *b, HulkNodeList *out) {
    if (!check(b, TOKEN_IDENT)) return;  // ε

    do {
        int line = cur_line(b), col = cur_col(b);
        char *pname = expect_ident(b);
        char *ptype = parse_type_annotation(b);
        VarBindingNode *vb = hulk_ast_var_binding(b->ctx, pname, ptype, line, col);
        hulk_node_list_push(out, (HulkNode*)vb);
    } while (match(b, TOKEN_COMMA));
}

// TypeAnnotation → COLON IDENT | ε
static char* parse_type_annotation(ASTBuilder *b) {
    if (check(b, TOKEN_COLON)) {
        advance(b);
        return expect_ident(b);
    }
    return NULL;
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
