/*
 * hulk_ast_context.c — Arena (Object Pool) y HulkNodeList
 *
 * Gestión de memoria del AST:
 *   - HulkNodeList: lista dinámica de punteros a nodos
 *   - HulkASTContext: arena que registra todos los bloques asignados
 *     y los libera en batch con hulk_ast_context_free()
 *
 * SRP: Solo gestión de memoria y estructuras de datos auxiliares.
 */

#include "hulk_ast.h"
#include "../../error_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============== HULK NODE LIST ==============

void hulk_node_list_init(HulkNodeList *list) {
    list->items    = NULL;
    list->count    = 0;
    list->capacity = 0;
}

void hulk_node_list_push(HulkNodeList *list, HulkNode *node) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        list->items = realloc(list->items, sizeof(HulkNode*) * new_cap);
        if (!list->items) {
            LOG_FATAL_MSG("hulk_ast", "sin memoria para HulkNodeList");
            return;
        }
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

void hulk_node_list_free(HulkNodeList *list) {
    if (list->items) {
        free(list->items);
        list->items = NULL;
    }
    list->count = 0;
    list->capacity = 0;
}

// ============== OBJECT POOL (ARENA) ==============

void hulk_ast_context_init(HulkASTContext *ctx) {
    ctx->blocks         = NULL;
    ctx->block_count    = 0;
    ctx->block_capacity = 0;
}

void hulk_ast_context_free(HulkASTContext *ctx) {
    for (int i = 0; i < ctx->block_count; i++) {
        free(ctx->blocks[i]);
    }
    free(ctx->blocks);
    ctx->blocks         = NULL;
    ctx->block_count    = 0;
    ctx->block_capacity = 0;
}

void* hulk_ast_alloc(HulkASTContext *ctx, size_t size) {
    void *block = calloc(1, size);
    if (!block) {
        LOG_FATAL_MSG("hulk_ast", "sin memoria (%zu bytes)", size);
        return NULL;
    }
    // Registrar en el pool para liberación posterior
    if (ctx->block_count >= ctx->block_capacity) {
        int new_cap = ctx->block_capacity == 0
                      ? HULK_AST_POOL_INIT_CAP
                      : ctx->block_capacity * 2;
        ctx->blocks = realloc(ctx->blocks, sizeof(void*) * new_cap);
        if (!ctx->blocks) {
            LOG_FATAL_MSG("hulk_ast", "sin memoria para pool");
            free(block);
            return NULL;
        }
        ctx->block_capacity = new_cap;
    }
    ctx->blocks[ctx->block_count++] = block;
    return block;
}

char* hulk_ast_strdup(HulkASTContext *ctx, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = hulk_ast_alloc(ctx, len + 1);
    if (copy) memcpy(copy, s, len + 1);
    return copy;
}
