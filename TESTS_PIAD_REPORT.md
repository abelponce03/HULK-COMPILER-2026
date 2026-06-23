# Reporte de Ejecucion de `tests_piad`

## Resumen

- Fecha de actualizacion: `2026-06-23 15:03:08 CDT`
- Workspace: `/home/josue/HULK-COMPILER-2026`
- Parser principal: `hulk_build_ast` delega en `hulk_ll1_build_ast`.
- Runner usado: `bash <(tr -d '\r' < tests_piad/hulk/run_tests.sh) "$PWD" "$PWD/tests_piad/hulk"`
- Resultado global PIAD: `RESULT: ALL_PASS`

## Verificaciones Ejecutadas

| Comando | Resultado |
|---|---|
| `make build` | PASS |
| `make test-all` | PASS |
| `make test-ll1-builder` | PASS |
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
- `make test-all` tambien queda verde con el LL(1) como parser principal.
- La suite PIAD completa cubre `115` archivos `.hulk` en este arbol.
