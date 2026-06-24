# Informe tecnico del compilador HULK

**Proyecto:** HULK Compiler 2026  
**Asignatura:** Lenguajes de Programacion / Compilacion  
**Lenguaje de implementacion:** C99  
**Backend:** LLVM mediante la API C de LLVM  
**Entrada contractual:** `./hulk archivo.hulk`  
**Salida contractual esperada:** ejecutable nativo `./output`, si no hay errores

Este informe documenta el estado tecnico observable del proyecto a partir de sus fuentes internas: codigo C, gramatica, tests, logs, `README.md`, `agent.md`, `Makefile` y reportes de verificacion incluidos en el repositorio.

---

## 1. Introduccion

El proyecto implementa un compilador para HULK, sigla de *Havana University Language for Kompilers*, un lenguaje usado en el contexto docente de la asignatura de Compilacion. Segun `README.md`, el compilador recibe un archivo `.hulk`, ejecuta un pipeline de compilacion y, cuando la compilacion es exitosa, produce un ejecutable nativo llamado `./output`. La estructura real del repositorio confirma que el proyecto no se limita a tokenizar o parsear: contiene subsistemas para generacion lexica, analisis sintactico, construccion de AST, analisis semantico, sistema de tipos, transformaciones semanticas y generacion de codigo con LLVM.

El pipeline que debe describirse con precision es:

```text
fuente HULK
  -> lexer
  -> parser / builder del AST
  -> analisis semantico
  -> AST anotado con tipos
  -> LLVM IR
  -> objeto nativo
  -> ejecutable ./output
```

La evidencia central de esta arquitectura aparece en `README.md`, `hulk_cli.c`, `hulk_compiler.c`, `hulk_ast/builder/hulk_ll1_builder.c`, `hulk_ast/semantic/hulk_semantic_check.c`, `hulk_ast/semantic/hulk_semantic_desugar.c`, `hulk_ast/codegen/hulk_codegen.c` y `Makefile`. `agent.md` tambien advierte que la documentacion historica puede estar por detras del codigo, por lo que este informe toma el estado del codigo y los tests como fuente principal.

El compilador soporta un subconjunto amplio de HULK y varias extensiones observables:

- Expresiones aritmeticas, booleanas, comparaciones y concatenacion.
- Literales numericos, de string y booleanos.
- `let`, bloques, condicionales `if`/`elif`/`else`, ciclos `while` y `for`.
- Funciones globales, funciones anonimas/lambdas y funciones como valores.
- Tipos definidos por el usuario, atributos, metodos, herencia, `self`, `base`, `is` y `as`.
- Protocolos con conformidad estructural limitada.
- Vectores/arreglos, indexacion y operaciones auxiliares de arreglo.
- Decoradores sobre funciones y metodos, incluyendo decoradores parametrizados.
- Generacion de LLVM IR y emision de objeto nativo.

El objetivo academico del proyecto es mostrar el recorrido completo de un compilador: especificacion lexica, analisis sintactico, representacion intermedia de alto nivel mediante AST, comprobacion semantica, sistema de tipos, transformaciones del lenguaje y generacion de codigo de bajo nivel.

---

## 2. Arquitectura general del compilador

La arquitectura se divide en capas relativamente independientes. Esta separacion favorece mantenibilidad, depuracion por fase, pruebas por subsistema y extension futura del lenguaje.

### 2.1 Entrada y fachada del compilador

`hulk_cli.c` implementa el binario contractual `./hulk`. Sus responsabilidades observables son:

- leer la ruta del archivo fuente desde `argv[1]`;
- cargar el contenido del archivo;
- inicializar el compilador con `hulk_compiler_init`;
- construir el AST mediante `hulk_build_ast`;
- ejecutar `hulk_semantic_analyze`;
- generar ejecutable mediante `hulk_codegen_to_executable(ast, "./output")`;
- mapear diagnosticos internos a errores `LEXICAL`, `SYNTACTIC` y `SEMANTIC`.

El archivo `hulk_compiler.c` funciona como fachada de construccion del lexer y de ejecucion de pruebas del parser. Ahi se construye el DFA del lexer a partir de las expresiones regulares declaradas en `hulk_tokens.c`.

### 2.2 Analisis lexico

El subsistema `generador_analizadores_lexicos/` implementa un generador reutilizable de analizadores lexicos. La tabla de tokens HULK esta en `hulk_tokens.c`, mientras que el motor de lectura esta en `generador_analizadores_lexicos/lexer.c`.

### 2.3 Analisis sintactico y construccion del AST

El proyecto contiene dos niveles relacionados pero no identicos:

- Infraestructura generica LL(1) en `generador_parser_ll1/`, con calculo de FIRST, FOLLOW y tabla LL(1).
- Builder real de AST para HULK en `hulk_ast/builder/hulk_ll1_builder.c`, donde la gramatica se declara como datos (`HULK_PRODS`) con acciones semanticas intercaladas.

Esta distincion es importante porque `grammar.ll1` y los logs historicos muestran conflictos LL(1) en varios puntos, mientras que el builder integrado resuelve casos concretos con lookahead local.

### 2.4 AST

`hulk_ast/core/hulk_ast.h` define los tipos de nodo del AST. El AST almacena la estructura esencial del programa sin depender del texto original. `hulk_ast/core/hulk_ast_context.c` aporta una arena de memoria para asignar nodos y liberarlos en bloque. `hulk_ast/core/hulk_ast_visitor.c` implementa un patron visitante.

### 2.5 Analisis semantico

El analisis semantico vive en `hulk_ast/semantic/`. La API publica esta en `hulk_ast/semantic/hulk_semantic.h`; las estructuras internas estan en `hulk_ast/semantic/hulk_semantic_internal.h`.

La fase semantica realiza:

- desugaring de decoradores;
- construccion y consulta de scopes;
- registro de tipos, funciones, metodos y atributos;
- resolucion de nombres;
- verificacion de tipos;
- validacion de funciones, metodos, llamadas, control de flujo, arreglos, lambdas y decoradores;
- anotacion de nodos del AST con `HulkNode.static_type`.

### 2.6 Generacion LLVM

El backend esta en `hulk_ast/codegen/`. `hulk_ast/codegen/hulk_codegen.c` inicializa LLVM, genera el modulo, verifica el IR con `LLVMVerifyModule`, emite objeto nativo mediante `LLVMTargetMachineEmitToFile` y enlaza con `cc -lm`. El `Makefile` declara la dependencia de `llvm-config-18` o `llvm-config`, asi como las bibliotecas LLVM necesarias (`core`, `analysis`, `native`, `bitwriter`).

---

## 3. Analisis lexico

### 3.1 Estrategia de implementacion

El lexer HULK no esta escrito como una secuencia manual de `if` o `switch` sobre caracteres. El proyecto declara expresiones regulares por token en `hulk_tokens.c` y construye un DFA a partir de ellas. El generador esta compuesto por:

- `generador_analizadores_lexicos/regex_lexer.l`: meta-lexer de expresiones regulares, generado con `flex`.
- `generador_analizadores_lexicos/regex_parser.c`: parser de regex.
- `generador_analizadores_lexicos/ast.c`: AST para regex con informacion estructural.
- `generador_analizadores_lexicos/afd.c`: construccion del automata finito determinista.
- `generador_analizadores_lexicos/lexer.c`: ejecucion del DFA sobre la entrada fuente.

`hulk_compiler.c` contiene el pipeline que construye el lexer: crea contexto de AST de regex, parsea las expresiones regulares de `hulk_tokens.c`, calcula las funciones necesarias sobre el AST de regex, construye el DFA y lo deja listo para `lexer_next_token`. La secuencia real de fases se ve en el arreglo `lexer_pipeline`:

```c
static int phase_build_ast(LexerBuildContext *lbc) {
    lbc->ast = build_lexer_ast(hulk_tokens, hulk_token_count,
                               lbc->ast_ctx, lbc->rctx);
    ...
}

static int phase_compute_functions(LexerBuildContext *lbc) {
    ast_compute_functions(lbc->ast);
    ast_build_leaf_index(lbc->ast, lbc->ast_ctx);
    ast_compute_followpos(lbc->ast, lbc->ast_ctx);
    return 1;
}

static int phase_build_dfa(LexerBuildContext *lbc) {
    ...
    lbc->dfa = dfa_create(alphabet, alphabet_size);
    dfa_build(lbc->dfa, lbc->ast, lbc->ast_ctx, NULL);
    ...
}

static const CompilerPhase lexer_pipeline[] = {
    { "Asignar contextos",     phase_alloc_contexts     },
    { "Construir AST",         phase_build_ast          },
    { "Calcular funciones",    phase_compute_functions  },
    { "Construir DFA",         phase_build_dfa          },
    { "Exportar DFA",          phase_export_dfa         },
    { NULL, NULL }
};
```

El algoritmo usado no construye un AFN intermedio de Thompson para luego determinizarlo. La evidencia principal esta en `generador_analizadores_lexicos/afd.c`, cuyo comentario identifica el "Algoritmo 3.36" del Dragon Book: construccion directa de un DFA desde el AST de la expresion regular usando posiciones. En terminos de compilacion, el proyecto aplica el metodo directo regex -> arbol sintactico -> `nullable`/`firstpos`/`lastpos`/`followpos` -> DFA.

La entrada del algoritmo no es una sola regex, sino una union de todas las regex de tokens. `generador_analizadores_lexicos/regex_ast_actions.c` construye para cada token un arbol `regex#`, registra en que posicion del marcador `#` termina ese token y combina todos los arboles con `OR`:

```c
ASTNode* ast = regex_parse(tokens[i].regex, ctx, rctx);

int end_pos = get_next_position(ctx);
ASTNode* end_marker = ast_create_leaf(ctx, '#', end_pos);

ctx->pos_to_token[end_pos] = tokens[i].token_id;

ASTNode* marked = ast_create_concat(ctx, ast, end_marker);

if (combined == NULL) {
    combined = marked;
} else {
    combined = ast_create_or(ctx, combined, marked);
}
```

Ese marcador `#` es fundamental: permite decidir que estado del DFA es aceptador y que token acepta. Si un estado contiene una posicion asociada en `ctx->pos_to_token`, entonces reconoce el token correspondiente.

### 3.1.1 Propiedades calculadas sobre el AST de regex

El archivo `generador_analizadores_lexicos/ast.h` documenta que cada nodo del AST guarda:

- `nullable`: indica si la subexpresion puede reconocer la cadena vacia.
- `firstpos`: conjunto de posiciones de hojas que pueden aparecer como primer simbolo de alguna cadena reconocida por la subexpresion.
- `lastpos`: conjunto de posiciones de hojas que pueden aparecer como ultimo simbolo de alguna cadena reconocida por la subexpresion.
- `followpos`: para cada posicion de hoja `p`, conjunto de posiciones que pueden aparecer inmediatamente despues de `p` en alguna cadena reconocida por la regex completa.

Estas propiedades no son decorativas: son las que sustituyen la construccion de un AFN. `ast_compute_functions` recorre el arbol en postorden, de los hijos hacia la raiz. En hojas, `firstpos` y `lastpos` contienen la propia posicion y `nullable` es falso. En alternancia se unen los conjuntos. En concatenacion, `nullable`, `firstpos` y `lastpos` dependen de si el lado izquierdo o derecho puede ser vacio:

```c
static void visit_compute_leaf(ASTNode *n, void *data) {
    ...
    n->nullable = 0;
    posset_add(&n->firstpos, n->pos);
    posset_add(&n->lastpos, n->pos);
}

static void visit_compute_or(ASTNode *n, void *data) {
    ...
    n->nullable = n->left->nullable || n->right->nullable;
    posset_union(&n->firstpos, &n->left->firstpos, &n->right->firstpos);
    posset_union(&n->lastpos, &n->left->lastpos, &n->right->lastpos);
}

static void visit_compute_concat(ASTNode *n, void *data) {
    ...
    n->nullable = c1->nullable && c2->nullable;
    if (c1->nullable)
        posset_union(&n->firstpos, &c1->firstpos, &c2->firstpos);
    else
        n->firstpos = c1->firstpos;
    if (c2->nullable)
        posset_union(&n->lastpos, &c1->lastpos, &c2->lastpos);
    else
        n->lastpos = c2->lastpos;
}
```

Los operadores de repeticion tambien siguen las reglas clasicas. `a*` y `a?` son anulables; `a+` solo es anulable si su hijo lo es:

```c
static void visit_compute_star(ASTNode *n, void *data) {
    n->nullable = 1;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}

static void visit_compute_plus(ASTNode *n, void *data) {
    n->nullable = n->left->nullable;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}

static void visit_compute_question(ASTNode *n, void *data) {
    n->nullable = 1;
    n->firstpos = n->left->firstpos;
    n->lastpos  = n->left->lastpos;
}
```

Despues se calcula `followpos`. En una concatenacion `c1 c2`, todo simbolo que pueda terminar `c1` puede estar seguido por cualquier simbolo que pueda iniciar `c2`. En una repeticion `c*` o `c+`, todo simbolo que pueda terminar `c` puede estar seguido por cualquier simbolo que pueda iniciar otra iteracion de `c`:

```c
static void visit_followpos_concat(ASTNode *n, void *data) {
    ASTContext *ctx = (ASTContext*)data;
    int limit = ctx->max_position + 1;
    for (int i = 0; i < limit; i++) {
        if (posset_contains(&n->left->lastpos, i))
            posset_union(&ctx->followpos[i], &ctx->followpos[i],
                         &n->right->firstpos);
    }
}

static void visit_followpos_repeat(ASTNode *n, void *data) {
    ASTContext *ctx = (ASTContext*)data;
    int limit = ctx->max_position + 1;
    for (int i = 0; i < limit; i++) {
        if (posset_contains(&n->left->lastpos, i))
            posset_union(&ctx->followpos[i], &ctx->followpos[i],
                         &n->left->firstpos);
    }
}
```

`tests/test_ast.c` valida estas reglas de forma unitaria. Por ejemplo, comprueba que en `a.b` el `firstpos` sea `{1}`, el `lastpos` sea `{2}` y `nullable` sea falso; que en `a|b` ambos conjuntos contengan `{1,2}`; y que `a*` sea anulable.

### 3.1.2 Construccion directa del DFA

`generador_analizadores_lexicos/afd.c` representa cada estado del DFA como un conjunto de posiciones. El estado inicial es `firstpos(root)`. Luego se procesa una worklist de estados: para cada simbolo del alfabeto, el siguiente estado es la union de `followpos(p)` para todas las posiciones `p` del estado actual cuya hoja tenga ese simbolo.

```c
PositionSet start = root->firstpos;
dfa_add_state(dfa, &start);
worklist[0] = start;

while (front < dfa->count) {
    PositionSet current = worklist[front];
    int s_id = front;
    ...
    for (int a = 0; a < dfa->alphabet_size; a++) {
        char sym = dfa->alphabet[a];
        PositionSet next;
        posset_init(&next);

        for (int p = 0; p < limit; p++) {
            if (posset_contains(&current, p)) {
                ASTNode *leaf = ctx->leaf_at[p];
                if (leaf && leaf->symbol == sym) {
                    posset_union(&next, &next, &ctx->followpos[p]);
                }
            }
        }
        ...
        dfa->states[s_id].transitions[a] = to_id;
    }
    front++;
}
```

La aceptacion de un estado tambien se deriva de las posiciones. Si el conjunto contiene una posicion `#` registrada en `ctx->pos_to_token`, el estado es aceptador:

```c
for (int p = 0; p < limit; p++) {
    if (posset_contains(&current, p) && ctx->pos_to_token[p] != -1) {
        dfa->states[s_id].is_accept = 1;
        if (dfa->states[s_id].token_id == -1) {
            dfa->states[s_id].token_id = ctx->pos_to_token[p];
        } else {
            dfa->states[s_id].token_id =
                priority(dfa->states[s_id].token_id, ctx->pos_to_token[p]);
        }
    }
}
```

Cuando varias regex pueden aceptar el mismo prefijo, se usa la funcion de prioridad. Por defecto, `dfa_priority_min_id` escoge el menor `token_id`:

```c
int dfa_priority_min_id(int a, int b) {
    return (a <= b) ? a : b;
}
```

Esto se combina con el orden de `hulk_tokens.c`: las palabras clave se declaran antes que `TOKEN_IDENT`. Por eso `decor` se tokeniza como `TOKEN_DECOR`, pero `decoration` no se parte como `decor` + `ation`; el lexer lo reconoce como `TOKEN_IDENT` por maximal munch, y `tests/test_lexer.c` lo comprueba en el caso `decor_not_identifier`.

### 3.2 Tokens reconocidos

`hulk_tokens.c` declara palabras clave, operadores, delimitadores, literales e identificadores. Entre los tokens observables estan:

- Palabras clave: `function`, `type`, `inherits`, `while`, `for`, `in`, `if`, `elif`, `else`, `let`, `true`, `false`, `new`, `self`, `base`, `as`, `is`, `decor`, `protocol`, `extends`, `define`.
- Operadores compuestos: `->`, `=>`, `:=`, `<=`, `>=`, `==`, `!=`, `||`, `&&`, `@@`, `**`.
- Operadores simples: `@`, `=`, `+`, `-`, `*`, `/`, `%`, `<`, `>`, `!`.
- Delimitadores: `;`, `(`, `)`, `{`, `}`, `[`, `]`, `,`, `:`, `.`.
- Literales: numeros con regex `[0-9]+(\.[0-9]+)?` y strings con regex `"[^"]*"`.
- Identificadores: `[a-zA-Z_][a-zA-Z0-9_]*`.
- Espacios y comentarios: whitespace `[ \t\n\r]+` y comentarios `//.*`.

La presencia de `TOKEN_DECOR`, `TOKEN_LBRACKET`, `TOKEN_RBRACKET`, `TOKEN_ARROW`, `TOKEN_PROTOCOL`, `TOKEN_EXTENDS` y `TOKEN_DEFINE` muestra que el lexer fue extendido para features mas alla de expresiones aritmeticas basicas.

### 3.3 Prioridad y maximal munch

`generador_analizadores_lexicos/lexer.c` implementa lectura tipo maximal munch: avanza por el DFA mientras haya transiciones, recuerda el ultimo estado aceptador y devuelve el token mas largo reconocido. `generador_analizadores_lexicos/afd.c` define una prioridad por menor `token_id` (`dfa_priority_min_id`). Esto es relevante porque las palabras clave aparecen antes que `TOKEN_IDENT` en `hulk_tokens.c`; por tanto, entradas como `function` o `while` se reconocen como palabras reservadas y no como identificadores.

Los operadores de varios caracteres tambien dependen de maximal munch. Por ejemplo, `:=`, `<=`, `>=`, `==`, `!=`, `||`, `&&`, `@@` y `**` deben reconocerse antes de dividirse en tokens mas cortos.

El bucle central de `lexer_next_token` evidencia esa estrategia: no devuelve el primer estado aceptador que encuentra, sino que sigue avanzando mientras exista transicion y guarda `last_accept_pos` y `last_token`.

```c
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
    ...
    err.type = TOKEN_ERROR;
    ...
}
```

Al terminar ese recorrido, `len = last_accept_pos - start` selecciona el lexema mas largo reconocido. Esta regla explica por que `@@` se reconoce como `TOKEN_CONCAT_WS` y no como dos tokens `@`, y por que `decoration` queda como identificador completo aunque empiece por la palabra `decor`.

### 3.4 Whitespace, comentarios y errores lexicos

`lexer.c` ignora tokens `TOKEN_WS` y `TOKEN_COMMENT`, como se indica tambien en `lexer.h`. El lexer mantiene linea y columna en el token devuelto (`Token { type, lexeme, line, col }`, definido en `token_types.h`).

Cuando no hay transicion valida desde la posicion actual, `lexer.c` genera `TOKEN_ERROR`, avanza un caracter y reporta error lexico. Tambien valida literales string en `validate_string_literal`, con deteccion de escapes invalidos y saltos de linea dentro de cadenas.

```c
if (last_token == TOKEN_WS || last_token == TOKEN_COMMENT) {
    continue;
}

if (last_token == TOKEN_STRING) {
    ...
    if (!validate_string_literal(lexeme, len, start_line, start_col,
                                 &err_line, &err_col, &msg)) {
        ...
        err.type = TOKEN_ERROR;
        return err;
    }
}
```

Los tests y casos de error lexicos estan en:

- `tests/test_lexer.c`;
- `tests_piad/hulk/errors/lexical/bad_string.hulk`;
- `tests_piad/hulk/errors/lexical/bad_string_newline.hulk`;
- `tests_piad/hulk/errors/lexical/invalid_char.hulk`;
- `tests_piad/hulk/errors/lexical/invalid_escape.hulk`;
- `tests_piad/hulk/errors/lexical/stray_backtick.hulk`;
- `tests_piad/hulk/errors/lexical/stray_tilde.hulk`.

### 3.5 Complejidad del lexer

Bajo el supuesto de que las transiciones del DFA se consultan en tiempo constante o acotado por el tamano fijo del alfabeto implementado, el analisis lexico es lineal en la longitud de la entrada. Cada caracter se consume como parte de un recorrido de DFA y el lexer conserva solo el ultimo estado aceptador. Por tanto, para una entrada de longitud `n`, el procesamiento lexico es `O(n)` respecto al texto fuente. Esta afirmacion se limita al escaneo de una entrada con el DFA ya construido; la construccion del DFA depende del numero y complejidad de regex declaradas, pero esas regex pertenecen a la especificacion del compilador, no al programa fuente del usuario.

---

## 4. Parser y gramatica

### 4.1 Infraestructura LL(1)

El directorio `generador_parser_ll1/` implementa un generador de parser LL(1):

- `grammar.c` y `grammar.h` definen simbolos, producciones y carga de gramatica.
- `first_follow.c` calcula conjuntos FIRST y FOLLOW.
- `ll1_table.c` construye la tabla predictiva y detecta conflictos.
- `parser.c` implementa el motor de parsing con pila.
- `grammar_hulk.c` y `grammar_regex.c` registran gramaticas especificas.

Esta infraestructura es importante desde el punto de vista academico porque implementa conceptos clasicos de analisis sintactico predictivo: FIRST, FOLLOW, producciones epsilon y tabla `M[NoTerminal, Terminal]`.

### 4.2 Builder real del AST HULK

El parser integrado para construir el AST de HULK esta en `hulk_ast/builder/hulk_ll1_builder.c`. Su cabecera (`hulk_ll1_builder.h`) lo describe como un parser LL(1) dirigido por tabla que construye el AST mediante acciones semanticas. El archivo fuente declara:

- no terminales como `NT_Program`, `NT_Expr`, `NT_Primary`, `NT_Lambda`, `NT_TypeDef`, `NT_DecorBlock`, `NT_ArrayInit`;
- acciones semanticas como `A_NUM`, `A_FUNCEXPR`, `A_VEC`, `A_INDEX`, `A_ARRAY_NEW`, `A_ARRAY_INIT`, `A_DECOR_BLOCK`;
- producciones en el arreglo `HULK_PRODS`.

La ventaja de este diseno es que la gramatica y las acciones que construyen el AST se mantienen juntas. Cuando se expande una produccion, las acciones se ejecutan sobre una pila semantica tipada para construir nodos concretos.

### 4.3 Gramática base soportada

La gramatica integrada soporta, segun `hulk_ll1_builder.c`:

- programas como listas de elementos top-level;
- declaraciones de funcion con `function`;
- declaracion alternativa con `define`;
- declaraciones de tipo con `type`;
- protocolos con `protocol`;
- bloques `{ ... }`;
- expresiones terminadas por `;`;
- `let`;
- `if`/`elif`/`else`;
- `while`;
- `for (x in expr)`;
- llamadas, acceso a miembros, indexacion y asignaciones;
- `new`;
- `self` y `base`;
- operadores aritmeticos, logicos, comparativos, concatenacion y potencia.

La precedencia se implementa por niveles de no terminales:

```text
Or -> And
And -> Cmp
Cmp -> Concat
Concat -> Add
Add -> Term
Term -> Factor
Factor -> Unary
Unary -> Postfix
Postfix -> Primary Call
```

La potencia se documenta como asociativa a la derecha en `hulk_ll1_builder.c`: `2 ** 3 ** 4` se interpreta como `2 ** (3 ** 4)`.

### 4.4 Extensiones de gramatica

#### Lambdas

`hulk_ll1_builder.c` define `NT_Lambda` y acepta dos formas:

- `function (params) typeAnn body`;
- `(params) typeAnn body`.

El cuerpo de una lambda usa `NT_FuncExprBody`, que puede ser `ARROW Expr` o `Block`. El token `TOKEN_ARROW` se usa tanto para `->` como para `=>` porque `hulk_tokens.c` registra ambas regex con el mismo token.

La ambiguedad principal aparece cuando el parser ve `(`: puede comenzar una expresion parentizada o una lambda. `hulk_ll1_builder.h` documenta que este es el punto no estrictamente LL(1) de HULK. `hulk_ll1_builder.c` resuelve el caso con `lookahead_is_lambda`, que escanea una copia del estado del lexer hasta encontrar el parentesis balanceado y comprobar si sigue `TOKEN_ARROW`. Por tanto, el informe no debe afirmar que toda la gramatica extendida sea LL(1) pura.

#### Arreglos y vectores

El builder reconoce:

- literales con `[` `]` mediante `NT_VecItems` y accion `A_VEC`;
- tambien una forma con `{` `}` en `NT_Primary`, tratada como vector;
- indexacion postfija `expr[Expr]` con accion `A_INDEX`;
- sufijos de tipo `[]` mediante `NT_TypeSuffix` y accion `A_TYPE_ARRAY`;
- `new` con sufijos de arreglo e inicializador opcional mediante `NT_NewTail`, `NT_ArrayTypeSuffix` y `NT_ArrayInit`.

La gramatica evita confundir indexacion con literales porque los literales aparecen como `Primary`, mientras que la indexacion aparece en la cola postfija `NT_Call` despues de una expresion primaria.

#### Decoradores

La gramatica integrada reconoce:

- `DecorBlock -> DECOR DecorItems DecorMore DecorTarget`;
- listas separadas por coma;
- decoradores con argumentos;
- decoradores apilados;
- decoradores sobre funciones y tipos a nivel top-level;
- prefijos `decor` dentro de miembros de tipos para metodos.

La estructura sintactica queda representada con `NODE_DECOR_BLOCK` y `NODE_DECOR_ITEM`, definidos en `hulk_ast/core/hulk_ast.h`.

#### Protocolos y tipos funcion

`hulk_ll1_builder.c` incluye `NT_ProtocolDef`, `NT_ProtoExt`, `NT_ProtoSigs` y `NT_ProtoSig`. Tambien incluye tipos funcion mediante `NT_TypeRef` con forma `(TypeList) -> TypeRef`, y sufijos `[]` y `*`.

### 4.5 Sobre LL(1)

El proyecto contiene una infraestructura LL(1) real y valiosa, pero el estado observado exige una afirmacion matizada:

- `generador_parser_ll1/` implementa los algoritmos LL(1) clasicos.
- `hulk_ast/builder/hulk_ll1_builder.c` declara una gramatica como datos y usa una tabla con acciones.
- `hulk_ast/builder/hulk_ll1_builder.h` reconoce explicitamente un punto no LL(1): lambda `(x) -> ...` frente a expresion parentizada `(expr)`.
- `probar/logs/test_all.log` contiene advertencias de conflictos LL(1) al cargar `grammar.ll1`, por ejemplo en `TopLevel`, `TypeMember`, `CmpExpr'`, `ConcatExpr'`, `AddExpr'`, `Term'`, `Factor'`, `AsExpr` y `PrimaryTail`.

Por tanto, la conclusion correcta no es "la gramatica HULK completa es estrictamente LL(1)", sino: el proyecto implementa infraestructura LL(1), usa un builder dirigido por tabla para HULK, y resuelve algunos puntos conflictivos mediante lookahead local y reglas especializadas. Esta es una decision tecnica razonable para mantener el parser simple sin negar la ambiguedad real introducida por lambdas, funciones como expresion, postfijos y decoradores.

---

## 5. Representacion mediante AST

El AST esta definido en `hulk_ast/core/hulk_ast.h`. Cada nodo comienza con una cabecera comun:

```c
typedef struct HulkNode_s {
    HulkNodeType type;
    int line;
    int col;
    const char *static_type;
} HulkNode;
```

El campo `static_type` es especialmente relevante: antes del analisis semantico es `NULL`, y despues se usa para almacenar el nombre canonico del tipo inferido o verificado. `hulk_ast/semantic/hulk_semantic_check_expr.c` anota este campo desde el dispatcher `sem_check_expr`.

Los nodos principales incluyen:

- `ProgramNode`;
- `FunctionDefNode` y `FunctionExprNode`;
- `TypeDefNode`, `MethodDefNode`, `AttributeDefNode`;
- `LetExprNode`, `VarBindingNode`;
- `IfExprNode`, `ElifBranchNode`, `WhileStmtNode`, `ForStmtNode`, `BlockStmtNode`;
- `BinaryOpNode`, `UnaryOpNode`, `ConcatExprNode`;
- literales numericos, strings, booleanos e identificadores;
- `CallExprNode`, `MemberAccessNode`, `NewExprNode`;
- `AssignNode`, `DestructAssignNode`;
- `AsExprNode`, `IsExprNode`, `SelfNode`, `BaseCallNode`;
- `DecorBlockNode`, `DecorItemNode`;
- `VectorLitNode`, `IndexExprNode`.

Esta representacion permite que el parser se limite a construir estructura, mientras que el analisis semantico y el backend trabajan sobre nodos ya normalizados. La separacion evita que fases posteriores dependan de detalles de tokenizacion o de texto fuente.

La memoria del AST se gestiona mediante `HulkASTContext`, implementado en `hulk_ast/core/hulk_ast_context.c`. El uso de una arena simplifica la propiedad de los nodos y reduce la necesidad de liberar manualmente cada subarbol.

---

## 6. Lambdas y closures

### 6.1 Sintaxis y parsing

Las lambdas aparecen como `NODE_FUNCTION_EXPR`. `hulk_ast/core/hulk_ast.h` define `FunctionExprNode` con:

- lista de parametros;
- tipo de retorno opcional;
- cuerpo;
- lista de capturas (`captures`).

`hulk_ll1_builder.c` reconoce lambdas con palabra clave `function` y tambien con parentesis iniciales. El caso parentizado necesita lookahead local porque la entrada `(x) => x + 1` comparte prefijo con una expresion entre parentesis.

Los tests relevantes incluyen:

- `tests/test_ast_builder.c`, casos `function_expr_simple` y `function_expr_inside_let`;
- `tests/test_ll1_builder.c`, casos sobre anotaciones funcionales y lambdas;
- `tests/test_feature_decorators_closures.c`, caso `ast_tracks_closure_capture_candidates`;
- `tests_piad/hulk/ok/lambdas/lambda_basic.hulk`;
- `tests_piad/hulk/ok/lambdas/lambda_as_arg.hulk`;
- `tests_piad/hulk/ok/lambdas/lambda_closure.hulk`;
- `tests_piad/hulk/ok/lambdas/lambda_compose.hulk`;
- `tests_piad/hulk/ok/lambdas/lambda_higher_order.hulk`;
- `tests_piad/hulk/ok/lambdas/lambda_make_adder.hulk`.

### 6.2 Analisis semantico de lambdas

`hulk_ast/semantic/hulk_semantic_check_expr.c` implementa `check_function_expr`. La estrategia observada es:

- crear un nuevo scope para los parametros de la lambda;
- resolver o inferir tipos de parametros mediante `sem_param_annotation_for`;
- verificar el cuerpo;
- verificar compatibilidad con tipo de retorno anotado, si existe;
- construir un tipo funcion con `sem_function_type_new`;
- registrar capturas cuando una lambda referencia variables externas.

La captura semantica se apoya en `capture_target` y `capture_scope`, definidos en `SemanticContext` (`hulk_ast/semantic/hulk_semantic_internal.h`). `check_ident` detecta identificadores que no pertenecen localmente a la lambda y los agrega a `FunctionExprNode.captures`.

### 6.3 Generacion LLVM de closures

`hulk_ast/codegen/hulk_codegen_expr.c` contiene la emision de `NODE_FUNCTION_EXPR`. La lambda se compila como una funcion LLVM global con nombre unico `lambda.N`. El primer parametro de la funcion generada es un `i8*` que representa el entorno de closure; los parametros del usuario se agregan despues.

El entorno se reserva con `malloc` y almacena:

1. puntero a la funcion;
2. valores capturados.

Las llamadas indirectas se implementan en `hulk_ast/codegen/hulk_codegen_call.c` mediante `cg_emit_call_closure_raw` y `emit_closure_call`, que cargan el puntero de funcion desde el entorno y llaman con el propio closure como primer argumento.

Esta implementacion muestra funciones anonimas como valores de primera clase, aunque con limitaciones: el tipado de closures depende de anotaciones, inferencias locales y del campo `static_type`; no hay evidencia de un sistema general de inferencia Hindley-Milner ni de polimorfismo parametrico para lambdas.

---

## 7. Arreglos y vectores

### 7.1 Sintaxis soportada

El proyecto usa la terminologia de vectores y arreglos en distintos puntos. La evidencia muestra soporte para:

- literales tipo `[a, b, c]`;
- literales con `{a, b, c}` tratados por el builder como vector;
- indexacion `expr[index]`;
- tipos anotados con sufijo `[]`, por ejemplo `Number[]`;
- construccion con `new` y sufijos de arreglo;
- inicializacion con funcion/lambda mediante `__array_init`;
- llamada `.size()` sobre arreglos/vectores.

Las reglas estan en `hulk_ast/builder/hulk_ll1_builder.c`, especialmente `NT_VecItems`, `NT_Call`, `NT_NewTail`, `NT_ArrayTypeSuffix`, `NT_ArrayInit` y `NT_TypeSuffix`.

### 7.2 AST y semantica

`hulk_ast/core/hulk_ast.h` define:

- `VectorLitNode`, con lista de items;
- `IndexExprNode`, con objeto e indice.

`hulk_ast/semantic/hulk_semantic_check_expr.c` asigna a `NODE_VECTOR_LIT` el tipo `Number[]` despues de verificar sus elementos. Para `NODE_INDEX_EXPR`, verifica que el indice conforme a `Number` y deduce el tipo de elemento si el tipo del objeto termina en `[]`; por defecto retorna `Number`.

Esta semantica es util para los casos numericos del proyecto, pero debe documentarse como limitada: no se observa una verificacion profunda de homogeneidad para todos los tipos posibles de arreglo, ni chequeos estaticos de rango de indices. En LLVM, tampoco se observa un chequeo runtime de bounds para indexacion.

### 7.3 Representacion LLVM

`hulk_ast/codegen/hulk_codegen_expr.c` emite `NODE_VECTOR_LIT` reservando memoria con `malloc`. El layout observado es:

```text
offset 0: size i32
offset 8: elemento 0
offset 16: elemento 1
...
```

La indexacion convierte el indice de `double` a entero y calcula `8 + 8 * idx`. La funcion `.size()` se intercepta en `hulk_ast/codegen/hulk_codegen_call.c` cargando el entero almacenado al inicio del arreglo y convirtiendolo a `double`.

`hulk_ast/codegen/hulk_codegen_call.c` tambien contiene `emit_array_new` y `emit_array_init`, asociados a los builtins internos `__array_new` y `__array_init`, que se registran semanticamente en `hulk_ast/semantic/hulk_semantic_types.c`.

Los tests de evidencia estan en `tests_piad/hulk/ok/arrays`, por ejemplo:

- `array_basic.hulk`;
- `array_literal.hulk`;
- `array_mutation.hulk`;
- `array_size.hulk`;
- `array_sum.hulk`;
- `array_pass.hulk`;
- `array_2d.hulk`;
- `array_auto_init.hulk`.

---

## 8. Decoradores

### 8.1 Sintaxis

La feature de decoradores se identifica por la palabra clave `decor`, tokenizada como `TOKEN_DECOR` en `hulk_tokens.c`. La gramatica integrada en `hulk_ll1_builder.c` acepta:

- un decorador simple: `decor log function f(...) ...`;
- varios decoradores en lista: `decor a, b function f(...) ...`;
- decoradores con argumentos: `decor memoize(100) function f(...) ...`;
- decoradores apilados con varios `decor`;
- decoradores sobre funciones y tipos en top-level;
- decoradores de metodos dentro de un `type`.

En el AST se representan mediante:

- `DecorBlockNode`, con lista de decoradores y target;
- `DecorItemNode`, con nombre y argumentos;
- `MethodDefNode.decorators` para metodos decorados.

### 8.2 Desugaring semantico

`hulk_ast/semantic/hulk_semantic_desugar.c` transforma decoradores top-level. El comentario del archivo especifica:

```text
decor d1, d2(arg) function f(...) -> body;
```

se transforma en una declaracion de `f` seguida de una asignacion destructiva:

```text
f := d1(d2(arg)(f))
```

Los decoradores se aplican de derecha a izquierda. Si un decorador tiene argumentos, se trata como fabrica currificada: primero se llama al decorador con sus argumentos y luego al resultado se le pasa la funcion objetivo.

### 8.3 Validacion semantica

`hulk_ast/semantic/hulk_semantic_check.c` contiene `apply_decorators_to_type`, que valida:

- que el decorador exista;
- que sea invocable;
- que una fabrica decoradora reciba la cantidad correcta de argumentos;
- que los argumentos de la fabrica conformen a los tipos esperados;
- que la fabrica retorne una funcion;
- que el decorador acepte exactamente una funcion objetivo;
- que la firma de la funcion objetivo sea compatible;
- que el resultado tambien sea una funcion.

Para metodos, el chequeo se realiza sobre `MethodDefNode.decorators` y actualiza `callable_type` del metodo cuando corresponde.

### 8.4 Codegen de decoradores

El backend tambien contiene soporte especifico:

- `hulk_ast/codegen/hulk_codegen_stmt.c` maneja `NODE_DECOR_BLOCK`, actualiza `callable_cell` de funciones globales y aplica decoradores en orden inverso.
- `hulk_ast/codegen/hulk_codegen_typedecl.c` contiene funciones para adaptadores y wrappers de metodos decorados, como `emit_method_decorator_adapter` y `emit_method_decorator_wrapper`.
- `hulk_ast/codegen/hulk_codegen_oop.c` contempla reasignacion de funciones decoradas mediante `callable_cell`.

La evidencia de tests incluye:

- `tests/test_parser.c`, suite de decoradores;
- `tests/test_ast_builder.c`, casos `decor_simple`, `decor_with_args`, `decor_multiple`, `decor_on_type`, `decor_method_inside_type`;
- `tests/test_semantic.c`, casos `decorator_basic`, `decorator_multiple`, `decorator_curried`, `decorator_curried_factory_must_return_function`, `method_decorator_basic`;
- `tests/test_feature_decorators_closures.c`;
- `tests_piad/hulk/ok/test_decorators`;
- errores en `tests_piad/hulk/errors/semantic/decorator_undefined.hulk`, `decorator_signature_incompatible.hulk` y `decorator_factory_bad_return.hulk`.

---

## 9. Analisis semantico

### 9.1 Separacion respecto al parser

El parser verifica estructura: que los tokens formen una sentencia, expresion, declaracion o tipo segun la gramatica. El analisis semantico verifica significado: que los nombres existan, que los tipos sean compatibles, que las llamadas tengan aridad correcta, que las condiciones sean booleanas, que `self` y `base` aparezcan en contextos validos y que decoradores/lambdas/arreglos cumplan sus restricciones.

Esta separacion es visible en `hulk_cli.c`: primero se construye el AST, luego se llama a `hulk_semantic_analyze`.

### 9.2 Estructuras semanticas

`hulk_ast/semantic/hulk_semantic_internal.h` define:

- `HulkType`, con `kind`, `name`, `parent`, `members`, parametros, retorno e indicador `is_protocol`;
- `Symbol`, con nombre, clase (`SYM_VARIABLE`, `SYM_FUNCTION`, `SYM_TYPE`, `SYM_METHOD`, `SYM_ATTRIBUTE`), tipo, firma invocable y nodo de declaracion;
- `Scope`, como arreglo dinamico de simbolos y puntero a scope padre;
- `SemanticContext`, con caches de tipos built-in, scope global, scope actual, registro de tipos y estado de verificacion.

### 9.3 Registro de tipos y funciones

`hulk_ast/semantic/hulk_semantic_types.c` inicializa tipos built-in:

- `Object`;
- `Number`;
- `String`;
- `Boolean`;
- `<function>`;
- `Void`;
- `<error>`.

Tambien registra funciones built-in:

- `print`;
- `sqrt`;
- `sin`;
- `cos`;
- `exp`;
- `log`;
- `rand`;
- `parse`;
- `range`;
- `__array_new`;
- `__array_init`.

`hulk_ast/semantic/hulk_semantic_collect.c` realiza pases de recoleccion para tipos, herencia, funciones y miembros. Esto permite referencias adelantadas y separa el registro de nombres de la verificacion de cuerpos.

### 9.4 Verificacion de expresiones

`hulk_ast/semantic/hulk_semantic_check_expr.c` usa un enfoque bottom-up:

- literales retornan su tipo built-in;
- identificadores se resuelven en scopes;
- lambdas producen tipos funcion;
- operadores aritmeticos exigen `Number`;
- operadores logicos exigen `Boolean`;
- comparaciones numericas producen `Boolean`;
- concatenacion produce `String`;
- llamadas verifican aridad y tipos de argumentos;
- acceso a miembro consulta `sem_lookup_member`;
- vectores e indexacion se verifican segun las reglas descritas en la seccion 7.

El dispatcher anota `node->static_type` con el tipo resultante, salvo en errores. Esta anotacion conecta el analisis semantico con el backend.

### 9.5 Control de flujo y OOP

`hulk_ast/semantic/hulk_semantic_check_stmt.c` implementa:

- `sem_check_let`: crea scope, registra bindings e infiere tipo cuando no hay anotacion;
- `sem_check_if`: exige condicion booleana y calcula join de ramas;
- `sem_check_while`: exige condicion booleana;
- `sem_check_for`: valida iterabilidad mediante convenciones de tipo y miembros;
- `sem_check_block`: retorna el tipo del ultimo statement;
- `sem_check_new`: valida tipo y argumentos de constructor;
- `sem_check_assign` y `sem_check_destruct`: validan asignacion;
- `sem_check_as`, `sem_check_is`, `sem_check_self`, `sem_check_base`.

Los errores semanticos estan cubiertos por `tests/test_semantic.c` y por `tests_piad/hulk/errors/semantic`, con casos como variable no declarada, llamada a no funcion, herencia circular, tipo no declarado, aridad incorrecta, condicion no booleana, miembro inexistente y errores de decoradores.

---

## 10. Sistema de tipos y polimorfismo

### 10.1 Tipado observado

El sistema de tipos es principalmente nominal:

- los tipos built-in y definidos por el usuario se registran por nombre;
- la herencia se representa con `HulkType.parent`;
- `sem_type_conforms(child, ancestor)` recorre la cadena de herencia;
- todo tipo conforma a `Object`.

Tambien hay soporte para:

- tipos funcion (`HULK_TYPE_FUNCTION`);
- tipos arreglo y tipos iterables creados a partir de anotaciones con `[]` y `*`;
- protocolos con conformidad estructural limitada.

No hay evidencia de polimorfismo parametrico ni generics. Por tanto, el informe no debe afirmar que el compilador implementa genericos o inferencia polimorfica completa.

### 10.2 Protocolos

`sem_type_conforms` contiene una rama para protocolos: si el ancestro es protocolo, el tipo hijo conforma si tiene metodos con los nombres exigidos por el protocolo. El comentario del codigo aclara que esta comprobacion es por nombre y no implementa varianza estricta de firmas en ese punto. Por ello, la conformidad estructural debe describirse como limitada.

Los tests de protocolos aparecen en:

- `tests/hulk_programs/23_protocol.hulk`;
- `tests_piad/hulk/ok/interfaces/interface_basic.hulk`;
- `interface_multiple_impl.hulk`;
- `interface_return.hulk`;
- `interface_inherit_compat.hulk`;
- `interface_polymorphism.hulk`;
- `interface_param.hulk`.

### 10.3 Polimorfismo dinamico

El backend implementa despacho dinamico de metodos mediante vtables y tags de tipo. La evidencia esta en:

- `hulk_ast/codegen/hulk_codegen_internal.h`, donde `CGTypeInfo` contiene `type_tag`, `methods`, `parent`, `vtable_global` y `vtable_type`;
- `hulk_ast/codegen/hulk_codegen_types.c`, que asigna slots globales de metodo con `cg_method_slot` y resuelve metodos con `cg_type_resolve_method`;
- `hulk_ast/codegen/hulk_codegen_typedecl.c`, que construye vtables y tabla de padres;
- `hulk_ast/codegen/hulk_codegen_call.c`, que emite llamadas por vtable;
- `hulk_ast/codegen/hulk_codegen_expr.c`, que implementa `is` recorriendo la tabla de padres.

El patron de despacho es:

```text
objeto -> tag dinamico
tag -> vtable global
slot de metodo -> puntero de funcion
call indirecto
```

Esto corresponde a polimorfismo por subtipado con despacho dinamico. Las pruebas relacionadas incluyen:

- `tests/hulk_programs/16_poly_static.hulk`;
- `17_poly_dynamic_annot.hulk`;
- `18_poly_via_func.hulk`;
- `19_poly_branches_if.hulk`;
- `20_base_call.hulk`;
- `21_is_op.hulk`;
- `22_as_downcast.hulk`;
- `26_poly_chain3.hulk`;
- `tests_piad/hulk/ok/oop/polymorphism.hulk`;
- `tests_piad/hulk/ok/oop/method_override.hulk`;
- `tests_piad/hulk/ok/oop/inheritance.hulk`.


---

## 11. Generacion de codigo intermedio con LLVM

### 11.1 Justificacion de LLVM

LLVM aporta una representacion intermedia tipada, verificable y portable. En este proyecto, el uso de LLVM permite:

- separar frontend y backend;
- representar operaciones de bajo nivel sin escribir ensamblador manualmente;
- verificar la estructura del modulo con `LLVMVerifyModule`;
- emitir objeto nativo con `LLVMTargetMachineEmitToFile`;
- enlazar con herramientas del sistema (`cc -lm`).

La dependencia esta declarada en `Makefile` mediante `llvm-config-18` o `llvm-config`.

### 11.2 Modulos de codegen

El backend esta dividido por responsabilidad:

- `hulk_codegen.c`: API publica, inicializacion, verificacion, emision de `.ll` u objeto y enlace.
- `hulk_codegen_internal.h`: contexto LLVM, tipos basicos, scopes, simbolos, informacion de tipos, vtables y tablas RTTI.
- `hulk_codegen_types.c`: scopes, simbolos, registro de tipos y slots de metodo.
- `hulk_codegen_runtime.c`: declaraciones y helpers runtime.
- `hulk_codegen_expr.c`: literales, operadores, lambdas, `is`, vectores, indexacion y `base`.
- `hulk_codegen_call.c`: llamadas, closures, builtins, print polimorfico y arrays internos.
- `hulk_codegen_oop.c`: acceso a miembros, `new`, `self`, asignaciones y tipo estatico.
- `hulk_codegen_control.c`: `let`, `if`, `while`, `for` y bloques con basic blocks y PHI.
- `hulk_codegen_typedecl.c`: layout de tipos, constructores, metodos, vtables y decoradores de metodo.
- `hulk_codegen_stmt.c`: orquestacion del programa, funciones top-level y decoradores.
- `hulk_codegen_infer.c`: heuristicas de tipos LLVM para anotaciones o casos no anotados.

### 11.3 Traduccion de construcciones

El backend traduce expresiones a `LLVMValueRef`:

- numeros a `double`;
- booleanos a `i1`;
- strings a punteros globales `i8*`;
- operadores aritmeticos a operaciones floating point;
- potencia a llamada `pow`;
- concatenacion a helpers runtime;
- `if`, `while` y `for` a bloques basicos y saltos;
- lambdas a funciones globales con entorno `i8*`;
- objetos a structs con tag;
- metodos a funciones con `self` explicito;
- llamadas dinamicas a carga desde vtable;
- arreglos/vectores a memoria reservada con `malloc`.

`hulk_codegen.c` verifica el modulo con `LLVMVerifyModule`. Si la generacion a ejecutable se solicita, configura target machine, emite objeto y enlaza con `cc`.

---

## 12. Complejidad temporal

La afirmacion de complejidad debe formularse con cuidado. Algunas fases son lineales bajo supuestos razonables, pero el proyecto usa arreglos dinamicos y busquedas lineales en scopes y registros de tipos. Por tanto, no se debe afirmar O(n) global incondicional.

### 12.1 Lexer

Con el DFA ya construido, el lexer procesa la entrada en `O(n)` respecto a la longitud del programa fuente, siempre que la transicion de DFA se considere constante o acotada.

### 12.2 Parser y AST

El builder dirigido por tabla consume tokens y ejecuta acciones semanticas. En el caso normal, cada token y cada produccion se procesa un numero acotado de veces, por lo que la construccion del AST es lineal respecto al numero de tokens/nodos.

La excepcion matizada es el lookahead local para lambdas: escanea hacia adelante para distinguir lambdas parentizadas de expresiones parentizadas. En programas normales esto sigue siendo proporcional al tamano de las regiones inspeccionadas; sin embargo, formalmente introduce trabajo adicional si se activa muchas veces sobre prefijos largos.

### 12.3 Analisis semantico

El recorrido de AST es lineal en cantidad de nodos si cada consulta de entorno fuera constante. En este codigo, `sem_lookup_local`, `sem_lookup`, `sem_lookup_member` y `sem_type_resolve` usan arreglos y busquedas lineales. Por ello, la complejidad real puede expresarse como:

```text
O(n * costo_busqueda)
```

donde `costo_busqueda` depende del numero de simbolos por scope, profundidad de scopes, cantidad de miembros por tipo y cantidad de tipos registrados. En programas pequenos o con scopes acotados, el comportamiento esperado se aproxima a lineal. En casos patologicos con muchos simbolos en un mismo scope, puede crecer de forma superlineal.

Tambien existen inferencias heuristicas en `hulk_semantic_infer.c` que recorren cuerpos para deducir tipos no anotados. Si se ejecutan repetidamente sobre cuerpos grandes para muchos parametros, pueden anadir coste adicional.

### 12.4 Codegen LLVM

La emision propia de IR recorre el AST y produce una cantidad de instrucciones proporcional a las construcciones del programa, salvo estructuras que generan tablas globales como vtables y RTTI. La generacion de vtables depende de cantidad de tipos por cantidad de slots de metodo.

No se incluye en esta complejidad:

- optimizaciones internas de LLVM;
- emision nativa del target;
- linking con `cc`;
- ejecucion del programa generado.

### 12.5 Conclusion de complejidad

La conclusion correcta es:

- lexer: lineal en la longitud del texto;
- parser/builder: lineal en el numero de tokens en el caso normal, con lookahead local para lambdas;
- AST: lineal en numero de construcciones;
- semantica: lineal en recorridos, pero con busquedas lineales en tablas internas;
- codegen: proporcional al AST y a estructuras globales generadas.

Por tanto, el pipeline se comporta aproximadamente como `O(n)` para programas con scopes y jerarquias acotadas, pero no debe presentarse como `O(n)` absoluto sin esos supuestos.

---

## 13. Tests y resultados

### 13.1 Tests internos

El `Makefile` define targets:

```bash
make test-lexer
make test-parser
make test-ast
make test-hulk-ast
make test-ast-builder
make test-semantic
make test-codegen
make test-feature-decorators-closures
make test-ll1-builder
make test-all
```

Los tests internos estan en `tests/`:

- `tests/test_lexer.c`: tokens, literales, operadores, errores lexicos.
- `tests/test_parser.c`: gramatica cargada desde `grammar.ll1`, expresiones, funciones, tipos y decoradores.
- `tests/test_ast.c` y `tests/test_hulk_ast.c`: nodos y utilidades del AST.
- `tests/test_ast_builder.c`: construccion del AST para literales, operadores, precedencia, control, funciones, tipos, llamadas, decoradores y lambdas.
- `tests/test_semantic.c`: chequeo de tipos, scopes, funciones, OOP, control, decoradores y errores.
- `tests/test_codegen.c`: IR para literales, operaciones, funciones, let, control, concatenacion, tipos, runtime y verificacion de modulo.
- `tests/test_feature_decorators_closures.c`: pruebas dedicadas de closures y decoradores.
- `tests/test_ll1_builder.c`: builder LL(1) integrado, anotaciones funcionales, arrays, iteradores y otras extensiones.

En la auditoria local del 24 de junio de 2026, el entorno si tiene disponibles las herramientas necesarias para construir y ejecutar las pruebas:

```text
/usr/bin/llvm-config-18
/usr/bin/flex
/usr/bin/cc
```

Se ejecuto:

```bash
make test-all
```

El resultado fue exitoso: `make test-all` termino con `Todos los tests pasaron`. Los totales observados por suite fueron:

| Suite | Resultado |
| --- | ---: |
| `tests/test_lexer` | 31/31 PASS |
| `tests/test_parser` | 26/26 PASS |
| `tests/test_ast` | 18/18 PASS |
| `tests/test_hulk_ast` | 52/52 PASS |
| `tests/test_ast_builder` | 79/79 PASS |
| `tests/test_semantic` | 68/68 PASS |
| `tests/test_codegen` | 41/41 PASS |
| `tests/test_feature_decorators_closures` | 7/7 PASS |
| `tests/test_ll1_builder` | 8/8 PASS |

Total interno observado: 330 tests pasados, 0 fallidos.

Algunas suites imprimen advertencias de conflictos LL(1), por ejemplo en el parser historico y en el builder integrado. Esas advertencias no deben leerse como fallos de ejecucion: las propias suites terminan en PASS. Tambien aparecen mensajes de error durante pruebas negativas del parser y la semantica; esos mensajes son esperados cuando el test verifica que una entrada invalida se rechaza correctamente.

### 13.2 Evidencia de tests sobre el lexer

Los tests del lexer respaldan directamente las propiedades descritas en la seccion 3:

- `keyword_decor` comprueba que `decor` se reconoce como `TOKEN_DECOR`.
- `decor_not_identifier` comprueba que `decoration` se reconoce como `TOKEN_IDENT`, evidencia de maximal munch combinado con prioridad.
- `comparison_operators`, `assignment_operators`, `logical_operators`, `concat_operators`, `power_operator` y `arrow_operator` validan operadores simples y compuestos.
- `whitespace_ignored` y `comments_ignored` validan que `TOKEN_WS` y `TOKEN_COMMENT` no llegan al parser.
- `line_col_tracking` valida que el lexer mantiene linea y columna.
- `string_invalid_escape` valida que un escape invalido dentro de string produce `TOKEN_ERROR`.

`tests/test_ast.c` complementa estos tests porque valida el soporte teorico del generador de lexer: conjuntos de posiciones, creacion de hojas, anulabilidad de `*`, `+` y `?`, y calculo de `nullable`, `firstpos` y `lastpos` en concatenacion y alternancia.

### 13.3 Suite PIAD / end-to-end

`tests_piad/hulk` contiene programas `.hulk`, salidas `.expected` y archivos `.exit`. Organiza pruebas en categorias:

- `ok/minimal`;
- `ok/types`;
- `ok/oop`;
- `ok/interfaces`;
- `ok/lambdas`;
- `ok/arrays`;
- `ok/generators`;
- `ok/macros`;
- `ok/test_decorators`;
- errores lexicos, sintacticos y semanticos.

Segun `README.md`, la suite se ejecuta con:

```bash
make build
bash tests_piad/hulk/run_tests.sh "$(pwd)" "$(pwd)/tests_piad/hulk"
```

La ejecucion directa de ese script falla en este checkout por finales de linea CRLF dentro de `tests_piad/hulk/run_tests.sh`. El error ocurre antes de probar programas HULK:

```text
tests_piad/hulk/run_tests.sh: line 7: $'\r': command not found
tests_piad/hulk/run_tests.sh: line 8: set: pipefail^M: invalid option name
```

Sin modificar el archivo del repositorio, se ejecuto la misma suite normalizando los `\r` solo en memoria:

```bash
bash <(tr -d '\r' < tests_piad/hulk/run_tests.sh) "$(pwd)" "$(pwd)/tests_piad/hulk"
```

El resultado fue `RESULT: ALL_PASS`. El resumen de la suite reporto:

```text
ok/minimal          20/20 [PASS]
ok/types            10/10 [PASS]
ok/oop              10/10 [PASS]
errors/lexical       6/6  [PASS]
errors/syntactic    10/10 [PASS]
errors/semantic     18/18 [PASS]
ok/extras           10/10 [bonus]
ok/macros            8/8  [bonus]
ok/arrays            8/8  [bonus]
ok/interfaces        6/6  [bonus]
ok/lambdas           6/6  [bonus]
ok/generators        6/6  [bonus]
ok/test_decorators   6/6  [bonus]
```

Este resultado no implica que el archivo `run_tests.sh` este corregido: solo demuestra que la suite funcional de programas pasa cuando se elimina el problema de CRLF durante la invocacion.

### 13.4 Logs historicos incluidos en el repositorio

`probar/logs/summary.txt` registra:

```text
build: OK
test_file: OK
inline_valid: OK
test_all: FAIL (exit 2)
```

`probar/logs/test_all.log` contiene suites con muchos casos `PASS`, incluyendo una seccion de codegen con `Total: 41 | Passed: 41 | Failed: 0`, y una seccion de `Feature: Closures + Decorators` con `Total: 7 | Passed: 7 | Failed: 0`. Sin embargo, el mismo log termina con fallo global de `make test-all`, por lo que no debe resumirse como "toda la suite pasa".

Estos logs son historicos y no contradicen la corrida actual: documentan el estado del repositorio o del entorno en el momento en que fueron generados.

`probar/spec_check/REPORTE_2026-05-28.txt` reporta una verificacion historica de programas `tests/hulk_programs` con:

```text
Resumen: 6/26 PASS
PASS          6
PARSE_FAIL    4
SEM_FAIL      4
CODEGEN_FAIL  11
MISMATCH      1
```

Este dato es importante porque muestra limitaciones reales en una verificacion anterior. Debe incorporarse como evidencia honesta, no ocultarse.

`probar/REPORTE_VERIFICACION_2026-03-23.md` registra una falla parcial historica en `test-ast-builder`, caso `function_expr_simple`. En la corrida actual de `make test-all`, `tests/test_ast_builder` reporto `79/79 PASS`, por lo que ese fallo historico no se reprodujo en esta auditoria.

---

## 14. Decisiones de diseno

### 14.1 Separacion por fases

Separar lexer, parser, AST, semantica y backend permite aislar responsabilidades:

- el lexer solo reconoce tokens;
- el parser solo reconoce estructura y construye AST;
- la semantica verifica significado;
- el backend traduce un AST ya validado.

Esta separacion facilita pruebas unitarias por fase y evita mezclar reglas de tipos con reglas sintacticas.

### 14.2 Uso de AST

El AST elimina ruido de la gramatica concreta y conserva solo informacion relevante para las fases posteriores. Tambien permite transformaciones como el desugaring de decoradores sin volver al texto fuente.

### 14.3 Uso de LLVM

LLVM evita escribir un backend nativo completo desde cero. El proyecto aprovecha IR tipado, verificacion estructural y emision de objeto. Esta eleccion es coherente con un proyecto academico que busca demostrar frontend y backend sin implementar manualmente seleccion de instrucciones.

### 14.4 Decoradores como extension

Los decoradores se implementan como azucar sintactica para composicion de funciones. Esta decision reduce el impacto en el backend: una funcion decorada puede modelarse como reasignacion del valor invocable, y una fabrica decoradora como llamada currificada.

### 14.5 Lambdas y closures

Las lambdas introducen ambiguedad sintactica y complejidad semantica por capturas. El proyecto resuelve la ambiguedad con lookahead local y representa closures con un entorno explicito `i8*` en LLVM, una tecnica clasica en compiladores.

### 14.6 Vtables para polimorfismo

La representacion de despacho dinamico con tag y vtable es una tecnica estandar para lenguajes orientados a objetos. Permite que el metodo ejecutado dependa del tipo dinamico del objeto, no solo del tipo estatico.

---

## 15. Limitaciones y trabajo futuro

Las siguientes limitaciones se desprenden del codigo y de los logs:

- La ejecucion directa de `tests_piad/hulk/run_tests.sh` falla en este checkout por finales de linea CRLF; la suite PIAD pasa cuando se normalizan los `\r` solo durante la invocacion.
- Los logs historicos muestran que no todas las suites han pasado en todo momento: `probar/logs/summary.txt` registra `test_all: FAIL`, y `probar/spec_check/REPORTE_2026-05-28.txt` registra 6/26 PASS. Esto debe leerse como evidencia historica, no como el resultado actual de `make test-all`.
- `grammar.ll1` presenta conflictos LL(1) en logs historicos; por tanto, la documentacion debe distinguir esa gramatica del builder integrado.
- El parser HULK necesita lookahead local para distinguir lambdas parentizadas de expresiones parentizadas.
- El sistema de scopes usa arreglos dinamicos con busqueda lineal; esto limita la afirmacion formal de complejidad `O(n)`.
- La conformidad estructural de protocolos se verifica por presencia de nombres de metodos, sin evidencia de chequeo completo de varianza o equivalencia profunda de firmas en esa rama.
- Los arreglos/vectores observados estan fuertemente orientados a casos numericos; no hay evidencia de chequeo completo de homogeneidad generica ni de bounds checking runtime.
- `as` aparece en codegen como no-op con opaque pointers; no se observa un chequeo runtime general de downcast.
- La inferencia de tipos es heuristica/ad-hoc, no un algoritmo general de inferencia polimorfica.
- Algunas features presentes en `tests_piad`, como macros o generadores, deben mencionarse solo si se auditan con mayor profundidad. Este informe se centra en las features requeridas y evidenciadas por los modulos principales.

Trabajo futuro razonable:

- normalizar los finales de linea de `tests_piad/hulk/run_tests.sh` para que la ejecucion directa funcione sin `tr -d '\r'`;
- regenerar logs historicos de verificacion para que coincidan con el estado actual observado;
- reducir conflictos en `grammar.ll1` o documentarla explicitamente como gramatica experimental;
- reemplazar busquedas lineales de scopes/tipos por tablas hash si se quiere sostener una complejidad lineal mas fuerte;
- ampliar chequeos de arrays y protocolos;
- agregar chequeos runtime para `as` e indexacion;
- consolidar documentacion de features que aparecen en PIAD pero no fueron parte central de esta auditoria.

---

## 16. Conclusiones

El proyecto implementa un compilador HULK de varias fases y contiene evidencia concreta de un frontend, un analizador semantico y un backend LLVM. El lexer se genera a partir de expresiones regulares; el parser integrado construye AST mediante una gramatica declarada como datos y acciones semanticas; el AST separa estructura de significado; el analisis semantico resuelve nombres, tipos, scopes, decoradores, lambdas, arreglos y OOP; el backend emite LLVM IR y contempla objetos, closures, vtables y ejecutables nativos.

Desde el punto de vista de Lenguajes de Programacion, el proyecto aborda funciones como valores, closures, tipos nominales, protocolos estructurales limitados, decoradores y polimorfismo por subtipado. Desde Compilacion, muestra analisis lexico por DFA, parsing predictivo con matices, construccion de AST, chequeo semantico, transformaciones, generacion de IR y enlace nativo.

La conclusion debe ser fuerte pero honesta: el compilador tiene una arquitectura rica y tecnicamente relevante, y la corrida actual de tests internos y PIAD normalizada respalda muchas de sus features. Al mismo tiempo, los logs historicos y las limitaciones documentadas obligan a distinguir entre estado actual observado, evidencia historica y aspectos que requieren auditoria mas profunda. Esa honestidad fortalece el informe porque alinea la explicacion tecnica con el estado real del repositorio.

---

## Fuentes internas consultadas

- `README.md`
- `agent.md`
- `Makefile`
- `grammar.ll1`
- `hulk_cli.c`
- `hulk_compiler.c`
- `hulk_tokens.c`
- `hulk_tokens.h`
- `generador_analizadores_lexicos/token_types.h`
- `generador_analizadores_lexicos/ast.h`
- `generador_analizadores_lexicos/ast.c`
- `generador_analizadores_lexicos/lexer.c`
- `generador_analizadores_lexicos/lexer.h`
- `generador_analizadores_lexicos/afd.c`
- `generador_analizadores_lexicos/afd.h`
- `generador_analizadores_lexicos/regex_lexer.l`
- `generador_analizadores_lexicos/regex_parser.c`
- `generador_analizadores_lexicos/regex_ast_actions.c`
- `generador_parser_ll1/grammar.c`
- `generador_parser_ll1/first_follow.c`
- `generador_parser_ll1/ll1_table.c`
- `generador_parser_ll1/parser.c`
- `hulk_ast/builder/hulk_ll1_builder.c`
- `hulk_ast/builder/hulk_ll1_builder.h`
- `hulk_ast/core/hulk_ast.h`
- `hulk_ast/core/hulk_ast_nodes.c`
- `hulk_ast/core/hulk_ast_context.c`
- `hulk_ast/semantic/hulk_semantic.h`
- `hulk_ast/semantic/hulk_semantic_internal.h`
- `hulk_ast/semantic/hulk_semantic_types.c`
- `hulk_ast/semantic/hulk_semantic_scope.c`
- `hulk_ast/semantic/hulk_semantic_collect.c`
- `hulk_ast/semantic/hulk_semantic_check.c`
- `hulk_ast/semantic/hulk_semantic_check_expr.c`
- `hulk_ast/semantic/hulk_semantic_check_stmt.c`
- `hulk_ast/semantic/hulk_semantic_desugar.c`
- `hulk_ast/semantic/hulk_semantic_infer.c`
- `hulk_ast/codegen/hulk_codegen.c`
- `hulk_ast/codegen/hulk_codegen_internal.h`
- `hulk_ast/codegen/hulk_codegen_expr.c`
- `hulk_ast/codegen/hulk_codegen_call.c`
- `hulk_ast/codegen/hulk_codegen_control.c`
- `hulk_ast/codegen/hulk_codegen_oop.c`
- `hulk_ast/codegen/hulk_codegen_stmt.c`
- `hulk_ast/codegen/hulk_codegen_typedecl.c`
- `hulk_ast/codegen/hulk_codegen_types.c`
- `hulk_ast/codegen/hulk_codegen_runtime.c`
- `hulk_ast/codegen/hulk_codegen_infer.c`
- `tests/test_lexer.c`
- `tests/test_parser.c`
- `tests/test_ast.c`
- `tests/test_hulk_ast.c`
- `tests/test_ast_builder.c`
- `tests/test_semantic.c`
- `tests/test_codegen.c`
- `tests/test_feature_decorators_closures.c`
- `tests/test_ll1_builder.c`
- `tests/hulk_programs/*`
- `tests_piad/hulk/**/*`
- `probar/logs/summary.txt`
- `probar/logs/test_all.log`
- `probar/spec_check/REPORTE_2026-05-28.txt`
- `probar/REPORTE_VERIFICACION_2026-03-23.md`
