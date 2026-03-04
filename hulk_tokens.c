/*
 * hulk_tokens.c — Definiciones léxicas del lenguaje HULK
 *
 * Contiene las parejas (token_id, regex) que definen cada token,
 * y la tabla de nombres legibles para impresión/depuración.
 */

#include "hulk_tokens.h"

// ============== DEFINICIÓN DE TOKENS PARA HULK ==============

TokenRegex hulk_tokens[] = {
    // ===== PALABRAS CLAVE (deben ir ANTES de identificadores) =====
    { TOKEN_FUNCTION,   "function" },
    { TOKEN_TYPE,       "type" },
    { TOKEN_INHERITS,   "inherits" },
    { TOKEN_WHILE,      "while" },
    { TOKEN_FOR,        "for" },
    { TOKEN_IN,         "in" },
    { TOKEN_IF,         "if" },
    { TOKEN_ELIF,       "elif" },
    { TOKEN_ELSE,       "else" },
    { TOKEN_LET,        "let" },
    { TOKEN_TRUE,       "true" },
    { TOKEN_FALSE,      "false" },
    { TOKEN_NEW,        "new" },
    { TOKEN_SELF,       "self" },
    { TOKEN_BASE,       "base" },
    { TOKEN_AS,         "as" },
    { TOKEN_IS,         "is" },
    { TOKEN_DECOR,      "decor" },
    
    // ===== OPERADORES MULTI-CARÁCTER (antes de simples) =====
    { TOKEN_ARROW,          "=>" },
    { TOKEN_ASSIGN_DESTRUCT,":=" },
    { TOKEN_LE,             "<=" },
    { TOKEN_GE,             ">=" },
    { TOKEN_EQ,             "==" },
    { TOKEN_NEQ,            "!=" },
    { TOKEN_OR,             "\\|\\|" },
    { TOKEN_AND,            "&&" },
    { TOKEN_CONCAT_WS,      "@@" },
    { TOKEN_CONCAT,         "@" },
    { TOKEN_POW,            "\\*\\*" },
    
    // ===== OPERADORES SIMPLES =====
    { TOKEN_SEMICOLON,  ";" },
    { TOKEN_LPAREN,     "\\(" },
    { TOKEN_RPAREN,     "\\)" },
    { TOKEN_LBRACE,     "\\{" },
    { TOKEN_RBRACE,     "\\}" },
    { TOKEN_COMMA,      "," },
    { TOKEN_COLON,      ":" },
    { TOKEN_DOT,        "\\." },
    { TOKEN_ASSIGN,     "=" },
    { TOKEN_PLUS,       "\\+" },
    { TOKEN_MINUS,      "\\-" },
    { TOKEN_MULT,       "\\*" },
    { TOKEN_DIV,        "/" },
    { TOKEN_MOD,        "%" },
    { TOKEN_LT,         "<" },
    { TOKEN_GT,         ">" },
    
    // ===== LITERALES =====
    { TOKEN_NUMBER,     "[0-9]+(\\.[0-9]+)?" },
    { TOKEN_STRING,     "\"[a-zA-Z0-9 ]*\"" },
    
    // ===== IDENTIFICADORES (debe ir DESPUÉS de palabras clave) =====
    { TOKEN_IDENT,      "[a-zA-Z_][a-zA-Z0-9_]*" },
    
    // ===== WHITESPACE (será ignorado) =====
    { TOKEN_WS,         "[ \\t\\n\\r]+" },
    
    // ===== COMENTARIOS =====
    { TOKEN_COMMENT,    "//.*" },
};

int hulk_token_count = sizeof(hulk_tokens) / sizeof(hulk_tokens[0]);

// Nombres de tokens para imprimir (indexados por TokenType)
const char* token_names[] = {
    "EOF", "WS", "COMMENT",
    "FUNCTION", "TYPE", "INHERITS", "WHILE", "FOR", "IN", "IF", "ELIF", "ELSE",
    "LET", "TRUE", "FALSE", "NEW", "SELF", "BASE", "AS", "IS", "DECOR",
    "SEMICOLON", "LPAREN", "RPAREN", "LBRACE", "RBRACE", "COMMA", "COLON", "DOT",
    "ASSIGN", "ASSIGN_DESTRUCT", "PLUS", "MINUS", "MULT", "DIV", "MOD", "POW",
    "LT", "GT", "LE", "GE", "EQ", "NEQ", "OR", "AND", "CONCAT", "CONCAT_WS", "ARROW",
    "IDENT", "NUMBER", "STRING", "ERROR"
};

const char* get_token_name(int type) {
    if (type >= 0 && type < (int)(sizeof(token_names)/sizeof(token_names[0])))
        return token_names[type];
    return "UNKNOWN";
}
