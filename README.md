# HULK Compiler 2026

Compilador para el lenguaje **HULK** (Havana University Language for
Kompilers), implementado en C99. El proyecto cubre el pipeline principal de
compilacion:

```text
fuente .hulk -> lexer -> parser/AST -> analisis semantico -> codegen LLVM -> ./output
```

El binario contractual del compilador es `./hulk`. Cuando la compilacion es
exitosa, genera un ejecutable nativo llamado `./output`.

## Requisitos

- `gcc` con soporte C99
- `make`
- `flex`
- LLVM con `llvm-config` disponible (`llvm-config-18` o `llvm-config`)
- `cc` para enlazar el ejecutable final
- `libm`

En una instalacion basada en Debian/Ubuntu, una base razonable es:

```bash
sudo apt install build-essential make flex llvm llvm-dev libfl-dev
```

Si se usa LLVM 18:

```bash
sudo apt install llvm-18 llvm-18-dev
```

## Compilar y ejecutar

```bash
make                 # compila el proyecto y genera ./hulk
make build           # equivalente al target principal
./hulk test.hulk     # compila un programa HULK y genera ./output
./output             # ejecuta el programa compilado
make clean           # elimina artefactos generados
make rebuild         # limpia y recompila
```

El compilador recibe siempre un archivo `.hulk`:

```bash
./hulk programa.hulk
```

En caso de exito, el exit code de `./hulk` es `0` y el resultado ejecutable
queda en `./output`.

## Contrato de errores

`hulk_cli.c` adapta los diagnosticos internos al contrato esperado por la
facultad:

| Error | Exit code | Formato |
| --- | ---: | --- |
| Lexico | `1` | `(line,col) LEXICAL: message` |
| Sintactico | `2` | `(line,col) SYNTACTIC: message` |
| Semantico/codegen | `3` | `(line,col) SEMANTIC: message` |

Si aparecen errores de varias fases, la prioridad de salida es:

```text
LEXICAL > SYNTACTIC > SEMANTIC
```

## Caracteristicas implementadas

- Aritmetica: `+`, `-`, `*`, `/`, `%`, `**`
- Strings y concatenacion con `@` / `@@`
- Booleanos, comparadores y operadores logicos
- Builtins: `print`, `sqrt`, `sin`, `cos`, `exp`, `log`, `rand`, `parse`,
  `range`, `PI`, `E`
- `let` simple y multiple, anidamiento y shadowing
- Asignacion destructiva con `:=`
- Condicionales `if`, `elif`, `else`
- Ciclos `while` y `for (x in range(...))`
- Funciones inline y con cuerpo, recursion y referencias entre funciones
- Tipos definidos por el usuario, atributos, metodos y constructores
- Herencia con `inherits Parent(args)`
- Polimorfismo dinamico mediante vtables y tags de tipo
- Operadores `is` y `as`
- Llamadas a `base()` desde metodos sobrescritos
- Protocolos con conformidad estructural
- Vectores, literales de vector e indexacion
- Lambdas como expresion y closures basicas
- Decoradores con desugaring en la fase semantica

## Arquitectura

```text
HULK-COMPILER-2026/
|-- hulk_cli.c                         Entrada contractual: ./hulk archivo.hulk
|-- hulk_compiler.h/.c                 Fachada del compilador
|-- hulk_tokens.h/.c                   Tabla de tokens HULK y regex asociadas
|-- error_handler.h/.c                 Canal centralizado de diagnosticos
|-- grammar.ll1                        Gramatica LL(1) de HULK
|-- Makefile                           Build, tests y limpieza
|
|-- generador_analizadores_lexicos/    Regex -> AST regex -> DFA -> lexer
|-- generador_parser_ll1/              Gramatica, FIRST/FOLLOW, tabla LL(1), parser
|
|-- hulk_ast/
|   |-- core/                          Nodos AST, contexto/arena y visitor
|   |-- builder/                       Construccion del AST HULK
|   |-- semantic/                      Scopes, tipos, chequeos y desugaring
|   |-- codegen/                       Emision LLVM IR y ejecutable nativo
|   `-- printer/                       Impresion/debug del AST
|
|-- tests/                             Tests internos en C
|-- tests_piad/                        Suite PIAD / pruebas end-to-end
`-- test.hulk                          Programa pequeno de ejemplo
```

### Flujo interno

1. `hulk_compiler_init` construye el DFA del lexer desde las regex declaradas en
   `hulk_tokens.c`.
2. El builder consume la fuente, tokeniza y construye el AST HULK.
3. El analizador semantico registra tipos, funciones y simbolos; valida scopes,
   conformidad de tipos, herencia, protocolos y decoradores.
4. El backend genera LLVM IR, emite un objeto nativo y enlaza `./output`.

## Tests

La suite interna se compila y ejecuta con:

```bash
make test-all
```

Tambien existen targets individuales:

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
```

Para compilar los binarios de test sin ejecutarlos:

```bash
make test-build
```

La suite PIAD se encuentra en `tests_piad/hulk/run_tests.sh`. Se debe ejecutar
despues de construir `./hulk`:

```bash
make build
bash tests_piad/hulk/run_tests.sh "$(pwd)" "$(pwd)/tests_piad/hulk"
```

Esa suite compila programas `.hulk`, ejecuta el `./output` generado y compara la
salida con los archivos `.expected`. Tambien valida errores lexicos, sintacticos
y semanticos mediante archivos `.exit`.

## Artefactos generados

Durante el build y la ejecucion pueden aparecer:

- `./hulk`: binario del compilador
- `./output`: ejecutable nativo producido al compilar un programa HULK
- `./output.o`: objeto intermedio usado por el backend
- `.build/`: archivos auxiliares de debug o tablas generadas
- `*.o`, `*.d`: objetos y dependencias de compilacion
- `generador_analizadores_lexicos/regex_lexer.c`: fuente generado por `flex`

`make clean` elimina estos artefactos.

## Documentacion adicional

- `REPORT.md`: reporte de diseno e implementacion con detalles del pipeline,
  backend OOP, semantica y codegen.
- `agent.md`: guia de trabajo para agentes que modifiquen el repositorio.
- `hulk-docs.pdf`: documentacion del lenguaje HULK.
