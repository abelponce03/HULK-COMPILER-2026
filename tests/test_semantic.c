/*
 * test_semantic.c — Tests unitarios para el análisis semántico
 *
 * Cubre:
 *   - Resolución de nombres (scope): variables definidas/no definidas
 *   - Verificación de tipos: aritmética, booleanos, comparaciones
 *   - Let bindings con y sin anotación de tipo
 *   - Funciones: parámetros, retorno, inferencia
 *   - Tipos: herencia, constructor, self, base, métodos, atributos
 *   - Operadores: concat, as, is, unary
 *   - Control de flujo: if/elif/else, while, for, block
 *   - Desugaring de decoradores
 *   - Programas completos válidos (0 errores)
 *   - Detección de errores semánticos (>0 errores)
 */

#include "test_framework.h"
#include "../hulk_compiler.h"
#include "../hulk_ast/core/hulk_ast.h"
#include "../hulk_ast/builder/hulk_ast_builder.h"
#include "../hulk_ast/semantic/hulk_semantic.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 *  Fixture: compilador compartido
 * ============================================================ */

static HulkCompiler hc;
static int hc_ready = 0;

static void ensure_compiler(void) {
    if (!hc_ready) {
        FILE *saved = stdout;
        stdout = fopen("/dev/null", "w");
        hc_ready = hulk_compiler_init(&hc);
        fclose(stdout);
        stdout = saved;
    }
}

/* Helper: construye AST y ejecuta análisis semántico.
 * Retorna número de errores (0 = OK).
 * Silencia stderr para evitar ruido en consola de tests. */
static int analyze(const char *src) {
    ensure_compiler();
    HulkASTContext ctx;
    hulk_ast_context_init(&ctx);

    /* Silenciar stderr durante el análisis (errores esperados) */
    FILE *saved_err = stderr;
    stderr = fopen("/dev/null", "w");

    HulkNode *ast = hulk_build_ast(&ctx, hc.dfa, src);
    int errors = -1;
    if (ast) {
        errors = hulk_semantic_analyze(&ctx, ast);
    }

    fclose(stderr);
    stderr = saved_err;

    hulk_ast_context_free(&ctx);
    return errors;
}

/* ============================================================
 *  SUITE: Literales y expresiones simples (0 errores)
 * ============================================================ */

TEST(number_literal) {
    ASSERT_EQ(0, analyze("42;"));
}

TEST(string_literal) {
    ASSERT_EQ(0, analyze("\"hello\";"));
}

TEST(bool_literal) {
    ASSERT_EQ(0, analyze("true;"));
}

TEST(arithmetic_valid) {
    ASSERT_EQ(0, analyze("1 + 2 * 3;"));
}

TEST(comparison_valid) {
    ASSERT_EQ(0, analyze("1 < 2;"));
}

TEST(logical_valid) {
    ASSERT_EQ(0, analyze("true && false;"));
}

TEST(unary_minus_valid) {
    ASSERT_EQ(0, analyze("-42;"));
}

TEST(concat_valid) {
    ASSERT_EQ(0, analyze("\"hello\" @ \" world\";"));
}

TEST(concat_with_number) {
    /* @ acepta cualquier tipo — se convierte a String */
    ASSERT_EQ(0, analyze("\"value\" @ 42;"));
}

/* ============================================================
 *  SUITE: Resolución de nombres
 * ============================================================ */

TEST(let_binding_basic) {
    ASSERT_EQ(0, analyze("let x = 5 in x + 1;"));
}

TEST(let_multiple_bindings) {
    ASSERT_EQ(0, analyze("let x = 1, y = 2 in x + y;"));
}

TEST(let_with_type_annotation) {
    ASSERT_EQ(0, analyze("let x: Number = 5 in x * 2;"));
}

TEST(undefined_variable) {
    ASSERT_GT(analyze("x + 1;"), 0);
}

TEST(undefined_in_let_body) {
    ASSERT_GT(analyze("let x = 5 in y;"), 0);
}

TEST(variable_out_of_scope) {
    /* x solo vive dentro del let */
    ASSERT_GT(analyze("let x = 5 in x; x;"), 0);
}

/* ============================================================
 *  SUITE: Verificación de tipos — errores
 * ============================================================ */

TEST(arithmetic_with_string) {
    ASSERT_GT(analyze("\"hello\" + 1;"), 0);
}

TEST(arithmetic_with_bool) {
    ASSERT_GT(analyze("true + 1;"), 0);
}

TEST(comparison_with_string) {
    ASSERT_GT(analyze("\"hello\" < 1;"), 0);
}

TEST(logical_with_number) {
    ASSERT_GT(analyze("1 && 2;"), 0);
}

TEST(unary_minus_string) {
    ASSERT_GT(analyze("-\"hello\";"), 0);
}

TEST(let_type_mismatch) {
    ASSERT_GT(analyze("let x: Number = \"hello\" in x;"), 0);
}

/* ============================================================
 *  SUITE: Funciones
 * ============================================================ */

TEST(function_basic) {
    ASSERT_EQ(0, analyze("function f(x: Number): Number => x * 2; f(5);"));
}

TEST(function_no_annotation) {
    /* Sin anotación de tipo — inferencia */
    ASSERT_EQ(0, analyze("function f(x) => x; f(5);"));
}

TEST(function_wrong_arg_count) {
    ASSERT_GT(analyze("function f(x: Number): Number => x; f(1, 2);"), 0);
}

TEST(function_wrong_arg_type) {
    ASSERT_GT(analyze(
        "function f(x: Number): Number => x; f(\"hello\");"), 0);
}

TEST(function_undefined) {
    ASSERT_GT(analyze("foo(42);"), 0);
}

TEST(function_return_type_mismatch) {
    ASSERT_GT(analyze(
        "function f(): Number => \"hello\"; f();"), 0);
}

TEST(builtin_print) {
    ASSERT_EQ(0, analyze("print(42);"));
}

TEST(builtin_sqrt) {
    ASSERT_EQ(0, analyze("sqrt(16);"));
}

TEST(builtin_wrong_args) {
    ASSERT_GT(analyze("sqrt(\"hello\");"), 0);
}

/* ============================================================
 *  SUITE: Tipos — definición, herencia, miembros
 * ============================================================ */

TEST(type_basic) {
    ASSERT_EQ(0, analyze(
        "type Point(x: Number, y: Number) {}\n"
        "new Point(1, 2);"));
}

TEST(type_with_method) {
    ASSERT_EQ(0, analyze(
        "type Point(x: Number, y: Number) {\n"
        "    getX(): Number => x;\n"
        "}\n"
        "let p = new Point(1, 2) in p.getX();"));
}

TEST(type_with_attribute) {
    ASSERT_EQ(0, analyze(
        "type Counter() {\n"
        "    count: Number = 0;\n"
        "}\n"
        "let c = new Counter() in c.count;"));
}

TEST(type_inheritance) {
    ASSERT_EQ(0, analyze(
        "type Animal(name: String) {}\n"
        "type Dog(name: String) inherits Animal(name) {}\n"
        "new Dog(\"Rex\");"));
}

TEST(type_self_valid) {
    /* Constructor params son accesibles como variables locales,
     * self.xxx requiere que xxx sea atributo explícito */
    ASSERT_EQ(0, analyze(
        "type Box(value: Number) {\n"
        "    val: Number = value;\n"
        "    get(): Number => self.val;\n"
        "}\n"
        "42;"));
}

TEST(self_outside_type) {
    ASSERT_GT(analyze("self;"), 0);
}

TEST(base_outside_type) {
    ASSERT_GT(analyze("base();"), 0);
}

TEST(type_constructor_wrong_args) {
    ASSERT_GT(analyze(
        "type Point(x: Number, y: Number) {}\n"
        "new Point(1);"), 0);
}

TEST(undefined_type_in_new) {
    ASSERT_GT(analyze("new Foo(1);"), 0);
}

TEST(member_undefined) {
    ASSERT_GT(analyze(
        "type Point(x: Number, y: Number) {}\n"
        "let p = new Point(1, 2) in p.z;"), 0);
}

/* ============================================================
 *  SUITE: Control de flujo
 * ============================================================ */

TEST(if_else_valid) {
    ASSERT_EQ(0, analyze("if (true) 1 else 2;"));
}

TEST(if_condition_not_bool) {
    ASSERT_GT(analyze("if (42) 1 else 2;"), 0);
}

TEST(while_valid) {
    ASSERT_EQ(0, analyze("while (true) 42;"));
}

TEST(while_condition_not_bool) {
    ASSERT_GT(analyze("while (42) 1;"), 0);
}

TEST(for_valid) {
    ASSERT_EQ(0, analyze(
        "let items = 42 in for (x in items) print(x);"));
}

TEST(block_valid) {
    ASSERT_EQ(0, analyze("{ 1; 2; 3; };"));
}

/* ============================================================
 *  SUITE: Operadores especiales
 * ============================================================ */

TEST(equality_valid) {
    ASSERT_EQ(0, analyze("1 == 1;"));
}

TEST(is_expr_valid) {
    ASSERT_EQ(0, analyze("42 is Number;"));
}

TEST(as_expr_valid) {
    ASSERT_EQ(0, analyze("42 as Number;"));
}

TEST(is_expr_undefined_type) {
    ASSERT_GT(analyze("42 is Foo;"), 0);
}

TEST(as_expr_undefined_type) {
    ASSERT_GT(analyze("42 as Foo;"), 0);
}

/* ============================================================
 *  SUITE: Asignación
 * ============================================================ */

TEST(assign_valid) {
    ASSERT_EQ(0, analyze(
        "let x = 5 in x := 10;"));
}

TEST(assign_undefined) {
    ASSERT_GT(analyze("x := 10;"), 0);
}

/* ============================================================
 *  SUITE: Desugaring de decoradores
 * ============================================================ */

TEST(decorator_basic) {
    /* decor d function f() desugariza a: function f(); f := d(f);
     * d debe ser una función definida */
    ASSERT_EQ(0, analyze(
        "function logger(f) => f;\n"
        "decor logger\n"
        "function greet() => \"hello\";\n"
        "greet();"));
}

TEST(decorator_multiple) {
    ASSERT_EQ(0, analyze(
        "function d1(f) => f;\n"
        "function d2(f) => f;\n"
        "decor d1, d2\n"
        "function greet() => \"hello\";\n"
        "greet();"));
}

/* ============================================================
 *  SUITE: Programas completos válidos
 * ============================================================ */

TEST(program_fibonacci) {
    ASSERT_EQ(0, analyze(
        "function fib(n: Number): Number =>\n"
        "    if (n <= 1) n\n"
        "    else fib(n - 1) + fib(n - 2);\n"
        "print(fib(10));"));
}

TEST(program_type_hierarchy) {
    ASSERT_EQ(0, analyze(
        "type Shape() {\n"
        "    area(): Number => 0;\n"
        "}\n"
        "type Circle(r: Number) inherits Shape() {\n"
        "    area(): Number => 3.14159 * r * r;\n"
        "}\n"
        "let c = new Circle(5) in print(c.area());"));
}

TEST(program_let_chain) {
    ASSERT_EQ(0, analyze(
        "let x = 1 in\n"
        "let y = x + 1 in\n"
        "let z = y + 1 in\n"
        "z;"));
}

/* ============================================================
 *  main
 * ============================================================ */

int main(void) {
    printf("\n");

    TEST_SUITE("Literales y expresiones simples");
    RUN_TEST(number_literal);
    RUN_TEST(string_literal);
    RUN_TEST(bool_literal);
    RUN_TEST(arithmetic_valid);
    RUN_TEST(comparison_valid);
    RUN_TEST(logical_valid);
    RUN_TEST(unary_minus_valid);
    RUN_TEST(concat_valid);
    RUN_TEST(concat_with_number);

    TEST_SUITE("Resolución de nombres");
    RUN_TEST(let_binding_basic);
    RUN_TEST(let_multiple_bindings);
    RUN_TEST(let_with_type_annotation);
    RUN_TEST(undefined_variable);
    RUN_TEST(undefined_in_let_body);
    RUN_TEST(variable_out_of_scope);

    TEST_SUITE("Verificación de tipos — errores");
    RUN_TEST(arithmetic_with_string);
    RUN_TEST(arithmetic_with_bool);
    RUN_TEST(comparison_with_string);
    RUN_TEST(logical_with_number);
    RUN_TEST(unary_minus_string);
    RUN_TEST(let_type_mismatch);

    TEST_SUITE("Funciones");
    RUN_TEST(function_basic);
    RUN_TEST(function_no_annotation);
    RUN_TEST(function_wrong_arg_count);
    RUN_TEST(function_wrong_arg_type);
    RUN_TEST(function_undefined);
    RUN_TEST(function_return_type_mismatch);
    RUN_TEST(builtin_print);
    RUN_TEST(builtin_sqrt);
    RUN_TEST(builtin_wrong_args);

    TEST_SUITE("Tipos");
    RUN_TEST(type_basic);
    RUN_TEST(type_with_method);
    RUN_TEST(type_with_attribute);
    RUN_TEST(type_inheritance);
    RUN_TEST(type_self_valid);
    RUN_TEST(self_outside_type);
    RUN_TEST(base_outside_type);
    RUN_TEST(type_constructor_wrong_args);
    RUN_TEST(undefined_type_in_new);
    RUN_TEST(member_undefined);

    TEST_SUITE("Control de flujo");
    RUN_TEST(if_else_valid);
    RUN_TEST(if_condition_not_bool);
    RUN_TEST(while_valid);
    RUN_TEST(while_condition_not_bool);
    RUN_TEST(for_valid);
    RUN_TEST(block_valid);

    TEST_SUITE("Operadores especiales");
    RUN_TEST(equality_valid);
    RUN_TEST(is_expr_valid);
    RUN_TEST(as_expr_valid);
    RUN_TEST(is_expr_undefined_type);
    RUN_TEST(as_expr_undefined_type);

    TEST_SUITE("Asignación");
    RUN_TEST(assign_valid);
    RUN_TEST(assign_undefined);

    TEST_SUITE("Desugaring de decoradores");
    RUN_TEST(decorator_basic);
    RUN_TEST(decorator_multiple);

    TEST_SUITE("Programas completos");
    RUN_TEST(program_fibonacci);
    RUN_TEST(program_type_hierarchy);
    RUN_TEST(program_let_chain);

    TEST_REPORT();
    return TEST_EXIT_CODE();
}
