/*
 * hulk_codegen_typedecl.c — Declaración y emisión de tipos de usuario
 *
 * Layout del struct (con tag para RTTI), constructor encadenado
 * (cg_forward_declare_type), emisión de métodos y T_new/T_init
 * (cg_emit_type_def) y la construcción de las vtables + tablas de
 * RTTI globales (cg_emit_rtti_globals) que habilitan el dispatch
 * dinámico y los operadores is/as.
 */
#include "hulk_codegen_internal.h"

void cg_forward_declare_type(CodegenContext *c, TypeDefNode *n) {
    /* Los protocols no tienen representación runtime: solo restringen
     * el typecheck. Se ignoran en codegen. */
    if (n->is_protocol) return;
    /* Crear struct opaco */
    LLVMTypeRef st = LLVMStructCreateNamed(c->llvm_ctx, n->name);
    CGTypeInfo *ti = cg_type_info_create(c, n->name);
    ti->struct_type = st;
    ti->ptr_type = LLVMPointerType(st, 0);

    /* Enlazar con tipo padre si hereda */
    if (n->parent) {
        CGTypeInfo *parent_ti = cg_type_info_find(c, n->parent);
        ti->parent = parent_ti;  /* puede ser NULL si padre aún no declarado */
    }

    /* Layout: { i32 __tag__, padre_fields..., self_fields... }
     * Si no hay padre, "padre_fields" es vacío (sólo el tag inicial). */
    int parent_field_count = ti->parent ? ti->parent->field_count : 1;
    int self_attr_count = 0;
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF)
            self_attr_count++;
    }
    int self_field_count = n->params.count + self_attr_count;
    int total_fields = parent_field_count + self_field_count;
    ti->field_offset_self = parent_field_count;
    ti->field_count = total_fields;

    LLVMTypeRef *field_types = calloc(total_fields, sizeof(LLVMTypeRef));
    ti->field_names = calloc(total_fields, sizeof(const char*));
    ti->field_types_arr = calloc(total_fields, sizeof(LLVMTypeRef));

    if (ti->parent) {
        /* Copiar layout completo del padre */
        for (int i = 0; i < ti->parent->field_count; i++) {
            field_types[i] = ti->parent->field_types_arr[i];
            ti->field_names[i] = ti->parent->field_names[i];
        }
    } else {
        /* Solo el tag */
        field_types[0] = c->t_i32;
        ti->field_names[0] = "__tag__";
    }

    int idx = parent_field_count;
    for (int i = 0; i < n->params.count; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        field_types[idx] = p->type_annotation
            ? cg_infer_param_type(c, p->type_annotation)
            : cg_infer_ctor_param_type(c, n, p->name);
        ti->field_names[idx] = p->name;
        idx++;
    }
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF) {
            AttributeDefNode *a = (AttributeDefNode*)n->members.items[i];
            field_types[idx] = a->type_annotation
                ? cg_infer_param_type(c, a->type_annotation)
                : cg_infer_ctor_param_type(c, n, a->name);
            ti->field_names[idx] = a->name;
            idx++;
        }
    }

    memcpy(ti->field_types_arr, field_types,
           total_fields * sizeof(LLVMTypeRef));
    LLVMStructSetBody(st, field_types, total_fields, 0);
    free(field_types);

    /* Forward-declare T_init(self, params...) -> void
     * (inicializa: llama padre_init si hay, set tag, copia params a fields,
     *  inicializa atributos). Se separa de T_new para encadenar herencia. */
    int param_argc = n->params.count;
    LLVMTypeRef *init_params = calloc(param_argc + 1, sizeof(LLVMTypeRef));
    init_params[0] = ti->ptr_type;
    for (int i = 0; i < param_argc; i++) {
        VarBindingNode *p = (VarBindingNode*)n->params.items[i];
        init_params[i + 1] = p->type_annotation
            ? cg_infer_param_type(c, p->type_annotation)
            : cg_infer_ctor_param_type(c, n, p->name);
    }
    char init_name[256];
    snprintf(init_name, sizeof(init_name), "%s_init", n->name);
    LLVMTypeRef init_ft = LLVMFunctionType(c->t_void, init_params,
                                            param_argc + 1, 0);
    LLVMValueRef init_fn = LLVMAddFunction(c->module, init_name, init_ft);
    ti->fn_init = init_fn;
    ti->fn_init_type = init_ft;

    /* Forward-declare T_new(params...) -> T* */
    LLVMTypeRef *ctor_params = calloc(param_argc > 0 ? param_argc : 1,
                                       sizeof(LLVMTypeRef));
    for (int i = 0; i < param_argc; i++)
        ctor_params[i] = init_params[i + 1];
    char ctor_name[256];
    snprintf(ctor_name, sizeof(ctor_name), "%s_new", n->name);
    LLVMTypeRef ctor_ft = LLVMFunctionType(ti->ptr_type, ctor_params,
                                            param_argc, 0);
    LLVMValueRef ctor_fn = LLVMAddFunction(c->module, ctor_name, ctor_ft);
    ti->fn_new = ctor_fn;

    cg_define_in(c, c->global, n->name, ctor_fn, ctor_ft, 1);
    cg_define_in(c, c->global, strdup(ctor_name), ctor_fn, ctor_ft, 1);

    free(init_params);
    free(ctor_params);

    /* Forward-declare métodos y registrar slots globales */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_METHOD_DEF) {
            MethodDefNode *m = (MethodDefNode*)n->members.items[i];

            int m_argc = m->params.count + 1;  /* +1 para self */
            LLVMTypeRef *m_params = calloc(m_argc, sizeof(LLVMTypeRef));
            m_params[0] = ti->ptr_type;
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                m_params[j + 1] = cg_infer_param_type(c, p->type_annotation);
            }
            LLVMTypeRef m_ret;
            if (m->return_type) {
                m_ret = cg_infer_return_type(c, m->return_type);
            } else {
                LLVMTypeRef inferred = cg_infer_body_return_type(c, m->body);
                m_ret = inferred ? inferred : c->t_double;
            }
            char mname[256];
            snprintf(mname, sizeof(mname), "%s_%s", n->name, m->name);
            LLVMTypeRef m_ft = LLVMFunctionType(m_ret, m_params, m_argc, 0);
            LLVMValueRef m_fn = LLVMAddFunction(c->module, mname, m_ft);

            cg_define_in(c, c->global, strdup(mname), m_fn, m_ft, 1);
            cg_type_add_method(ti, m->name, m_fn);
            cg_method_slot(c, m->name);  /* asigna slot global si no existe */

            free(m_params);
        }
    }
}

void cg_emit_type_def(CodegenContext *c, TypeDefNode *n) {
    if (n->is_protocol) return;
    CGTypeInfo *ti = cg_type_info_find(c, n->name);
    if (!ti) return;

    CGTypeInfo *saved_type = c->enclosing_type;
    c->enclosing_type = ti;

    /* ---- Emitir T_init(self, params...) ----
     * Pasos:
     *   1. Si el tipo tiene padre, evaluar parent_args y llamar Parent_init(self, ...)
     *   2. Setear el __tag__ a ti->type_tag (sobreescribe el que puso el padre)
     *   3. Copiar params del constructor a sus fields
     *   4. Inicializar atributos */
    if (ti->fn_init) {
        LLVMValueRef init_fn = ti->fn_init;
        LLVMValueRef saved_fn = c->current_fn;
        c->current_fn = init_fn;

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, init_fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, entry);

        cg_push_scope(c);

        LLVMValueRef self = LLVMGetParam(init_fn, 0);
        c->self_ptr = self;
        CGSymbol *self_sym = cg_define(c, "self", self, ti->ptr_type, 0);
        if (self_sym) self_sym->hulk_type = ti;

        /* Registrar params del constructor como vars en el scope de init,
         * para que init_expr de atributos y parent_args los puedan ver */
        for (int i = 0; i < n->params.count; i++) {
            VarBindingNode *p = (VarBindingNode*)n->params.items[i];
            LLVMValueRef param_val = LLVMGetParam(init_fn, i + 1);
            LLVMTypeRef pt = LLVMTypeOf(param_val);
            LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
            LLVMBuildStore(c->builder, param_val, alloca);
            CGSymbol *psym = cg_define(c, p->name, alloca, pt, 0);
            if (psym && p->type_annotation) {
                CGTypeInfo *pti = cg_type_info_find(c, p->type_annotation);
                if (pti) psym->hulk_type = pti;
            }
        }

        /* 1. Encadenar Parent_init si hay padre */
        if (ti->parent && ti->parent->fn_init) {
            int parent_argc = n->parent_args.count;
            LLVMValueRef *args = calloc(parent_argc + 1, sizeof(LLVMValueRef));
            args[0] = self;
            for (int i = 0; i < parent_argc; i++)
                args[i + 1] = cg_emit_expr(c, n->parent_args.items[i]);
            LLVMBuildCall2(c->builder, ti->parent->fn_init_type,
                           ti->parent->fn_init, args, parent_argc + 1, "");
            free(args);
        }

        /* 2. Tag (sobrescribe el del padre — el dispatch usa el más derivado) */
        LLVMValueRef tag_gep = LLVMBuildStructGEP2(
            c->builder, ti->struct_type, self, 0, "tag.ptr");
        LLVMBuildStore(c->builder,
                       LLVMConstInt(c->t_i32, ti->type_tag, 0), tag_gep);

        /* 3. Copiar params a fields propios */
        int field_idx = ti->field_offset_self;
        for (int i = 0; i < n->params.count; i++) {
            LLVMValueRef param_val = LLVMGetParam(init_fn, i + 1);
            LLVMValueRef gep = LLVMBuildStructGEP2(
                c->builder, ti->struct_type, self, field_idx, "field.ptr");
            LLVMBuildStore(c->builder, param_val, gep);
            field_idx++;
        }

        /* 4. Atributos */
        for (int i = 0; i < n->members.count; i++) {
            if (n->members.items[i]->type == NODE_ATTRIBUTE_DEF) {
                AttributeDefNode *a = (AttributeDefNode*)n->members.items[i];
                LLVMValueRef val = a->init_expr
                    ? cg_emit_expr(c, a->init_expr)
                    : LLVMConstReal(c->t_double, 0.0);
                LLVMValueRef gep = LLVMBuildStructGEP2(
                    c->builder, ti->struct_type, self, field_idx, "attr.ptr");
                LLVMBuildStore(c->builder, val, gep);
                field_idx++;
            }
        }

        LLVMBuildRetVoid(c->builder);

        cg_pop_scope(c);
        c->current_fn = saved_fn;
        c->self_ptr = NULL;
    }

    /* ---- Emitir T_new(params...) ----
     *   self = malloc(sizeof(struct T))
     *   T_init(self, params...)
     *   return self */
    if (ti->fn_new) {
        LLVMValueRef ctor_fn = ti->fn_new;
        LLVMValueRef saved_fn = c->current_fn;
        c->current_fn = ctor_fn;

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, ctor_fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, entry);

        LLVMValueRef size = LLVMSizeOf(ti->struct_type);
        LLVMTypeRef malloc_params[1] = { LLVMInt64TypeInContext(c->llvm_ctx) };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr,
                                                  malloc_params, 1, 0);
        LLVMValueRef raw = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &size, 1, "raw");
        LLVMValueRef self = LLVMBuildBitCast(c->builder, raw,
                                              ti->ptr_type, "self");

        /* Llamar T_init(self, params...) */
        int init_argc = n->params.count + 1;
        LLVMValueRef *init_args = calloc(init_argc, sizeof(LLVMValueRef));
        init_args[0] = self;
        for (int i = 0; i < n->params.count; i++)
            init_args[i + 1] = LLVMGetParam(ctor_fn, i);
        LLVMBuildCall2(c->builder, ti->fn_init_type, ti->fn_init,
                       init_args, init_argc, "");
        free(init_args);

        LLVMBuildRet(c->builder, self);

        c->current_fn = saved_fn;
    }

    /* ---- Emitir métodos ---- */
    for (int i = 0; i < n->members.count; i++) {
        if (n->members.items[i]->type == NODE_METHOD_DEF) {
            MethodDefNode *m = (MethodDefNode*)n->members.items[i];

            char mname[256];
            snprintf(mname, sizeof(mname), "%s_%s", n->name, m->name);
            CGSymbol *msym = cg_lookup(c->global, mname);
            if (!msym) continue;

            LLVMValueRef m_fn = msym->value;
            LLVMValueRef saved_fn = c->current_fn;
            c->current_fn = m_fn;

            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(
                c->llvm_ctx, m_fn, "entry");
            LLVMPositionBuilderAtEnd(c->builder, entry);

            cg_push_scope(c);

            /* self es el primer parámetro */
            c->self_ptr = LLVMGetParam(m_fn, 0);
            const char *prev_method_name = c->current_method_name;
            c->current_method_name = m->name;

            /* Registrar self en scope con su hulk_type */
            CGSymbol *self_sym = cg_define(c, "self", c->self_ptr,
                                            ti->ptr_type, 0);
            if (self_sym) self_sym->hulk_type = ti;

            /* Parámetros del método: si el tipo declarado es un tipo HULK,
             * propagar hulk_type para que el field access estático funcione. */
            for (int j = 0; j < m->params.count; j++) {
                VarBindingNode *p = (VarBindingNode*)m->params.items[j];
                LLVMValueRef param_val = LLVMGetParam(m_fn, j + 1);
                LLVMTypeRef  pt = LLVMTypeOf(param_val);
                LLVMValueRef alloca = cg_create_entry_alloca(c, pt, p->name);
                LLVMBuildStore(c->builder, param_val, alloca);
                CGSymbol *psym = cg_define(c, p->name, alloca, pt, 0);
                if (psym && p->type_annotation) {
                    CGTypeInfo *pti = cg_type_info_find(c, p->type_annotation);
                    if (pti) psym->hulk_type = pti;
                }
            }

            LLVMValueRef body_val = cg_emit_expr(c, m->body);

            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(c->builder);
            if (!LLVMGetBasicBlockTerminator(cur_bb)) {
                LLVMTypeRef ret_t = LLVMGetReturnType(
                    LLVMGlobalGetValueType(m_fn));
                if (ret_t == c->t_void)
                    LLVMBuildRetVoid(c->builder);
                else
                    LLVMBuildRet(c->builder, body_val);
            }

            cg_pop_scope(c);
            c->current_fn = saved_fn;
            c->self_ptr = NULL;
            c->current_method_name = prev_method_name;
        }
    }

    c->enclosing_type = saved_type;
}

void cg_emit_rtti_globals(CodegenContext *c) {
    int slot_count = c->method_slot_count;
    int type_count = c->type_info_count;
    if (type_count == 0) return;

    LLVMTypeRef ptr_t = c->t_i8ptr;  /* opaque ptr */
    int eff_slots = slot_count > 0 ? slot_count : 1;
    LLVMTypeRef vt_arr_t = LLVMArrayType(ptr_t, eff_slots);

    /* 1. Por cada tipo: construir vtable con cg_type_resolve_method */
    for (int t = 0; t < type_count; t++) {
        CGTypeInfo *ti = c->type_infos[t];
        char name[256];
        snprintf(name, sizeof(name), "%s_vtable", ti->name);
        LLVMValueRef vt_global = LLVMAddGlobal(c->module, vt_arr_t, name);
        LLVMSetLinkage(vt_global, LLVMInternalLinkage);
        LLVMSetGlobalConstant(vt_global, 1);

        LLVMValueRef *entries = calloc(eff_slots, sizeof(LLVMValueRef));
        for (int s = 0; s < slot_count; s++) {
            const char *mname = c->method_slot_names[s];
            LLVMValueRef fn = cg_type_resolve_method(ti, mname);
            entries[s] = fn ? fn : LLVMConstNull(ptr_t);
        }
        if (slot_count == 0)
            entries[0] = LLVMConstNull(ptr_t);
        LLVMValueRef init = LLVMConstArray(ptr_t, entries, eff_slots);
        LLVMSetInitializer(vt_global, init);
        free(entries);

        ti->vtable_global = vt_global;
        ti->vtable_type   = vt_arr_t;
    }

    /* 2. @hulk_vtables[type_count] = { ptr @T0_vtable, ptr @T1_vtable, ... } */
    LLVMTypeRef vt_table_t = LLVMArrayType(ptr_t, type_count);
    LLVMValueRef vt_table = LLVMAddGlobal(c->module, vt_table_t,
                                           "hulk_vtables");
    LLVMSetLinkage(vt_table, LLVMInternalLinkage);
    LLVMSetGlobalConstant(vt_table, 1);
    {
        LLVMValueRef *entries = calloc(type_count, sizeof(LLVMValueRef));
        for (int t = 0; t < type_count; t++)
            entries[t] = c->type_infos[t]->vtable_global;
        LLVMSetInitializer(vt_table,
            LLVMConstArray(ptr_t, entries, type_count));
        free(entries);
    }
    c->vtables_table = vt_table;

    /* 3. @hulk_parents[type_count] = tag del padre o -1 si raíz */
    LLVMTypeRef parent_table_t = LLVMArrayType(c->t_i32, type_count);
    LLVMValueRef parent_table = LLVMAddGlobal(c->module, parent_table_t,
                                               "hulk_parents");
    LLVMSetLinkage(parent_table, LLVMInternalLinkage);
    LLVMSetGlobalConstant(parent_table, 1);
    {
        LLVMValueRef *entries = calloc(type_count, sizeof(LLVMValueRef));
        for (int t = 0; t < type_count; t++) {
            CGTypeInfo *ti = c->type_infos[t];
            int ptag = ti->parent ? ti->parent->type_tag : -1;
            entries[t] = LLVMConstInt(c->t_i32, (unsigned)ptag, 1);
        }
        LLVMSetInitializer(parent_table,
            LLVMConstArray(c->t_i32, entries, type_count));
        free(entries);
    }
    c->parent_table = parent_table;
}
