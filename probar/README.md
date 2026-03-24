# Guia completa para testear HULK-COMPILER-2026

Esta carpeta contiene una guia practica para probar el proyecto por fases: build, smoke tests, pruebas unitarias por modulo, y diagnostico de fallas.

Fecha de verificacion: 2026-03-23
Rama usada para la verificacion: dev_josue

## 1) Requisitos

Necesitas tener instalado:

- gcc con soporte C99
- make
- flex
- llvm-config (idealmente LLVM 18)
- libreria libfl

En Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential make flex llvm-18 llvm-18-dev libfl-dev
```

Comprobacion rapida:

```bash
gcc --version
make --version
flex --version
llvm-config-18 --version || llvm-config --version
```

## 2) Preparar entorno limpio

Desde la raiz del repo:

```bash
make clean
```

Compilar proyecto:

```bash
make
```

## 3) Smoke tests (comprobacion rapida)

### 3.1 Con archivo de ejemplo

```bash
make test-file
```

Esperado: ejecucion del lexer, parser y AST sobre test.hulk sin error de salida del comando.

### 3.2 Entrada directa valida

```bash
./hulk_compiler "let x = 5 in x;"
```

Esperado: AST construido exitosamente.

### 3.3 Entrada directa invalida (para validar manejo de errores)

```bash
./hulk_compiler "let x = 5;"
```

Esperado: parser reporta errores sintacticos y puede fallar la construccion del AST.

## 4) Suite completa de tests

```bash
make test-all
```

Que hace:

- Compila todos los tests
- Ejecuta todas las suites de tests del repositorio
- Devuelve codigo de salida 0 solo si todo pasa

## 5) Tests por aspecto

Estos comandos permiten aislar problemas por modulo:

```bash
make test-lexer
make test-parser
make test-ast
make test-hulk-ast
make test-ast-builder
make test-semantic
make test-codegen
make test-feature-decorators-closures
```

## 6) Estado observado en esta verificacion

Resumen ejecutado el 2026-03-23:

- make test-file: OK (exit 0)
- Entrada directa valida (let x = 5 in x;): OK (exit 0)
- make test-all: FALLA (exit 2)

Falla detectada en suite AST Builder:

- Test: function_expr_simple
- Archivo de referencia del test: tests/test_ast_builder.c
- Indicador en salida: ASSERT_NOT_NULL(ast) en linea 952
- Resumen de suite AST Builder: 78 total, 77 pass, 1 fail

Nota: el fallo es puntual de AST Builder; lexer/parser/AST base/semantic/codegen/decorators-closures reportan resultados en verde en la corrida global.

## 7) Script recomendado para ejecutar todo y guardar logs

Puedes usar el script de esta carpeta:

```bash
bash probar/run_tests.sh
```

El script crea logs dentro de probar/logs para cada etapa.

## 8) Deteccion rapida de regresiones

Flujo sugerido antes de push:

```bash
make clean
make
make test-lexer
make test-parser
make test-semantic
make test-codegen
make test-all
```

Si falla test-all, ir a logs y correr test individual del modulo fallido.

## 9) Troubleshooting frecuente

- Error de flex o regex_lexer.c no generado:
  - Ejecuta make clean && make
  - Verifica que flex este instalado

- Error con llvm-config:
  - Instala llvm-18 y llvm-18-dev
  - Si tu distro usa otro nombre, confirma que llvm-config exista en PATH

- Errores LL(1) como warnings:
  - Actualmente hay conflictos reportados en salida como advertencias del generador LL(1)
  - No necesariamente hacen fallar todos los tests, pero conviene revisarlos si tocas gramatica

- Falla puntual de AST Builder:
  - Reproducir con make test-ast-builder
  - Revisar parseo de function expressions en tests/test_ast_builder.c (caso function_expr_simple)

## 10) Limpieza final

```bash
make clean
```

Tambien puedes borrar logs de pruebas:

```bash
rm -rf probar/logs
```
