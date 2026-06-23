/*
 * hulk_codegen_oop.c — Emisión de la parte orientada a objetos
 *
 * Acceso a atributos (cg_emit_member_access), instanciación
 * (cg_emit_new), self, asignación destructiva sobre miembros
 * (cg_emit_assign/cg_emit_destruct) y la inferencia del tipo HULK
 * estático de una expresión (cg_static_type_of + cg_type_lca), base
 * del dispatch de métodos y del acceso a campos.
 */
#include "hulk_codegen_internal.h"

static CGTypeInfo* cg_type_lca(CGTypeInfo *a, CGTypeInfo *b) {
    if (!a) return b;
    if (!b) return a;
    if (a == b) return a;
    /* Si a desciende de b */
    for (CGTypeInfo *t = a; t; t = t->parent)
        if (t == b) return b;
    /* Si b desciende de a */
    for (CGTypeInfo *t = b; t; t = t->parent)
        if (t == a) return a;
    /* Subir por a buscando un ancestro común con b */
    for (CGTypeInfo *ta = a->parent; ta; ta = ta->parent) {
        for (CGTypeInfo *tb = b; tb; tb = tb->parent)
            if (ta == tb) return ta;
    }
    return NULL;
}

CGTypeInfo* cg_static_type_of(CodegenContext *c, HulkNode *expr) {
    if (!expr) return NULL;

    /* Camino canónico: el análisis semántico ya anotó el nodo con el
     * nombre de su tipo estático. Si nombra un tipo de usuario conocido,
     * esa es la respuesta autoritativa (el semántico ya resolvió join de
     * ramas y herencia mejor que la derivación sintáctica de abajo). */
    if (expr->static_type) {
        CGTypeInfo *ti = cg_type_info_find(c, expr->static_type);
        if (ti) return ti;
    }

    /* Fallback sintáctico: si no se corrió el semántico, o el tipo
     * anotado es primitivo/función (sin CGTypeInfo asociado). */
    switch (expr->type) {
        case NODE_SELF:
            return c->enclosing_type;
        case NODE_IDENT: {
            IdentNode *id = (IdentNode*)expr;
            CGSymbol *sym = cg_lookup(c->current, id->name);
            return sym ? sym->hulk_type : NULL;
        }
        case NODE_NEW_EXPR: {
            NewExprNode *ne = (NewExprNode*)expr;
            return cg_type_info_find(c, ne->type_name);
        }
        case NODE_AS_EXPR: {
            AsExprNode *ae = (AsExprNode*)expr;
            return cg_type_info_find(c, ae->type_name);
        }
        case NODE_IF_EXPR: {
            IfExprNode *iff = (IfExprNode*)expr;
            CGTypeInfo *agg = cg_static_type_of(c, iff->then_body);
            for (int i = 0; i < iff->elifs.count; i++) {
                ElifBranchNode *e = (ElifBranchNode*)iff->elifs.items[i];
                agg = cg_type_lca(agg, cg_static_type_of(c, e->body));
            }
            if (iff->else_body)
                agg = cg_type_lca(agg, cg_static_type_of(c, iff->else_body));
            return agg;
        }
        case NODE_BLOCK_STMT: {
            BlockStmtNode *b = (BlockStmtNode*)expr;
            if (b->statements.count == 0) return NULL;
            return cg_static_type_of(c,
                b->statements.items[b->statements.count - 1]);
        }
        case NODE_LET_EXPR:
            return cg_static_type_of(c, ((LetExprNode*)expr)->body);
        default: return NULL;
    }
}

LLVMValueRef cg_emit_member_access(CodegenContext *c, MemberAccessNode *n) {
    LLVMValueRef obj = cg_emit_expr(c, n->object);

    /* Determinar el CGTypeInfo del receiver de forma estática. */
    CGTypeInfo *ti = cg_static_type_of(c, n->object);
    if (!ti && c->enclosing_type && obj == c->self_ptr)
        ti = c->enclosing_type;

    /* Buscar campo en la jerarquía: como nuestro layout incluye los
     * fields del padre al inicio, ti->field_names tiene todos. */
    if (ti) {
        for (CGTypeInfo *cur = ti; cur; cur = cur->parent) {
            for (int f = 0; f < cur->field_count; f++) {
                if (strcmp(cur->field_names[f], n->member) == 0) {
                    LLVMValueRef target_obj = obj;
                    if (cur != ti)
                        target_obj = LLVMBuildBitCast(c->builder, obj,
                                                      cur->ptr_type, "upcast");
                    LLVMValueRef gep = LLVMBuildStructGEP2(
                        c->builder, cur->struct_type, target_obj, f, n->member);
                    LLVMTypeRef field_t = LLVMStructGetTypeAtIndex(
                        cur->struct_type, f);
                    return LLVMBuildLoad2(c->builder, field_t, gep, "field");
                }
            }
        }
    }
    cg_error(c, (HulkNode*)n, "campo '%s' no encontrado", n->member);
    return LLVMConstReal(c->t_double, 0.0);
}

LLVMValueRef cg_emit_new(CodegenContext *c, NewExprNode *n) {
    /* Buscar constructor generado: TypeName_new */
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->type_name);
    CGSymbol *ctor_sym = cg_lookup(c->global, ctor_name);

    if (ctor_sym && ctor_sym->value && LLVMIsAFunction(ctor_sym->value)) {
        /* Llamar al constructor generado */
        int argc = n->args.count;
        LLVMValueRef *argv = calloc(argc > 0 ? argc : 1, sizeof(LLVMValueRef));
        for (int i = 0; i < argc; i++)
            argv[i] = cg_emit_expr(c, n->args.items[i]);

        LLVMTypeRef fn_type = LLVMGlobalGetValueType(ctor_sym->value);
        LLVMValueRef result = LLVMBuildCall2(
            c->builder, fn_type, ctor_sym->value, argv, argc, "new");
        free(argv);
        return result;
    }

    /* Fallback: tipo no tiene constructor generado — malloc + init campos */
    CGTypeInfo *ti = cg_type_info_find(c, n->type_name);
    if (!ti) {
        cg_error(c, (HulkNode*)n, "tipo '%s' no registrado", n->type_name);
        return LLVMConstNull(c->t_i8ptr);
    }

    LLVMValueRef size = LLVMSizeOf(ti->struct_type);
    LLVMTypeRef malloc_params[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
    LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
    LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                       c->fn_malloc, &size, 1, "raw");
    LLVMValueRef obj = LLVMBuildBitCast(c->builder, raw, ti->ptr_type, "obj");

    int arg_count = n->args.count < ti->field_count
                    ? n->args.count : ti->field_count;
    for (int i = 0; i < arg_count; i++) {
        LLVMValueRef val = cg_emit_expr(c, n->args.items[i]);
        LLVMValueRef gep = LLVMBuildStructGEP2(
            c->builder, ti->struct_type, obj, i, "field.ptr");
        LLVMBuildStore(c->builder, val, gep);
    }
    return obj;
}

LLVMValueRef cg_emit_assign(CodegenContext *c, AssignNode *n) {
    LLVMValueRef val = cg_emit_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (sym && !sym->is_func)
            LLVMBuildStore(c->builder, val, sym->value);
        else
            cg_error(c, (HulkNode*)n, "no se puede asignar a '%s'", id->name);
    }
    return val;
}

LLVMValueRef cg_emit_destruct(CodegenContext *c, DestructAssignNode *n) {
    LLVMValueRef val = cg_emit_expr(c, n->value);
    if (n->target->type == NODE_IDENT) {
        IdentNode *id = (IdentNode*)n->target;
        CGSymbol *sym = cg_lookup(c->current, id->name);
        if (sym) {
            if (sym->is_func) {
                /* Reasignar función (decoradores) */
                sym->value   = val;
                sym->is_func = LLVMIsAFunction(val) ? 1 : 0;
            } else {
                LLVMBuildStore(c->builder, val, sym->value);
            }
        }
    } else if (n->target->type == NODE_INDEX_EXPR) {
        IndexExprNode *ix = (IndexExprNode*)n->target;
        LLVMValueRef obj = cg_emit_expr(c, ix->object);
        LLVMValueRef idx_d = cg_emit_expr(c, ix->index);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMValueRef idx_i = LLVMBuildFPToSI(c->builder, idx_d, i64, "idx.set");
        LLVMValueRef offset = LLVMBuildAdd(
            c->builder,
            LLVMBuildMul(c->builder, idx_i, LLVMConstInt(i64, 8, 0), "ofs.set.mul"),
            LLVMConstInt(i64, 8, 0),
            "ofs.set");
        LLVMValueRef ptr = LLVMBuildInBoundsGEP2(
            c->builder, LLVMInt8TypeInContext(c->llvm_ctx),
            obj, &offset, 1, "elem.set.ptr");
        LLVMBuildStore(c->builder, val, ptr);
    } else if (n->target->type == NODE_MEMBER_ACCESS) {
        /* self.field := val — store en el field del struct */
        MemberAccessNode *ma = (MemberAccessNode*)n->target;
        LLVMValueRef obj = cg_emit_expr(c, ma->object);
        CGTypeInfo *ti = cg_static_type_of(c, ma->object);
        if (!ti && c->enclosing_type && obj == c->self_ptr)
            ti = c->enclosing_type;
        if (ti) {
            for (CGTypeInfo *cur = ti; cur; cur = cur->parent) {
                int fidx = cg_type_field_index(cur, ma->member);
                if (fidx >= 0) {
                    LLVMValueRef target_obj = obj;
                    if (cur != ti)
                        target_obj = LLVMBuildBitCast(c->builder, obj,
                                                      cur->ptr_type, "up");
                    LLVMValueRef gep = LLVMBuildStructGEP2(
                        c->builder, cur->struct_type, target_obj, fidx,
                        "field.set");
                    LLVMBuildStore(c->builder, val, gep);
                    break;
                }
            }
        }
    }
    return val;
}

LLVMValueRef cg_emit_self(CodegenContext *c, SelfNode *n) {
    if (c->self_ptr) return c->self_ptr;
    cg_error(c, (HulkNode*)n, "'self' fuera de un tipo");
    return LLVMConstNull(c->t_i8ptr);
}
