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
    int state = 0;  // estado inicial
    int start = LEXER_POS;

    int last_accept_state = -1;
    int last_accept_pos = -1;
    int last_token = -1;

    int pos = LEXER_POS;

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
        printf("Error léxico en posición %d\n", LEXER_POS);
        exit(1);
    }

    int len = last_accept_pos - start;
    char *lexeme = malloc(len + 1);
    memcpy(lexeme, LEXER_INPUT + start, len);
    lexeme[len] = '\0';

    LEXER_POS = last_accept_pos;

    Token tok;
    tok.type = last_token;
    tok.lexeme = lexeme;
    tok.length = len;

    return tok;
}



