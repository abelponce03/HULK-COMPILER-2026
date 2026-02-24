# HULK Compiler 2026

Compilador para el lenguaje **HULK** (Havana University Language for Kompilers),
implementado en C99.  Actualmente cubre las fases de **análisis léxico** y
**análisis sintáctico** (parser LL(1) predictivo controlado por tabla).

---

## Compilar y ejecutar

```bash
make                    # compila todo (genera flex, construye DFA y binario)
./hulk_compiler test.hulk   # lexer + parser sobre un archivo HULK
./hulk_compiler "let x = 5;" # o directamente una expresión
make clean              # elimina artefactos generados
```

Requisitos: `gcc` (C99), `flex`.

---

## Arquitectura del proyecto

```
HULK-COMPILER-2026/
│
├── main.c                  Punto de entrada
├── hulk_compiler.h/c       Fachada del compilador (Pipeline pattern)
├── hulk_tokens.h/c         Definiciones léxicas de HULK (regex → token_id)
├── error_handler.h/c       Logging centralizado (Observer pattern)
├── grammar.ll1             Gramática LL(1) de HULK
├── test.hulk               Archivo de prueba
├── Makefile
│
├── generador_analizadores_lexicos/     ← Generador de Lexer (reutilizable)
│   ├── token_types.h       Tipos Token y TokenRegex (dependencia base)
│   ├── regex_tokens.h      Tokens del lexer de regex (Flex ↔ parser)
│   ├── regex_lexer.l       Definición Flex para tokenizar regex
│   ├── regex_parser.h/c    Parser LL(1) de regex → AST
│   ├── ast.h/c             AST de regex + Object Pool + Visitor pattern
│   ├── afd.h/c             Construcción directa de DFA (Algorithm 3.36)
│   └── lexer.h/c           Tokenizador basado en DFA
│
├── generador_parser_ll1/              ← Generador de Parser LL(1) (reutilizable)
│   ├── grammar.h/c         Estructura de gramáticas + Abstract Factory
│   ├── grammar_utils.h     Helpers compartidos (str_dup, str_trim)
│   ├── grammar_regex.c     Gramática de expresiones regulares
│   ├── grammar_hulk.c      Gramática de HULK + carga desde archivo
│   ├── first_follow.h/c    Cálculo de FIRST y FOLLOW
│   ├── ll1_table.h/c       Tabla LL(1): construcción, consulta, serialización
│   └── parser.h/c          Motor de parsing LL(1) (Algorithm 4.34)
│
└── output/                 ← Artefactos generados (CSV, DOT, etc.)
```

### Patrones de diseño aplicados

| Patrón           | Ubicación                 | Propósito                                    |
|------------------|---------------------------|----------------------------------------------|
| Pipeline         | `hulk_compiler.c`         | Fases de construcción del lexer              |
| Visitor          | `ast.h/c`                 | Recorridos genéricos del AST                 |
| Object Pool      | `ast.c` (ASTContext)      | Arena de nodos AST, O(1) alloc/free          |
| Abstract Factory | `grammar.h/c`             | Creación de gramáticas (HULK, regex, ...)    |
| Strategy         | `afd.h` (TokenPriorityFn) | Resolución de conflictos de tokens en DFA    |
| Observer         | `error_handler.h/c`       | Handler de errores reemplazable              |

### Algoritmos implementados

- **Algoritmo 3.36** (Dragon Book) — Construcción directa de DFA desde AST de regex
- **Algoritmo 4.34** (Dragon Book) — Parser predictivo no recursivo controlado por tabla LL(1)
- Cálculo iterativo de **FIRST** y **FOLLOW**
- Tokenización por **maximal munch** sobre tabla de transiciones DFA

---

## Próximas fases

- [ ] Construcción del AST del lenguaje HULK durante el parsing
- [ ] Análisis semántico (chequeo de tipos, resolución de nombres)
- [ ] Generación de código intermedio
- [ ] Evaluación / intérprete