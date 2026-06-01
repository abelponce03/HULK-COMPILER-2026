/*
 * spec_runner.c — Runner exploratorio para verificar qué subconjunto
 *                 del lenguaje HULK soporta el compilador end-to-end.
 *
 * NO está integrado al Makefile (es análisis exploratorio).
 * Compila manualmente:
 *   make                                  # asegúrate de tener los .o
 *   gcc -std=c99 -g probar/spec_check/spec_runner.c \
 *       <todos los LIB_OBJS>              # ver build.sh
 *       -lfl -lLLVM-18 -lm -o probar/spec_check/spec_runner
 *
 * Uso:
 *   ./probar/spec_check/spec_runner tests/hulk_programs
 */

#define _DEFAULT_SOURCE
#include "../../hulk_compiler.h"
#include "../../hulk_ast/core/hulk_ast.h"
#include "../../hulk_ast/builder/hulk_ast_builder.h"
#include "../../hulk_ast/semantic/hulk_semantic.h"
#include "../../hulk_ast/codegen/hulk_codegen.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
    R_PASS,
    R_PARSE_FAIL,
    R_SEM_FAIL,
    R_CODEGEN_FAIL,
    R_RUN_FAIL,
    R_MISMATCH
} ResultKind;

static const char* kind_name(ResultKind k) {
    switch (k) {
        case R_PASS:         return "PASS";
        case R_PARSE_FAIL:   return "PARSE_FAIL";
        case R_SEM_FAIL:     return "SEM_FAIL";
        case R_CODEGEN_FAIL: return "CODEGEN_FAIL";
        case R_RUN_FAIL:     return "RUN_FAIL";
        case R_MISMATCH:     return "MISMATCH";
    }
    return "?";
}

static char* slurp_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, n, f);
    buf[n] = '\0';
    fclose(f);
    if (out_size) *out_size = n;
    return buf;
}

static int starts_with(const char *s, const char *pre) {
    while (*pre) if (*s++ != *pre++) return 0;
    return 1;
}

static int ends_with(const char *s, const char *suf) {
    size_t ls = strlen(s), lsf = strlen(suf);
    return ls >= lsf && strcmp(s + ls - lsf, suf) == 0;
}

static int cmp_names(const void *a, const void *b) {
    return strcmp(*(const char**)a, *(const char**)b);
}

/* Captura stdout/stderr del compilador en cada fase para no contaminar el reporte. */
static int silent_pipeline(const char *src,
                           const char *ll_out,
                           HulkCompiler *hc,
                           ResultKind *kind_on_fail,
                           char *err_msg, size_t err_cap) {
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    /* Silenciar stdout+stderr del compilador */
    fflush(stdout);
    fflush(stderr);
    int saved_out = dup(1);
    int saved_err = dup(2);
    int dev_null = open("/dev/null", O_WRONLY);
    dup2(dev_null, 1);
    dup2(dev_null, 2);
    close(dev_null);

    HulkNode *ast = hulk_build_ast(&ctx, hc->dfa, src);
    int rc = 0;
    if (!ast) {
        *kind_on_fail = R_PARSE_FAIL;
        snprintf(err_msg, err_cap, "build_ast retornó NULL");
        rc = 1;
    } else {
        int sem = hulk_semantic_analyze(&ctx, ast);
        if (sem != 0) {
            *kind_on_fail = R_SEM_FAIL;
            snprintf(err_msg, err_cap, "%d error(es) semántico(s)", sem);
            rc = 1;
        } else {
            int cg = hulk_codegen(ast, ll_out);
            if (cg != 0) {
                *kind_on_fail = R_CODEGEN_FAIL;
                snprintf(err_msg, err_cap, "codegen rc=%d", cg);
                rc = 1;
            }
        }
    }

    hulk_ast_context_free(&ctx);

    fflush(stdout);
    fflush(stderr);
    dup2(saved_out, 1);
    dup2(saved_err, 2);
    close(saved_out);
    close(saved_err);

    return rc;
}

static int run_capture(const char *ll_file, char *out_buf, size_t out_cap) {
    int p[2];
    if (pipe(p) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(p[0]); close(p[1]); return -1; }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);   /* stdout */
        close(p[1]);
        int dn = open("/dev/null", O_WRONLY);
        if (dn >= 0) { dup2(dn, 2); close(dn); }
        execlp("lli-18", "lli-18", ll_file, (char*)NULL);
        execlp("lli", "lli", ll_file, (char*)NULL);
        _exit(127);
    }
    close(p[1]);
    size_t total = 0;
    for (;;) {
        if (total + 1 >= out_cap) break;
        ssize_t n = read(p[0], out_buf + total, out_cap - 1 - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    out_buf[total] = '\0';
    close(p[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static int normalize_lines(char *s) {
    /* Eliminar trailing whitespace al final */
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' '))
        s[--n] = '\0';
    return (int)n;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "tests/hulk_programs";

    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "no se pudo abrir %s\n", dir); return 2; }

    char **names = NULL;
    int n_count = 0, n_cap = 0;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (!ends_with(e->d_name, ".hulk")) continue;
        if (n_count == n_cap) {
            n_cap = n_cap ? n_cap * 2 : 16;
            names = realloc(names, sizeof(char*) * n_cap);
        }
        names[n_count++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n_count, sizeof(char*), cmp_names);

    /* Inicializar compilador (silenciado) */
    fflush(stdout);
    int saved_out = dup(1);
    int saved_err = dup(2);
    int dev_null = open("/dev/null", O_WRONLY);
    dup2(dev_null, 1);
    dup2(dev_null, 2);
    close(dev_null);

    HulkCompiler hc;
    int hc_ok = hulk_compiler_init(&hc);

    fflush(stdout);
    dup2(saved_out, 1);
    dup2(saved_err, 2);
    close(saved_out);
    close(saved_err);

    if (!hc_ok) {
        fprintf(stderr, "no se pudo construir el lexer HULK\n");
        return 3;
    }

    int total = 0, passed = 0;
    int counts[6] = {0};

    printf("┌─────┬──────────────────────────────────────┬──────────────┐\n");
    printf("│  #  │ test                                 │ resultado    │\n");
    printf("├─────┼──────────────────────────────────────┼──────────────┤\n");

    for (int i = 0; i < n_count; i++) {
        char src_path[512], exp_path[512], ll_path[512];
        snprintf(src_path, sizeof(src_path), "%s/%s", dir, names[i]);
        char base[256];
        strncpy(base, names[i], sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        char *dot = strrchr(base, '.');
        if (dot) *dot = '\0';
        snprintf(exp_path, sizeof(exp_path), "%s/%s.expected", dir, base);
        snprintf(ll_path, sizeof(ll_path), "/tmp/hulk_spec_%s.ll", base);

        char *src = slurp_file(src_path, NULL);
        if (!src) {
            printf("│ %3d │ %-36s │ %-12s │\n", i+1, base, "NOSRC");
            total++;
            continue;
        }

        char err_msg[256] = "";
        ResultKind kind = R_PASS;
        int rc = silent_pipeline(src, ll_path, &hc, &kind, err_msg, sizeof(err_msg));
        free(src);

        if (rc != 0) {
            counts[kind]++;
            printf("│ %3d │ %-36s │ %-12s │  (%s)\n", i+1, base, kind_name(kind), err_msg);
            total++;
            continue;
        }

        /* Ejecutar el binario y capturar stdout */
        char out_buf[8192];
        int exit_code = run_capture(ll_path, out_buf, sizeof(out_buf));
        unlink(ll_path);
        if (exit_code < 0) {
            counts[R_RUN_FAIL]++;
            printf("│ %3d │ %-36s │ %-12s │  (exec falló)\n", i+1, base, "RUN_FAIL");
            total++;
            continue;
        }

        char *expected = slurp_file(exp_path, NULL);
        if (!expected) {
            printf("│ %3d │ %-36s │ %-12s │  (no .expected)\n", i+1, base, "NO_EXPECTED");
            total++;
            continue;
        }
        normalize_lines(expected);
        normalize_lines(out_buf);

        if (strcmp(out_buf, expected) == 0) {
            counts[R_PASS]++;
            passed++;
            printf("│ %3d │ %-36s │ %-12s │\n", i+1, base, "PASS");
        } else {
            counts[R_MISMATCH]++;
            printf("│ %3d │ %-36s │ %-12s │\n", i+1, base, "MISMATCH");
            printf("│     │   esperado:  %-60s\n", expected);
            printf("│     │   obtenido:  %-60s\n", out_buf);
        }
        free(expected);
        total++;
    }

    printf("└─────┴──────────────────────────────────────┴──────────────┘\n");
    printf("\nResumen: %d/%d PASS\n", passed, total);
    printf("  PASS          %d\n", counts[R_PASS]);
    printf("  PARSE_FAIL    %d\n", counts[R_PARSE_FAIL]);
    printf("  SEM_FAIL      %d\n", counts[R_SEM_FAIL]);
    printf("  CODEGEN_FAIL  %d\n", counts[R_CODEGEN_FAIL]);
    printf("  RUN_FAIL      %d\n", counts[R_RUN_FAIL]);
    printf("  MISMATCH      %d\n", counts[R_MISMATCH]);

    for (int i = 0; i < n_count; i++) free(names[i]);
    free(names);
    hulk_compiler_free(&hc);
    return passed == total ? 0 : 1;
}
