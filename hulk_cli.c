/*
 * hulk_cli.c — Punto de entrada del compilador HULK conforme al
 * contrato de interfaz de la facultad (matcom/compilers).
 *
 * Uso:
 *   ./hulk <archivo.hulk>
 *
 * En éxito (exit 0): produce ./output (binario nativo).
 * En error:
 *   1 = LEXICAL
 *   2 = SYNTACTIC
 *   3 = SEMANTIC
 * Cada error se imprime a stderr en formato:
 *   (line,col) TYPE: message
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

#include "hulk_compiler.h"
#include "error_handler.h"
#include "hulk_ast/core/hulk_ast.h"
#include "hulk_ast/builder/hulk_ast_builder.h"
#include "hulk_ast/semantic/hulk_semantic.h"
#include "hulk_ast/codegen/hulk_codegen.h"

/* ============================================================
 *  Estado global del reporte de errores
 * ============================================================ */

static int n_lex = 0;
static int n_syn = 0;
static int n_sem = 0;

/* stderr real, antes de redirección */
static FILE *real_stderr = NULL;

static void emit_diag(int line, int col, const char *type, const char *msg) {
    if (!real_stderr) real_stderr = stderr;
    fprintf(real_stderr, "(%d,%d) %s: %s\n", line, col, type, msg);
    fflush(real_stderr);
}

/* Convierte el módulo del compiler_log a TYPE del contrato e
 * incrementa el contador correspondiente. */
static const char* module_to_type(const char *module) {
    if (!module) return "SEMANTIC";
    if (strcmp(module, "lexer") == 0)        { n_lex++; return "LEXICAL"; }
    if (strcmp(module, "regex") == 0)        { n_syn++; return "SYNTACTIC"; }
    if (strcmp(module, "ast_builder") == 0)  { n_syn++; return "SYNTACTIC"; }
    if (strcmp(module, "parser") == 0)       { n_syn++; return "SYNTACTIC"; }
    if (strcmp(module, "ll1") == 0)          { n_syn++; return "SYNTACTIC"; }
    if (strcmp(module, "semantic") == 0)     { n_sem++; return "SEMANTIC"; }
    if (strcmp(module, "codegen") == 0)      { n_sem++; return "SEMANTIC"; }
    n_sem++;
    return "SEMANTIC";
}

static void hulk_diag_handler(LogLevel level, const char *module,
                              const char *fmt, va_list args) {
    /* Suprime INFO/WARNING — el contrato solo pide errores */
    if (level != LOG_ERROR && level != LOG_FATAL) return;

    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);

    /* Algunos call-sites prefijan "[L:C] resto..." dentro del fmt. */
    int line = 0, col = 0;
    const char *msg = buf;
    if (buf[0] == '[') {
        int n_consumed = 0;
        if (sscanf(buf, "[%d:%d] %n", &line, &col, &n_consumed) >= 2 &&
            n_consumed > 0) {
            msg = buf + n_consumed;
        } else {
            line = 0; col = 0;
        }
    }

    const char *type = module_to_type(module);
    emit_diag(line, col, type, msg);
}

/* ============================================================
 *  Decide el exit code del contrato:
 *  1=LEXICAL > 2=SYNTACTIC > 3=SEMANTIC.
 * ============================================================ */

static int compute_exit_code(void) {
    if (n_lex > 0) return 1;
    if (n_syn > 0) return 2;
    if (n_sem > 0) return 3;
    return 0;
}

/* ============================================================
 *  Slurp file
 * ============================================================ */

static char* slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = 0;
    fclose(f);
    return buf;
}

/* ============================================================
 *  Redirección de stdout/stderr internos del compilador
 *
 *  El compilador imprime mucho por stdout (banner, fase del DFA, etc.)
 *  y algunas fases imprimen errores directos a stderr en formato no
 *  contractual. Para mantener el contrato limpio:
 *    1. Guardamos el fd real de stderr en `real_stderr` antes de
 *       cualquier redirección.
 *    2. Redirigimos stdout y stderr a /dev/null durante la ejecución.
 *    3. Nuestro handler escribe los diagnósticos a `real_stderr`.
 * ============================================================ */

static void setup_io(void) {
    int saved_err = dup(fileno(stderr));
    if (saved_err < 0) {
        real_stderr = stderr;
        return;
    }
    real_stderr = fdopen(saved_err, "w");
    if (!real_stderr) real_stderr = stderr;

    /* Silenciar stdout y stderr internos */
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

/* ============================================================
 *  main
 * ============================================================ */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "uso: %s <archivo.hulk>\n", argv[0]);
        return 1;
    }

    /* Lee el archivo antes de redirección, así el error aparece bien */
    char *src = slurp(argv[1]);
    if (!src) {
        fprintf(stderr, "(0,0) LEXICAL: cannot read file '%s'\n", argv[1]);
        return 1;
    }

    setup_io();
    error_handler_set(hulk_diag_handler);

    /* ---- Fase 1: lexer + parser → AST ---- */
    HulkCompiler hc;
    if (!hulk_compiler_init(&hc)) {
        emit_diag(0, 0, "SEMANTIC", "compiler init failed");
        free(src);
        return 3;
    }

    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);
    HulkNode *ast = hulk_build_ast(&ctx, hc.dfa, src);

    if (!ast || n_lex > 0 || n_syn > 0) {
        int ec = compute_exit_code();
        if (ec == 0) ec = 2;  /* AST sin diagnóstico explícito → SYNTACTIC */
        hulk_ast_context_free(&ctx);
        hulk_compiler_free(&hc);
        free(src);
        return ec;
    }

    /* ---- Fase 2: análisis semántico ---- */
    int sem_errs = hulk_semantic_analyze(&ctx, ast);
    if (sem_errs > 0 || n_sem > 0) {
        int ec = compute_exit_code();
        if (ec == 0) ec = 3;
        hulk_ast_context_free(&ctx);
        hulk_compiler_free(&hc);
        free(src);
        return ec;
    }

    /* ---- Fase 3: codegen + link → ./output ---- */
    int cg_rc = hulk_codegen_to_executable(ast, "./output");

    hulk_ast_context_free(&ctx);
    hulk_compiler_free(&hc);
    free(src);

    if (cg_rc != 0) {
        int ec = compute_exit_code();
        if (ec == 0) ec = 3;
        return ec;
    }
    return 0;
}
