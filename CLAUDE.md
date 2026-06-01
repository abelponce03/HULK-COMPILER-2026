# HULK Compiler 2026
**Equipo:** Abel Ponce + 1 colaborador | **Lenguaje fuente:** HULK (Havana University Language for Kompilers)
**Implementación:** C99 + `flex` + LLVM 18 (codegen) | **Repo:** rama activa `dev_josue`, integra a `main`.

## Alcance vigente (estado real, no el aspiracional del PDF)

- **Pipeline real:** lexer maximal-munch (DFA propio) → parser LL(1) →
  builder de AST → análisis semántico → emisión LLVM IR. El `README.md`
  solo describe lexer + parser y debe tratarse como **desactualizado**.
- **`hulk-docs.pdf` describe la *especificación aspiracional* del
  lenguaje HULK** (Appendix A). NO es contrato de implementación: muchas
  features descritas allí NO están en la gramática, ni en el AST, ni en
  codegen. Ver `probar/spec_check/REPORTE_2026-05-28.txt` para el grado
  real de cumplimiento (6/26 PASS al 2026-05-28).
- **Estado del backend (verificado 2026-05-28):**
  - `hulk_codegen(ast, "out.ll")` funciona — produce IR válido para
    programas simples (aritmética, let, destruct, if con números).
  - `hulk_codegen_to_executable()` está **roto en este host**: el `.o`
    se emite sin `-fPIE` y `cc` rechaza el link en Ubuntu/Debian
    modernos. Para ejecutar usar `lli-18 out.ll` o regenerar el `.o`
    con `-fPIE`.
  - **Dispatch de métodos roto bajo LLVM 18 (opaque pointers).** El
    codegen identifica el `CGTypeInfo` por
    `LLVMTypeOf(obj) == ti->ptr_type`, pero LLVM 18 colapsa todos los
    pointer types a `ptr`. Esto produce: matching ambiguo cuando hay
    varios `type`, "método no encontrado" en muchos casos, y atributos
    leyendo offsets incorrectos. Verificado: `print(new Point(7).getX())`
    imprime `0` (esperado `7`).
  - **No hay vtable.** Todo el dispatch es estático (resuelto en
    compile time por el tipo LLVM del puntero). `is`/`as` también son
    estáticos. Por lo tanto, **el polimorfismo dinámico que describe la
    spec A.7.4 no está implementado**, incluso si el problema de
    opaque pointers se arreglara.
  - **`new` con herencia no llama al ctor padre.** Los `parent_args`
    del AST no se traducen a `LLVMBuildCall2(Parent_new, ...)`. Los
    atributos del padre quedan sin inicializar.
  - **El struct hijo no contiene los fields del padre.** `field_types`
    en `forward_declare_type` solo lleva params propios + attrs
    propios. Un `bitcast Child* → Parent*` y `GEP` accederá a memoria
    incorrecta.
  - **`print` runtime solo acepta `double`.** `print("hello")` rompe
    `LLVMVerifyModule`. La rama del top-level que imprime `i8*` con
    `printf("%s\n", ...)` solo se aplica al *último* statement
    top-level, no a los `print(...)` intermedios.
- **Lo que sí funciona end-to-end** (verificado con `lli-18`):
  aritmética con `**` (no `^`), strings concatenados con `@`/`@@`,
  `let`, `let` multi-binding con dependencias izq→der, shadowing,
  destructive assignment `:=`, `is` con tipo estáticamente conocido,
  expresiones bloque `{ ; ; }`.
- **Lo que falla y por qué — resumen** (mapeo a la spec del PDF):
  - `^` para potencia → **no en lexer** (se usa `**`).
  - `range(...)` → **no es builtin** en `sem_types_init`.
  - Inferencia de tipo de parámetros de función sin anotación → falla
    semántico cuando el cuerpo aplica operadores aritméticos al
    parámetro.
  - `print("string")` y `print(if c "a" else "b")` → codegen falla.
  - `let f = (x) => x*2 in ...` → parser falla: lambda como init de
    let no soportada.
  - Strings con `\"` escapado → lexer falla.
  - `protocol`, `[1,2,3]`, `vec[i]`, `T*`, `T[]`, lambdas como
    expresión libre, `def macro`, `match`/`case` → **ni siquiera en
    la gramática** (`generador_parser_ll1/grammar_hulk.c`).
- **Estado de los tests internos del Makefile (2026-05-28):**
  `make test-all` falla por un único caso conocido,
  `function_expr_simple` en `tests/test_ast_builder.c:952`. Lexer,
  parser, AST core, semantic, codegen (helpers internos) y
  decoradores/clausuras pasan. Cualquier cambio debe respetar este
  baseline.
- **Suite exploratoria de end-to-end:** `tests/hulk_programs/*.hulk`
  + `probar/spec_check/spec_runner.c`. NO está integrada al Makefile
  (es para análisis del grado de cumplimiento de la spec, no para
  regresión continua). Recompilarla con:
  ```
  make
  OBJS="$(make -np 2>/dev/null | grep -E '^LIB_OBJS\s*:?=' \
    | sed 's/^LIB_OBJS *= *//' \
    | sed 's/\$(HULK_AST_DIR)/hulk_ast/g; s/\$(LEXER_DIR)/generador_analizadores_lexicos/g; s/\$(PARSER_DIR)/generador_parser_ll1/g')"
  gcc -std=c99 -g -I. -I/usr/lib/llvm-18/include \
      probar/spec_check/spec_runner.c $OBJS \
      -lfl -L/usr/lib/llvm-18/lib -lLLVM-18 -lm \
      -o probar/spec_check/spec_runner
  ./probar/spec_check/spec_runner tests/hulk_programs
  ```
- **Subsistemas reutilizables:**
  - `generador_analizadores_lexicos/` — regex → AST → DFA (Algoritmo
    3.36 del Dragon Book) → tokenizador maximal-munch. Usa `flex` solo
    para el meta-lexer de expresiones regulares.
  - `generador_parser_ll1/` — gramática → FIRST/FOLLOW → tabla LL(1) →
    parser predictivo (Algoritmo 4.34). Gramáticas registradas:
    `grammar_regex.c` y `grammar_hulk.c`.
- **Subsistemas específicos de HULK:**
  - `hulk_ast/core/` — nodos, contexto (arena/Object Pool), visitor.
  - `hulk_ast/builder/` — construcción del AST desde el flujo de
    tokens, dividido en `parse_expressions`, `parse_statements`,
    `parse_definitions`, `parse_primary`, `parse_helpers`.
  - `hulk_ast/semantic/` — `scope`, `types`, `check`, `check_expr`,
    `desugar`.
  - `hulk_ast/codegen/` — emisión LLVM IR (`hulk_codegen`, `_expr`,
    `_stmt`, `_types`).
  - `hulk_ast/printer/` — pretty-print del AST para debug.
- **Orquestación:** `main.c` (CLI) → `hulk_compiler.c` (fachada
  Pipeline). El CLI **solo ejecuta hasta build_ast**; no invoca
  `hulk_semantic_analyze` ni `hulk_codegen`. Los tests internos sí
  ejercen el pipeline completo.
- **Patrones ya presentes (respetar, no recrear):**
  Pipeline (`hulk_compiler.c`), Visitor (`hulk_ast/core/`), Object Pool
  (`hulk_ast_context`), Abstract Factory (`grammar.h/c`), Strategy
  (`afd.h` `TokenPriorityFn`), Observer (`error_handler.*`).
- **Fuera de alcance hasta que el usuario lo pida:**
  selectores tipo portafolio, intérprete alternativo, otro tipo de
  parser (LR, LALR), backends distintos a LLVM, IR intermedia propia.

## Regla de Consulta de 4 Capas

Antes de responder o modificar nada del compilador, sigue este orden:

1. **Capa 1 — Código fuente.** Es la fuente autoritativa. Si una duda
   se resuelve leyendo el `.c`/`.h`, hazlo antes que cualquier otra
   cosa.
2. **Capa 2 — Grafo Graphify**, si existe. Si hay un directorio
   `graphify-out/` (ver sección Graphify abajo), consultar primero
   `graphify-out/GRAPH_REPORT.md` y luego `graphify query "..."` para
   navegar relaciones entre subsistemas, símbolos y dependencias.
3. **Capa 3 — `agent.md`.** Guía operativa con convenciones de
   nombres, módulos, flujo Git y comandos. Útil para alinear estilo y
   límites de subsistema.
4. **Capa 4 — `README.md`, `CONTRIBUTING.md`, `hulk-docs.pdf`,
   `probar/README.md`.** Contexto histórico / pedagógico / de prueba.
   `hulk-docs.pdf` describe el lenguaje HULK como roadmap, no como
   estado implementado.

**Si una capa contradice el "Alcance vigente" de este archivo,
prevalece el alcance vigente.** Si el README sugiere que solo hay
lexer+parser, ignóralo: hay AST + semántico + codegen (incluso si
codegen tiene los bugs descritos arriba).

## Graphify y Obsidian (Capas 2 y 5)

> Convención heredada de otros proyectos del autor: usar `graphify` como
> índice navegable del proyecto y exportar a Obsidian para razonamiento
> visual sobre relaciones. **Solo aplicar si esos directorios ya
> existen** — no inicializarlos sin permiso del usuario.

### Flujo recomendado tras un cambio sustantivo en `hulk_ast/`

```bash
# 1. Refresca AST + estructura del grafo del proyecto (sin coste LLM)
graphify update .

# 2. Si los cambios afectan contenido semántico (renombres, nuevas
#    relaciones entre subsistemas, archivos nuevos/eliminados),
#    regenera la capa LLM
graphify extract . --backend gemini   # coste API, pedir confirmación

# 3. Si hay bóveda Obsidian para este repo, reproyectarla
rm -f ~/Documentos/HULK-Obsidian/graphify/*.md
graphify export obsidian \
  --dir ~/Documentos/HULK-Obsidian/graphify/
```

Cuándo usar cada paso:
- **Solo `update`** si solo moviste archivos, renombraste, o agregaste
  tests/.hulk sin tocar estructura semántica.
- **`update` + `extract`** si reescribiste un subsistema, cambiaste la
  forma en que se llaman funciones públicas, o eliminaste/agregaste
  archivos al pipeline.
- **`extract` + `export obsidian`** si la wiki Obsidian quedó
  desactualizada (muestra archivos que ya no existen, o no muestra los
  nuevos).

### Consultas útiles

- `graphify query "<pregunta>"` — pregunta libre sobre el grafo.
- `graphify path "<A>" "<B>"` — camino de dependencias entre dos
  símbolos (p. ej. `path "emit_call" "hulk_codegen_to_executable"`).
- `graphify explain "<concepto>"` — resumen de un cluster del grafo
  (p. ej. `explain "dispatch de métodos"`).

### Disciplina de actualización

- Lee `graphify-out/GRAPH_REPORT.md` antes de búsquedas amplias en el
  código (más barato que grep masivo).
- Cualquier nodo del grafo que mencione features descartadas
  (`protocol`, vectores `[...]`, macros `def`, lambdas como init de
  let) es rastro de la spec aspiracional: tratarlo como tal hasta una
  re-extracción semántica que refleje el estado real del código.

## Disciplina de commits (regla del proyecto)

**Cada cambio coherente cierra con un commit.** Un "cambio coherente"
es: una corrección de bug, una feature aislada, un refactor con su
test, o un grupo de tests + sus fixtures. No acumular cambios
heterogéneos en un solo commit.

Reglas concretas:
- Antes de empezar a trabajar, verificar `git status` y `git diff` para
  no mezclar cambios previos no relacionados.
- Si el cambio toca varios subsistemas pero responde a un único
  objetivo, va en un único commit con un mensaje que lo refleje.
- Si el cambio responde a varios objetivos, **dividir en varios
  commits** — usar `git add -p` para staging parcial.
- Prefijos obligatorios (de `CONTRIBUTING.md`): `feat:`, `fix:`,
  `refactor:`, `test:`, `docs:`, `build:`.
- Mensaje corto en imperativo, en español o inglés (consistente con
  los commits recientes del repo).
- No commitear con tests rotos si la suite estaba verde antes (el
  baseline conocido permite que falle solo `function_expr_simple`).
- **No usar `--amend` sobre commits ya empujados** a `main` ni a la
  rama compartida.
- **No `--no-verify`** salvo solicitud explícita.

Si la tarea genera dudas sobre dónde cortar, preguntar antes de
empezar — es más barato que reorganizar `git rebase` después.

## Convenciones aplicadas

- **Idioma:** identificadores en inglés; comentarios explicativos en
  español, cortos y centrados en la intención.
- **C99 estricto:** compilar con `-Wall -Wextra -std=c99 -g
  -D_GNU_SOURCE -MMD -MP`. **0 warnings** es regla, no aspiración.
- **Header guards** con `#ifndef / #define / #endif` en todos los `.h`.
- **Funciones cortas** (preferentemente < 50 líneas). Si crece, dividir
  por responsabilidad, no por más ramas.
- **Memoria:** toda asignación tiene ruta de liberación clara. Respetar
  el contexto/arena del AST (`hulk_ast_context.c`) antes de hacer
  `malloc` directo. No introducir nuevo estado global sin razón
  arquitectónica fuerte.
- **Tests:** todo test nuevo destinado al pipeline regresivo debe
  quedar integrado al `Makefile` (regla explícita del equipo —
  `probar/nota_de_abel.txt`). Sin scripts paralelos sueltos. La suite
  `probar/spec_check/` es exploratoria, no regresiva — no integrarla al
  Makefile.

## Reglas por subsistema (no cruzar fronteras)

- `hulk_compiler.*` orquesta, no hospeda lógica de fase.
- Lexer dentro de `generador_analizadores_lexicos/`.
- Gramáticas, FIRST/FOLLOW, tabla LL(1) y motor de parsing dentro de
  `generador_parser_ll1/`.
- Nodos del AST, propiedad y travesía dentro de `hulk_ast/core/`.
- Construcción del AST dentro de `hulk_ast/builder/`.
- Chequeos semánticos, ámbitos y reglas de tipo dentro de
  `hulk_ast/semantic/`.
- LLVM IR dentro de `hulk_ast/codegen/` (es el único subsistema que
  necesita `LLVM_CFLAGS`; el `Makefile` ya lo aplica solo a esa
  carpeta).
- Preferir extender un subsistema existente a crear helpers paralelos.

## Comandos canónicos

```bash
# Build completo (regenera lexer Flex + binario)
make
make clean       # limpia artefactos generados
make rebuild     # clean + build

# Ejecutar el compilador (solo ejecuta hasta build_ast — NO codegen)
./hulk_compiler test.hulk
./hulk_compiler "let x = 5 in x;"

# Tests internos del Makefile — integrar todo test nuevo aquí
make test-build  # solo compila los binarios de test
make test-all    # corre toda la suite (criterio de regresión)

# Aislar subsistemas
make test-lexer
make test-parser
make test-ast
make test-hulk-ast
make test-ast-builder
make test-semantic
make test-codegen
make test-feature-decorators-closures

# Suite exploratoria de cumplimiento de la spec (NO integrada al Make)
./probar/spec_check/spec_runner tests/hulk_programs
```

Prerrequisitos del sistema: `gcc` (C99), `make`, `flex`, `llvm-config-18`
o `llvm-config`, `libfl-dev`, `lli-18`. En Ubuntu/Debian:
`sudo apt install -y build-essential make flex llvm-18 llvm-18-dev libfl-dev`.

Si `flex` o `llvm-config` faltan, el build no pasa: decirlo
explícitamente al usuario en vez de forzar workarounds.

## Pautas de comportamiento (heredadas, condensadas)

### 1. Pensar antes de codear

- Estado las suposiciones explícitamente. Si hay duda, preguntar.
- Si hay varias interpretaciones, presentarlas — no elegir en
  silencio.
- Si existe una vía más simple, decirlo. Empujar en sentido contrario
  cuando proceda.

### 2. Simplicidad primero

- Mínimo código que resuelva el problema. Nada especulativo.
- No abstracciones para uso único, no flags de configuración no
  pedidos, no manejo de errores para escenarios imposibles.
- "¿Un senior diría que esto está sobre-ingenierizado?" Si sí,
  simplificar.

### 3. Cambios quirúrgicos

- Tocar solo lo necesario. No "mejorar" código vecino, formato o
  comentarios.
- No refactorizar lo que no está roto. Respetar el estilo existente
  aunque difiera del propio.
- Limpiar únicamente los huérfanos que tus propios cambios crearon. Si
  ves código muerto previo, mencionarlo — no borrarlo sin permiso.

### 4. Ejecución guiada por objetivo

- Convertir tareas en metas verificables: "arreglar el bug" →
  "escribir un test que reproduzca el bug, luego hacerlo pasar".
- Para tareas multi-paso, declarar plan breve con criterio de
  verificación por paso.
- Antes de declarar éxito: `make test-all` debe pasar, salvo regresión
  ya conocida documentada en este archivo.

## Reglas operativas específicas del compilador

- **No declarar soporte de un feature parcialmente implementado.** Si
  un nodo existe en el AST pero no llega a codegen, decirlo con esas
  palabras. La spec del PDF describe muchas features así.
- **`hulk-docs.pdf` es roadmap, no contrato.** Una característica
  descrita allí solo está "implementada" si hay código y test que la
  ejerciten. Cuando se cite la spec, citar también si está
  implementada o no según la tabla de cumplimiento de
  `probar/spec_check/`.
- **LLVM 18 usa opaque pointers** (`ptr`). Cualquier código en
  `hulk_ast/codegen/` que compare `LLVMTypeOf(x) == ti->ptr_type` es
  potencialmente ambiguo: todos los `ptr` del contexto son el mismo
  tipo. Si el cambio afecta dispatch de métodos o acceso a atributos,
  considerar pasar el tipo como metadato adicional en vez de inferirlo
  del `LLVMTypeRef`.
- **La tabla LL(1) tiene caché en disco** (`*.ll1.cache`). Si modificas
  `grammar_hulk.c` o `grammar.ll1`, asegúrate de que la caché se
  invalide / regenere; no editar el `.cache` a mano.
- **Conflictos LL(1) aparecen como warnings del generador**, no
  necesariamente como fallos de test. Si tocas la gramática,
  revisarlos.
- **El test que falla hoy** (`function_expr_simple`,
  `tests/test_ast_builder.c:952`) es el lugar correcto para empezar si
  el usuario pide "arregla los tests rotos". Reproducir con
  `make test-ast-builder`.
- **No mezclar fases.** Si una corrección semántica parece requerir
  cambios en el builder, validar primero si el síntoma no nace de un
  error semántico mal localizado.
