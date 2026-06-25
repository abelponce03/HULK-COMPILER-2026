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

Esta distincion es importante porque `grammar.ll1` presenta conflictos LL(1) en varios puntos -que `build_ll1_table` reporta como `WARN` al cargarla-, mientras que el builder integrado resuelve casos concretos con lookahead local.

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

#### Por que es posible construir el DFA directamente (intuicion)

Para reconocer un lexema, un automata necesita responder en cada paso una sola pregunta: *"dado lo que llevo leido, en que puntos de la expresion regular podria encontrarme ahora mismo"*. La ruta clasica para responder esto tiene dos pasos:

1. **Thompson:** traducir la regex a un AFN con transiciones-epsilon (un fragmento por operador).
2. **Construccion de subconjuntos:** determinizar ese AFN, donde cada estado del DFA es un conjunto de estados del AFN alcanzables.

El metodo directo observa que el paso intermedio (el AFN con epsilons) es prescindible. La informacion que la construccion de subconjuntos extrae del AFN -*"que simbolos pueden ir primero", "cuales pueden ir al final", "que puede seguir a que"*- se puede leer directamente del arbol sintactico de la regex. La clave es **numerar cada hoja del arbol** (cada aparicion de un simbolo concreto) con una *posicion* unica. Entonces:

- un **estado del DFA** = un conjunto de posiciones (exactamente el rol que jugaba un conjunto de estados-AFN);
- la **transicion** desde un estado con el simbolo `x` = "estando en estas posiciones, si leo `x`, a que posiciones puedo saltar".

El nexo entre "estoy en la posicion `p`" y "puedo saltar a la posicion `q`" es la funcion `followpos`. Por eso, una vez calculado `followpos`, el AFN ya no aporta nada: el DFA se construye recorriendo conjuntos de posiciones. Esto ahorra construir y almacenar el AFN intermedio y evita los estados muertos que la determinizacion tendria que colapsar despues.

```text
metodo clasico:   regex --Thompson--> AFN(epsilons) --subconjuntos--> DFA
metodo directo:   regex --> arbol --> {nullable, firstpos, lastpos, followpos} --> DFA
                          (este proyecto)
```

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

#### Ejemplo trabajado de las funciones del arbol

Considerese la regex `(a|b)*abb#`, el ejemplo cannonico del Dragon Book (el `#` es el marcador de fin que el proyecto agrega a cada token). Numerando las hojas de izquierda a derecha:

```text
posicion:   1   2       3   4   5   6
simbolo:    a   b       a   b   b   #
            \___/       (la (a|b) esta repetida por *)
            (a|b)*
```

Recorriendo el arbol en postorden, `ast_compute_functions` produce:

| nodo | nullable | firstpos | lastpos |
| --- | :---: | --- | --- |
| hoja `a` (1) | no | {1} | {1} |
| hoja `b` (2) | no | {2} | {2} |
| `a\|b` | no | {1,2} | {1,2} |
| `(a\|b)*` | **si** | {1,2} | {1,2} |
| `(a\|b)*a` (concat) | no | {1,2,3} | {3} |
| ... `a b b #` | no | {1,2,3} | {6} |

Dos casos ilustran las reglas. En la raiz, `firstpos` es `{1,2,3}`: como `(a|b)*` es anulable, el primer simbolo de una cadena puede venir de la repeticion (posiciones 1 o 2) **o** saltarsela y empezar en la `a` de la posicion 3. En cambio `lastpos` de la raiz es `{6}` solo, porque el `#` (posicion 6) no es anulable y siempre cierra.

Luego `ast_compute_followpos` deriva, aplicando la regla de concatenacion en cada `.` y la regla de repeticion en el `*`:

```text
followpos(1) = {1,2,3}     followpos(4) = {5}
followpos(2) = {1,2,3}     followpos(5) = {6}
followpos(3) = {4}         followpos(6) = {}   (# es estado final)
```

`followpos(1) = followpos(2) = {1,2,3}` captura el bucle: tras leer cualquier `a` o `b` del `(a|b)*` se puede repetir (volver a 1 o 2) o avanzar a la `a` fija (posicion 3). Estas seis filas son toda la informacion que el constructor de DFA necesita; el AFN de Thompson nunca se materializa.

### 3.1.2 Construccion directa del DFA

`generador_analizadores_lexicos/afd.c` representa cada estado del DFA como un conjunto de posiciones. El estado inicial es `firstpos(root)`. Luego se procesa una worklist de estados: para cada simbolo del alfabeto, el siguiente estado es la union de `followpos(p)` para todas las posiciones `p` del estado actual cuya hoja tenga ese simbolo.

La idea, expresada como pseudocodigo, es un punto fijo sobre conjuntos de posiciones:

```text
Dstates <- { firstpos(root) }      // sin marcar
mientras exista un estado S sin marcar en Dstates:
    marcar S
    para cada simbolo a del alfabeto:
        U <- union de followpos(p) para toda p in S con simbolo(p) == a
        si U no esta vacio:
            si U no esta en Dstates: agregar U sin marcar
            transicion[S, a] <- U
```

El invariante que da correctitud al metodo es: *un estado `S` representa exactamente el conjunto de posiciones donde el automata podria estar tras haber consumido el prefijo leido hasta ahora*. Por eso el estado inicial es `firstpos(root)` (lo que puede ir primero) y la transicion con `a` reune los `followpos` de las posiciones de `S` que llevan una `a` (a donde se puede ir tras consumir esa `a`). Como el numero de subconjuntos de posiciones es finito, la worklist termina.

En el codigo, la "marca" de un estado es implicita: el indice `front` recorre `dfa->states` y un estado queda "procesado" cuando `front` pasa sobre el. Dos detalles de implementacion eficientes:

- `dfa_find_state` compara conjuntos con `memcmp` sobre el bitset (`positions_equal`), de modo que un conjunto de posiciones ya visto se reusa como estado en vez de duplicarse; esto es lo que cierra los ciclos del automata.
- `ctx->leaf_at[p]` indexa la hoja de cada posicion en O(1), evitando recorrer el arbol para saber que simbolo lleva la posicion `p`.

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

#### Traza de la construccion del DFA

Retomando `(a|b)*abb#` con las `followpos` ya calculadas, el algoritmo produce el siguiente automata. Cada estado es el conjunto de posiciones que lo define:

```text
A = firstpos(root) = {1,2,3}        (estado inicial)

desde A:
  con 'a': union de followpos de las posiciones de A con simbolo 'a' = {1,3}
           followpos(1) ∪ followpos(3) = {1,2,3} ∪ {4} = {1,2,3,4} = B
  con 'b': posiciones de A con simbolo 'b' = {2}
           followpos(2) = {1,2,3} = A   (vuelve a si mismo)

desde B = {1,2,3,4}:
  con 'a': {1,3} -> {1,2,3,4} = B
  con 'b': {2,4} -> followpos(2) ∪ followpos(4) = {1,2,3} ∪ {5} = {1,2,3,5} = C

desde C = {1,2,3,5}:
  con 'a': {1,3} -> B
  con 'b': {2,5} -> {1,2,3} ∪ {6} = {1,2,3,6} = D

desde D = {1,2,3,6}:
  con 'a': {1,3} -> B
  con 'b': {2}   -> A
  D contiene la posicion 6 (#)  =>  D es estado de aceptacion
```

Tabla de transiciones resultante (4 estados, ningun estado muerto generado):

| estado | conjunto | `a` | `b` | acepta |
| --- | --- | :---: | :---: | :---: |
| A | {1,2,3} | B | A | no |
| B | {1,2,3,4} | B | C | no |
| C | {1,2,3,5} | B | D | no |
| D | {1,2,3,6} | B | A | **si** |

Este es exactamente el DFA minimo que reconoce `(a|b)*abb`, obtenido sin construir ningun AFN. Dos observaciones conectan la traza con `afd.c`:

- la aceptacion de `D` surge de que su conjunto contiene la posicion del `#` (`ctx->pos_to_token[6] != -1`), tal como hace el bucle de `is_accept`;
- las aristas que "vuelven" a estados ya creados (por ejemplo `b` desde A, o `a` desde B) son detectadas por `dfa_find_state`, que reconoce el conjunto de posiciones repetido y reusa el estado en lugar de crear uno nuevo.

En el lexer real, la regex de cada token aporta su propio `#` con un `pos_to_token` distinto, y todas se combinan con `OR` bajo una unica raiz. Por eso un mismo estado del DFA puede contener varios marcadores `#`; ahi es donde interviene `dfa_priority_min_id` para decidir, por menor `token_id`, cual token gana (palabra clave sobre identificador).

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

#### FIRST y FOLLOW como punto fijo

`first_follow.c` calcula ambos conjuntos por iteracion hasta convergencia (`do { ... } while (changed)`), el patron estandar para ecuaciones recursivas sobre conjuntos. La estructura es identica en ambos: se repite un barrido de todas las producciones, acumulando elementos, hasta que un barrido completo no agrega nada nuevo (`changed == 0`). La terminacion esta garantizada porque los conjuntos solo crecen y estan acotados por la cantidad de terminales.

`FIRST` se inicializa con `FIRST(a) = {a}` para cada terminal `a`, y luego, para cada produccion `A -> X1 X2 ... Xn`, agrega `FIRST` de la secuencia derecha a `FIRST(A)`. La funcion clave es `first_of_sequence`, que implementa la regla de propagacion de epsilon: recorre los simbolos de izquierda a derecha agregando su `FIRST`, y **solo continua al siguiente simbolo si el actual es anulable**; epsilon pertenece a `FIRST` de la secuencia unicamente si *todos* los simbolos son anulables.

```c
for(int i = 0; i < n; i++) {
    First_Set* fs = &table->first[symbol_index(seq[i])];
    for(int j = 0; j < fs->count; j++) first_set_add(result, fs->elements[j]);
    if(!fs->has_epsilon) { all_have_epsilon = 0; break; }  // se detiene
}
if(all_have_epsilon) result->has_epsilon = 1;
```

`FOLLOW` se inicializa con `$ ∈ FOLLOW(S)` para el simbolo inicial. Para cada produccion `A -> ... Xi β`, aplica las dos reglas clasicas: `FIRST(β) - {ε} ⊆ FOLLOW(Xi)`, y ademas, si `β` es vacio o anulable (`β ⇒* ε`), entonces `FOLLOW(A) ⊆ FOLLOW(Xi)`. Este segundo caso es el que propaga el contexto del no terminal padre hacia el ultimo simbolo de su cuerpo, y por eso tambien debe iterarse hasta punto fijo: un cambio en `FOLLOW(A)` puede obligar a re-propagar a `FOLLOW(Xi)`.

#### Construccion de la tabla y conflictos LL(1)

`ll1_table.c` llena `M[A, a]` con dos reglas (`build_ll1_table`): para cada produccion `A -> α`, (1) por cada terminal `a ∈ FIRST(α)` se pone `M[A,a] = A->α`; (2) si `ε ∈ FIRST(α)`, por cada `b ∈ FOLLOW(A)` se pone `M[A,b] = A->α`. Una gramatica es LL(1) si y solo si ninguna celda recibe dos producciones distintas.

El detalle relevante para este proyecto es como se reacciona ante un conflicto. Cuando una celda ya ocupada recibe otra produccion, `build_ll1_table` **no aborta**: marca `is_ll1 = 0`, emite `LOG_WARN_MSG("ll1", "Conflicto LL(1)...")` y resuelve la celda con una heuristica fija -preferir la produccion no-epsilon sobre la epsilon-. Por eso, al cargar `grammar.ll1` aparecen avisos de conflicto en `TopLevel`, `CmpExpr'`, `AddExpr'`, etc.: son `WARN` que documentan que esa gramatica no es estrictamente LL(1), no fallos de ejecucion. La tabla queda construida y utilizable, pero su validez como LL(1) puro queda invalidada, y es justo la razon por la que el builder de HULK recurre ademas a lookahead local (seccion 4.4).

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

Por que un lookahead de un token no basta: tanto `(x)` (expresion) como `(x) => x+1` (lambda) empiezan con `(` y pueden tener un prefijo arbitrariamente largo identico (`(a, b, c, ...)`). La decision solo se puede tomar al ver lo que sigue al parentesis de cierre, que puede estar a cualquier distancia. Por eso no existe un `k` fijo que haga LL(k) este punto: se necesita lookahead no acotado.

El algoritmo de `lookahead_is_lambda` resuelve esto con un escaneo de parentesis balanceados sobre una **copia** del estado del lexer (no consume la entrada real):

```c
static int lookahead_is_lambda(LexerContext lx_copy) {
    int depth = 1;                       // ya se vio el '(' de apertura
    for (;;) {
        Token t = lexer_next_token(&lx_copy);
        if (t.type == TOKEN_EOF) return 0;
        if (t.type == TOKEN_LPAREN) depth++;
        else if (t.type == TOKEN_RPAREN && --depth == 0) {
            int next = next_type_inplace(&lx_copy);
            if (next == TOKEN_ARROW) return 1;          // (params) =>
            if (next != TOKEN_COLON) return 0;          // (expr)
            // (params) : Tipo => ...  -> saltar la anotacion de tipo
            return lookahead_skip_type_ref(&lx_copy) &&
                   next_type_inplace(&lx_copy) == TOKEN_ARROW;
        }
    }
}
```

Mantiene un contador `depth` para emparejar parentesis anidados y, al cerrar el parentesis de nivel 0, decide: si lo que sigue es `=>`/`->` (`TOKEN_ARROW`), es lambda; si es `:`, todavia puede ser una lambda con tipo de retorno anotado, asi que salta la anotacion (`lookahead_skip_type_ref`) y vuelve a comprobar la flecha; cualquier otra cosa significa expresion parentizada. Como trabaja sobre una copia por valor de `LexerContext`, el lexer principal no se ve afectado: el lookahead es puramente predictivo. Este `lookahead_is_lambda` se invoca desde el bucle del parser (lineas ~960 y ~1077) justamente cuando el tope de la pila es un no terminal de expresion y el token actual es `(`, expandiendo entonces hacia `NT_Lambda` en lugar de hacia la produccion de expresion parentizada.

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
- al cargar `grammar.ll1`, `build_ll1_table` emite advertencias de conflictos LL(1), por ejemplo en `TopLevel`, `TypeMember`, `CmpExpr'`, `ConcatExpr'`, `AddExpr'`, `Term'`, `Factor'`, `AsExpr` y `PrimaryTail`.

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

`hulk_ast/codegen/hulk_codegen_expr.c` contiene la emision de `NODE_FUNCTION_EXPR`. La lambda se compila con la tecnica clasica de *closure conversion*: una funcion anonima con variables libres se transforma en (a) una funcion global de nivel superior que recibe su entorno como un parametro extra, y (b) un registro en memoria (el *closure*) que empaqueta el puntero a esa funcion junto con los valores capturados. Una funcion que en el fuente "captura" su entorno lexico se vuelve, en el IR, una funcion plana mas un dato.

##### Paso 1 — Determinar las capturas

El conjunto de variables libres no se toma solo del campo `captures` que dejo la semantica: `collect_lambda_captures` recorre el cuerpo de la lambda y reune todo identificador que **no** sea un parametro propio ni una captura ya conocida. Es un calculo de variables libres `FV(cuerpo) \ parametros`, con cuidado especial en lambdas anidadas (una lambda interior captura por su cuenta, pero la exterior debe arrastrar lo que la interior necesitara del scope comun). El resultado es la lista ordenada `cap_names`.

##### Paso 2 — Layout del entorno en heap

El closure es un bloque reservado con `malloc` cuyo tamano es `8 * (cap_count + 1)` bytes: una ranura por el puntero a funcion y una por cada captura, alineadas a 8.

```text
closure (i8*) ->  +--------+ offset 0   : puntero a lambda.N
                  | fn ptr |
                  +--------+ offset 8   : captura 0
                  | cap 0  |
                  +--------+ offset 16  : captura 1
                  | cap 1  |
                  +--------+ ...
```

El codigo escribe primero el puntero a funcion (`LLVMBuildStore(c->builder, fn, closure)`) y luego cada captura en su ranura, calculando la direccion con `GEP` de offset `8 * (i + 1)` sobre el `i8*`:

```c
LLVMValueRef closure = LLVMBuildCall2(..., c->fn_malloc, &bytes, 1, "closure");
LLVMBuildStore(c->builder, fn, closure);                 // ranura 0: la funcion
for (int i = 0; i < cap_count; i++) {
    LLVMValueRef offset = LLVMConstInt(i64, 8 * (i + 1), 0);
    LLVMValueRef slot = LLVMBuildInBoundsGEP2(c->builder, i8, closure, &offset, 1, ...);
    LLVMBuildStore(c->builder, caps[i].global, slot);     // ranura i+1: la captura
}
```

##### Paso 3 — Prologo de la funcion generada

`lambda.N` se declara con firma `(i8* env, <params...>) -> ret`: **el entorno es siempre el parametro 0** y los parametros del usuario van detras. En su bloque `entry`, el codigo (i) hace `alloca` + `store` de cada parametro real (`LLVMGetParam(fn, i+1)`), y (ii) **rehidrata las capturas**: por cada captura lee su ranura del `env` con el mismo offset `8*(i+1)`, hace `alloca` local y la define en el scope. Asi, dentro del cuerpo, una variable capturada se usa igual que una local; la indireccion al heap es transparente.

##### Paso 4 — Llamada indirecta (ABI del closure)

`cg_emit_call_closure_raw` materializa el convenio: carga el puntero de funcion desde el offset 0 del closure y llama pasando **el propio closure como argumento 0**, seguido de los argumentos del usuario.

```c
LLVMValueRef fn_ptr = LLVMBuildLoad2(c->builder, c->t_i8ptr, closure, "closure.fn");
argv[0] = closure;  /* el entorno viaja como primer argumento */
... // argv[1..] = argumentos del usuario
LLVMBuildCall2(c->builder, fn_type, fn_ptr, argv, argc, ...);
```

El ciclo se cierra de forma consistente: en el paso 2 se guardo `fn` en el offset 0 y aqui se carga de ese mismo offset; en el paso 3 la funcion espera `env` como parametro 0 y aqui se le pasa el closure como argumento 0. Ese acuerdo entre productor y consumidor es lo que hace funcionar a las funciones de orden superior (`lambda_compose`, `lambda_make_adder`) sin necesidad de tipos dependientes.

Esta implementacion muestra funciones anonimas como valores de primera clase, aunque con limitaciones: las capturas se copian por valor en el momento de crear el closure (captura por valor, no por referencia compartida); el tipado de closures depende de anotaciones, inferencias locales y del campo `static_type`; y no hay evidencia de un sistema general de inferencia Hindley-Milner ni de polimorfismo parametrico para lambdas.

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

`hulk_ast/codegen/hulk_codegen_expr.c` emite `NODE_VECTOR_LIT` reservando memoria con `malloc`. El arreglo es un bloque contiguo con una cabecera de tamano seguida de los elementos. El layout observado es:

```text
offset 0: size i32        (cuantos elementos)
offset 8: elemento 0      (double, o i8* si es arreglo de objetos)
offset 16: elemento 1
...
```

##### Construccion del literal

Para `[e0, e1, ..., e(n-1)]` se reserva `8 + 8*n` bytes (8 de cabecera alineada + 8 por elemento), se escribe el tamano `n` como `i32` en el offset 0, y luego cada elemento en su posicion mediante aritmetica de punteros con `GEP`:

```c
int byte_size = 8 + 8 * n;                         /* cabecera + n elementos */
LLVMValueRef raw = LLVMBuildCall2(..., c->fn_malloc, &size_const, 1, "vec");
LLVMBuildStore(c->builder, LLVMConstInt(c->t_i32, n, 0), raw);   /* size en [0..3] */
for (int i = 0; i < n; i++) {
    LLVMValueRef offset = LLVMConstInt(i64, 8 + 8 * i, 0);
    LLVMValueRef gep = LLVMBuildInBoundsGEP2(c->builder, i8, raw, &offset, 1, ...);
    LLVMBuildStore(c->builder, cg_emit_expr(c, vn->items.items[i]), gep);  /* elem i */
}
```

La razon de reservar la cabecera a 8 bytes (aunque el tamano sea un `i32` de 4) es de **alineacion**: los elementos `double` deben quedar en direcciones multiplo de 8, asi que el primer elemento empieza en el offset 8 y no en el 4. Por eso el `size` ocupa `[0..3]` y `[4..7]` queda como relleno.

##### Indexacion

`NODE_INDEX_EXPR` traduce `arr[i]` a la direccion `base + 8 + 8*i`. Como los indices HULK son `double`, primero se convierte el indice a entero con `LLVMBuildFPToSI` y luego se calcula el offset:

```text
offset = 8 (cabecera) + 8 * (i64)i
elem   = load <tipo>, base + offset
```

```c
LLVMValueRef idx_i = LLVMBuildFPToSI(c->builder, idx_d, i64, "idx");   /* double -> i64 */
LLVMValueRef mul    = LLVMBuildMul(c->builder, idx_i, eight, "ofsmul"); /* 8*i */
LLVMValueRef offset = LLVMBuildAdd(c->builder, mul, eight, "ofs");      /* +8 cabecera */
LLVMValueRef gep    = LLVMBuildInBoundsGEP2(c->builder, i8, obj, &offset, 1, "elem.ptr");
```

El tipo del elemento cargado se decide por `static_type` anotado en semantica: `double` por defecto, o `i8*` cuando el arreglo es de objetos (`[]` sobre un tipo, u `Object`). No se emite comprobacion de rango: un indice fuera de limites produce acceso a memoria invalida, coherente con la limitacion ya senalada de ausencia de bounds checking.

La funcion `.size()` se intercepta en `hulk_ast/codegen/hulk_codegen_call.c` cargando el entero `i32` almacenado en el offset 0 del arreglo y convirtiendolo a `double` (`raw_size`), de modo que encaja con el resto del sistema numerico de HULK.

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

El algoritmo es un plegado (fold) sobre la lista de decoradores. Partiendo de la expresion `f` (el nombre de la funcion ya declarada), se envuelve por cada decorador, de derecha a izquierda:

```text
fuente:   decor log, memoize(100) function f(...) -> body;

paso 0:   acc = f
paso 1:   aplicar memoize(100):   acc = memoize(100)(acc)   // fabrica currificada
paso 2:   aplicar log:            acc = log(acc)            // decorador simple

resultado:
    function f(...) -> body;          // target intacto
    f := log( memoize(100)(f) );      // reasignacion del invocable
```

Cada paso construye en la arena del `HulkASTContext` un `CallExprNode`: para un decorador simple, una llamada con un argumento (la funcion acumulada); para una fabrica con argumentos, primero una llamada `decor(args)` y sobre su resultado otra llamada con la funcion. El orden derecha-a-izquierda hace que el decorador escrito mas a la izquierda quede como el *envoltorio mas externo*, que es la semantica habitual de los decoradores. Como la transformacion es puramente AST -> AST, las fases posteriores (chequeo de tipos y codegen) ven una funcion normal y una asignacion, sin reglas especiales para decoradores en el nucleo del lenguaje.

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

##### El dispatcher como sintesis de atributos

`sem_check_expr` es la pieza que implementa este enfoque bottom-up. Funciona como una **gramatica de atributos sintetizados**: el tipo de un nodo se calcula a partir de los tipos de sus hijos, que se obtienen llamando recursivamente a `sem_check_expr` antes de combinar. El recorrido es, en efecto, postorden: primero los operandos, luego el operador.

```c
HulkType* sem_check_expr(SemanticContext *c, HulkNode *node) {
    HulkType *t;
    switch (node->type) {
        case NODE_NUMBER_LIT: t = c->t_number;  break;   // hoja: tipo directo
        case NODE_BINARY_OP:  t = check_binary_op(c, ...); break;  // combina hijos
        ...
    }
    if (t && t->name && t->kind != HULK_TYPE_ERROR)
        node->static_type = t->name;   // <-- UNICO punto de anotacion
    return t;
}
```

Dos propiedades de diseno valen la pena:

- **Un solo punto de anotacion.** Cada subrutina (`check_binary_op`, `check_call`, ...) solo *retorna* el `HulkType`; es el dispatcher central el que escribe `node->static_type`. Esto garantiza el invariante de que **todo nodo de expresion chequeado queda anotado de forma uniforme** con el nombre canonico del tipo, que es justo lo que el codegen lee despues (`cg_static_type_of`). El analisis semantico no transforma el arbol: lo *decora*.

- **Recuperacion de errores por tipo absorbente.** Existe un tipo especial `<error>` (`c->t_error`). Cuando una comprobacion falla -por ejemplo, sumar algo que no conforma a `Number`- se reporta el error y se propaga `t_error`. La clave es que `t_error` **no se anota** (la guarda `t->kind != HULK_TYPE_ERROR`) y conforma de forma laxa, de modo que un error en una subexpresion no desencadena una avalancha de errores derivados en los nodos padre. Esto permite reportar varios errores independientes en una sola pasada en vez de abortar al primero.

El nucleo de las reglas de tipo es `sem_type_conforms(sub, super)`, que decide compatibilidad: recorre la cadena `parent` para subtipado nominal, trata `Object` como tope universal, y aplica la rama estructural para protocolos. Por ejemplo, en `NODE_INDEX_EXPR` el chequeo exige `conforms(idx, Number)` y deriva el tipo del elemento parseando el sufijo `[]` del tipo del objeto; en los operadores aritmeticos exige que ambos lados conformen a `Number`. Asi, "verificar tipos" se reduce a llamadas a `sem_type_conforms` sobre los tipos sintetizados de los hijos.

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

##### Asignacion de slots y resolucion de overrides

El mecanismo se sostiene en dos algoritmos concretos de `hulk_codegen_types.c`:

- **Slots globales por nombre.** `cg_method_slot` mantiene una tabla unica para todo el programa que asocia cada *nombre* de metodo a un indice de slot. La primera vez que aparece un nombre se le asigna el siguiente indice libre; las apariciones posteriores reusan el mismo. La consecuencia clave es que un mismo metodo (por ejemplo `area`) ocupa **el mismo slot en la vtable de toda clase que lo defina**, sin importar la jerarquia. Asi, el call site no necesita conocer el tipo dinamico: basta indexar `vtable[slot(area)]`.

```c
int cg_method_slot(CodegenContext *c, const char *name) {
    for (int i = 0; i < c->method_slot_count; i++)
        if (strcmp(c->method_slot_names[i], name) == 0) return i;   // reusar
    c->method_slot_names[c->method_slot_count] = name;              // nuevo
    return c->method_slot_count++;
}
```

- **Resolucion por la cadena de herencia.** Al construir la vtable de una clase, `cg_type_resolve_method` decide que funcion concreta va en cada slot recorriendo `ti -> parent -> parent...` y devolviendo la primera implementacion encontrada. Como parte del tipo mas derivado hacia arriba, una redefinicion en la subclase oculta a la del padre (override), mientras que los slots no redefinidos heredan el puntero del ancestro.

```c
LLVMValueRef cg_type_resolve_method(CGTypeInfo *ti, const char *name) {
    for (CGTypeInfo *cur = ti; cur; cur = cur->parent)
        for (int i = 0; i < cur->method_count; i++)
            if (strcmp(cur->methods[i]->name, name) == 0)
                return cur->methods[i]->value;   // primera = mas derivada
    return NULL;
}
```

En resumen: la fase de codegen aplana la jerarquia en una vtable por tipo donde cada slot ya apunta a la implementacion correcta (propia o heredada), y el despacho en tiempo de ejecucion es un unico salto indirecto `vtable[slot]`. Esto corresponde a polimorfismo por subtipado con despacho dinamico. Las pruebas relacionadas incluyen:

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

### 11.4 Control de flujo: basic blocks, ramas y SSA

En HULK las estructuras de control son *expresiones* (un `if` tiene valor), pero el IR de LLVM esta en forma SSA (cada variable se asigna una sola vez). Reconciliar ambas cosas es el problema central que resuelve `hulk_codegen_control.c`, y lo hace de dos maneras segun la construccion.

##### `if`/`elif`/`else` con nodo PHI

Un `if` con valor genera un grafo de bloques basicos que convergen en un bloque `ifmerge`. El reto SSA es: el resultado del `if` proviene de un bloque distinto segun la rama tomada, pero en SSA no se puede "reasignar" una variable desde varios sitios. La solucion canonica es la **instruccion PHI**, que en el bloque de convergencia elige el valor segun *de que bloque predecesor se llego*.

```text
        cond?
       /     \
   [then]   [else/elif...]
       \     /
     [ifmerge]
        phi = φ( valor_then desde then, valor_else desde else, ... )
```

El algoritmo (`cg_emit_if`): por cada rama emite su cuerpo, registra el **par (valor, bloque de salida)** -el bloque se relee con `LLVMGetInsertBlock` porque el cuerpo pudo crear bloques anidados- y salta a `ifmerge`. Al final construye un unico `phi` con todos esos pares:

```c
values[idx] = cg_emit_expr(c, n->then_body);
blocks[idx] = LLVMGetInsertBlock(c->builder);   /* predecesor real */
LLVMBuildBr(c->builder, merge_bb);
...
LLVMValueRef phi = LLVMBuildPhi(c->builder, result_type, "ifval");
LLVMAddIncoming(phi, values, blocks, idx);       /* φ(v_i desde bloque_i) */
```

Los `elif` se encadenan: el bloque `else` de cada condicion es donde se emite el siguiente `elif`, formando la cascada `cond1 ? ... : (cond2 ? ... : else)`. Hay un caso especial honesto: si **todas** las ramas son `void`, no se puede formar un PHI de tipo void, asi que el `if` entero se considera void y se devuelve `undef`.

##### `while` con alloca (memoria mutable)

El `while` no usa PHI sino la otra tecnica estandar para valores que cambian en un bucle: una **celda en memoria** (`alloca`) que se actualiza con `store`. Esto sortea SSA delegando la "reasignacion" a la memoria, que LLVM despues promueve a registros (mem2reg) si conviene.

```text
[entry] -> store 0 -> while.cond
while.cond: cond? -> while.body | while.end
while.body: result_ptr := body ; br while.cond
while.end:  load result_ptr      (valor del while = ultimo cuerpo ejecutado)
```

El bucle reserva `result_ptr`, lo inicializa en `0.0`, y en cada iteracion del `body` guarda el valor del cuerpo; al salir, `while.end` carga ese ultimo valor. Igual que en el `if`, si el cuerpo es `void` el `while` entero es void (se devuelve `undef`) para no imprimir un `0` espurio en posicion top-level.

Las condiciones de ambos se normalizan a `i1`: como los booleanos viven como `double`, una condicion `double` se compara con `0.0` (`LLVMRealONE`, "truthy = distinto de cero") antes de usarse en `LLVMBuildCondBr`. Esta es la conexion entre el sistema numerico de HULK y las ramas tipadas de LLVM.

---

## 12. Complejidad temporal

Una afirmacion de complejidad solo es util si se dice **respecto a que** se mide. Por eso conviene fijar primero los parametros de tamano, porque cada fase del pipeline opera sobre una entrada distinta:

| simbolo | que mide |
| --- | --- |
| `n_c` | numero de caracteres del fuente |
| `n_t` | numero de tokens producidos por el lexer |
| `n_a` | numero de nodos del AST |
| `s` | numero de simbolos en un scope (variables/funciones visibles) |
| `d` | profundidad de anidamiento de scopes |
| `T` | numero de tipos definidos en el programa |
| `m` | numero de miembros (atributos + metodos) de un tipo |
| `K` | numero de slots de metodo distintos del programa |

Hay una relacion natural `n_a ≤ n_t ≤ n_c`: cada nodo consume al menos un token y cada token al menos un caracter. Eso permite, al final, expresar el coste global en terminos de un unico `n` cuando los demas parametros estan acotados.

#### Nota previa: el costo amortizado de los arreglos dinamicos

Varias estructuras del proyecto (la arena de nodos en `ast.c`, la worklist del DFA en `afd.c`, las tablas de simbolos) crecen **duplicando capacidad** (`capacity *= 2`). Este patron tiene un costo amortizado O(1) por insercion: aunque un `realloc` puntual copia todo el arreglo, las copias sucesivas suman una serie geometrica `n + n/2 + n/4 + ... < 2n`, es decir O(n) total para n inserciones. Por eso los crecimientos de buffer **no** elevan la complejidad asintotica de las fases que los usan; se pueden tratar como insercion constante.

### 12.1 Lexer

Hay que separar dos costos de naturaleza distinta:

- **Construccion del DFA (una sola vez, sobre la especificacion):** depende del numero de posiciones `p` del arbol de regex combinado, no del programa del usuario. `ast_compute_followpos` es O(p²) en el peor caso (por cada posicion en un `lastpos` se une un `firstpos`), y la construccion directa visita cada estado por cada simbolo del alfabeto Σ uniendo `followpos`: O(|estados| · |Σ| · p). El numero de estados es a lo sumo `2^p` en teoria, pero para regex de tokens reales es pequeño y acotado. **Este costo es fijo: se paga una vez al arrancar el compilador, no por programa compilado.**

- **Escaneo (por programa):** con el DFA ya construido, `lexer_next_token` avanza una transicion por caracter consultando `dfa->next_state[state][c]` en O(1). El reconocimiento maximal-munch puede releer hasta el ultimo aceptador, pero cada caracter se visita un numero acotado de veces, de modo que el escaneo es **O(n_c)**, lineal en el tamano del fuente.

### 12.2 Parser y construccion del AST

El builder dirigido por tabla hace operaciones de pila O(1) por simbolo de gramatica y ejecuta acciones semanticas de costo acotado. En el caso normal cada token se consume una vez, de modo que la construccion del AST es **O(n_t)**.

La excepcion es el lookahead de lambdas (`lookahead_is_lambda`), que ante un `(` escanea hasta el parentesis de cierre balanceado. Si ese escaneo recorre `L` tokens, su costo es O(L). En el peor caso patologico -muchos `(` con prefijos largos cada uno- esos escaneos pueden solaparse y dar **O(n_t²)**. En programas reales `L` es pequeño (la lista de parametros de una lambda), por lo que el comportamiento observado sigue siendo lineal. La distincion honesta es: *amortizado lineal en la practica, cuadratico en el peor caso teorico*.

### 12.3 Analisis semantico

`sem_check_expr` visita cada nodo una vez (recorrido O(n_a)), pero **cada visita hace consultas de entorno cuyo costo no es constante**. `sem_lookup`, `sem_lookup_local`, `sem_lookup_member` y `sem_type_resolve` recorren arreglos linealmente. Por tanto el costo de una consulta es, en el peor caso, O(s·d) para resolver un nombre (s simbolos por scope, d scopes en la cadena) o O(m + T) para resolver un miembro o un tipo. El total queda:

```text
T_sem(programa) = O(n_a · (s·d + m + T))
```

Cuando los scopes y las jerarquias estan acotados por una constante -el caso de programas tipicos- el factor entre parentesis es O(1) y la fase es **efectivamente O(n_a)**. El termino que rompe la linealidad es `s·d`: un unico scope con miles de variables haria que cada resolucion fuera cara y el conjunto creciera de forma superlineal. La mejora estructural clara es reemplazar esas busquedas lineales por **tablas hash**, que llevarian la consulta a O(1) esperado y restaurarian O(n_a) incondicional. La inferencia heuristica de `hulk_semantic_infer.c` añade recorridos extra de cuerpos para deducir tipos no anotados, proporcionales al tamaño de esos cuerpos.

### 12.4 Codegen LLVM

La emision propia recorre el AST una vez generando un numero acotado de instrucciones por nodo: **O(n_a)** para el cuerpo del programa. A esto se suma la construccion de tablas globales: cada vtable se llena resolviendo `K` slots y cada resolucion sube por la cadena de herencia, dando **O(T · K · h)** con `h` la altura de la jerarquia (en `cg_type_resolve_method`). El total de codegen propio es `O(n_a + T·K·h)`. No se contabilizan aqui las fases externas, cuyo costo no controla este proyecto: las optimizaciones internas de LLVM, la seleccion de instrucciones del target, el enlace con `cc` ni la ejecucion del binario resultante.

### 12.5 Conclusion de complejidad

Sumando las fases y usando `n_a ≤ n_t ≤ n_c = n`:

```text
T_total(n) = O(n_c)                      [lexer, escaneo]
           + O(n_t)        ~ O(n)        [parser, caso normal]
           + O(n_a·(s·d+m+T)) ~ O(n)     [semantica, scopes/jerarquias acotados]
           + O(n_a + T·K·h)  ~ O(n)      [codegen propio]
           = O(n)   bajo supuestos de acotamiento
```

Es decir: **el pipeline es lineal en el tamaño del programa siempre que los scopes, el numero de tipos y la altura de las jerarquias esten acotados por constantes**, supuesto razonable para programas reales. Sin esos supuestos, los puntos no lineales son concretos y localizados: las busquedas lineales en la semantica (solucionables con hashing) y el lookahead de lambdas en el peor caso. No se debe presentar como `O(n)` absoluto e incondicional, pero si como lineal en la practica con los cuellos de botella identificados.

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

Algunas suites imprimen advertencias de conflictos LL(1) emitidas por `build_ll1_table` al cargar `grammar.ll1`. Esas advertencias no deben leerse como fallos de ejecucion: son `WARN` sobre la gramatica y las propias suites terminan en PASS. Tambien aparecen mensajes de error durante pruebas negativas del parser y la semantica; esos mensajes son esperados cuando el test verifica que una entrada invalida se rechaza correctamente.

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

Las siguientes limitaciones se desprenden del codigo:

- La ejecucion directa de `tests_piad/hulk/run_tests.sh` falla en este checkout por finales de linea CRLF; la suite PIAD pasa cuando se normalizan los `\r` solo durante la invocacion.
- `grammar.ll1` presenta conflictos LL(1) que `build_ll1_table` reporta como `WARN`; por tanto, la documentacion debe distinguir esa gramatica del builder integrado.
- El parser HULK necesita lookahead local para distinguir lambdas parentizadas de expresiones parentizadas.
- El sistema de scopes usa arreglos dinamicos con busqueda lineal; esto limita la afirmacion formal de complejidad `O(n)`.
- La conformidad estructural de protocolos se verifica por presencia de nombres de metodos, sin evidencia de chequeo completo de varianza o equivalencia profunda de firmas en esa rama.
- Los arreglos/vectores observados estan fuertemente orientados a casos numericos; no hay evidencia de chequeo completo de homogeneidad generica ni de bounds checking runtime.
- `as` aparece en codegen como no-op con opaque pointers; no se observa un chequeo runtime general de downcast.
- La inferencia de tipos es heuristica/ad-hoc, no un algoritmo general de inferencia polimorfica.
- Algunas features presentes en `tests_piad`, como macros o generadores, deben mencionarse solo si se auditan con mayor profundidad. Este informe se centra en las features requeridas y evidenciadas por los modulos principales.

Trabajo futuro razonable:

- normalizar los finales de linea de `tests_piad/hulk/run_tests.sh` para que la ejecucion directa funcione sin `tr -d '\r'`;
- reducir conflictos en `grammar.ll1` o documentarla explicitamente como gramatica experimental;
- reemplazar busquedas lineales de scopes/tipos por tablas hash si se quiere sostener una complejidad lineal mas fuerte;
- ampliar chequeos de arrays y protocolos;
- agregar chequeos runtime para `as` e indexacion;
- consolidar documentacion de features que aparecen en PIAD pero no fueron parte central de esta auditoria.

---

## 16. Conclusiones

El proyecto implementa un compilador HULK de varias fases y contiene evidencia concreta de un frontend, un analizador semantico y un backend LLVM. El lexer se genera a partir de expresiones regulares; el parser integrado construye AST mediante una gramatica declarada como datos y acciones semanticas; el AST separa estructura de significado; el analisis semantico resuelve nombres, tipos, scopes, decoradores, lambdas, arreglos y OOP; el backend emite LLVM IR y contempla objetos, closures, vtables y ejecutables nativos.

Desde el punto de vista de Lenguajes de Programacion, el proyecto aborda funciones como valores, closures, tipos nominales, protocolos estructurales limitados, decoradores y polimorfismo por subtipado. Desde Compilacion, muestra analisis lexico por DFA, parsing predictivo con matices, construccion de AST, chequeo semantico, transformaciones, generacion de IR y enlace nativo.

La conclusion debe ser fuerte pero honesta: el compilador tiene una arquitectura rica y tecnicamente relevante, y la corrida actual de tests internos (330/330) y de la suite PIAD (`RESULT: ALL_PASS`) respalda sus features. Al mismo tiempo, las limitaciones documentadas -conformidad estructural acotada de protocolos, ausencia de bounds checking, inferencia heuristica- delimitan con precision el alcance verificado. Esa honestidad fortalece el informe porque alinea la explicacion tecnica con el estado real del repositorio.

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
