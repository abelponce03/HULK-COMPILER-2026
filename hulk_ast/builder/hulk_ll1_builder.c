/*
 * hulk_ll1_builder.c — Parser LL(1) dirigido por tabla (opción B)
 *
 * La gramática de HULK se declara como DATOS (HULK_PRODS): cada producción
 * lista su RHS con las acciones semánticas intercaladas. De esa única
 * fuente se derivan (1) la gramática pura para FIRST/FOLLOW/tabla LL(1)
 * —ignorando las acciones— y (2) la secuencia que el autómata de pila
 * empuja al expandir —incluyendo las acciones—. Esto evita el frágil
 * switch-por-índice y mantiene gramática y acciones sincronizadas.
 *
 * Una pila semántica tipada (nodo | lexema | centinela) acumula los
 * resultados; las acciones construyen los nodos del AST. Las listas de
 * longitud variable (args, bindings, …) usan el patrón centinela.
 *
 * Estado: CAPA 3 — parser LL(1) funcional con definiciones, decoradores,
 * lambdas, tipos/protocolos, arrays/iteradores y `define`.
 */

#include "hulk_ll1_builder.h"
#include "hulk_ast_builder.h"
#include "../../generador_analizadores_lexicos/lexer.h"
#include "../../generador_parser_ll1/grammar.h"
#include "../../generador_parser_ll1/first_follow.h"
#include "../../generador_parser_ll1/ll1_table.h"
#include "../../error_handler.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 *  No-terminales
 * ============================================================ */
enum {
    NT_Program, NT_TopList, NT_TopItem, NT_OptSemi, NT_StmtList, NT_TermStmt, NT_Stmt,
    NT_Expr, NT_Or, NT_OrP, NT_And, NT_AndP, NT_Cmp, NT_CmpP,
    NT_Concat, NT_ConcatP, NT_Add, NT_AddP, NT_Term, NT_TermP,
    NT_Factor, NT_FactorP, NT_Unary, NT_Postfix,
    NT_Primary, NT_Call, NT_Args, NT_ArgsT,
    NT_Let, NT_Bindings, NT_BindingsT, NT_Binding, NT_TypeAnn,
    NT_If, NT_ElifL, NT_Body,
    NT_While, NT_For, NT_Block,
    NT_VecItems, NT_VecItemsT,
    /* Capa 2: definiciones */
    NT_FunctionDef, NT_Params, NT_ParamsT, NT_Param, NT_FuncBody, NT_FuncExprBody,
    NT_TypeDef, NT_TypeParams, NT_TypeInherit, NT_TypeBaseArgs,
    NT_TypeBody, NT_TypeMember, NT_TypeMemberTail, NT_AttrTail, NT_MethodBody,
    NT_ProtocolDef, NT_ProtoExt, NT_ProtoSigs, NT_ProtoSig,
    NT_DecorBlock, NT_DecorMore, NT_DecorTarget,
    NT_DecorItems, NT_DecorItemsT, NT_DecorItem, NT_DecorArgs,
    NT_DecorPrefix,
    NT_TypeRef, NT_TypeSuffix, NT_TypeList, NT_TypeListT,
    NT_NewTail, NT_ArrayTypeSuffix, NT_ArrayInit,
    NT_Lambda,
    NT_COUNT
};

/* ============================================================
 *  Acciones semánticas (offset alto para distinguir de NT/T)
 * ============================================================ */
enum {
    A_NUM = 9000, A_STR, A_TRUE, A_FALSE, A_IDENT, A_SELF,
    A_OR, A_AND, A_LT, A_GT, A_LE, A_GE, A_EQ, A_NEQ,
    A_CONCAT, A_CONCATWS, A_ADD, A_SUB, A_MUL, A_DIV, A_MOD, A_POW,
    A_NEG, A_NOT, A_AS, A_IS,
    A_SENT, A_CALL, A_MEMBER, A_INDEX, A_ASSIGN, A_DESTRUCT,
    A_NEW, A_BASE,
    A_LET, A_BIND, A_TYPE_NAME, A_TYPE_NONE,
    A_IF, A_ELIF, A_WHILE, A_FOR, A_BLOCK_BEGIN, A_BLOCK,
    A_VEC,
    /* Capa 2 */
    A_PARAM, A_FUNCDEF, A_FUNCEXPR,
    A_TD_BEGIN, A_TD_PARAMS, A_TD_PARENT, A_TD_PARGS,
    A_METHOD, A_ATTR, A_ATTR_NOINIT,
    A_PROTO_BEGIN, A_PROTO_METHOD,
    A_DECOR_ITEM, A_DECOR_BLOCK, A_TYPE_FUNC,
    /* Capa 3 */
    A_TYPE_ARRAY, A_TYPE_ITER, A_ARRAY_NEW, A_ARRAY_INIT,
};

/* Codificación de símbolos en el RHS de los datos:
 *   NT:  0 .. NT_COUNT-1
 *   T:   TBASE + TOKEN_*
 *   ACT: (ya >= 9000) */
#define TBASE 1000
#define T(tok) (TBASE + (tok))
#define IS_T(x)   ((x) >= TBASE && (x) < 9000)
#define IS_ACT(x) ((x) >= 9000)
#define IS_NT(x)  ((x) >= 0 && (x) < TBASE)

typedef struct { int lhs; int rhs[16]; int n; } Prod;

/* ============================================================
 *  Gramática de HULK como datos (LHS -> RHS con acciones).
 *  ε se expresa con n==0.
 * ============================================================ */
static const Prod HULK_PRODS[] = {
    /* Program -> TopList */
    { NT_Program, { NT_TopList }, 1 },
    /* TopList -> TopItem TopList | ε */
    { NT_TopList, { NT_TopItem, NT_TopList }, 2 },
    { NT_TopList, { 0 }, 0 },
    /* TopItem -> FunctionDef | DefineDef | TypeDef | ProtocolDef | Block OptSemi | TermStmt
       (FUNCTION es ambiguo def/expr: lookahead local en el parser) */
    { NT_TopItem, { NT_FunctionDef }, 1 },
    { NT_TopItem, { T(TOKEN_DEFINE), T(TOKEN_IDENT), T(TOKEN_LPAREN), A_SENT,
                    NT_Params, T(TOKEN_RPAREN), NT_TypeAnn, NT_FuncBody, A_FUNCDEF }, 9 },
    { NT_TopItem, { NT_TypeDef }, 1 },
    { NT_TopItem, { NT_ProtocolDef }, 1 },
    { NT_TopItem, { NT_DecorBlock }, 1 },
    { NT_TopItem, { NT_Block, NT_OptSemi }, 2 },
    { NT_TopItem, { NT_TermStmt }, 1 },
    { NT_OptSemi, { T(TOKEN_SEMICOLON) }, 1 },
    { NT_OptSemi, { 0 }, 0 },
    /* StmtList -> TermStmt StmtList | ε  (solo statements, dentro de bloques) */
    { NT_StmtList, { NT_TermStmt, NT_StmtList }, 2 },
    { NT_StmtList, { 0 }, 0 },
    /* TermStmt -> Stmt SEMICOLON   (el `;` final lo maneja el lexer/EOF) */
    { NT_TermStmt, { NT_Stmt, T(TOKEN_SEMICOLON) }, 2 },
    /* Stmt -> Block | While | For | Expr */
    { NT_Stmt, { NT_Block }, 1 },
    { NT_Stmt, { NT_While }, 1 },
    { NT_Stmt, { NT_For }, 1 },
    { NT_Stmt, { NT_Expr }, 1 },

    /* Expr -> Or | Let | If */
    { NT_Expr, { NT_Or }, 1 },
    { NT_Expr, { NT_Let }, 1 },
    { NT_Expr, { NT_If }, 1 },

    /* Or -> And Or'   ;  Or' -> OR And @or Or' | ε */
    { NT_Or, { NT_And, NT_OrP }, 2 },
    { NT_OrP, { T(TOKEN_OR), NT_And, A_OR, NT_OrP }, 4 },
    { NT_OrP, { 0 }, 0 },
    /* And -> Cmp And' ; And' -> AND Cmp @and And' | ε */
    { NT_And, { NT_Cmp, NT_AndP }, 2 },
    { NT_AndP, { T(TOKEN_AND), NT_Cmp, A_AND, NT_AndP }, 4 },
    { NT_AndP, { 0 }, 0 },
    /* Cmp -> Concat Cmp' ; Cmp' -> (op Concat @op | IS IDENT @is) Cmp' | ε */
    { NT_Cmp, { NT_Concat, NT_CmpP }, 2 },
    { NT_CmpP, { T(TOKEN_LT), NT_Concat, A_LT, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_GT), NT_Concat, A_GT, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_LE), NT_Concat, A_LE, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_GE), NT_Concat, A_GE, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_EQ), NT_Concat, A_EQ, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_NEQ), NT_Concat, A_NEQ, NT_CmpP }, 4 },
    { NT_CmpP, { T(TOKEN_IS), T(TOKEN_IDENT), A_IS, NT_CmpP }, 4 },
    { NT_CmpP, { 0 }, 0 },
    /* Concat -> Add Concat' ; Concat' -> (@@|@) Add @op Concat' | ε */
    { NT_Concat, { NT_Add, NT_ConcatP }, 2 },
    { NT_ConcatP, { T(TOKEN_CONCAT), NT_Add, A_CONCAT, NT_ConcatP }, 4 },
    { NT_ConcatP, { T(TOKEN_CONCAT_WS), NT_Add, A_CONCATWS, NT_ConcatP }, 4 },
    { NT_ConcatP, { 0 }, 0 },
    /* Add -> Term Add' ; Add' -> (+|-) Term @op Add' | ε */
    { NT_Add, { NT_Term, NT_AddP }, 2 },
    { NT_AddP, { T(TOKEN_PLUS), NT_Term, A_ADD, NT_AddP }, 4 },
    { NT_AddP, { T(TOKEN_MINUS), NT_Term, A_SUB, NT_AddP }, 4 },
    { NT_AddP, { 0 }, 0 },
    /* Term -> Factor Term' ; Term' -> (*|/|%) Factor @op Term' | ε */
    { NT_Term, { NT_Factor, NT_TermP }, 2 },
    { NT_TermP, { T(TOKEN_MULT), NT_Factor, A_MUL, NT_TermP }, 4 },
    { NT_TermP, { T(TOKEN_DIV), NT_Factor, A_DIV, NT_TermP }, 4 },
    { NT_TermP, { T(TOKEN_MOD), NT_Factor, A_MOD, NT_TermP }, 4 },
    { NT_TermP, { 0 }, 0 },
    /* Factor -> Unary Factor' ; Factor' -> POW Factor @pow | ε
       (potencia right-assoc: 2 ** 3 ** 4 => 2 ** (3 ** 4)) */
    { NT_Factor, { NT_Unary, NT_FactorP }, 2 },
    { NT_FactorP, { T(TOKEN_POW), NT_Factor, A_POW }, 3 },
    { NT_FactorP, { 0 }, 0 },
    /* Unary -> MINUS Unary @neg | NOT Unary @not | Postfix */
    { NT_Unary, { T(TOKEN_MINUS), NT_Unary, A_NEG }, 3 },
    { NT_Unary, { T(TOKEN_NOT), NT_Unary, A_NOT }, 3 },
    { NT_Unary, { NT_Postfix }, 1 },
    /* Postfix -> Primary PostfixTail   (encadena call/member/index/as/assign) */
    { NT_Postfix, { NT_Primary, NT_Call }, 2 },
    /* Call (PostfixTail) -> LPAREN @sent Args RPAREN @call Call
                           | LBRACKET Expr RBRACKET @index Call
                           | DOT (IDENT|BASE) @member Call
                           | AS IDENT @as Call
                           | ASSIGN_DESTRUCT Expr @destruct
                           | ASSIGN Expr @assign
                           | ε */
    { NT_Call, { T(TOKEN_LPAREN), A_SENT, NT_Args, T(TOKEN_RPAREN), A_CALL, NT_Call }, 6 },
    { NT_Call, { T(TOKEN_LBRACKET), NT_Expr, T(TOKEN_RBRACKET), A_INDEX, NT_Call }, 5 },
    { NT_Call, { T(TOKEN_DOT), T(TOKEN_IDENT), A_MEMBER, NT_Call }, 4 },
    { NT_Call, { T(TOKEN_DOT), T(TOKEN_BASE), A_MEMBER, NT_Call }, 4 },
    { NT_Call, { T(TOKEN_AS), T(TOKEN_IDENT), A_AS, NT_Call }, 4 },
    { NT_Call, { T(TOKEN_ASSIGN_DESTRUCT), NT_Expr, A_DESTRUCT }, 3 },
    { NT_Call, { T(TOKEN_ASSIGN), NT_Expr, A_ASSIGN }, 3 },
    { NT_Call, { 0 }, 0 },
    /* Args -> Expr ArgsT | ε   ;  ArgsT -> COMMA Expr ArgsT | ε */
    { NT_Args, { NT_Expr, NT_ArgsT }, 2 },
    { NT_Args, { 0 }, 0 },
    { NT_ArgsT, { T(TOKEN_COMMA), NT_Expr, NT_ArgsT }, 3 },
    { NT_ArgsT, { 0 }, 0 },

    /* Primary -> NUMBER @num | STRING @str | TRUE @t | FALSE @f
                | IDENT @ident | SELF @self
                | LPAREN Expr RPAREN
                | IF ... | LET ...
                | NEW IDENT NewTail
                | BASE LPAREN @sent Args RPAREN @base
                | LBRACKET @sent VecItems RBRACKET @vec
                | LBRACE @sent VecItems RBRACE @vec */
    { NT_Primary, { T(TOKEN_NUMBER), A_NUM }, 2 },
    { NT_Primary, { T(TOKEN_STRING), A_STR }, 2 },
    { NT_Primary, { T(TOKEN_TRUE), A_TRUE }, 2 },
    { NT_Primary, { T(TOKEN_FALSE), A_FALSE }, 2 },
    { NT_Primary, { T(TOKEN_IDENT), A_IDENT }, 2 },
    { NT_Primary, { T(TOKEN_SELF), A_SELF }, 2 },
    { NT_Primary, { NT_If }, 1 },
    { NT_Primary, { NT_Let }, 1 },
    { NT_Primary, { T(TOKEN_LPAREN), NT_Expr, T(TOKEN_RPAREN) }, 3 },
    { NT_Primary, { T(TOKEN_NEW), T(TOKEN_IDENT), NT_NewTail }, 3 },
    { NT_Primary, { T(TOKEN_BASE), T(TOKEN_LPAREN), A_SENT, NT_Args, T(TOKEN_RPAREN), A_BASE }, 6 },
    { NT_Primary, { T(TOKEN_LBRACKET), A_SENT, NT_VecItems, T(TOKEN_RBRACKET), A_VEC }, 5 },
    { NT_Primary, { T(TOKEN_LBRACE), A_SENT, NT_VecItems, T(TOKEN_RBRACE), A_VEC }, 5 },
    /* NewTail -> (args) | array suffixes [size] [init] */
    { NT_NewTail, { T(TOKEN_LPAREN), A_SENT, NT_Args, T(TOKEN_RPAREN), A_NEW }, 5 },
    { NT_NewTail, { NT_ArrayTypeSuffix, T(TOKEN_LBRACKET), NT_Expr,
                    T(TOKEN_RBRACKET), A_ARRAY_NEW, NT_ArrayInit }, 6 },
    { NT_ArrayTypeSuffix, { T(TOKEN_LBRACKET), T(TOKEN_RBRACKET), NT_ArrayTypeSuffix }, 3 },
    { NT_ArrayTypeSuffix, { 0 }, 0 },
    { NT_ArrayInit, { T(TOKEN_LBRACE), T(TOKEN_IDENT), T(TOKEN_ARROW),
                      NT_Expr, T(TOKEN_RBRACE), A_ARRAY_INIT }, 6 },
    { NT_ArrayInit, { 0 }, 0 },
    /* VecItems -> Expr VecItemsT | ε ; VecItemsT -> COMMA Expr VecItemsT | ε */
    { NT_VecItems, { NT_Expr, NT_VecItemsT }, 2 },
    { NT_VecItems, { 0 }, 0 },
    { NT_VecItemsT, { T(TOKEN_COMMA), NT_Expr, NT_VecItemsT }, 3 },
    { NT_VecItemsT, { 0 }, 0 },

    /* Let -> LET @sent Bindings IN Body @let */
    { NT_Let, { T(TOKEN_LET), A_SENT, NT_Bindings, T(TOKEN_IN), NT_Body, A_LET }, 6 },
    { NT_Bindings, { NT_Binding, NT_BindingsT }, 2 },
    { NT_BindingsT, { T(TOKEN_COMMA), NT_Binding, NT_BindingsT }, 3 },
    { NT_BindingsT, { 0 }, 0 },
    /* Binding -> IDENT TypeAnn ASSIGN Expr @bind */
    { NT_Binding, { T(TOKEN_IDENT), NT_TypeAnn, T(TOKEN_ASSIGN), NT_Expr, A_BIND }, 5 },
    { NT_Binding, { T(TOKEN_BASE), NT_TypeAnn, T(TOKEN_ASSIGN), NT_Expr, A_BIND }, 5 },
    /* TypeAnn -> COLON TypeRef @typename | ε @typenone */
    { NT_TypeAnn, { T(TOKEN_COLON), NT_TypeRef, A_TYPE_NAME }, 3 },
    { NT_TypeAnn, { A_TYPE_NONE }, 1 },
    /* TypeRef -> (IDENT|function type) TypeSuffix */
    { NT_TypeRef, { T(TOKEN_IDENT), NT_TypeSuffix }, 2 },
    { NT_TypeRef, { T(TOKEN_LPAREN), A_SENT, NT_TypeList, T(TOKEN_RPAREN),
                    T(TOKEN_ARROW), NT_TypeRef, A_TYPE_FUNC, NT_TypeSuffix }, 8 },
    { NT_TypeSuffix, { T(TOKEN_LBRACKET), T(TOKEN_RBRACKET), A_TYPE_ARRAY, NT_TypeSuffix }, 4 },
    { NT_TypeSuffix, { T(TOKEN_MULT), A_TYPE_ITER, NT_TypeSuffix }, 3 },
    { NT_TypeSuffix, { 0 }, 0 },
    /* TypeList -> TypeRef TypeListT | ε ; TypeListT -> COMMA TypeRef TypeListT | ε */
    { NT_TypeList, { NT_TypeRef, NT_TypeListT }, 2 },
    { NT_TypeList, { 0 }, 0 },
    { NT_TypeListT, { T(TOKEN_COMMA), NT_TypeRef, NT_TypeListT }, 3 },
    { NT_TypeListT, { 0 }, 0 },

    /* If -> IF LPAREN Expr RPAREN Body ElifL ELSE Body @if */
    { NT_If, { T(TOKEN_IF), T(TOKEN_LPAREN), NT_Expr, T(TOKEN_RPAREN), NT_Body, A_SENT, NT_ElifL, T(TOKEN_ELSE), NT_Body, A_IF }, 10 },
    /* ElifL -> ELIF LPAREN Expr RPAREN Body @elif ElifL | ε */
    { NT_ElifL, { T(TOKEN_ELIF), T(TOKEN_LPAREN), NT_Expr, T(TOKEN_RPAREN), NT_Body, A_ELIF, NT_ElifL }, 7 },
    { NT_ElifL, { 0 }, 0 },
    /* Body -> Block | While | For | Expr  (el cuerpo de let/if/while/for
       puede ser un loop; FIRST disjuntos: LBRACE / WHILE / FOR / resto) */
    { NT_Body, { NT_Block }, 1 },
    { NT_Body, { NT_While }, 1 },
    { NT_Body, { NT_For }, 1 },
    { NT_Body, { NT_Expr }, 1 },

    /* While -> WHILE Expr Body @while  (Body=Block|Expr) */
    { NT_While, { T(TOKEN_WHILE), NT_Expr, NT_Body, A_WHILE }, 4 },
    /* For -> FOR LPAREN IDENT IN Expr RPAREN Body @for */
    { NT_For, { T(TOKEN_FOR), T(TOKEN_LPAREN), T(TOKEN_IDENT), T(TOKEN_IN), NT_Expr, T(TOKEN_RPAREN), NT_Body, A_FOR }, 8 },
    /* Block -> LBRACE @blockbegin StmtList RBRACE @block */
    { NT_Block, { T(TOKEN_LBRACE), A_BLOCK_BEGIN, NT_StmtList, T(TOKEN_RBRACE), A_BLOCK }, 5 },

    /* ---- Capa 2: definiciones ---- */
    /* FunctionDef -> FUNCTION IDENT LPAREN @sent Params RPAREN TypeAnn FuncBody @funcdef */
    { NT_FunctionDef, { T(TOKEN_FUNCTION), T(TOKEN_IDENT), T(TOKEN_LPAREN), A_SENT,
                        NT_Params, T(TOKEN_RPAREN), NT_TypeAnn, NT_FuncBody, A_FUNCDEF }, 9 },
    /* Params -> Param ParamsT | ε ; ParamsT -> COMMA Param ParamsT | ε */
    { NT_Params, { NT_Param, NT_ParamsT }, 2 },
    { NT_Params, { 0 }, 0 },
    { NT_ParamsT, { T(TOKEN_COMMA), NT_Param, NT_ParamsT }, 3 },
    { NT_ParamsT, { 0 }, 0 },
    /* Param -> IDENT TypeAnn @param */
    { NT_Param, { T(TOKEN_IDENT), NT_TypeAnn, A_PARAM }, 3 },
    { NT_Param, { T(TOKEN_BASE), NT_TypeAnn, A_PARAM }, 3 },
    /* FuncBody -> ARROW Expr SEMICOLON | Block */
    { NT_FuncBody, { T(TOKEN_ARROW), NT_Expr, T(TOKEN_SEMICOLON) }, 3 },
    { NT_FuncBody, { NT_Block }, 1 },
    /* FuncExprBody -> ARROW Expr | Block   (cuerpo de lambda; sin `;`) */
    { NT_FuncExprBody, { T(TOKEN_ARROW), NT_Expr }, 2 },
    { NT_FuncExprBody, { NT_Block }, 1 },

    /* Lambda (FunctionExpr): se empuja por lookahead. Dos formas:
       function LPAREN @sent Params RPAREN TypeAnn FuncExprBody @funcexpr
       LPAREN @sent Params RPAREN TypeAnn FuncExprBody @funcexpr */
    { NT_Lambda, { T(TOKEN_FUNCTION), T(TOKEN_LPAREN), A_SENT, NT_Params, T(TOKEN_RPAREN),
                   NT_TypeAnn, NT_FuncExprBody, A_FUNCEXPR }, 8 },
    { NT_Lambda, { T(TOKEN_LPAREN), A_SENT, NT_Params, T(TOKEN_RPAREN),
                   NT_TypeAnn, NT_FuncExprBody, A_FUNCEXPR }, 7 },

    /* TypeDef -> TYPE IDENT @td_begin TypeParams TypeInherit LBRACE TypeBody RBRACE */
    { NT_TypeDef, { T(TOKEN_TYPE), T(TOKEN_IDENT), A_TD_BEGIN, NT_TypeParams,
                    NT_TypeInherit, T(TOKEN_LBRACE), NT_TypeBody, T(TOKEN_RBRACE) }, 8 },
    /* TypeParams -> LPAREN @sent Params RPAREN @td_params | ε */
    { NT_TypeParams, { T(TOKEN_LPAREN), A_SENT, NT_Params, T(TOKEN_RPAREN), A_TD_PARAMS }, 5 },
    { NT_TypeParams, { 0 }, 0 },
    /* TypeInherit -> INHERITS IDENT @td_parent TypeBaseArgs | ε */
    { NT_TypeInherit, { T(TOKEN_INHERITS), T(TOKEN_IDENT), A_TD_PARENT, NT_TypeBaseArgs }, 4 },
    { NT_TypeInherit, { 0 }, 0 },
    /* TypeBaseArgs -> LPAREN @sent Args RPAREN @td_pargs | ε */
    { NT_TypeBaseArgs, { T(TOKEN_LPAREN), A_SENT, NT_Args, T(TOKEN_RPAREN), A_TD_PARGS }, 5 },
    { NT_TypeBaseArgs, { 0 }, 0 },
    /* TypeBody -> TypeMember TypeBody | ε */
    { NT_TypeBody, { NT_TypeMember, NT_TypeBody }, 2 },
    { NT_TypeBody, { 0 }, 0 },
    /* TypeMember -> @sent DecorPrefix IDENT TypeMemberTail */
    { NT_TypeMember, { A_SENT, NT_DecorPrefix, T(TOKEN_IDENT), NT_TypeMemberTail }, 4 },
    { NT_TypeMember, { A_SENT, NT_DecorPrefix, T(TOKEN_BASE), NT_TypeMemberTail }, 4 },
    /* DecorPrefix -> DECOR DecorItems DecorPrefix | ε */
    { NT_DecorPrefix, { T(TOKEN_DECOR), NT_DecorItems, NT_DecorPrefix }, 3 },
    { NT_DecorPrefix, { 0 }, 0 },
    /* TypeMemberTail -> LPAREN @sent Params RPAREN TypeAnn MethodBody @method
                       | TypeAnn AttrTail */
    { NT_TypeMemberTail, { T(TOKEN_LPAREN), A_SENT, NT_Params, T(TOKEN_RPAREN),
                           NT_TypeAnn, NT_MethodBody, A_METHOD }, 7 },
    { NT_TypeMemberTail, { NT_TypeAnn, NT_AttrTail }, 2 },
    /* AttrTail -> ASSIGN Expr SEMICOLON @attr | SEMICOLON @attr_noinit */
    { NT_AttrTail, { T(TOKEN_ASSIGN), NT_Expr, T(TOKEN_SEMICOLON), A_ATTR }, 4 },
    { NT_AttrTail, { T(TOKEN_SEMICOLON), A_ATTR_NOINIT }, 2 },
    /* MethodBody -> ARROW Expr SEMICOLON | Block */
    { NT_MethodBody, { T(TOKEN_ARROW), NT_Expr, T(TOKEN_SEMICOLON) }, 3 },
    { NT_MethodBody, { NT_Block }, 1 },

    /* ProtocolDef -> PROTOCOL IDENT @proto_begin ProtoExt LBRACE ProtoSigs RBRACE */
    { NT_ProtocolDef, { T(TOKEN_PROTOCOL), T(TOKEN_IDENT), A_PROTO_BEGIN, NT_ProtoExt,
                        T(TOKEN_LBRACE), NT_ProtoSigs, T(TOKEN_RBRACE) }, 7 },
    /* ProtoExt -> EXTENDS IDENT @td_parent | ε */
    { NT_ProtoExt, { T(TOKEN_EXTENDS), T(TOKEN_IDENT), A_TD_PARENT }, 3 },
    { NT_ProtoExt, { 0 }, 0 },
    /* ProtoSigs -> ProtoSig ProtoSigs | ε */
    { NT_ProtoSigs, { NT_ProtoSig, NT_ProtoSigs }, 2 },
    { NT_ProtoSigs, { 0 }, 0 },
    /* ProtoSig -> IDENT LPAREN @sent Params RPAREN TypeAnn SEMICOLON @proto_method */
    { NT_ProtoSig, { T(TOKEN_IDENT), T(TOKEN_LPAREN), A_SENT, NT_Params, T(TOKEN_RPAREN),
                     NT_TypeAnn, T(TOKEN_SEMICOLON), A_PROTO_METHOD }, 8 },

    /* DecorBlock -> DECOR @sent DecorItems DecorMore DecorTarget @decor_block */
    { NT_DecorBlock, { T(TOKEN_DECOR), A_SENT, NT_DecorItems, NT_DecorMore,
                       NT_DecorTarget, A_DECOR_BLOCK }, 6 },
    /* DecorMore -> DECOR DecorItems DecorMore | ε */
    { NT_DecorMore, { T(TOKEN_DECOR), NT_DecorItems, NT_DecorMore }, 3 },
    { NT_DecorMore, { 0 }, 0 },
    /* DecorTarget -> FunctionDef | TypeDef */
    { NT_DecorTarget, { NT_FunctionDef }, 1 },
    { NT_DecorTarget, { NT_TypeDef }, 1 },
    /* DecorItems -> DecorItem DecorItemsT ; DecorItemsT -> COMMA DecorItem DecorItemsT | ε */
    { NT_DecorItems, { NT_DecorItem, NT_DecorItemsT }, 2 },
    { NT_DecorItemsT, { T(TOKEN_COMMA), NT_DecorItem, NT_DecorItemsT }, 3 },
    { NT_DecorItemsT, { 0 }, 0 },
    /* DecorItem -> IDENT @sent DecorArgs @decor_item */
    { NT_DecorItem, { T(TOKEN_IDENT), A_SENT, NT_DecorArgs, A_DECOR_ITEM }, 4 },
    /* DecorArgs -> LPAREN Args RPAREN | ε */
    { NT_DecorArgs, { T(TOKEN_LPAREN), NT_Args, T(TOKEN_RPAREN) }, 3 },
    { NT_DecorArgs, { 0 }, 0 },
};
#define HULK_PROD_COUNT ((int)(sizeof(HULK_PRODS)/sizeof(HULK_PRODS[0])))

static const char *NT_NAMES[NT_COUNT] = {
    "Program","TopList","TopItem","OptSemi","StmtList","TermStmt","Stmt","Expr","Or","Or'",
    "And","And'","Cmp","Cmp'","Concat","Concat'","Add","Add'","Term","Term'",
    "Factor","Factor'","Unary","Postfix","Primary","Call","Args","Args'","Let",
    "Bindings","Bindings'","Binding","TypeAnn","If","ElifL","Body","While",
    "For","Block","VecItems","VecItems'",
    "FunctionDef","Params","ParamsT","Param","FuncBody","FuncExprBody",
    "TypeDef","TypeParams","TypeInherit","TypeBaseArgs","TypeBody","TypeMember",
    "TypeMemberTail","AttrTail","MethodBody","ProtocolDef","ProtoExt","ProtoSigs",
    "ProtoSig","DecorBlock","DecorMore","DecorTarget","DecorItems","DecorItemsT",
    "DecorItem","DecorArgs","DecorPrefix","TypeRef","TypeSuffix","TypeList","TypeListT",
    "NewTail","ArrayTypeSuffix","ArrayInit",
    "Lambda"
};

/* ============================================================
 *  Pila semántica tipada
 * ============================================================ */
typedef enum { V_NODE, V_LEX, V_SENT } VKind;
typedef struct { VKind k; HulkNode *node; char *lex; } SemVal;

typedef struct {
    HulkASTContext *ctx;
    SemVal *s;
    int sp, cap;
    int had_error;
} SemStack;

static void sv_push_node(SemStack *S, HulkNode *n) {
    if (S->sp >= S->cap) return;
    S->s[S->sp].k = V_NODE; S->s[S->sp].node = n; S->s[S->sp].lex = NULL; S->sp++;
}
static void sv_push_lex(SemStack *S, char *lex) {
    if (S->sp >= S->cap) return;
    S->s[S->sp].k = V_LEX; S->s[S->sp].node = NULL; S->s[S->sp].lex = lex; S->sp++;
}
static void sv_push_sent(SemStack *S) {
    if (S->sp >= S->cap) return;
    S->s[S->sp].k = V_SENT; S->s[S->sp].node = NULL; S->s[S->sp].lex = NULL; S->sp++;
}
static HulkNode* sv_pop_node(SemStack *S) {
    if (S->sp <= 0) { S->had_error = 1; return NULL; }
    SemVal v = S->s[--S->sp];
    if (v.k != V_NODE) { S->had_error = 1; return NULL; }
    return v.node;
}
static char* sv_pop_lex(SemStack *S) {
    if (S->sp <= 0) { S->had_error = 1; return NULL; }
    SemVal v = S->s[--S->sp];
    if (v.k != V_LEX) { S->had_error = 1; return NULL; }
    return v.lex;
}

/* Mira (sin extraer) el nodo en el tope de la pila. Usado por las
 * definiciones de tipo, que dejan el TypeDefNode como acumulador al fondo
 * y le agregan miembros sin sacarlo. */
static HulkNode* sv_peek_node(SemStack *S) {
    if (S->sp <= 0) { S->had_error = 1; return NULL; }
    SemVal v = S->s[S->sp - 1];
    if (v.k != V_NODE) { S->had_error = 1; return NULL; }
    return v.node;
}

/* Recolecta los nodos por encima del centinela en una HulkNodeList (en
 * orden de aparición); consume también el centinela. */
static void sv_collect_to_sentinel(SemStack *S, HulkNodeList *out) {
    /* contar hasta el centinela */
    int start = S->sp;
    while (start > 0 && S->s[start-1].k != V_SENT) start--;
    int first = start; /* índice del primer elemento tras el centinela */
    for (int i = first; i < S->sp; i++)
        if (S->s[i].k == V_NODE)
            hulk_node_list_push(out, S->s[i].node);
    /* descartar elementos + centinela */
    S->sp = (first > 0) ? first - 1 : 0;
}

static char* ll1_join3(HulkASTContext *ctx, const char *a,
                       const char *mid, const char *c) {
    const char *sa = a ? a : "";
    const char *sm = mid ? mid : "";
    const char *sc = c ? c : "";
    size_t la = strlen(sa), lm = strlen(sm), lc = strlen(sc);
    char *out = hulk_ast_alloc(ctx, la + lm + lc + 1);
    memcpy(out, sa, la);
    memcpy(out + la, sm, lm);
    memcpy(out + la + lm, sc, lc);
    out[la + lm + lc] = '\0';
    return out;
}

static char* sv_collect_lex_to_sentinel(SemStack *S) {
    int start = S->sp;
    while (start > 0 && S->s[start-1].k != V_SENT) start--;
    int first = start;

    char *out = hulk_ast_strdup(S->ctx, "");
    int seen = 0;
    for (int i = first; i < S->sp; i++) {
        if (S->s[i].k != V_LEX) continue;
        if (seen)
            out = ll1_join3(S->ctx, out, ",", S->s[i].lex);
        else
            out = hulk_ast_strdup(S->ctx, S->s[i].lex ? S->s[i].lex : "");
        seen = 1;
    }

    S->sp = (first > 0) ? first - 1 : 0;
    return out;
}

/* ============================================================
 *  Acciones — construyen el AST sobre la pila semántica
 * ============================================================ */
static void exec_hulk_action(int act, SemStack *S, int line, int col) {
    HulkASTContext *c = S->ctx;
    switch (act) {
        case A_NUM: { char *l = sv_pop_lex(S);
            sv_push_node(S, (HulkNode*)hulk_ast_number_lit(c, l ? l : "0", line, col)); break; }
        case A_STR: { char *l = sv_pop_lex(S);
            /* el lexema viene con comillas; strip */
            char *content = l ? l : "";
            int len = (int)strlen(content);
            char *body = hulk_ast_alloc(c, len > 1 ? len - 1 : 1);
            if (len >= 2) { memcpy(body, content+1, len-2); body[len-2]='\0'; }
            else body[0]='\0';
            sv_push_node(S, (HulkNode*)hulk_ast_string_lit(c, body, line, col)); break; }
        case A_TRUE:  sv_push_node(S, (HulkNode*)hulk_ast_bool_lit(c, 1, line, col)); break;
        case A_FALSE: sv_push_node(S, (HulkNode*)hulk_ast_bool_lit(c, 0, line, col)); break;
        case A_IDENT: { char *l = sv_pop_lex(S);
            sv_push_node(S, (HulkNode*)hulk_ast_ident(c, l ? l : "?", line, col)); break; }
        case A_SELF:  sv_push_node(S, (HulkNode*)hulk_ast_self(c, line, col)); break;

        case A_OR: case A_AND: case A_LT: case A_GT: case A_LE: case A_GE:
        case A_EQ: case A_NEQ: case A_ADD: case A_SUB: case A_MUL:
        case A_DIV: case A_MOD: case A_POW: {
            HulkNode *r = sv_pop_node(S), *l = sv_pop_node(S);
            BinaryOp op = OP_ADD;
            switch (act) {
                case A_OR: op=OP_OR; break; case A_AND: op=OP_AND; break;
                case A_LT: op=OP_LT; break; case A_GT: op=OP_GT; break;
                case A_LE: op=OP_LE; break; case A_GE: op=OP_GE; break;
                case A_EQ: op=OP_EQ; break; case A_NEQ: op=OP_NEQ; break;
                case A_ADD: op=OP_ADD; break; case A_SUB: op=OP_SUB; break;
                case A_MUL: op=OP_MUL; break; case A_DIV: op=OP_DIV; break;
                case A_MOD: op=OP_MOD; break; case A_POW: op=OP_POW; break;
            }
            sv_push_node(S, (HulkNode*)hulk_ast_binary_op(c, op, l, r, line, col)); break; }
        case A_CONCAT: case A_CONCATWS: {
            HulkNode *r = sv_pop_node(S), *l = sv_pop_node(S);
            BinaryOp op = (act==A_CONCATWS) ? OP_CONCAT_WS : OP_CONCAT;
            sv_push_node(S, (HulkNode*)hulk_ast_concat_expr(c, op, l, r, line, col)); break; }
        case A_NEG: { HulkNode *o = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_unary_op(c, o, line, col)); break; }
        case A_NOT: { HulkNode *o = sv_pop_node(S);
            UnaryOpNode *u = hulk_ast_unary_op(c, o, line, col); u->is_not = 1;
            sv_push_node(S, (HulkNode*)u); break; }
        case A_AS: { char *tn = sv_pop_lex(S); HulkNode *e = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_as_expr(c, e, tn ? tn : "Object", line, col)); break; }
        case A_IS: { char *tn = sv_pop_lex(S); HulkNode *e = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_is_expr(c, e, tn ? tn : "Object", line, col)); break; }

        case A_SENT: sv_push_sent(S); break;
        case A_CALL: { /* args sobre centinela; debajo el callee */
            CallExprNode *call = hulk_ast_call_expr(c, NULL, line, col);
            sv_collect_to_sentinel(S, &call->args);
            HulkNode *callee = sv_pop_node(S);
            call->callee = callee;
            sv_push_node(S, (HulkNode*)call); break; }
        case A_INDEX: { HulkNode *idx = sv_pop_node(S), *obj = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_index_expr(c, obj, idx, line, col)); break; }
        case A_MEMBER: { char *m = sv_pop_lex(S); HulkNode *obj = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_member_access(c, obj, m ? m : "?", line, col)); break; }
        case A_ASSIGN: { HulkNode *val = sv_pop_node(S), *tgt = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_assign(c, tgt, val, line, col)); break; }
        case A_DESTRUCT: { HulkNode *val = sv_pop_node(S), *tgt = sv_pop_node(S);
            sv_push_node(S, (HulkNode*)hulk_ast_destruct_assign(c, tgt, val, line, col)); break; }
        case A_NEW: { /* args sobre centinela; debajo el lexema del typename */
            NewExprNode *ne = hulk_ast_new_expr(c, "?", line, col);
            sv_collect_to_sentinel(S, &ne->args);
            char *tn = sv_pop_lex(S);
            ne->type_name = tn ? hulk_ast_strdup(c, tn) : ne->type_name;
            sv_push_node(S, (HulkNode*)ne); break; }
        case A_BASE: { BaseCallNode *bc = hulk_ast_base_call(c, line, col);
            sv_collect_to_sentinel(S, &bc->args);
            (void)sv_pop_lex(S); /* keyword `base` */
            sv_push_node(S, (HulkNode*)bc); break; }
        case A_VEC: { VectorLitNode *v = hulk_ast_vector_lit(c, line, col);
            sv_collect_to_sentinel(S, &v->items);
            sv_push_node(S, (HulkNode*)v); break; }
        case A_ARRAY_NEW: {
            HulkNode *size = sv_pop_node(S);
            char *tn = sv_pop_lex(S);
            (void)tn;
            CallExprNode *call = hulk_ast_call_expr(
                c, (HulkNode*)hulk_ast_ident(c, "__array_new", line, col),
                line, col);
            hulk_node_list_push(&call->args, size);
            sv_push_node(S, (HulkNode*)call);
            break; }
        case A_ARRAY_INIT: {
            HulkNode *body = sv_pop_node(S);
            char *idx_name = sv_pop_lex(S);
            HulkNode *node = sv_pop_node(S);
            if (!node || node->type != NODE_CALL_EXPR) {
                S->had_error = 1;
                break;
            }
            CallExprNode *call = (CallExprNode*)node;
            if (call->callee && call->callee->type == NODE_IDENT)
                ((IdentNode*)call->callee)->name = hulk_ast_strdup(c, "__array_init");
            FunctionExprNode *fn = hulk_ast_function_expr(c, "Number", line, col);
            VarBindingNode *p = hulk_ast_var_binding(c, idx_name ? idx_name : "i",
                                                     "Number", line, col);
            hulk_node_list_push(&fn->params, (HulkNode*)p);
            fn->body = body;
            hulk_node_list_push(&call->args, (HulkNode*)fn);
            sv_push_node(S, (HulkNode*)call);
            break; }

        case A_TYPE_NAME: break;  /* el lexema del tipo queda en la pila como V_LEX */
        case A_TYPE_NONE: sv_push_lex(S, NULL); break;  /* slot de tipo vacío */
        case A_TYPE_FUNC: {
            char *ret = sv_pop_lex(S);
            char *params = sv_collect_lex_to_sentinel(S);
            char *head = ll1_join3(c, "(", params, ")->");
            sv_push_lex(S, ll1_join3(c, head, "", ret));
            break; }
        case A_TYPE_ARRAY: {
            char *base = sv_pop_lex(S);
            sv_push_lex(S, ll1_join3(c, base, "", "[]"));
            break; }
        case A_TYPE_ITER: {
            char *base = sv_pop_lex(S);
            sv_push_lex(S, ll1_join3(c, base, "", "*"));
            break; }
        case A_BIND: { /* … IDENT TypeAnn ASSIGN Expr : pila = [name, type, init] */
            HulkNode *init = sv_pop_node(S);
            char *type = sv_pop_lex(S);
            char *name = sv_pop_lex(S);
            VarBindingNode *vb = hulk_ast_var_binding(c, name ? name : "?", type, line, col);
            vb->init_expr = init;
            sv_push_node(S, (HulkNode*)vb); break; }
        case A_LET: { /* body sobre la pila; bindings sobre centinela */
            HulkNode *body = sv_pop_node(S);
            LetExprNode *let = hulk_ast_let_expr(c, line, col);
            sv_collect_to_sentinel(S, &let->bindings);
            let->body = body;
            sv_push_node(S, (HulkNode*)let); break; }

        case A_ELIF: { /* … Expr Body : pila = [cond, body] */
            HulkNode *body = sv_pop_node(S), *cond = sv_pop_node(S);
            ElifBranchNode *e = hulk_ast_elif_branch(c, line, col);
            e->condition = cond; e->body = body;
            sv_push_node(S, (HulkNode*)e); break; }
        case A_IF: { /* pila: [cond, then, SENT, elif*, else] */
            HulkNode *else_b = sv_pop_node(S);
            IfExprNode *iff = hulk_ast_if_expr(c, line, col);
            /* recolectar elifs hasta el centinela */
            HulkNodeList elifs; hulk_node_list_init(&elifs);
            sv_collect_to_sentinel(S, &elifs);
            HulkNode *then_b = sv_pop_node(S);
            HulkNode *cond = sv_pop_node(S);
            iff->condition = cond; iff->then_body = then_b; iff->else_body = else_b;
            iff->elifs = elifs;
            sv_push_node(S, (HulkNode*)iff); break; }
        case A_WHILE: { HulkNode *body = sv_pop_node(S), *cond = sv_pop_node(S);
            WhileStmtNode *w = hulk_ast_while_stmt(c, line, col);
            w->condition = cond; w->body = body;
            sv_push_node(S, (HulkNode*)w); break; }
        case A_FOR: { HulkNode *body = sv_pop_node(S), *iter = sv_pop_node(S);
            char *var = sv_pop_lex(S);
            ForStmtNode *f = hulk_ast_for_stmt(c, var ? var : "?", line, col);
            f->iterable = iter; f->body = body;
            sv_push_node(S, (HulkNode*)f); break; }
        case A_BLOCK_BEGIN: sv_push_sent(S); break;
        case A_BLOCK: { BlockStmtNode *b = hulk_ast_block_stmt(c, line, col);
            sv_collect_to_sentinel(S, &b->statements);
            sv_push_node(S, (HulkNode*)b); break; }

        /* ---- Capa 2: definiciones ---- */
        case A_PARAM: { char *type = sv_pop_lex(S); char *name = sv_pop_lex(S);
            sv_push_node(S, (HulkNode*)hulk_ast_var_binding(c, name?name:"?", type, line, col));
            break; }
        case A_FUNCDEF: { HulkNode *body = sv_pop_node(S); char *ret = sv_pop_lex(S);
            HulkNodeList params; hulk_node_list_init(&params);
            sv_collect_to_sentinel(S, &params);
            char *name = sv_pop_lex(S);
            FunctionDefNode *fn = hulk_ast_function_def(c, name?name:"?", ret, line, col);
            fn->params = params; fn->body = body;
            sv_push_node(S, (HulkNode*)fn); break; }
        case A_FUNCEXPR: { HulkNode *body = sv_pop_node(S); char *ret = sv_pop_lex(S);
            HulkNodeList params; hulk_node_list_init(&params);
            sv_collect_to_sentinel(S, &params);
            FunctionExprNode *fn = hulk_ast_function_expr(c, ret, line, col);
            fn->params = params; fn->body = body;
            sv_push_node(S, (HulkNode*)fn); break; }

        case A_TD_BEGIN: { char *name = sv_pop_lex(S);
            sv_push_node(S, (HulkNode*)hulk_ast_type_def(c, name?name:"?", NULL, line, col));
            break; }
        case A_PROTO_BEGIN: { char *name = sv_pop_lex(S);
            TypeDefNode *td = hulk_ast_type_def(c, name?name:"?", NULL, line, col);
            td->is_protocol = 1;
            sv_push_node(S, (HulkNode*)td); break; }
        case A_TD_PARAMS: { HulkNodeList params; hulk_node_list_init(&params);
            sv_collect_to_sentinel(S, &params);
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) td->params = params;
            break; }
        case A_TD_PARENT: { char *p = sv_pop_lex(S);
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td && p) td->parent = hulk_ast_strdup(c, p);
            break; }
        case A_TD_PARGS: { HulkNodeList args; hulk_node_list_init(&args);
            sv_collect_to_sentinel(S, &args);
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) td->parent_args = args;
            break; }
        case A_METHOD: { HulkNode *body = sv_pop_node(S); char *ret = sv_pop_lex(S);
            HulkNodeList params; hulk_node_list_init(&params);
            sv_collect_to_sentinel(S, &params);
            char *name = sv_pop_lex(S);
            HulkNodeList decorators; hulk_node_list_init(&decorators);
            sv_collect_to_sentinel(S, &decorators);
            MethodDefNode *m = hulk_ast_method_def(c, name?name:"?", ret, line, col);
            m->params = params; m->body = body;
            m->decorators = decorators;
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) hulk_node_list_push(&td->members, (HulkNode*)m);
            break; }
        case A_ATTR: { HulkNode *init = sv_pop_node(S); char *type = sv_pop_lex(S);
            char *name = sv_pop_lex(S);
            HulkNodeList decorators; hulk_node_list_init(&decorators);
            sv_collect_to_sentinel(S, &decorators);
            hulk_node_list_free(&decorators);
            AttributeDefNode *a = hulk_ast_attribute_def(c, name?name:"?", type, line, col);
            a->init_expr = init;
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) hulk_node_list_push(&td->members, (HulkNode*)a);
            break; }
        case A_ATTR_NOINIT: { char *type = sv_pop_lex(S); char *name = sv_pop_lex(S);
            HulkNodeList decorators; hulk_node_list_init(&decorators);
            sv_collect_to_sentinel(S, &decorators);
            hulk_node_list_free(&decorators);
            AttributeDefNode *a = hulk_ast_attribute_def(c, name?name:"?", type, line, col);
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) hulk_node_list_push(&td->members, (HulkNode*)a);
            break; }
        case A_PROTO_METHOD: { char *ret = sv_pop_lex(S);
            HulkNodeList params; hulk_node_list_init(&params);
            sv_collect_to_sentinel(S, &params);
            char *name = sv_pop_lex(S);
            MethodDefNode *m = hulk_ast_method_def(c, name?name:"?", ret, line, col);
            m->params = params;
            m->body = (HulkNode*)hulk_ast_number_lit(c, "0", line, col); /* dummy */
            TypeDefNode *td = (TypeDefNode*)sv_peek_node(S);
            if (td) hulk_node_list_push(&td->members, (HulkNode*)m);
            break; }
        case A_DECOR_ITEM: {
            DecorItemNode *di = hulk_ast_decor_item(c, "?", line, col);
            sv_collect_to_sentinel(S, &di->args);
            char *name = sv_pop_lex(S);
            di->name = name ? hulk_ast_strdup(c, name) : di->name;
            sv_push_node(S, (HulkNode*)di);
            break; }
        case A_DECOR_BLOCK: {
            HulkNode *target = sv_pop_node(S);
            DecorBlockNode *db = hulk_ast_decor_block(c, line, col);
            sv_collect_to_sentinel(S, &db->decorators);
            db->target = target;
            sv_push_node(S, (HulkNode*)db);
            break; }
        default: break;
    }
}

/* ============================================================
 *  Construcción de la gramática pura (para FIRST/FOLLOW/tabla)
 * ============================================================ */
typedef struct {
    Grammar g;
    First_Table first;
    Follow_Table follow;
    LL1_Table ll1;
    int prod_index[HULK_PROD_COUNT]; /* prod data idx -> grammar prod id (==idx) */
    int initialized;
} HulkLL1;

static HulkLL1 G;  /* singleton; construido una vez */

static void build_grammar(void) {
    grammar_init(&G.g, "hulk");
    /* registrar no-terminales en orden de su id */
    for (int i = 0; i < NT_COUNT; i++)
        grammar_add_nonterminal(&G.g, NT_NAMES[i]);
    G.g.start_symbol = NT_Program;

    /* registrar terminales que aparecen (por TokenType). Usamos un set. */
    int seen[256] = {0};
    for (int p = 0; p < HULK_PROD_COUNT; p++)
        for (int k = 0; k < HULK_PRODS[p].n; k++) {
            int x = HULK_PRODS[p].rhs[k];
            if (IS_T(x)) { int tok = x - TBASE; if (tok>=0 && tok<256 && !seen[tok]) {
                seen[tok]=1; } }
        }
    /* nombres de terminales: usar get_token_name si existe; aquí simple */
    extern const char* get_token_name(int type);
    for (int tok = 0; tok < 256; tok++)
        if (seen[tok]) grammar_add_terminal(&G.g, get_token_name(tok), tok);

    /* producciones: SOLO símbolos NT/T (ignorar acciones) */
    for (int p = 0; p < HULK_PROD_COUNT; p++) {
        const Prod *pr = &HULK_PRODS[p];
        GrammarSymbol rhs[16]; int n = 0;
        for (int k = 0; k < pr->n; k++) {
            int x = pr->rhs[k];
            if (IS_ACT(x)) continue;
            if (IS_T(x)) rhs[n++] = (GrammarSymbol){SYMBOL_TERMINAL, x - TBASE};
            else         rhs[n++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, x};
        }
        int pid = grammar_add_production(&G.g, pr->lhs, n ? rhs : NULL, n);
        G.prod_index[p] = pid;
    }

    compute_first_sets(&G.g, &G.first);
    compute_follow_sets(&G.g, &G.first, &G.follow);
    if (!build_ll1_table(&G.g, &G.first, &G.follow, &G.ll1))
        LOG_WARN_MSG("ll1", "gramática HULK con conflictos LL(1) (resueltos por prioridad)");
    G.initialized = 1;
}

/* push del RHS COMPLETO (con acciones) en orden inverso a la pila */
static void push_production_actions(int prod_data_idx, GrammarSymbol *stk, int *top) {
    const Prod *pr = &HULK_PRODS[prod_data_idx];
    for (int k = pr->n - 1; k >= 0; k--) {
        int x = pr->rhs[k];
        if (IS_ACT(x))      stk[(*top)++] = (GrammarSymbol){SYMBOL_ACTION, x};
        else if (IS_T(x))   stk[(*top)++] = (GrammarSymbol){SYMBOL_TERMINAL, x - TBASE};
        else                stk[(*top)++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, x};
    }
}

/* ============================================================
 *  Lookahead local (resuelve los puntos no-LL(1) de HULK)
 * ============================================================ */

/* Tipo del token que sigue a `cur` sin alterar el lexer real. */
static int peek_next_type(LexerContext lx_copy) {
    Token t = lexer_next_token(&lx_copy);
    int ty = t.type;
    if (t.lexeme) free(t.lexeme);
    return ty;
}

static int next_type_inplace(LexerContext *lx_copy) {
    Token t = lexer_next_token(lx_copy);
    int ty = t.type;
    if (t.lexeme) free(t.lexeme);
    return ty;
}

static int lookahead_skip_type_ref(LexerContext *lx_copy) {
    int ty = next_type_inplace(lx_copy);
    if (ty == TOKEN_IDENT || ty == TOKEN_BASE) {
        /* nombre simple */
    } else if (ty == TOKEN_LPAREN) {
        ty = peek_next_type(*lx_copy);
        if (ty != TOKEN_RPAREN) {
            for (;;) {
                if (!lookahead_skip_type_ref(lx_copy)) return 0;
                ty = peek_next_type(*lx_copy);
                if (ty != TOKEN_COMMA) break;
                (void)next_type_inplace(lx_copy);
            }
        }

        if (next_type_inplace(lx_copy) != TOKEN_RPAREN) return 0;
        if (next_type_inplace(lx_copy) != TOKEN_ARROW) return 0;
        if (!lookahead_skip_type_ref(lx_copy)) return 0;
    } else {
        return 0;
    }

    for (;;) {
        ty = peek_next_type(*lx_copy);
        if (ty == TOKEN_MULT) {
            (void)next_type_inplace(lx_copy);
            continue;
        }
        if (ty == TOKEN_LBRACKET) {
            (void)next_type_inplace(lx_copy);
            if (next_type_inplace(lx_copy) != TOKEN_RBRACKET) return 0;
            continue;
        }
        break;
    }

    return 1;
}

/* Con `cur`==LPAREN y `lx` posicionado justo tras ese LPAREN, decide si
 * lo que sigue es una lambda `(params) ->` escaneando hasta el RPAREN
 * que balancea y mirando si viene ARROW. No altera el lexer real. */
static int lookahead_is_lambda(LexerContext lx_copy) {
    int depth = 1;
    for (;;) {
        Token t = lexer_next_token(&lx_copy);
        int ty = t.type;
        if (t.lexeme) free(t.lexeme);
        if (ty == TOKEN_EOF) return 0;
        if (ty == TOKEN_LPAREN) depth++;
        else if (ty == TOKEN_RPAREN) {
            if (--depth == 0) {
                int next = next_type_inplace(&lx_copy);
                if (next == TOKEN_ARROW) return 1;
                if (next != TOKEN_COLON) return 0;
                return lookahead_skip_type_ref(&lx_copy) &&
                       next_type_inplace(&lx_copy) == TOKEN_ARROW;
            }
        }
    }
}

/* ============================================================
 *  Parser principal
 * ============================================================ */
#define PSTACK_MAX 4096
#define SEMSTACK_MAX 4096

HulkNode* hulk_ll1_build_ast(HulkASTContext *ctx, DFA *dfa, const char *input) {
    if (!ctx || !dfa || !input) return NULL;
    if (!G.initialized) build_grammar();

    LexerContext lx; lexer_init(&lx, dfa, input);
    Token cur = lexer_next_token(&lx);
    int last_line = cur.line;
    int last_col = cur.col;

    GrammarSymbol pstk[PSTACK_MAX]; int ptop = 0;
    SemVal semarr[SEMSTACK_MAX];
    SemStack S = { ctx, semarr, 0, SEMSTACK_MAX, 0 };

    pstk[ptop++] = (GrammarSymbol){SYMBOL_END, 0};
    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Program};

    char *pending_lex = NULL;  /* lexema del último terminal con valor */
    int had_error = 0;

    while (ptop > 0 && !had_error) {
        GrammarSymbol top = pstk[--ptop];

        if (top.type == SYMBOL_END) break;
        if (top.type == SYMBOL_EPSILON) continue;

        if (top.type == SYMBOL_ACTION) {
            /* Antes de ejecutar la acción, si hay un lexema pendiente de un
             * terminal con valor (IDENT/NUMBER/STRING), empujarlo a la pila
             * semántica para que la acción lo consuma. */
            exec_hulk_action(top.id, &S, last_line, last_col);
            if (S.had_error) { had_error = 1; }
            continue;
        }

        if (top.type == SYMBOL_TERMINAL) {
            if (top.id == TOKEN_SEMICOLON && cur.type == TOKEN_EOF) {
                continue; /* el `;` final es opcional al EOF */
            }
            if (cur.type == (TokenType)top.id) {
                /* terminales con valor: empujar su lexema a la pila semántica */
                if (top.id == TOKEN_IDENT || top.id == TOKEN_BASE ||
                    top.id == TOKEN_NUMBER || top.id == TOKEN_STRING) {
                    char *dup = hulk_ast_strdup(ctx, cur.lexeme ? cur.lexeme : "");
                    sv_push_lex(&S, dup);
                }
                last_line = cur.line;
                last_col = cur.col;
                if (cur.lexeme) free(cur.lexeme);
                cur = lexer_next_token(&lx);
            } else {
                LOG_ERROR_MSG("ast_builder", "[%d:%d] se esperaba token %d, se encontró %d",
                              cur.line, cur.col, top.id, cur.type);
                had_error = 1;
            }
            continue;
        }

        /* ---- Lookahead local: puntos no-LL(1) de HULK ----
         * (a) TopItem con FUNCTION: def `function f(` vs expr `function(`.
         * (b) Primary con LPAREN: lambda `(x)->` vs paréntesis `(expr)`.
         * (c) Primary con FUNCTION: siempre lambda (FunctionExpr). */
        if (top.type == SYMBOL_NON_TERMINAL) {
            int lambda_start =
                (cur.type == TOKEN_FUNCTION) ||
                (cur.type == TOKEN_LPAREN && lookahead_is_lambda(lx));
            if (lambda_start) {
                if (top.id == NT_Expr) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Or};
                    continue;
                }
                if (top.id == NT_Or) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_OrP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_And};
                    continue;
                }
                if (top.id == NT_And) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_AndP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Cmp};
                    continue;
                }
                if (top.id == NT_Cmp) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_CmpP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Concat};
                    continue;
                }
                if (top.id == NT_Concat) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ConcatP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Add};
                    continue;
                }
                if (top.id == NT_Add) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_AddP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Term};
                    continue;
                }
                if (top.id == NT_Term) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_TermP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Factor};
                    continue;
                }
                if (top.id == NT_Factor) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_FactorP};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Unary};
                    continue;
                }
                if (top.id == NT_Unary) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Postfix};
                    continue;
                }
                if (top.id == NT_Postfix) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Call};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Primary};
                    continue;
                }
                if (top.id == NT_Stmt || top.id == NT_Body) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Expr};
                    continue;
                }
                if (top.id == NT_TermStmt) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_TERMINAL, TOKEN_SEMICOLON};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Stmt};
                    continue;
                }
                if (top.id == NT_StmtList) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_StmtList};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_TermStmt};
                    continue;
                }
                if (top.id == NT_Args) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_ArgsT};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Expr};
                    continue;
                }
                if (top.id == NT_VecItems) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_VecItemsT};
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Expr};
                    continue;
                }
            }
            if (top.id == NT_TopItem && cur.type == TOKEN_FUNCTION) {
                if (peek_next_type(lx) == TOKEN_IDENT) {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_FunctionDef};
                } else {
                    pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_TermStmt};
                }
                continue;
            }
            if (top.id == NT_TopItem && cur.type == TOKEN_LBRACE) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_OptSemi};
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Block};
                continue;
            }
            if ((top.id == NT_Stmt || top.id == NT_Body) && cur.type == TOKEN_LBRACE) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Block};
                continue;
            }
            if (top.id == NT_ArrayTypeSuffix && cur.type == TOKEN_LBRACKET &&
                peek_next_type(lx) != TOKEN_RBRACKET) {
                continue; /* ε: este `[` pertenece al tamaño de new T[expr] */
            }
            if (top.id == NT_Call && cur.type == TOKEN_DOT &&
                peek_next_type(lx) == TOKEN_BASE) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Call};
                pstk[ptop++] = (GrammarSymbol){SYMBOL_ACTION, A_MEMBER};
                pstk[ptop++] = (GrammarSymbol){SYMBOL_TERMINAL, TOKEN_BASE};
                pstk[ptop++] = (GrammarSymbol){SYMBOL_TERMINAL, TOKEN_DOT};
                continue;
            }
            if (cur.type == TOKEN_EOF &&
                (top.id == NT_OrP || top.id == NT_AndP || top.id == NT_CmpP ||
                 top.id == NT_ConcatP || top.id == NT_AddP || top.id == NT_TermP ||
                 top.id == NT_FactorP || top.id == NT_Call)) {
                continue; /* ε: expresión final sin `;` explícito */
            }
            if (top.id == NT_Primary && cur.type == TOKEN_FUNCTION) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Lambda};
                continue;
            }
            if (top.id == NT_Primary && cur.type == TOKEN_LPAREN &&
                lookahead_is_lambda(lx)) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_NON_TERMINAL, NT_Lambda};
                continue;
            }
            if (top.id == NT_Primary && cur.type == TOKEN_BASE &&
                peek_next_type(lx) != TOKEN_LPAREN) {
                pstk[ptop++] = (GrammarSymbol){SYMBOL_ACTION, A_IDENT};
                pstk[ptop++] = (GrammarSymbol){SYMBOL_TERMINAL, TOKEN_BASE};
                continue;
            }
        }

        /* NON_TERMINAL: consultar tabla */
        int la = (cur.type == TOKEN_EOF) ? END_MARKER : (int)cur.type;
        int colm = ll1_table_get_column(&G.ll1, &G.g, la);
        int prod = (colm >= 0) ? G.ll1.table[top.id][colm] : -1;
        if (prod < 0) {
            LOG_ERROR_MSG("ast_builder", "[%d:%d] no hay producción para [%s, token %d]",
                          cur.line, cur.col,
                          (top.id>=0 && top.id<NT_COUNT)?NT_NAMES[top.id]:"?", la);
            had_error = 1;
            break;
        }
        /* prod es el grammar production id; coincide con el índice de datos */
        push_production_actions(prod, pstk, &ptop);
    }
    (void)pending_lex;

    if (cur.lexeme) free(cur.lexeme);

    if (had_error || S.had_error) return NULL;
    /* El resultado: el Program se construye implícitamente. Como no hay una
     * acción que arme ProgramNode (StmtList deja los stmts sueltos), los
     * recolectamos: el AST de cada TermStmt quedó en la pila en orden. */
    ProgramNode *prog = hulk_ast_program(ctx, 1, 1);
    for (int i = 0; i < S.sp; i++)
        if (S.s[i].k == V_NODE)
            hulk_node_list_push(&prog->declarations, S.s[i].node);
    return (HulkNode*)prog;
}
