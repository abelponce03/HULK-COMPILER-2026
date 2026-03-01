# Guía de Contribución — HULK Compiler 2026

## Equipo

Somos un equipo de 2 personas. Usamos **GitHub Flow** (ramas cortas desde `main`).

---

## Flujo de Trabajo

### 1. Elegir una tarea

- Revisar las [Issues](../../issues) abiertas
- Asignarte la issue antes de empezar (para evitar trabajo duplicado)
- Si no hay issue para lo que quieres hacer, **créala primero**

### 2. Crear una rama

```bash
git checkout main
git pull origin main
git checkout -b <tipo>/<descripcion-corta>
```

**Convención de nombres de rama:**

| Prefijo      | Uso                          | Ejemplo                     |
|-------------|------------------------------|-----------------------------|
| `feature/`  | Nueva funcionalidad          | `feature/closures`          |
| `fix/`      | Corrección de bug            | `fix/lexer-string-escape`   |
| `refactor/` | Reestructuración de código   | `refactor/ast-pool`         |
| `docs/`     | Documentación                | `docs/readme-update`        |
| `test/`     | Nuevos tests                 | `test/parser-edge-cases`    |

### 3. Hacer commits atómicos

Formato de commit message:

```
<tipo>: <descripción breve>

[cuerpo opcional con más detalle]
```

**Tipos:**
- `feat:` nueva funcionalidad
- `fix:` corrección de bug
- `refactor:` reestructuración sin cambiar funcionalidad
- `test:` agregar o modificar tests
- `docs:` documentación
- `build:` cambios al Makefile o CI

**Ejemplos:**
```
feat: agregar soporte para closures en el lexer
fix: corregir off-by-one en line tracking del lexer
test: agregar tests de edge cases para strings
refactor: extraer tokenización a función auxiliar
```

### 4. Abrir un Pull Request

- Push tu rama: `git push origin <tu-rama>`
- Abre un PR hacia `main`
- Llena la plantilla del PR (se genera automáticamente)
- Pide review a tu compañero

### 5. Code Review

- **Todo PR necesita al menos 1 aprobación** antes de mergear
- Usa comentarios constructivos, sugiere soluciones
- Verifica que el CI pase (build + tests verdes)

### 6. Mergear

- Usa **Squash and Merge** para mantener el historial limpio
- Borra la rama después del merge

---

## Configurar el Proyecto

```bash
# Clonar
git clone <url-del-repo>
cd HULK-COMPILER-2026

# Dependencias: gcc, flex, make
# Ubuntu/Debian:
sudo apt-get install gcc flex make

# Compilar
make

# Ejecutar
./hulk_compiler test.hulk

# Tests
make test-all           # ejecutar todos los tests
make test-lexer         # solo tests del lexer
make test-parser        # solo tests del parser
make test-ast           # solo tests del AST

# Limpiar
make clean
make rebuild            # clean + build
```

---

## Estructura del Proyecto

```
├── main.c                          # Punto de entrada
├── hulk_compiler.h/c               # Orquestador principal (Pipeline)
├── hulk_tokens.h/c                 # Definiciones de tokens HULK
├── error_handler.h/c               # Sistema centralizado de errores
├── grammar.ll1                     # Gramática LL(1)
├── generador_analizadores_lexicos/ # Lexer: regex→AST→DFA→tokens
├── generador_parser_ll1/           # Parser: gramática→FIRST/FOLLOW→tabla LL(1)
├── tests/                          # Tests unitarios
│   ├── test_framework.h            # Mini framework de testing (header-only)
│   ├── test_lexer.c                # Tests del lexer (~30 tests)
│   ├── test_parser.c               # Tests del parser (~15 tests)
│   └── test_ast.c                  # Tests del AST (~20 tests)
├── .github/
│   ├── workflows/ci.yml            # CI automático (build + tests)
│   ├── ISSUE_TEMPLATE/             # Plantillas de issues
│   └── pull_request_template.md    # Plantilla de PR
└── output/                         # Archivos generados (csv, dot, png)
```

---

## Reglas de Código

1. **C99** — compilar siempre con `-Wall -Wextra -std=c99`
2. **0 warnings** — no se acepta código con warnings
3. **Tests pasan** — `make test-all` debe pasar antes de cualquier PR
4. **Nombrar en inglés** — variables, funciones y tipos en inglés
5. **Comentarios en español** — los comentarios explicativos van en español
6. **Header guards** — todos los `.h` usan `#ifndef / #define / #endif`
7. **Funciones < 50 líneas** — si es más larga, refactorizar
8. **No memory leaks** — toda memoria `malloc`/`realloc` debe tener su `free`

---

## Agregar un Test

1. Abre el archivo de test correspondiente en `tests/`
2. Define el test con la macro `TEST(nombre)`
3. Usa los asserts: `ASSERT()`, `ASSERT_EQ()`, `ASSERT_STR_EQ()`, `ASSERT_NOT_NULL()`
4. Registra el test con `RUN_TEST(nombre)` en `main()`
5. Compila y ejecuta: `make test-all`

```c
TEST(mi_nuevo_test) {
    int n; Token *t = tokenize("let x = 42;", &n);
    ASSERT_EQ(TOKEN_LET, t[0].type);
    ASSERT_EQ(TOKEN_NUMBER, t[3].type);
    ASSERT_STR_EQ("42", t[3].lexeme);
    free_tokens(t, n);
}
```

---

## Preguntas?

Abre un issue con la etiqueta `question` o habla con tu compañero directamente.
