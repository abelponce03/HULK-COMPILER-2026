/*
 * hulk_ast_printer.c — Visitor que imprime el AST como árbol indentado
 *
 * Cada callback imprime el nodo actual con indentación y luego
 * recursivamente visita sus hijos incrementando la profundidad.
 */

#include "hulk_ast_printer.h"

// Datos compartidos entre callbacks
typedef struct {
    FILE *out;
    int depth;
} PrinterData;

// ============== HELPERS ==============

static void indent(PrinterData *d) {
    for (int i = 0; i < d->depth; i++)
        fprintf(d->out, "  ");
}

static void print_children(HulkNodeList *list, HulkASTVisitor *v, PrinterData *d) {
    d->depth++;
    hulk_ast_accept_list(list, v, d);
    d->depth--;
}

static void print_child(HulkNode *child, HulkASTVisitor *v, PrinterData *d) {
    d->depth++;
    hulk_ast_accept(child, v, d);
    d->depth--;
}

// ============== CALLBACKS POR TIPO DE NODO ==============

static void visit_program(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    ProgramNode *p = (ProgramNode*)n;
    indent(d); fprintf(d->out, "Program [%d:%d]\n", n->line, n->col);
    print_children(&p->declarations, v, d);
}

static void visit_function_def(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    FunctionDefNode *f = (FunctionDefNode*)n;
    indent(d); fprintf(d->out, "FunctionDef '%s'", f->name);
    if (f->return_type) fprintf(d->out, " : %s", f->return_type);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (f->params.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Params:\n");
        print_children(&f->params, v, d);
        d->depth--;
    }
    if (f->body) {
        d->depth++;
        indent(d); fprintf(d->out, "Body:\n");
        print_child(f->body, v, d);
        d->depth--;
    }
}

static void visit_function_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    FunctionExprNode *f = (FunctionExprNode*)n;
    indent(d); fprintf(d->out, "FunctionExpr");
    if (f->return_type) fprintf(d->out, " : %s", f->return_type);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (f->params.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Params:\n");
        print_children(&f->params, v, d);
        d->depth--;
    }
    if (f->captures.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Captures:\n");
        print_children(&f->captures, v, d);
        d->depth--;
    }
    if (f->body) {
        d->depth++;
        indent(d); fprintf(d->out, "Body:\n");
        print_child(f->body, v, d);
        d->depth--;
    }
}

static void visit_type_def(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    TypeDefNode *t = (TypeDefNode*)n;
    indent(d); fprintf(d->out, "TypeDef '%s'", t->name);
    if (t->parent) fprintf(d->out, " inherits %s", t->parent);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (t->params.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Params:\n");
        print_children(&t->params, v, d);
        d->depth--;
    }
    if (t->members.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Members:\n");
        print_children(&t->members, v, d);
        d->depth--;
    }
}

static void visit_method_def(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    MethodDefNode *m = (MethodDefNode*)n;
    indent(d); fprintf(d->out, "MethodDef '%s'", m->name);
    if (m->return_type) fprintf(d->out, " : %s", m->return_type);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (m->params.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Params:\n");
        print_children(&m->params, v, d);
        d->depth--;
    }
    if (m->decorators.count > 0) {
        d->depth++;
        indent(d); fprintf(d->out, "Decorators:\n");
        print_children(&m->decorators, v, d);
        d->depth--;
    }
    if (m->body) {
        d->depth++;
        indent(d); fprintf(d->out, "Body:\n");
        print_child(m->body, v, d);
        d->depth--;
    }
}

static void visit_attribute_def(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    AttributeDefNode *a = (AttributeDefNode*)n;
    indent(d); fprintf(d->out, "AttributeDef '%s'", a->name);
    if (a->type_annotation) fprintf(d->out, " : %s", a->type_annotation);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (a->init_expr) print_child(a->init_expr, v, d);
}

static void visit_let_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    LetExprNode *l = (LetExprNode*)n;
    indent(d); fprintf(d->out, "LetExpr [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Bindings:\n");
    print_children(&l->bindings, v, d);
    if (l->body) {
        indent(d); fprintf(d->out, "In:\n");
        print_child(l->body, v, d);
    }
    d->depth--;
}

static void visit_var_binding(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    VarBindingNode *vb = (VarBindingNode*)n;
    indent(d); fprintf(d->out, "VarBinding '%s'", vb->name);
    if (vb->type_annotation) fprintf(d->out, " : %s", vb->type_annotation);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (vb->init_expr) print_child(vb->init_expr, v, d);
}

static void visit_if_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    IfExprNode *i = (IfExprNode*)n;
    indent(d); fprintf(d->out, "IfExpr [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Condition:\n");
    print_child(i->condition, v, d);
    indent(d); fprintf(d->out, "Then:\n");
    print_child(i->then_body, v, d);
    if (i->elifs.count > 0) {
        indent(d); fprintf(d->out, "Elifs:\n");
        print_children(&i->elifs, v, d);
    }
    indent(d); fprintf(d->out, "Else:\n");
    print_child(i->else_body, v, d);
    d->depth--;
}

static void visit_elif_branch(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    ElifBranchNode *e = (ElifBranchNode*)n;
    indent(d); fprintf(d->out, "Elif [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Condition:\n");
    print_child(e->condition, v, d);
    indent(d); fprintf(d->out, "Body:\n");
    print_child(e->body, v, d);
    d->depth--;
}

static void visit_while_stmt(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    WhileStmtNode *w = (WhileStmtNode*)n;
    indent(d); fprintf(d->out, "WhileStmt [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Condition:\n");
    print_child(w->condition, v, d);
    indent(d); fprintf(d->out, "Body:\n");
    print_child(w->body, v, d);
    d->depth--;
}

static void visit_for_stmt(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    ForStmtNode *f = (ForStmtNode*)n;
    indent(d); fprintf(d->out, "ForStmt '%s' [%d:%d]\n", f->var_name, n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Iterable:\n");
    print_child(f->iterable, v, d);
    indent(d); fprintf(d->out, "Body:\n");
    print_child(f->body, v, d);
    d->depth--;
}

static void visit_block_stmt(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    BlockStmtNode *b = (BlockStmtNode*)n;
    indent(d); fprintf(d->out, "BlockStmt [%d:%d]\n", n->line, n->col);
    print_children(&b->statements, v, d);
}

static void visit_binary_op(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    BinaryOpNode *b = (BinaryOpNode*)n;
    indent(d); fprintf(d->out, "BinaryOp '%s' [%d:%d]\n",
                       hulk_binary_op_name(b->op), n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Left:\n");
    print_child(b->left, v, d);
    indent(d); fprintf(d->out, "Right:\n");
    print_child(b->right, v, d);
    d->depth--;
}

static void visit_unary_op(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    UnaryOpNode *u = (UnaryOpNode*)n;
    indent(d); fprintf(d->out, "UnaryOp '-' [%d:%d]\n", n->line, n->col);
    print_child(u->operand, v, d);
}

static void visit_number_lit(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)v;
    PrinterData *d = data;
    NumberLitNode *num = (NumberLitNode*)n;
    indent(d); fprintf(d->out, "NumberLit %s [%d:%d]\n", num->raw, n->line, n->col);
}

static void visit_string_lit(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)v;
    PrinterData *d = data;
    StringLitNode *s = (StringLitNode*)n;
    indent(d); fprintf(d->out, "StringLit \"%s\" [%d:%d]\n", s->value, n->line, n->col);
}

static void visit_bool_lit(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)v;
    PrinterData *d = data;
    BoolLitNode *b = (BoolLitNode*)n;
    indent(d); fprintf(d->out, "BoolLit %s [%d:%d]\n",
                       b->value ? "true" : "false", n->line, n->col);
}

static void visit_ident(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)v;
    PrinterData *d = data;
    IdentNode *id = (IdentNode*)n;
    indent(d); fprintf(d->out, "Ident '%s' [%d:%d]\n", id->name, n->line, n->col);
}

static void visit_call_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    CallExprNode *c = (CallExprNode*)n;
    indent(d); fprintf(d->out, "CallExpr [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Callee:\n");
    print_child(c->callee, v, d);
    if (c->args.count > 0) {
        indent(d); fprintf(d->out, "Args:\n");
        print_children(&c->args, v, d);
    }
    d->depth--;
}

static void visit_member_access(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    MemberAccessNode *m = (MemberAccessNode*)n;
    indent(d); fprintf(d->out, "MemberAccess '.%s' [%d:%d]\n", m->member, n->line, n->col);
    print_child(m->object, v, d);
}

static void visit_new_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    NewExprNode *ne = (NewExprNode*)n;
    indent(d); fprintf(d->out, "NewExpr '%s' [%d:%d]\n", ne->type_name, n->line, n->col);
    if (ne->args.count > 0) print_children(&ne->args, v, d);
}

static void visit_assign(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    AssignNode *a = (AssignNode*)n;
    indent(d); fprintf(d->out, "Assign '=' [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Target:\n");
    print_child(a->target, v, d);
    indent(d); fprintf(d->out, "Value:\n");
    print_child(a->value, v, d);
    d->depth--;
}

static void visit_destruct_assign(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    DestructAssignNode *a = (DestructAssignNode*)n;
    indent(d); fprintf(d->out, "DestructAssign ':=' [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Target:\n");
    print_child(a->target, v, d);
    indent(d); fprintf(d->out, "Value:\n");
    print_child(a->value, v, d);
    d->depth--;
}

static void visit_as_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    AsExprNode *a = (AsExprNode*)n;
    indent(d); fprintf(d->out, "AsExpr 'as %s' [%d:%d]\n", a->type_name, n->line, n->col);
    print_child(a->expr, v, d);
}

static void visit_is_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    IsExprNode *i = (IsExprNode*)n;
    indent(d); fprintf(d->out, "IsExpr 'is %s' [%d:%d]\n", i->type_name, n->line, n->col);
    print_child(i->expr, v, d);
}

static void visit_self(HulkNode *n, HulkASTVisitor *v, void *data) {
    (void)v;
    PrinterData *d = data;
    indent(d); fprintf(d->out, "Self [%d:%d]\n", n->line, n->col);
}

static void visit_base_call(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    BaseCallNode *b = (BaseCallNode*)n;
    indent(d); fprintf(d->out, "BaseCall [%d:%d]\n", n->line, n->col);
    if (b->args.count > 0) print_children(&b->args, v, d);
}

static void visit_decor_block(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    DecorBlockNode *db = (DecorBlockNode*)n;
    indent(d); fprintf(d->out, "DecorBlock [%d:%d]\n", n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Decorators:\n");
    print_children(&db->decorators, v, d);
    if (db->target) {
        indent(d); fprintf(d->out, "Target:\n");
        print_child(db->target, v, d);
    }
    d->depth--;
}

static void visit_decor_item(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    DecorItemNode *di = (DecorItemNode*)n;
    indent(d); fprintf(d->out, "DecorItem '%s'", di->name);
    if (di->args.count > 0) fprintf(d->out, "(%d args)", di->args.count);
    fprintf(d->out, " [%d:%d]\n", n->line, n->col);
    if (di->args.count > 0) print_children(&di->args, v, d);
}

static void visit_concat_expr(HulkNode *n, HulkASTVisitor *v, void *data) {
    PrinterData *d = data;
    ConcatExprNode *c = (ConcatExprNode*)n;
    indent(d); fprintf(d->out, "ConcatExpr '%s' [%d:%d]\n",
                       hulk_binary_op_name(c->op), n->line, n->col);
    d->depth++;
    indent(d); fprintf(d->out, "Left:\n");
    print_child(c->left, v, d);
    indent(d); fprintf(d->out, "Right:\n");
    print_child(c->right, v, d);
    d->depth--;
}

// ============== API PÚBLICA ==============

void hulk_ast_print(HulkNode *root, FILE *out) {
    if (!root || !out) return;

    HulkASTVisitor v;
    hulk_visitor_init(&v);

    v.visit[NODE_PROGRAM]          = visit_program;
    v.visit[NODE_FUNCTION_DEF]     = visit_function_def;
    v.visit[NODE_FUNCTION_EXPR]    = visit_function_expr;
    v.visit[NODE_TYPE_DEF]         = visit_type_def;
    v.visit[NODE_METHOD_DEF]       = visit_method_def;
    v.visit[NODE_ATTRIBUTE_DEF]    = visit_attribute_def;
    v.visit[NODE_LET_EXPR]         = visit_let_expr;
    v.visit[NODE_VAR_BINDING]      = visit_var_binding;
    v.visit[NODE_IF_EXPR]          = visit_if_expr;
    v.visit[NODE_ELIF_BRANCH]      = visit_elif_branch;
    v.visit[NODE_WHILE_STMT]       = visit_while_stmt;
    v.visit[NODE_FOR_STMT]         = visit_for_stmt;
    v.visit[NODE_BLOCK_STMT]       = visit_block_stmt;
    v.visit[NODE_BINARY_OP]        = visit_binary_op;
    v.visit[NODE_UNARY_OP]         = visit_unary_op;
    v.visit[NODE_NUMBER_LIT]       = visit_number_lit;
    v.visit[NODE_STRING_LIT]       = visit_string_lit;
    v.visit[NODE_BOOL_LIT]         = visit_bool_lit;
    v.visit[NODE_IDENT]            = visit_ident;
    v.visit[NODE_CALL_EXPR]        = visit_call_expr;
    v.visit[NODE_MEMBER_ACCESS]    = visit_member_access;
    v.visit[NODE_NEW_EXPR]         = visit_new_expr;
    v.visit[NODE_ASSIGN]           = visit_assign;
    v.visit[NODE_DESTRUCT_ASSIGN]  = visit_destruct_assign;
    v.visit[NODE_AS_EXPR]          = visit_as_expr;
    v.visit[NODE_IS_EXPR]          = visit_is_expr;
    v.visit[NODE_SELF]             = visit_self;
    v.visit[NODE_BASE_CALL]        = visit_base_call;
    v.visit[NODE_DECOR_BLOCK]      = visit_decor_block;
    v.visit[NODE_DECOR_ITEM]       = visit_decor_item;
    v.visit[NODE_CONCAT_EXPR]      = visit_concat_expr;

    PrinterData d = { .out = out, .depth = 0 };
    hulk_ast_accept(root, &v, &d);
}
