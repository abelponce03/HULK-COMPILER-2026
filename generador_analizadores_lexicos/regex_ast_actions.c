/*
 * regex_ast_actions.c — Construcción del AST de regex
 *
 * Dos piezas de la fase de "traducir regex → AST de Thompson":
 *   - exec_action: ejecuta cada marcador semántico ACT_* sobre la pila,
 *     construyendo hojas, concatenaciones, alternativas, cierres y
 *     clases de caracteres (incluida la negación [^...]).
 *   - build_lexer_ast: combina los AST de todos los tokens del lenguaje
 *     en un único AST (OR de todos, cada uno terminado con '#') del que
 *     se derivará el DFA maximal-munch.
 */

#include "regex_parser.h"
#include "regex_parser_internal.h"
#include "../error_handler.h"
#include <stdlib.h>
#include <string.h>

void exec_action(int act, ASTNode** sem, int* sem_top,
                        char saved_char, char range_start_char,
                        ASTContext *ctx) {
    switch (act) {
    case ACT_LEAF:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(ctx, saved_char, get_next_position(ctx));
        break;

    case ACT_DOT: {
        ASTNode* result = NULL;
        for (int c = 32; c < 127; c++) {
            ASTNode* leaf = ast_create_leaf(ctx, (char)c, get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_STAR:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_star(ctx, sem[*sem_top - 1]);
        break;

    case ACT_PLUS_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_plus(ctx, sem[*sem_top - 1]);
        break;

    case ACT_QUESTION_OP:
        if (*sem_top > 0)
            sem[*sem_top - 1] = ast_create_question(ctx, sem[*sem_top - 1]);
        break;

    case ACT_OR: {
        ASTNode* right = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* left  = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_or(ctx, left, right);
        break;
    }

    case ACT_CONCAT: {
        ASTNode* rest = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* item = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = (rest == NULL) ? item
                                               : ast_create_concat(ctx, item, rest);
        break;
    }

    case ACT_PUSH_NULL:
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = NULL;
        break;

    case ACT_OR_OPT: {
        ASTNode* rest = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        ASTNode* item = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = (rest == NULL) ? item
                                               : ast_create_or(ctx, item, rest);
        break;
    }

    case ACT_SAVE_RANGE_START:
        // Manejo especial: no se usa exec_action para esto
        break;

    case ACT_LEAF_RANGE_START:
        if (*sem_top < SEM_STACK_MAX)
            sem[(*sem_top)++] = ast_create_leaf(ctx, range_start_char, get_next_position(ctx));
        break;

    case ACT_RANGE: {
        ASTNode* result = NULL;
        for (char c = range_start_char; c <= saved_char; c++) {
            ASTNode* leaf = ast_create_leaf(ctx, c, get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }

    case ACT_NEGATE: {
        /* El tope de la pila tiene el AST de CCItems: un OR (o leaf) de
         * los caracteres del conjunto. Coleccionamos esos caracteres y
         * construimos un OR de todos los imprimibles ASCII (más \t\n\r)
         * que NO están en el conjunto. */
        ASTNode *set_ast = (*sem_top > 0) ? sem[--(*sem_top)] : NULL;

        unsigned char in_set[256] = {0};
        /* Recorrido iterativo del árbol OR/leaf para marcar caracteres. */
        ASTNode *stack[512];
        int sp = 0;
        if (set_ast) stack[sp++] = set_ast;
        while (sp > 0) {
            ASTNode *n = stack[--sp];
            if (!n) continue;
            if (n->type == NODE_LEAF) {
                in_set[(unsigned char)n->symbol] = 1;
            } else {
                if (n->left  && sp < 512) stack[sp++] = n->left;
                if (n->right && sp < 512) stack[sp++] = n->right;
            }
        }

        ASTNode *result = NULL;
        /* Imprimibles 0x20..0x7E + whitespace común. */
        for (int ch = 0x20; ch <= 0x7E; ch++) {
            if (in_set[ch]) continue;
            ASTNode *leaf = ast_create_leaf(ctx, (char)ch,
                                            get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        const char extra[] = { '\t', '\n', '\r' };
        for (int i = 0; i < 3; i++) {
            if (in_set[(unsigned char)extra[i]]) continue;
            ASTNode *leaf = ast_create_leaf(ctx, extra[i],
                                            get_next_position(ctx));
            result = result ? ast_create_or(ctx, result, leaf) : leaf;
        }
        if (*sem_top < SEM_STACK_MAX) sem[(*sem_top)++] = result;
        break;
    }
    }
}

ASTNode* build_lexer_ast(TokenRegex* tokens, int token_count,
                         ASTContext *ctx, RegexParserContext *rctx) {
    if (!tokens || token_count <= 0) {
        return NULL;
    }
    
    ASTNode* combined = NULL;
    
    for (int i = 0; i < token_count; i++) {
        ASTNode* ast = regex_parse(tokens[i].regex, ctx, rctx);
        
        if (ast == NULL) {
            LOG_ERROR_MSG("regex", "Error parseando regex para token %d: '%s'",
                          tokens[i].token_id, tokens[i].regex);
            continue;
        }
        
        // Agregar marcador de fin (#) y asociar token_id
        int end_pos = get_next_position(ctx);
        ASTNode* end_marker = ast_create_leaf(ctx, '#', end_pos);
        if (!end_marker) {
            LOG_ERROR_MSG("regex", "sin memoria creando end_marker para token %d",
                          tokens[i].token_id);
            continue;
        }
        
        // Registrar qué token corresponde a esta posición
        ctx->pos_to_token[end_pos] = tokens[i].token_id;
        
        // regex#
        ASTNode* marked = ast_create_concat(ctx, ast, end_marker);
        if (!marked) {
            LOG_ERROR_MSG("regex", "sin memoria concatenando marcador para token %d",
                          tokens[i].token_id);
            continue;
        }
        
        // Combinar con OR
        if (combined == NULL) {
            combined = marked;
        } else {
            combined = ast_create_or(ctx, combined, marked);
        }
    }
    
    return combined;
}
