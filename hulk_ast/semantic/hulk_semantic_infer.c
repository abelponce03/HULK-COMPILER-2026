/*
 * hulk_semantic_infer.c — Inferencia ad-hoc de tipos (frontend)
 *
 * HULK permite omitir anotaciones de tipo; el chequeador necesita igual
 * un tipo para cada símbolo. Estas heurísticas sintácticas examinan los
 * cuerpos para inferirlo (spec A.9.4):
 *   - sem_infer_param_type: tipo de un parámetro según su uso.
 *   - sem_infer_self_member_type: tipo de un atributo/param de tipo según
 *     el uso de self.x en los métodos.
 *   - sem_body_calls_name: detecta recursión (para el default de retorno).
 */
#include "hulk_semantic_internal.h"

/* ============================================================
 *  Inferencia ad-hoc del tipo de un parámetro
 *
 *  Walker sintáctico que escanea el body buscando ocurrencias del
 *  identificador `param_name` en contextos que disambiguan el tipo:
 *    - Operandos de + - * / ^ % < > <= >=      → Number
 *    - Operandos de & | !                       → Boolean
 *  Si ninguna ocurrencia revela tipo, retorna NULL (caller deja Object).
 * ============================================================ */

typedef enum { INF_NONE = 0, INF_NUMBER, INF_BOOLEAN, INF_STRING } InferTag;

static InferTag join_inf(InferTag a, InferTag b) {
    if (a == b) return a;
    if (a == INF_NONE) return b;
    if (b == INF_NONE) return a;
    return INF_NUMBER;  /* Number gana en conflicto numérico */
}

static int is_param_ident(HulkNode *n, const char *name) {
    return n && n->type == NODE_IDENT &&
           strcmp(((IdentNode*)n)->name, name) == 0;
}

static InferTag walk_infer(HulkNode *n, const char *name) {
    if (!n) return INF_NONE;
    switch (n->type) {
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            InferTag here = INF_NONE;
            switch (b->op) {
                case OP_ADD: case OP_SUB: case OP_MUL:
                case OP_DIV: case OP_MOD: case OP_POW:
                case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                    if (is_param_ident(b->left, name) ||
                        is_param_ident(b->right, name))
                        here = INF_NUMBER;
                    break;
                case OP_AND: case OP_OR:
                    if (is_param_ident(b->left, name) ||
                        is_param_ident(b->right, name))
                        here = INF_BOOLEAN;
                    break;
                default: break;
            }
            return join_inf(here,
                join_inf(walk_infer(b->left, name),
                          walk_infer(b->right, name)));
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)n;
            InferTag here = INF_NONE;
            if (is_param_ident(u->operand, name)) here = INF_NUMBER;
            return join_inf(here, walk_infer(u->operand, name));
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            InferTag a = walk_infer(iff->condition, name);
            a = join_inf(a, walk_infer(iff->then_body, name));
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                a = join_inf(a, walk_infer(e->condition, name));
                a = join_inf(a, walk_infer(e->body, name));
            }
            a = join_inf(a, walk_infer(iff->else_body, name));
            return a;
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return join_inf(walk_infer(w->condition, name),
                             walk_infer(w->body, name));
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)n;
            return join_inf(walk_infer(f->iterable, name),
                             walk_infer(f->body, name));
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < b->statements.count; i++)
                a = join_inf(a, walk_infer(b->statements.items[i], name));
            return a;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                a = join_inf(a, walk_infer(vb->init_expr, name));
            }
            a = join_inf(a, walk_infer(l->body, name));
            return a;
        }
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < ce->args.count; i++)
                a = join_inf(a, walk_infer(ce->args.items[i], name));
            return a;
        }
        case NODE_ASSIGN: {
            AssignNode *as = (AssignNode*)n;
            return walk_infer(as->value, name);
        }
        case NODE_DESTRUCT_ASSIGN: {
            DestructAssignNode *as = (DestructAssignNode*)n;
            return walk_infer(as->value, name);
        }
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            return join_inf(walk_infer(ce->left, name),
                             walk_infer(ce->right, name));
        }
        default: return INF_NONE;
    }
}

HulkType* sem_infer_param_type(SemanticContext *c, const char *param_name,
                                HulkNode *body) {
    if (!param_name || !body) return NULL;
    switch (walk_infer(body, param_name)) {
        case INF_NUMBER:  return c->t_number;
        case INF_BOOLEAN: return c->t_boolean;
        default:          return NULL;
    }
}

/* ============================================================
 *  Inferencia de attrs/params del tipo a partir del uso de self.X
 *
 *  Walker que recorre los método-bodies del tipo y, si encuentra
 *  `self.X` como operando de + - * / ^ % < > <= >=, retorna Number.
 * ============================================================ */

static int is_self_dot(HulkNode *n, const char *member) {
    if (!n || n->type != NODE_MEMBER_ACCESS) return 0;
    MemberAccessNode *ma = (MemberAccessNode*)n;
    if (!ma->object || ma->object->type != NODE_SELF) return 0;
    return ma->member && strcmp(ma->member, member) == 0;
}

static InferTag walk_self_member(HulkNode *n, const char *member) {
    if (!n) return INF_NONE;
    switch (n->type) {
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            InferTag here = INF_NONE;
            switch (b->op) {
                case OP_ADD: case OP_SUB: case OP_MUL:
                case OP_DIV: case OP_MOD: case OP_POW:
                case OP_LT: case OP_GT: case OP_LE: case OP_GE:
                    if (is_self_dot(b->left, member) ||
                        is_self_dot(b->right, member))
                        here = INF_NUMBER;
                    break;
                case OP_AND: case OP_OR:
                    if (is_self_dot(b->left, member) ||
                        is_self_dot(b->right, member))
                        here = INF_BOOLEAN;
                    break;
                default: break;
            }
            return join_inf(here,
                join_inf(walk_self_member(b->left, member),
                          walk_self_member(b->right, member)));
        }
        case NODE_UNARY_OP: {
            UnaryOpNode *u = (UnaryOpNode*)n;
            InferTag here = INF_NONE;
            if (is_self_dot(u->operand, member)) here = INF_NUMBER;
            return join_inf(here, walk_self_member(u->operand, member));
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            InferTag a = walk_self_member(iff->condition, member);
            a = join_inf(a, walk_self_member(iff->then_body, member));
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                a = join_inf(a, walk_self_member(e->condition, member));
                a = join_inf(a, walk_self_member(e->body, member));
            }
            a = join_inf(a, walk_self_member(iff->else_body, member));
            return a;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < b->statements.count; i++)
                a = join_inf(a, walk_self_member(b->statements.items[i], member));
            return a;
        }
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < ce->args.count; i++)
                a = join_inf(a, walk_self_member(ce->args.items[i], member));
            return a;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            InferTag a = INF_NONE;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                a = join_inf(a, walk_self_member(vb->init_expr, member));
            }
            return join_inf(a, walk_self_member(l->body, member));
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return join_inf(walk_self_member(w->condition, member),
                             walk_self_member(w->body, member));
        }
        case NODE_DESTRUCT_ASSIGN:
            return walk_self_member(((DestructAssignNode*)n)->value, member);
        case NODE_ASSIGN:
            return walk_self_member(((AssignNode*)n)->value, member);
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            InferTag here = INF_NONE;
            if (is_self_dot(ce->left, member) ||
                is_self_dot(ce->right, member))
                here = INF_STRING;
            return join_inf(here,
                join_inf(walk_self_member(ce->left, member),
                          walk_self_member(ce->right, member)));
        }
        default: return INF_NONE;
    }
}

HulkType* sem_infer_self_member_type(SemanticContext *c, const char *member,
                                      TypeDefNode *td) {
    if (!member || !td) return NULL;
    InferTag agg = INF_NONE;
    for (int i = 0; i < td->members.count; i++) {
        HulkNode *m = td->members.items[i];
        if (m->type != NODE_METHOD_DEF) continue;
        agg = join_inf(agg, walk_self_member(
            ((MethodDefNode*)m)->body, member));
    }
    switch (agg) {
        case INF_NUMBER:  return c->t_number;
        case INF_BOOLEAN: return c->t_boolean;
        case INF_STRING:  return c->t_string;
        default:          return NULL;
    }
}

/* Detecta si el ident `name` aparece como callee en el body — i.e., si
 * la función es recursiva. Walker sintáctico simple. */
int sem_body_calls_name(HulkNode *n, const char *name) {
    if (!n || !name) return 0;
    switch (n->type) {
        case NODE_CALL_EXPR: {
            CallExprNode *ce = (CallExprNode*)n;
            if (ce->callee && ce->callee->type == NODE_IDENT) {
                IdentNode *idn = (IdentNode*)ce->callee;
                if (idn->name && strcmp(idn->name, name) == 0) return 1;
            }
            if (sem_body_calls_name(ce->callee, name)) return 1;
            for (int i = 0; i < ce->args.count; i++)
                if (sem_body_calls_name(ce->args.items[i], name)) return 1;
            return 0;
        }
        case NODE_BINARY_OP: {
            BinaryOpNode *b = (BinaryOpNode*)n;
            return sem_body_calls_name(b->left, name) ||
                   sem_body_calls_name(b->right, name);
        }
        case NODE_UNARY_OP:
            return sem_body_calls_name(((UnaryOpNode*)n)->operand, name);
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)n;
            if (sem_body_calls_name(iff->condition, name)) return 1;
            if (sem_body_calls_name(iff->then_body, name)) return 1;
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                if (sem_body_calls_name(e->condition, name)) return 1;
                if (sem_body_calls_name(e->body, name)) return 1;
            }
            return sem_body_calls_name(iff->else_body, name);
        }
        case NODE_WHILE_STMT: {
            WhileStmtNode *w = (WhileStmtNode*)n;
            return sem_body_calls_name(w->condition, name) ||
                   sem_body_calls_name(w->body, name);
        }
        case NODE_FOR_STMT: {
            ForStmtNode *f = (ForStmtNode*)n;
            return sem_body_calls_name(f->iterable, name) ||
                   sem_body_calls_name(f->body, name);
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)n;
            for (int i = 0; i < b->statements.count; i++)
                if (sem_body_calls_name(b->statements.items[i], name)) return 1;
            return 0;
        }
        case NODE_LET_EXPR: {
            LetExprNode *l = (LetExprNode*)n;
            for (int i = 0; i < l->bindings.count; i++) {
                VarBindingNode *vb = (VarBindingNode*)l->bindings.items[i];
                if (sem_body_calls_name(vb->init_expr, name)) return 1;
            }
            return sem_body_calls_name(l->body, name);
        }
        case NODE_ASSIGN:
            return sem_body_calls_name(((AssignNode*)n)->value, name);
        case NODE_DESTRUCT_ASSIGN:
            return sem_body_calls_name(((DestructAssignNode*)n)->value, name);
        case NODE_CONCAT_EXPR: {
            ConcatExprNode *ce = (ConcatExprNode*)n;
            return sem_body_calls_name(ce->left, name) ||
                   sem_body_calls_name(ce->right, name);
        }
        default: return 0;
    }
}
