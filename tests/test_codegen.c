/*
 * test_codegen.c — Tests unitarios para generación de LLVM IR
 *
 * Cubre:
 *   - Generación de IR para literales (Number, String, Boolean)
 *   - Expresiones aritméticas y lógicas
 *   - Funciones y llamadas
 *   - Let bindings
 *   - If/elif/else
 *   - While y For
 *   - Tipos de usuario con atributos y métodos
 *   - Concatenación de strings
 *   - Decoradores (composición de funciones)
 *   - Verificación de módulo LLVM (sin crashear)
 *   - Escritura del IR a archivo
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_ast/core/hulk_ast.h"
#include "../hulk_ast/builder/hulk_ast_builder.h"
#include "../hulk_ast/semantic/hulk_semantic.h"
#include "../hulk_ast/codegen/hulk_codegen.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ============================================================
 *  Fixture: compilador compartido
 * ============================================================ */

static HulkCompiler hc;
static int hc_ready = 0;

static void ensure_compiler(void) {
    if (!hc_ready) {
#ifdef _WIN32
        freopen("NUL", "w", stdout);
#else
        freopen("/dev/null", "w", stdout);
#endif
        hc_ready = hulk_compiler_init(&hc);
#ifdef _WIN32
        freopen("CON", "w", stdout);
#else
        freopen("/dev/tty", "w", stdout);
#endif
    }
}

/* Helper: build AST + semantic + codegen → .ll file.
 * Returns 0 on success, >0 on error. */
static int codegen_to_file(const char *src, const char *out_file) {
    ensure_compiler();
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    HulkNode *ast = hulk_build_ast(&ctx, hc.dfa, src);
    int result = -1;
    if (ast) {
        int sem_err = hulk_semantic_analyze(&ctx, ast);
        if (sem_err == 0) {
            result = hulk_codegen(ast, out_file);
        } else {
            result = sem_err;
        }
    }

    hulk_ast_context_free(&ctx);
    return result;
}

/* Helper: codegen and check that .ll file was created and contains pattern */
static int codegen_contains(const char *src, const char *pattern) {
    const char *tmp = "/tmp/hulk_test.ll";
    int ret = codegen_to_file(src, tmp);
    if (ret != 0) return 0;

    FILE *f = fopen(tmp, "r");
    if (!f) return 0;

    char buf[8192];
    int found = 0;
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, pattern)) {
            found = 1;
            break;
        }
    }
    fclose(f);
    unlink(tmp);
    return found;
}

/* Helper: just check codegen succeeds (returns 0) */
static int codegen_ok(const char *src) {
    const char *tmp = "/tmp/hulk_test_ok.ll";
    int ret = codegen_to_file(src, tmp);
    unlink(tmp);
    return ret;
}

/* ============================================================
 *  SUITE: Literales
 * ============================================================ */

TEST(cg_number_literal) {
    ASSERT_EQ(0, codegen_ok("42;"));
}

TEST(cg_string_literal) {
    ASSERT_EQ(0, codegen_ok("\"hello\";"));
}

TEST(cg_bool_literal) {
    ASSERT_EQ(0, codegen_ok("true;"));
}

TEST(cg_negative_number) {
    ASSERT_EQ(0, codegen_ok("-5;"));
}

/* ============================================================
 *  SUITE: Expresiones aritméticas y lógicas
 * ============================================================ */

TEST(cg_arithmetic) {
    ASSERT_EQ(0, codegen_ok("1 + 2 * 3;"));
}

TEST(cg_division) {
    ASSERT_EQ(0, codegen_ok("10 / 3;"));
}

TEST(cg_modulo) {
    ASSERT_EQ(0, codegen_ok("10 % 3;"));
}

TEST(cg_power) {
    ASSERT_EQ(0, codegen_ok("2 ** 10;"));
}

TEST(cg_comparison) {
    ASSERT_EQ(0, codegen_ok("1 < 2;"));
}

TEST(cg_logical_and) {
    ASSERT_EQ(0, codegen_ok("true && false;"));
}

TEST(cg_logical_or) {
    ASSERT_EQ(0, codegen_ok("true || false;"));
}

/* ============================================================
 *  SUITE: Funciones
 * ============================================================ */

TEST(cg_function_simple) {
    ASSERT_EQ(0, codegen_ok("function f(x: Number): Number => x + 1; f(5);"));
}

TEST(cg_function_two_params) {
    ASSERT_EQ(0, codegen_ok(
        "function add(a: Number, b: Number): Number => a + b; add(3, 4);"));
}

TEST(cg_function_ir_has_define) {
    ASSERT(codegen_contains(
        "function f(x: Number): Number => x * 2; f(1);",
        "define"));
}

TEST(cg_function_call_builtin_sqrt) {
    ASSERT_EQ(0, codegen_ok("sqrt(16);"));
}

TEST(cg_function_call_builtin_sin) {
    ASSERT_EQ(0, codegen_ok("sin(3.14);"));
}

TEST(cg_function_call_print) {
    ASSERT_EQ(0, codegen_ok("print(42);"));
}

TEST(cg_ir_has_main) {
    ASSERT(codegen_contains("42;", "define i32 @main"));
}

/* ============================================================
 *  SUITE: Let bindings
 * ============================================================ */

TEST(cg_let_simple) {
    ASSERT_EQ(0, codegen_ok("let x = 5 in x + 1;"));
}

TEST(cg_let_multiple) {
    ASSERT_EQ(0, codegen_ok("let x = 1, y = 2 in x + y;"));
}

TEST(cg_let_nested) {
    ASSERT_EQ(0, codegen_ok("let x = 1 in let y = x + 1 in y * 2;"));
}

/* ============================================================
 *  SUITE: Control de flujo
 * ============================================================ */

TEST(cg_if_else) {
    ASSERT_EQ(0, codegen_ok("if (true) 1 else 0;"));
}

TEST(cg_if_elif_else) {
    ASSERT_EQ(0, codegen_ok("if (false) 1 elif (true) 2 else 3;"));
}

TEST(cg_while_loop) {
    ASSERT_EQ(0, codegen_ok("let x = 0 in while (x < 10) x;"));
}

/* ============================================================
 *  SUITE: Concatenación
 * ============================================================ */

TEST(cg_concat_strings) {
    ASSERT_EQ(0, codegen_ok("\"hello\" @ \" world\";"));
}

TEST(cg_concat_with_space) {
    ASSERT_EQ(0, codegen_ok("\"hello\" @@ \"world\";"));
}

TEST(cg_concat_mixed) {
    ASSERT_EQ(0, codegen_ok("\"value\" @ 42;"));
}

/* ============================================================
 *  SUITE: Tipos de usuario
 * ============================================================ */

TEST(cg_type_simple) {
    ASSERT_EQ(0, codegen_ok(
        "type Counter() {"
        "  count: Number = 0;"
        "}"
        "let c = new Counter() in c.count;"));
}

TEST(cg_type_ir_has_struct) {
    ASSERT(codegen_contains(
        "type Counter() { count: Number = 0; } let c = new Counter() in c.count;",
        "Counter"));
}

/* ============================================================
 *  SUITE: Bloques
 * ============================================================ */

TEST(cg_block) {
    ASSERT_EQ(0, codegen_ok("{ 1; 2; 3; };"));
}

/* ============================================================
 *  SUITE: IR tiene runtime
 * ============================================================ */

TEST(cg_ir_has_printf) {
    ASSERT(codegen_contains("42;", "printf"));
}

TEST(cg_ir_has_hulk_print) {
    ASSERT(codegen_contains("42;", "hulk_print"));
}

TEST(cg_ir_has_hulk_concat) {
    ASSERT(codegen_contains("42;", "hulk_concat"));
}

TEST(cg_ir_has_hulk_num_to_str) {
    ASSERT(codegen_contains("42;", "hulk_num_to_str"));
}

TEST(cg_ir_has_hulk_bool_to_str) {
    ASSERT(codegen_contains("42;", "hulk_bool_to_str"));
}

/* ============================================================
 *  SUITE: Módulo LLVM válido
 * ============================================================ */

TEST(cg_module_valid_arithmetic) {
    ASSERT_EQ(0, codegen_ok("1 + 2 * 3 - 4 / 2;"));
}

TEST(cg_module_valid_func_calls) {
    ASSERT_EQ(0, codegen_ok(
        "function double(x: Number): Number => x * 2;"
        "print(double(21));"));
}

TEST(cg_module_valid_let_if) {
    ASSERT_EQ(0, codegen_ok(
        "let x = 10 in if (x > 5) x * 2 else x;"));
}

TEST(cg_module_valid_nested) {
    ASSERT_EQ(0, codegen_ok(
        "function fact(n: Number): Number =>"
        "  if (n <= 1) 1 else n * fact(n - 1);"
        "print(fact(5));"));
}

TEST(cg_module_valid_string_ops) {
    ASSERT_EQ(0, codegen_ok(
        "let name = \"HULK\" in \"Hello\" @ name;"));
}

/* ============================================================
 *  SUITE: Archivo .ll se genera correctamente
 * ============================================================ */

TEST(cg_file_output) {
    const char *tmp = "/tmp/hulk_codegen_test_output.ll";
    int ret = codegen_to_file("42;", tmp);
    ASSERT_EQ(0, ret);
    /* Verificar que el archivo existe */
    FILE *f = fopen(tmp, "r");
    ASSERT_NOT_NULL(f);
    /* Verificar que tiene contenido */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    ASSERT_GT(sz, 0);
    unlink(tmp);
}

/* ============================================================
 *  main
 * ============================================================ */

int main(void) {
    printf("\n" "\033[1m" "╔═══════════════════════════════════════╗" "\033[0m" "\n");
    printf("\033[1m" "║   HULK Compiler — Codegen Tests       ║" "\033[0m" "\n");
    printf("\033[1m" "╚═══════════════════════════════════════╝" "\033[0m" "\n");

    TEST_SUITE("Literales");
    RUN_TEST(cg_number_literal);
    RUN_TEST(cg_string_literal);
    RUN_TEST(cg_bool_literal);
    RUN_TEST(cg_negative_number);

    TEST_SUITE("Aritmética y lógica");
    RUN_TEST(cg_arithmetic);
    RUN_TEST(cg_division);
    RUN_TEST(cg_modulo);
    RUN_TEST(cg_power);
    RUN_TEST(cg_comparison);
    RUN_TEST(cg_logical_and);
    RUN_TEST(cg_logical_or);

    TEST_SUITE("Funciones");
    RUN_TEST(cg_function_simple);
    RUN_TEST(cg_function_two_params);
    RUN_TEST(cg_function_ir_has_define);
    RUN_TEST(cg_function_call_builtin_sqrt);
    RUN_TEST(cg_function_call_builtin_sin);
    RUN_TEST(cg_function_call_print);
    RUN_TEST(cg_ir_has_main);

    TEST_SUITE("Let bindings");
    RUN_TEST(cg_let_simple);
    RUN_TEST(cg_let_multiple);
    RUN_TEST(cg_let_nested);

    TEST_SUITE("Control de flujo");
    RUN_TEST(cg_if_else);
    RUN_TEST(cg_if_elif_else);
    RUN_TEST(cg_while_loop);

    TEST_SUITE("Concatenación");
    RUN_TEST(cg_concat_strings);
    RUN_TEST(cg_concat_with_space);
    RUN_TEST(cg_concat_mixed);

    TEST_SUITE("Tipos de usuario");
    RUN_TEST(cg_type_simple);
    RUN_TEST(cg_type_ir_has_struct);

    TEST_SUITE("Bloques");
    RUN_TEST(cg_block);

    TEST_SUITE("Runtime en IR");
    RUN_TEST(cg_ir_has_printf);
    RUN_TEST(cg_ir_has_hulk_print);
    RUN_TEST(cg_ir_has_hulk_concat);
    RUN_TEST(cg_ir_has_hulk_num_to_str);
    RUN_TEST(cg_ir_has_hulk_bool_to_str);

    TEST_SUITE("Módulo LLVM válido");
    RUN_TEST(cg_module_valid_arithmetic);
    RUN_TEST(cg_module_valid_func_calls);
    RUN_TEST(cg_module_valid_let_if);
    RUN_TEST(cg_module_valid_nested);
    RUN_TEST(cg_module_valid_string_ops);

    TEST_SUITE("Archivo de salida");
    RUN_TEST(cg_file_output);

    TEST_REPORT();
    return TEST_EXIT_CODE();
}
