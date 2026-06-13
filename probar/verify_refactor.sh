#!/bin/bash
# verify_refactor.sh — verificación rápida tras cada paso de refactor.
# Construye ./hulk, corre la suite oficial de matcom (si está clonada en
# /tmp/matcom_clone) y la suite exploratoria propia. Reporta PASS/FAIL.
set -uo pipefail
cd "$(dirname "$0")/.."

echo "── make build ──"
if ! make build > /tmp/refactor_build.log 2>&1; then
    echo "BUILD FAIL"; tail -20 /tmp/refactor_build.log; exit 1
fi
grep -iE "warning|error" /tmp/refactor_build.log && echo "(warnings arriba)" || echo "build limpio"

OFFICIAL=/tmp/matcom_clone/tests/hulk
if [ -d "$OFFICIAL" ]; then
    echo "── suite oficial matcom ──"
    bash "$OFFICIAL/run_tests.sh" "$PWD" "$OFFICIAL" 2>&1 | grep -E "^RESULT|^Category|ok/|errors/" | tail -10
fi
