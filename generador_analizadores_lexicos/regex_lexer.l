%{
/*
 * Lexer para expresiones regulares usando Flex
 * Genera tokens para el parser LL(1)
 */

#include "regex_tokens.h"
#include <string.h>

// Buffer para el carácter reconocido
char regex_char_value;

%}

%option noyywrap
%option prefix="regex_"

%%

"*"             { return REGEX_T_STAR; }
"+"             { return REGEX_T_PLUS; }
"?"             { return REGEX_T_QUESTION; }
"|"             { return REGEX_T_OR; }
"("             { return REGEX_T_LPAREN; }
")"             { return REGEX_T_RPAREN; }
"["             { return REGEX_T_LBRACKET; }
"]"             { return REGEX_T_RBRACKET; }
"."             { return REGEX_T_DOT; }
"^"             { return REGEX_T_CARET; }
"-"             { return REGEX_T_DASH; }

\\n             { regex_char_value = '\n'; return REGEX_T_ESCAPE; }
\\t             { regex_char_value = '\t'; return REGEX_T_ESCAPE; }
\\r             { regex_char_value = '\r'; return REGEX_T_ESCAPE; }
\\.             { regex_char_value = yytext[1]; return REGEX_T_ESCAPE; }

.               { regex_char_value = yytext[0]; return REGEX_T_CHAR; }

<<EOF>>         { return REGEX_T_EOF; }

%%

/* Función para inicializar el lexer con una cadena */
void regex_lexer_set_string(const char* str) {
    regex__scan_string(str);
}

/* Función para limpiar el buffer del lexer */
void regex_lexer_cleanup(void) {
    regex__delete_buffer(YY_CURRENT_BUFFER);
}
