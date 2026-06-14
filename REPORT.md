# HULK Compiler — Reporte de diseño e implementación

**Autor:** Abel Ponce González · **Universidad de La Habana, 2026**
**Lenguaje fuente:** HULK (Havana University Language for Kompilers)
**Implementación:** C99 + `flex` + LLVM 18
**Punto de entrada del contrato:** `./hulk archivo.hulk` → produce `./output`

---

## 1. Panorama general

El proyecto implementa un compilador HULK completo, de extremo a extremo, que recorre todas las capas del protocolo clásico de un compilador:

```
código fuente ──lexer──► tokens ──parser LL(1)──► árbol de derivación
              ──builder──► AST ──semántico──► AST anotado
              ──codegen──► LLVM IR ──llc/cc──► .o + link ──► ./output
```

A diferencia de muchos proyectos donde el compilador se queda en una sola fase, este compilador genera un **ejecutable nativo x86_64** real, partiendo de un fuente HULK y produciendo un binario que puede correrse directamente. El backend está construido sobre la **infraestructura LLVM** (libLLVM-18), de manera que la generación de código aprovecha el optimizador y el emisor de código objeto de LLVM, y el ejecutable final se obtiene enlazando con `cc -lm`.

El frontend, en cambio, está construido a mano: el lexer se genera dinámicamente a partir de expresiones regulares declaradas en C, mediante el algoritmo de construcción directa de AFD (Algoritmo 3.36 del Dragon Book), y el parser HULK es un parser recursivo descendente con un solo token de lookahead más una técnica de *backtracking limitado* para las construcciones de lambda. Existe además un parser LL(1) genérico (con tabla calculada a partir de FIRST/FOLLOW) que sirve para experimentar con gramáticas alternativas y para los tests de la asignatura.

---

## 2. Arquitectura por capas

### 2.1 Subsistema `generador_analizadores_lexicos/`

Implementa un **generador de lexers** parametrizado por reglas regex → token. Las piezas:

- `regex_lexer.l` (flex): un meta-lexer que tokeniza expresiones regulares (`*`, `+`, `?`, `|`, `(`, `)`, `[`, `]`, `.`, `^`, `-`, escape `\.`).
- `regex_parser.c`: parser LL(1) construido sobre la tabla calculada por `first_follow.c` y `ll1_table.c`, que parsea cada regex hacia un AST regex (`ASTNode`).
- `ast.c`: estructuras del AST regex con anotaciones de **nullable**, **firstpos**, **lastpos** y **followpos**, necesarias para la construcción directa del AFD.
- `afd.c`: construye un AFD (no un AFN) directamente a partir del AST regex con marcadores `#` por token (Algoritmo 3.36). Estrategia de prioridad por defecto: menor `token_id` gana — esto garantiza que las palabras clave (definidas antes en el enum `TokenType`) tengan prioridad sobre `IDENT`.
- `lexer.c`: lector maximal-munch sobre el AFD, devuelve `Token { type, lexeme, line, col }`.

Los tokens del lenguaje HULK se declaran de forma decoupled en `hulk_tokens.c` como una tabla `{ TokenType, regex }`, de manera que **el lexer del lenguaje HULK no se escribe explícitamente: se genera** corriendo el pipeline regex→AST→AFD sobre esa tabla en `hulk_compiler_init`.

### 2.2 Subsistema `generador_parser_ll1/`

Implementa un **generador de parsers LL(1)**. Las piezas:

- `grammar.c`: estructuras (`Symbol`, `Production`, `Grammar`) y construcción incremental con `grammar_add_terminal`, `grammar_add_nonterminal`, `grammar_add_production`.
- `first_follow.c`: cómputo iterativo de los conjuntos FIRST y FOLLOW.
- `ll1_table.c`: tabla de parsing `M[NT, T] → producción`; detecta conflictos LL(1).
- `parser.c`: motor de parsing predictivo (Algoritmo 4.34) con stack semántica y acciones embebidas en las producciones.

Hay dos gramáticas registradas: `grammar_regex.c` (la que usa el meta-lexer) y `grammar_hulk.c` (la del lenguaje HULK, que se usa principalmente en el target `test-parser` para verificar que la gramática del libro de la asignatura sea LL(1)).

### 2.3 Subsistema `hulk_ast/`

Es el corazón del compilador. Se descompone en cinco módulos:

#### `hulk_ast/core/`
- `hulk_ast.h`: define `HulkNodeType` (un enum con todas las categorías de nodo del AST) y los structs concretos: `ProgramNode`, `FunctionDefNode`, `TypeDefNode`, `MethodDefNode`, `AttributeDefNode`, `LetExprNode`, `VarBindingNode`, `IfExprNode`, `WhileStmtNode`, `ForStmtNode`, `BlockStmtNode`, `BinaryOpNode`, `UnaryOpNode`, `NumberLitNode`, `StringLitNode`, `BoolLitNode`, `IdentNode`, `CallExprNode`, `MemberAccessNode`, `NewExprNode`, `AssignNode`, `DestructAssignNode`, `AsExprNode`, `IsExprNode`, `SelfNode`, `BaseCallNode`, `DecorBlockNode`, `DecorItemNode`, `ConcatExprNode`, `VectorLitNode`, `IndexExprNode`, `FunctionExprNode`. `TypeDefNode` incorpora un flag `is_protocol` para reusar la representación cuando proviene de la declaración `protocol`.
- `hulk_ast_context.c`: un **object pool / arena**. Toda asignación de nodo va a `hulk_ast_alloc(ctx, size)`. El AST entero se libera con `hulk_ast_context_free(ctx)`; esto simplifica enormemente la gestión de memoria, evita fugas en código de error-recovery, y mantiene los nodos próximos en cache.
- `hulk_ast_visitor.c`: implementa el patrón Visitor con un arreglo de `HulkVisitFn` indexado por `HulkNodeType`. Permite agregar operaciones (printer, analyses, transformations) sin modificar los nodos.

#### `hulk_ast/builder/`
Construye el AST a partir del flujo de tokens. Es un **parser recursivo descendente** dividido por responsabilidad:
- `parse_helpers.c`: `advance`, `check`, `match`, `expect`, `expect_ident`, `synchronize`, `error_at`, parsing de listas de argumentos y anotaciones de tipo.
- `parse_expressions.c`: cadena de precedencia (or → and → cmp → concat → add → term → factor → unary → as → primary), let, if.
- `parse_statements.c`: while, for, block.
- `parse_definitions.c`: function, type, protocol, decoradores.
- `parse_primary.c`: literales, identificadores, llamadas, member access, new, self, base, vectores `[...]`, indexación `v[i]`, lambdas `(args) => body`.

La detección de lambda merece mención: la gramática es ambigua porque tras un `(` puede venir tanto una expresión parentizada como los parámetros de una lambda. Como el parser solo tiene un token de lookahead, se implementa un **peek extendido**: `peek_is_lambda_start` guarda el estado del `LexerContext` y del token actual, escanea sin avanzar de forma destructiva, y reinstala el estado al terminar. Reconoce los patrones `() =>`, `(IDENT, …) =>` y `(IDENT: TYPE, …): TYPE =>`. Esto es estrictamente LL(1) extendido y mantiene la gramática simple.

#### `hulk_ast/semantic/`
- `hulk_semantic_internal.h`: define el sistema de tipos `HulkType { kind, name, parent, members, param_types, param_count, return_type, is_protocol }`, los símbolos `Symbol { kind, name, type, callable_type, decl_node, param_types, param_names, param_count }`, los scopes `Scope { parent, symbols, count }` y el `SemanticContext`.
- `hulk_semantic_types.c`: tabla de tipos, `sem_type_conforms` (relación `T1 <= T2`), `sem_type_join` (LCA), inicialización de tipos built-in (`Object`, `Number`, `String`, `Boolean`) y funciones built-in (`print`, `sqrt`, `sin`, `cos`, `exp`, `log`, `rand`, `parse`, `range`).
- `hulk_semantic_scope.c`: scopes anidados con cleanup automático.
- `hulk_semantic_collect.c`: pases 1-2 de recolección de símbolos — registrar nombres de tipo, resolver herencia (con detección de ciclos tortoise-and-hare), y registrar funciones y miembros. Permite referencias mutuas y herencia hacia adelante.
- `hulk_semantic_check.c`: pase 3 (verificación de cuerpos) + orquestación (`sem_check_program`, `hulk_semantic_analyze`).
- `hulk_semantic_check_expr.c`: bottom-up type-checking de literales, operadores, llamadas y member access (el dispatcher `sem_check_expr`).
- `hulk_semantic_check_stmt.c`: type-checking de las construcciones de control con valor (let/if/while/for/block) y OOP (new/assign/is/as/self/base).
- `hulk_semantic_infer.c`: inferencia ad-hoc de tipos no anotados (`sem_infer_param_type`, `sem_infer_self_member_type`, `sem_body_calls_name`).
- `hulk_semantic_desugar.c`: pase de transformación que reescribe decoradores como composición de llamadas (`decor d, e function f` → `f := d(e(f))`).

La conformidad de tipo es nominal por defecto (cadena de herencia), pero también **estructural cuando el target es un protocolo**: el child conforma a un protocolo si tiene métodos del mismo nombre que los signatures del protocolo.

La inferencia ad-hoc de tipos de parámetro de función/método sin anotación se hace con un walker sintáctico (`sem_infer_param_type`) que clasifica los usos: aritmético → `Number`, lógico → `Boolean`, concatenación → `String`. Esto sigue la sugerencia de la spec A.9.4 de HULK ("el tipo del argumento `n` debe inferirse como `Number` porque es el único tipo donde los operadores aritméticos están definidos"). Para atributos y parámetros del constructor de tipo sin anotación, hay un walker análogo (`sem_infer_self_member_type`) que detecta usos de `self.X` en operadores aritméticos o concatenación dentro de los método-bodies del propio tipo.

Para recursión, el `collect_function` defaultea el tipo de retorno a `Number` solo si el body contiene una autollamada (detectado por `body_calls_name`); de lo contrario el tipo de retorno se infiere del cuerpo en `check_function_def`.

**Árbol semántico anotado.** El dispatcher `sem_check_expr` escribe el nombre canónico del tipo inferido en cada nodo (`HulkNode.static_type`), materializando el "árbol semántico anotado" del flujo clásico de compilación. La anotación es centralizada (un único punto en el dispatcher anota todos los nodos de expresión) y desacoplada: el AST core guarda solo un `const char*` con el nombre del tipo, sin depender de los structs internos del semántico ni del codegen. El backend **consume** esa anotación (`cg_static_type_of` y `cg_infer_body_return_type` la leen primero, con las heurísticas sintácticas como fallback únicamente cuando no hubo análisis semántico previo o el tipo es `Object`/`<function>`). Esto elimina la duplicación de lógica de inferencia entre frontend y backend que existía antes, donde el codegen re-derivaba los tipos desde cero.

Ejemplo: para `let x = 3 + 4 in print(x @ " es la suma")`, el árbol queda anotado con `BinaryOp:Number`, `ConcatExpr:String` e `Ident:Number` (la `x`, sin anotación del usuario, inferida y anotada), entre otros.

#### `hulk_ast/codegen/`
Genera **LLVM IR** y, opcionalmente, ejecuta el pipeline LLVM completo hasta producir un binario ELF nativo. Está dividido en:
- `hulk_codegen_internal.h`: `CodegenContext` (llvm_ctx, module, builder, tipos básicos, scope chain, registro de tipos de usuario, slots globales de método para la vtable, tablas de RTTI), `CGScope` (chain de scopes), `CGSymbol { name, value, type, is_func, hulk_type }`, `CGTypeInfo { name, struct_type, ptr_type, field_count, field_offset_self, field_names, field_types_arr, type_tag, methods, parent, vtable_global, vtable_type, fn_new, fn_init, fn_init_type }`.
- `hulk_codegen_types.c`: gestión de scope chain con herencia, registro de tipos de usuario y tabla global de slots de método (`cg_method_slot`).
- `hulk_codegen_expr.c`: despachador `cg_emit_expr` + emisión de escalares (literales, ident, operadores, concat, short-circuit).
- `hulk_codegen_call.c`: invocación de funciones/closures/builtins, intercept de `print` polimórfico y coerción a string (`cg_emit_to_string`).
- `hulk_codegen_oop.c`: acceso a miembros, `new`, `self`, asignación destructiva sobre campos, y el tipo HULK estático de una expresión (`cg_static_type_of` + LCA).
- `hulk_codegen_control.c`: let/if/while/for/block como expresiones con valor (basic blocks + PHI/alloca).
- `hulk_codegen_typedecl.c`: layout del struct con tag, constructor encadenado, emisión de métodos y `T_new`/`T_init`, y construcción de vtables + tablas RTTI.
- `hulk_codegen_infer.c`: heurísticas que deciden el `LLVMTypeRef` de retornos/params/campos sin anotación, incluida la tabla de hints O(n) para inferir String en params de constructor.
- `hulk_codegen_stmt.c`: orquestación del programa (`cg_emit_program`), forward-declares y emisión de funciones/decoradores.
- `hulk_codegen_runtime.c`: declaración del runtime C externo (libc + libm) y definición en IR de los helpers HULK (print, concat, conversiones a string, log de base arbitraria).
- `hulk_codegen.c`: API pública `hulk_codegen(ast, "out.ll")` y `hulk_codegen_to_executable(ast, "./output")`.

### 2.4 Punto de entrada del contrato: `hulk_cli.c`

Es el binario `./hulk` que entiende el contrato. Sus responsabilidades:
1. Leer `argv[1]` como ruta del archivo `.hulk`.
2. Instalar un *error handler* (`hulk_diag_handler`) que captura todos los `compiler_log(LOG_ERROR, …)` que emiten el lexer, el parser, el semántico y el codegen.
3. Mapear el módulo origen del log a uno de los tipos del contrato:
   - `lexer` → `LEXICAL`
   - `regex`, `ast_builder`, `parser`, `ll1` → `SYNTACTIC`
   - `semantic`, `codegen` → `SEMANTIC`
4. Parsear el prefijo `[L:C]` que muchos call-sites embeben en su `fmt` y reescribirlo a `(L,C)`.
5. Mantener contadores `n_lex`, `n_syn`, `n_sem` para decidir el `exit code` según la prioridad **LEXICAL > SYNTACTIC > SEMANTIC**.
6. Redirigir `stdout` y `stderr` internos del compilador a `/dev/null` (capturando antes el fd real de `stderr` para reescritura limpia) — esto evita que el banner del compilador o los `fprintf` directos contaminan la salida del contrato.
7. Si todo va bien, llamar a `hulk_codegen_to_executable(ast, "./output")` que internamente emite `.o` con `LLVMTargetMachineEmitToFile`, lo enlaza con `cc -lm` vía `fork`/`execlp`, y deja el binario en `./output`.

---

## 3. Backend OOP — la parte más interesante

El backend con LLVM 18 fue el reto principal. La spec A.7.4 promete *"all type methods in HULK are virtual by definition"*, lo que requiere despacho dinámico. Pero LLVM 18 usa **opaque pointers** (todos los `T*` son simplemente `ptr`), de manera que la firma del puntero no codifica el tipo dinámico. La primera implementación del proyecto usaba `LLVMTypeOf(obj) == ti->ptr_type` para decidir qué método llamar, lo cual **colapsaba todos los punteros a la misma comparación** y el despacho era efectivamente estático (o peor: ambiguo). Eso significaba que el polimorfismo dinámico estaba roto.

La solución implementada es la clásica de los lenguajes orientados a objetos: **type tag + vtable**.

### 3.1 Layout del struct con herencia

Cada `type T(args) inherits P(...)` se traduce a un struct LLVM:

```
%T = type { i32 __tag__, ...parent_fields..., ...self_fields... }
```

Esto se llena en `forward_declare_type` (en `hulk_codegen_stmt.c`):

```c
int parent_field_count = ti->parent ? ti->parent->field_count : 1;  // 1 = solo el tag
int self_field_count = n->params.count + self_attr_count;
int total_fields = parent_field_count + self_field_count;
ti->field_offset_self = parent_field_count;
```

El struct hijo **empieza con los mismos fields que el padre en el mismo orden**. Esto garantiza que un `Child*` "es un" `Parent*` válido en memoria — un bitcast preserva semánticamente el contenido.

### 3.2 Slots globales de método y vtable por tipo

Cada **nombre de método único en todo el programa** recibe un slot fijo (índice estable) en una tabla virtual. Lo hace `cg_method_slot(c, name)` durante el forward-declare. La vtable de cada tipo es entonces un arreglo:

```
@T_vtable = internal constant [N x ptr] [ptr @T_m0, ptr @T_m1, ..., ptr null]
```

donde la entrada en el slot `s` apunta a `cg_type_resolve_method(T, slot_name)`, que camina la cadena de herencia y devuelve la implementación más derivada. Slots no implementados quedan en `null`.

### 3.3 Tabla global de vtables y de padres

Para poder hacer despacho indexado por tag a runtime, se emiten dos globales adicionales:

```
@hulk_vtables = internal constant [NumTipos x ptr]      // tag → vtable
@hulk_parents = internal constant [NumTipos x i32]      // tag → parent tag (o -1)
```

### 3.4 Constructor encadenado

Cada `type T` produce dos funciones:
- `void @T_init(T* self, …params)` — setea el tag, encadena `Parent_init(self, parent_args)`, copia los params a sus fields, ejecuta los inicializadores de los atributos.
- `T* @T_new(…params)` — `malloc(sizeof(T))` + `T_init(self, …)` + `return self`.

El encadenado `Parent_init` resuelve el bug clásico donde los fields heredados quedaban sin inicializar.

### 3.5 Despacho dinámico real

Una llamada `obj.m(args)` se compila a:

```
%tag      = load i32, ptr %obj
%vt_entry = getelementptr inbounds [N x ptr], ptr @hulk_vtables, i32 0, i32 %tag
%vt       = load ptr, ptr %vt_entry
%fn_slot  = getelementptr inbounds [M x ptr], ptr %vt, i32 0, i32 SLOT
%fn       = load ptr, ptr %fn_slot
call fn_type %fn(ptr %obj, args...)
```

Donde `SLOT` es la constante asignada en compile-time a `m`. La firma se toma del tipo estático del receiver (resuelto con `cg_static_type_of`, que entiende `self`, `Ident → sym->hulk_type`, `new T`, `as T`, `if (...) … else …` con LCA, etc.). Esto da despacho dinámico mientras la firma siga siendo conocida estáticamente.

### 3.6 `is` y `as`

- `is T`: emite un loop in-IR que parte del tag del objeto y camina `@hulk_parents` hasta encontrar el tag objetivo (retorna `i1 true`) o `-1` (retorna `i1 false`). Hay un `phi` final que une las dos salidas.
- `as T`: con opaque pointers es un noop en IR; el código posterior usa `cg_static_type_of` para tratar la expresión como del tipo destino. (Un check runtime sería trivial de agregar pero no aporta a los tests actuales.)

### 3.7 `base()`

`base()` dentro de un método `m()` no llama al constructor del padre como en la versión anterior, sino al método del padre con el mismo nombre. Lo resuelve `cg_type_resolve_method(enclosing_type->parent, current_method_name)`. Para esto, `CodegenContext` ahora lleva `current_method_name` que se setea/restaura cuando se emite el cuerpo de cada método.

---

## 4. Features del lenguaje implementadas

- **Aritmética**: `+ - * / % **`, con la precedencia y asociatividad usuales. (`^` está documentado como pendiente: el meta-lexer tiene un bug latente con `\^` en char class.)
- **Strings**: `"..."`, concat con `@` y con espacio `@@`.
- **Boolean**: `true`, `false`, comparadores `< > <= >= == !=`, lógicos `&&` `||` `!`.
- **Builtins**: `print`, `sqrt`, `sin`, `cos`, `exp`, `log(base, value)`, `rand`, `range(min, max)`, constantes `PI`, `E`.
- **Let**: simple, múltiple, con anidamiento y shadowing; bindings multi-var con resolución left-to-right.
- **Destructive assignment**: `:=`.
- **Conditionals**: `if … elif … else` como expresión con join de ramas para el tipo.
- **Loops**: `while`, `for (x in range(...))`.
- **Funciones**: inline (`function f(x) => body`), full (`function f(x) { body }`), recursión, default `Number` para param sin anotación cuando se usa con aritmética.
- **Tipos**: declaración con params, atributos privados, métodos virtuales, herencia con `inherits Parent(args)`.
- **Polimorfismo dinámico** vía vtable + type tag.
- **`is` / `as`** con conformidad nominal (cadena de tags) o estructural (protocolo).
- **`base()`** que llama al método del padre.
- **Protocols**: `protocol P { m(): T; }`, opcionalmente `extends P2`, conformidad estructural en el semántico.
- **Vectores**: literales `[a, b, c]`, indexación `v[i]`.
- **Lambdas como expresión**: `(x) => x * 2`, almacenables en variables (`let f = ...`).
- **Decoradores**: `decor d1, d2 function f(...) => ...` con desugaring en el semántico.

---

## 5. Pipeline de errores y contrato

El contrato pide:
- exit code `1` para errores **léxicos**,
- `2` para **sintácticos**,
- `3` para **semánticos**,
- formato `(line,col) TYPE: message` por error, una línea por error.

`hulk_cli.c` lo cumple instalando un *error handler* en el observador global del compilador (`error_handler_set`). El handler:
1. Filtra `INFO` y `WARNING`, deja pasar `ERROR` y `FATAL`.
2. Formatea el mensaje con `vsnprintf` y, si empieza con `[L:C] …`, extrae la posición con `sscanf`.
3. Despacha por nombre de módulo a `LEXICAL`, `SYNTACTIC` o `SEMANTIC` e incrementa el contador correspondiente.
4. Escribe al `stderr` real (preservado antes de redirigir el `stderr` interno del compilador a `/dev/null`).

El exit code se decide por la **prioridad LEXICAL > SYNTACTIC > SEMANTIC** y se evalúa al final de cada fase:
- Si `n_lex > 0` después de `hulk_build_ast`, sale `1`.
- Si `n_syn > 0` (y no léxico), sale `2`.
- Si `n_sem > 0` (y no anterior), sale `3`.
- Si llega a codegen y este falla con `rc != 0`, sale `3` por defecto.

El binario final se enlaza con `cc -o ./output ./output.o -lm` usando `fork`/`execlp` (sin `system()` para evitar inyección por nombres de archivo). El target machine se configura con `LLVMRelocPIC` para que el `.o` sea PIE-friendly, lo cual es necesario en Ubuntu 24.04 y otras distribuciones modernas donde `cc` produce ejecutables PIE por default.

---

## 6. Cómo verificar

```bash
make build           # produce ./hulk
./hulk programa.hulk # produce ./output (si compila) y exit 0
./output             # ejecuta el programa
echo $?              # = 0 si OK
```

Casos de error verificados manualmente:

```bash
$ echo 'let x = $invalid in print(x);' > /tmp/bad.hulk
$ ./hulk /tmp/bad.hulk
(1,9) LEXICAL: cerca de '$'
$ echo $?
1

$ echo 'let x in print(x);' > /tmp/bad.hulk
$ ./hulk /tmp/bad.hulk
(1,7) SYNTACTIC: se esperaba 'ASSIGN', se encontró 'IN' (encontrado 'in')
$ echo $?
2

$ echo 'print(foo);' > /tmp/bad.hulk
$ ./hulk /tmp/bad.hulk
(1,7) SEMANTIC: nombre 'foo' no definido
$ echo $?
3
```

---

## 7. Suite de tests del proyecto

El compilador trae dos suites de tests:

### 7.1 Tests internos (`make test-all`)

Ocho binarios de test escritos en C contra el framework propio `tests/test_framework.h`. Cubren:
- `test_lexer.c`: 30 casos. Construcción del DFA, reconocimiento de cada token, error recovery.
- `test_parser.c`: 26 casos. Parser LL(1) genérico sobre gramáticas de juguete.
- `test_ast.c`: 18 casos. AST del meta-lexer regex.
- `test_hulk_ast.c`: 52 casos. Constructores de nodos HULK AST, visitor.
- `test_ast_builder.c`: 78 casos. Parser HULK completo. (Hay un caso conocido `function_expr_simple` heredado del baseline anterior; no se considera regresión.)
- `test_semantic.c`: 65 casos. Conformidad de tipos, scopes, herencia, desugaring de decoradores.
- `test_codegen.c`: 41 casos. Generación de IR para cada construcción.
- `test_feature_decorators_closures.c`: 7 casos. Feature integrado.

Total: **317 tests**, 316 pasando. Estado verificado: 77/78 en test_ast_builder, 100% en todos los demás.

### 7.2 Suite exploratoria de cumplimiento de la spec (`probar/spec_check/spec_runner`)

Un runner end-to-end que toma cada `.hulk` en `tests/hulk_programs/`, lo pasa por todo el pipeline (lexer + parser + semantic + codegen), ejecuta el `.ll` con `lli-18` y compara la salida con el `.expected` adjunto. 26 casos cubriendo: aritmética, strings, let multi/shadow, destructive assign, if/elif, while, for+range, builtins de math, funciones inline y recursivas, tipos básicos, herencia con atributos, polimorfismo estático y dinámico, polimorfismo via función con param de tipo padre, polimorfismo con join de branches, `base()` con strings, `is`/`as`, protocolos, vectores, lambdas, chain de herencia de 3 niveles. **Estado final: 26/26 PASS**.

---

## 8. Decisiones de diseño relevantes

- **Arena de AST**: simplifica enormemente la gestión de memoria y elimina toda una clase de bugs por uso-después-de-libre. Trade-off: el AST entero vive hasta el final de la compilación.
- **Lexer generado dinámicamente**: el costo de construir el DFA en cada arranque es ~5 ms, pero permite agregar tokens nuevos editando solo `hulk_tokens.c`.
- **Parser recursivo descendente con peek extendido para lambdas**: mantiene la simplicidad del LL(1) puro sin tener que reescribir la gramática para evitar la ambigüedad de `( …`.
- **Vtable + type tag**: la única forma sana de implementar despacho dinámico en LLVM 18 con opaque pointers. Costo: una indirección extra por cada llamada de método, ganancia: polimorfismo real.
- **Default `Number` para params no anotados con uso aritmético**: la spec lo sugiere explícitamente y resuelve la mayoría de funciones HULK típicas sin obligar al usuario a anotar tipos. El walker es puramente sintáctico y por tanto rápido y predecible.
- **Backend LLVM en vez de un IR propio**: aprovecha décadas de optimización industrial sin escribir un solo paso de optimización a mano. La salida es ELF x86_64 real.

---

## 9. Complejidad temporal del proceso de compilación

El pipeline procesa la entrada de tamaño *n* (caracteres del fuente) en **tiempo lineal O(n)** en el caso práctico. Fase por fase:

| Fase | Complejidad | Justificación |
|------|-------------|---------------|
| Lexer (DFA maximal-munch) | **O(n)** | Cada carácter dispara una transición O(1) en `next_state[state][c]`. El retroceso a `last_accept_pos` está acotado por la longitud del token más largo; los lexemas de HULK no comparten prefijos largos arbitrarios, así que no hay re-escaneo significativo. |
| Parser LL(1) | **O(n)** | Predictivo con pila + tabla: cada token se consume una vez y cada expansión de producción es un lookup O(1). La tabla FIRST/FOLLOW/LL(1) se construye una sola vez sobre la gramática (constante respecto al input). |
| Builder de AST | **O(n)** | Una pasada sobre el árbol de derivación. |
| Semántico | **O(n)** amortizado | El recorrido base es lineal. Las heurísticas de inferencia de tipos no anotados (`sem_infer_param_type`, `sem_infer_self_member_type`) recorren el cuerpo de *cada declaración* una vez por símbolo sin anotar de esa declaración: O(símbolos × cuerpo) **local a cada declaración**, no al programa. Es lineal salvo en el caso patológico de una única declaración con un número de parámetros proporcional a su propio cuerpo. |
| Codegen | **O(n)** | La inferencia de tipos de constructor, que originalmente reescaneaba el programa completo por cada parámetro (O(params × n) — el peor offender), se reemplazó por una **tabla de hints precomputada en una sola pasada** (`collect_string_hints`), reduciendo cada consulta a O(1). El resto de la emisión es una pasada sobre el AST. |

**Medición empírica** (programa generado de *N* funciones + *N* statements):

| N | líneas | tiempo |
|---|--------|--------|
| 1000 | 2002 | 0.35 s |
| 2000 | 4002 | 0.73 s |
| 4000 | 8002 | 1.54 s |

Doblar la entrada **dobla** el tiempo (no lo cuadruplica), confirmando empíricamente el escalado lineal. Antes de la optimización de la tabla de hints, el caso con muchos constructores sin anotar exhibía comportamiento super-lineal; tras ella, el pipeline es O(n) end-to-end en todos los programas medidos.

---

## 10. Limitaciones conocidas

- La indización de vectores asume vectores de `Number` (representación: `{ i32 size, double items[N] }` empacada en un bloque de bytes). Otros tipos de vector quedarían para una iteración posterior.
- El protocolo `Iterable` que la spec menciona como mecanismo del `for` está implementado a nivel sintáctico (el `for` se reconoce a `range`), pero no hay infraestructura para iterar sobre tipos arbitrarios que conformen `Iterable`. En su lugar, el `for` detecta sintácticamente `range(a, b)` y emite el loop apropiado.
- Macros (`def`, `*expr`, `@symbol`, `$symbol`, `match`/`case`) descritas en A.14 no están implementadas.
- El semántico hace inferencia ad-hoc para los casos de la spec A.9.4, pero no implementa el "general strategy" de A.9.5 que sintetiza protocolos automáticamente como anotaciones.

---

## 11. Cierre

El compilador cubre con 26/26 los casos end-to-end que ejercitan polimorfismo dinámico, herencia, protocolos, lambdas, vectores y todos los constructos básicos del lenguaje. Implementa correctamente las tres fases del análisis (léxico, sintáctico, semántico) con reporte de errores conforme al contrato de la facultad, y genera un ejecutable nativo ELF x86_64 mediante LLVM. El código está organizado en subsistemas con responsabilidad única y comunicación por contratos, lo que ha permitido evolucionar el backend (especialmente la refactorización que introdujo vtables y type tags) sin romper el frontend.
