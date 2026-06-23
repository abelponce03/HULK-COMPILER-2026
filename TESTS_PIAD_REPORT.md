# Reporte de Ejecucion de `tests_piad`

## Resumen

- Fecha de actualizacion: `2026-06-23 15:36:06 CDT`
- Workspace: `/home/josue/HULK-COMPILER-2026`
- Parser principal: LL(1) unicamente; `hulk_build_ast` es una fachada minima sobre `hulk_ll1_build_ast`.
- Runner usado: `bash <(tr -d '\r' < tests_piad/hulk/run_tests.sh) "$PWD" "$PWD/tests_piad/hulk"`
- Resultado global PIAD: `RESULT: ALL_PASS`

## Verificaciones Ejecutadas

| Comando | Resultado |
|---|---|
| `make clean` | PASS |
| `make build` | PASS |
| `make test-all` | PASS |
| `./probar/spec_check/spec_runner tests/hulk_programs` | `26/26 PASS` |
| `bash <(tr -d '\r' < tests_piad/hulk/run_tests.sh) "$PWD" "$PWD/tests_piad/hulk"` | PASS |

## Resumen por Categoria PIAD

| Categoria | Resultado |
|---|---:|
| `ok/minimal` | `20/20 [PASS]` |
| `ok/types` | `10/10 [PASS]` |
| `ok/oop` | `10/10 [PASS]` |
| `errors/lexical` | `6/6 [PASS]` |
| `errors/syntactic` | `10/10 [PASS]` |
| `errors/semantic` | `15/15 [PASS]` |
| `ok/extras` | `10/10 [bonus]` |
| `ok/macros` | `8/8 [bonus]` |
| `ok/arrays` | `8/8 [bonus]` |
| `ok/interfaces` | `6/6 [bonus]` |
| `ok/lambdas` | `6/6 [bonus]` |
| `ok/generators` | `6/6 [bonus]` |

## Notas

- El script `tests_piad/hulk/run_tests.sh` conserva finales CRLF; por eso se invoca con `tr -d '\r'`.
- `make test-all` incluye `test-ll1-builder` y queda verde con el LL(1) como unico parser AST enlazado.
- `tests/hulk_programs` fue validado con el runner exploratorio compilado contra los objetos actuales, sin los modulos `parse_*` del parser descendente.
- La suite PIAD completa cubre `115` archivos `.hulk` en este arbol.
