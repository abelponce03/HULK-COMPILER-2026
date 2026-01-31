#include "lexer.h"

static DFA *LEXER_DFA;
static const char *LEXER_INPUT;
static int LEXER_POS;

void lexer_init(DFA *dfa, const char *input) {
    LEXER_DFA = dfa;
    LEXER_INPUT = input;
    LEXER_POS = 0;

    if (dfa->next_state == NULL) {
        dfa_build_table(dfa);
    }
}

Token lexer_next_token() {
    while (1) {
        int state = 0;  // estado actual
        int start = LEXER_POS;

        int last_accept_state = -1;
        int last_accept_pos = -1;
        int last_token = -1;

        int pos = LEXER_POS;

        if (LEXER_INPUT[pos] == '\0') {
            Token tok;
            tok.type = TOKEN_EOF;     
            tok.lexeme = NULL;
            tok.length = 0;
            return tok;
        }

        // maximul munch loop
        while (1) {
            unsigned char c = LEXER_INPUT[pos];
            if (c == '\0') break;

            int next = LEXER_DFA->next_state[state][c];
            if (next == -1) break;

            state = next;
            pos++;

            if (LEXER_DFA->states[state].is_accept) {
                last_accept_state = state;
                last_accept_pos = pos;
                last_token = LEXER_DFA->states[state].token_id;
            }
        }

        if (last_accept_state == -1) {
            printf("Error léxico en posición %d cerca de '%c'\n",
                   LEXER_POS, LEXER_INPUT[LEXER_POS]);
            exit(1);
        }

        int len = last_accept_pos - start;
        char *lexeme = malloc(len + 1);
        memcpy(lexeme, LEXER_INPUT + start, len);
        lexeme[len] = '\0';

        // avanzar
        LEXER_POS = last_accept_pos;

        Token tok;
        tok.type = last_token;
        tok.lexeme = lexeme;
        tok.length = len;

        // ***** IGNORE TOKENS DE ESPACIOS O COMENTARIOS *****
        // supongamos que tienes estos enums:
        // TOKEN_WS, TOKEN_COMMENT
        if (tok.type == TOKEN_WS || tok.type == TOKEN_COMMENT) {
            free(tok.lexeme);
            continue;  // buscar siguiente token válido
        }

        return tok;
    }
}


