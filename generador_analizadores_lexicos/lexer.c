#include "lexer.h"

void lexer_init(LexerContext *ctx, DFA *dfa, const char *input) {
    ctx->dfa   = dfa;
    ctx->input = input;
    ctx->pos   = 0;
    ctx->line  = 1;
    ctx->col   = 1;

    if (dfa->next_state == NULL) {
        dfa_build_table(dfa);
    }
}

// Avanza contadores line/col por el texto consumido
static void advance_position(LexerContext *ctx, const char *text, int len) {
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            ctx->line++;
            ctx->col = 1;
        } else {
            ctx->col++;
        }
    }
}

Token lexer_next_token(LexerContext *ctx) {
    while (1) {
        int state = 0;
        int start = ctx->pos;
        int start_line = ctx->line;
        int start_col  = ctx->col;

        int last_accept_state = -1;
        int last_accept_pos   = -1;
        int last_token        = -1;
        int pos = ctx->pos;

        if (ctx->input[pos] == '\0') {
            Token tok;
            tok.type   = TOKEN_EOF;
            tok.lexeme = NULL;
            tok.length = 0;
            tok.line   = ctx->line;
            tok.col    = ctx->col;
            return tok;
        }

        // Maximal munch: avanzar mientras haya transiciones válidas
        while (1) {
            unsigned char c = ctx->input[pos];
            if (c == '\0') break;

            int next = ctx->dfa->next_state[state][c];
            if (next == -1) break;

            state = next;
            pos++;

            if (ctx->dfa->states[state].is_accept) {
                last_accept_state = state;
                last_accept_pos   = pos;
                last_token        = ctx->dfa->states[state].token_id;
            }
        }

        if (last_accept_state == -1) {
            // Error léxico: emitir TOKEN_ERROR y avanzar 1 carácter
            fprintf(stderr, "Error léxico [%d:%d] cerca de '%c'\n",
                    ctx->line, ctx->col, ctx->input[ctx->pos]);
            Token err;
            err.type   = TOKEN_ERROR;
            err.length = 1;
            err.line   = ctx->line;
            err.col    = ctx->col;
            err.lexeme = malloc(2);
            err.lexeme[0] = ctx->input[ctx->pos];
            err.lexeme[1] = '\0';
            advance_position(ctx, ctx->input + ctx->pos, 1);
            ctx->pos++;
            return err;
        }

        int len = last_accept_pos - start;

        // Actualizar line/col por los caracteres consumidos
        advance_position(ctx, ctx->input + start, len);
        ctx->pos = last_accept_pos;

        // Ignorar whitespace y comentarios sin asignar memoria
        if (last_token == TOKEN_WS || last_token == TOKEN_COMMENT) {
            continue;
        }

        char *lexeme = malloc(len + 1);
        memcpy(lexeme, ctx->input + start, len);
        lexeme[len] = '\0';

        Token tok;
        tok.type   = last_token;
        tok.lexeme = lexeme;
        tok.length = len;
        tok.line   = start_line;
        tok.col    = start_col;
        return tok;
    }
}


