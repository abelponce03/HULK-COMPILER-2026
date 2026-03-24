# Reporte de verificacion de pruebas

Fecha: 2026-03-23
Rama: dev_josue

## Resultado global

- Estado: FALLA PARCIAL
- Causa: 1 test fallando en AST Builder

## Evidencia clave

- make test-all: no pasa completo (sale con error)
- make test-file: pasa
- Inline valido (let x = 5 in x;): pasa
- Inline invalido (let x = 5;): reporta errores sintacticos (comportamiento esperado para entrada invalida)

## Falla puntual detectada

- Suite: test-ast-builder
- Caso: function_expr_simple
- Indicador: ASSERT_NOT_NULL(ast)
- Referencia de test: tests/test_ast_builder.c (linea 952 mostrada por el runner)

## Nota

Los logs detallados de ejecucion pueden generarse y consultarse con:

```bash
bash probar/run_tests.sh
ls -la probar/logs
```
