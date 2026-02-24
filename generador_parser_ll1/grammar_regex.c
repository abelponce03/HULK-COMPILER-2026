/*
 * grammar_regex.c — Definición de la gramática de expresiones regulares
 *
 * Define los terminales, no-terminales y producciones LL(1) para
 * parsear expresiones regulares.  Extraído de grammar.c (SRP).
 */

#include "grammar.h"

void grammar_init_regex(Grammar* g) {
    grammar_init(g, "regex");
    
    // Terminales para regex
    int T_CHAR     = grammar_add_terminal(g, "CHAR", 0);       // cualquier carácter literal
    int T_OR       = grammar_add_terminal(g, "OR", 1);         // |
    int T_STAR     = grammar_add_terminal(g, "STAR", 2);       // *
    int T_PLUS     = grammar_add_terminal(g, "PLUS", 3);       // +
    int T_QUESTION = grammar_add_terminal(g, "QUESTION", 4);   // ?
    int T_LPAREN   = grammar_add_terminal(g, "LPAREN", 5);     // (
    int T_RPAREN   = grammar_add_terminal(g, "RPAREN", 6);     // )
    int T_LBRACKET = grammar_add_terminal(g, "LBRACKET", 7);   // [
    int T_RBRACKET = grammar_add_terminal(g, "RBRACKET", 8);   // ]
    int T_DOT      = grammar_add_terminal(g, "DOT", 9);        // .
    int T_CARET    = grammar_add_terminal(g, "CARET", 10);     // ^
    int T_DASH     = grammar_add_terminal(g, "DASH", 11);      // -
    int T_ESCAPE   = grammar_add_terminal(g, "ESCAPE", 12);    // \x
    
    // No terminales
    int NT_Regex         = grammar_add_nonterminal(g, "Regex");
    int NT_Concat        = grammar_add_nonterminal(g, "Concat");
    int NT_ConcatTail    = grammar_add_nonterminal(g, "ConcatTail");
    int NT_Repeat        = grammar_add_nonterminal(g, "Repeat");
    int NT_Postfix       = grammar_add_nonterminal(g, "Postfix");
    int NT_Atom          = grammar_add_nonterminal(g, "Atom");
    int NT_CharClass     = grammar_add_nonterminal(g, "CharClass");
    int NT_CCItems       = grammar_add_nonterminal(g, "CCItems");
    int NT_CCItem        = grammar_add_nonterminal(g, "CCItem");
    int NT_RangeOpt      = grammar_add_nonterminal(g, "RangeOpt");
    
    GrammarSymbol s[16];
    
    // Regex -> Concat ConcatTail
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ConcatTail};
    grammar_add_production(g, NT_Regex, s, 2);
    
    // ConcatTail -> OR Concat ConcatTail | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_OR};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    s[2] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ConcatTail};
    grammar_add_production(g, NT_ConcatTail, s, 3);
    grammar_add_production(g, NT_ConcatTail, NULL, 0);  // ε
    
    // Concat -> Repeat Concat | ε
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Repeat};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
    grammar_add_production(g, NT_Concat, s, 2);
    grammar_add_production(g, NT_Concat, NULL, 0);  // ε
    
    // Repeat -> Atom Postfix
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Atom};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Postfix};
    grammar_add_production(g, NT_Repeat, s, 2);
    
    // Postfix -> STAR | PLUS | QUESTION | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_STAR};
    grammar_add_production(g, NT_Postfix, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_PLUS};
    grammar_add_production(g, NT_Postfix, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_QUESTION};
    grammar_add_production(g, NT_Postfix, s, 1);
    grammar_add_production(g, NT_Postfix, NULL, 0);  // ε
    
    // Atom -> CHAR | ESCAPE | LPAREN Regex RPAREN | LBRACKET CharClass RBRACKET | DOT
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    grammar_add_production(g, NT_Atom, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_ESCAPE};
    grammar_add_production(g, NT_Atom, s, 1);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_LPAREN};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Regex};
    s[2] = (GrammarSymbol){SYMBOL_TERMINAL, T_RPAREN};
    grammar_add_production(g, NT_Atom, s, 3);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_LBRACKET};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CharClass};
    s[2] = (GrammarSymbol){SYMBOL_TERMINAL, T_RBRACKET};
    grammar_add_production(g, NT_Atom, s, 3);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_DOT};
    grammar_add_production(g, NT_Atom, s, 1);
    
    // CharClass -> CARET CCItems | CCItems
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CARET};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CharClass, s, 2);
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CharClass, s, 1);
    
    // CCItems -> CCItem CCItems | ε
    s[0] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItem};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CCItems};
    grammar_add_production(g, NT_CCItems, s, 2);
    grammar_add_production(g, NT_CCItems, NULL, 0);  // ε
    
    // CCItem -> CHAR RangeOpt | ESCAPE
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    s[1] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_RangeOpt};
    grammar_add_production(g, NT_CCItem, s, 2);
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_ESCAPE};
    grammar_add_production(g, NT_CCItem, s, 1);
    
    // RangeOpt -> DASH CHAR | ε
    s[0] = (GrammarSymbol){SYMBOL_TERMINAL, T_DASH};
    s[1] = (GrammarSymbol){SYMBOL_TERMINAL, T_CHAR};
    grammar_add_production(g, NT_RangeOpt, s, 2);
    grammar_add_production(g, NT_RangeOpt, NULL, 0);  // ε
}
