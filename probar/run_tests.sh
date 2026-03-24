#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_DIR="$ROOT_DIR/probar/logs"
mkdir -p "$LOG_DIR"

run_step() {
  local name="$1"
  shift
  local logfile="$LOG_DIR/${name}.log"

  echo ""
  echo "=================================================="
  echo "STEP: $name"
  echo "CMD : $*"
  echo "LOG : $logfile"
  echo "=================================================="

  "$@" >"$logfile" 2>&1
  local ec=$?

  if [ "$ec" -eq 0 ]; then
    echo "RESULT: OK"
  else
    echo "RESULT: FAIL (exit $ec)"
  fi

  echo "----- tail $name -----"
  tail -n 20 "$logfile" || true
  echo "----------------------"

  return "$ec"
}

summary_file="$LOG_DIR/summary.txt"
: > "$summary_file"

record() {
  local step="$1"
  local ec="$2"
  if [ "$ec" -eq 0 ]; then
    echo "$step: OK" | tee -a "$summary_file"
  else
    echo "$step: FAIL (exit $ec)" | tee -a "$summary_file"
  fi
}

cd "$ROOT_DIR" || exit 1

overall=0

run_step "build" make
ec=$?
record "build" "$ec"
[ "$ec" -ne 0 ] && overall=1

run_step "test_file" make test-file
ec=$?
record "test_file" "$ec"
[ "$ec" -ne 0 ] && overall=1

run_step "inline_valid" ./hulk_compiler "let x = 5 in x;"
ec=$?
record "inline_valid" "$ec"
[ "$ec" -ne 0 ] && overall=1

run_step "test_all" make test-all
ec=$?
record "test_all" "$ec"
[ "$ec" -ne 0 ] && overall=1

echo ""
echo "================ FINAL SUMMARY ================"
cat "$summary_file"
echo "Logs disponibles en: $LOG_DIR"

echo ""
if [ "$overall" -eq 0 ]; then
  echo "Estado general: OK"
  exit 0
else
  echo "Estado general: FAIL"
  exit 1
fi
