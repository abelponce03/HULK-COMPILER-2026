/*
 * test_framework.h — Mini framework de testing para C
 *
 * Uso:
 *   TEST(nombre_del_test) {
 *       ASSERT(condicion);
 *       ASSERT_EQ(esperado, actual);
 *       ASSERT_STR_EQ(str_esperado, str_actual);
 *       ASSERT_NOT_NULL(ptr);
 *   }
 *
 *   int main(void) {
 *       RUN_TEST(nombre_del_test);
 *       TEST_REPORT();
 *       return TEST_EXIT_CODE();
 *   }
 *
 * Sin dependencias externas. Compatible con C99.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

// ============== Contadores globales ==============

static int _tf_total   = 0;
static int _tf_passed  = 0;
static int _tf_failed  = 0;
static int _tf_current = 0;  // 1 = test actual pasó hasta ahora

// ============== Colores ANSI ==============

#define _TF_GREEN  "\033[32m"
#define _TF_RED    "\033[31m"
#define _TF_YELLOW "\033[33m"
#define _TF_RESET  "\033[0m"
#define _TF_BOLD   "\033[1m"

// ============== Macros principales ==============

// Define un test como función void
#define TEST(name) static void test_##name(void)

// Ejecuta un test registrado con TEST()
#define RUN_TEST(name) do {                                                \
    _tf_total++;                                                           \
    _tf_current = 1;                                                       \
    printf("  %-50s ", #name);                                             \
    test_##name();                                                         \
    if (_tf_current) {                                                     \
        _tf_passed++;                                                      \
        printf(_TF_GREEN "PASS" _TF_RESET "\n");                          \
    }                                                                      \
} while (0)

// Agrupar tests con un encabezado visual
#define TEST_SUITE(name) \
    printf("\n" _TF_BOLD "━━━ %s ━━━" _TF_RESET "\n", name)

// Imprimir resumen final
#define TEST_REPORT() do {                                                 \
    printf("\n" _TF_BOLD "═══════════════════════════════════════\n");      \
    printf("  Total: %d  |  ", _tf_total);                                 \
    printf(_TF_GREEN "Passed: %d" _TF_RESET "  |  ", _tf_passed);         \
    printf(_TF_RED "Failed: %d" _TF_RESET "\n", _tf_failed);              \
    printf("═══════════════════════════════════════" _TF_RESET "\n");      \
} while (0)

// Retorna 0 si todo pasó, 1 si hubo fallos (para exit code)
#define TEST_EXIT_CODE() (_tf_failed > 0 ? 1 : 0)

// ============== Asserts ==============

#define ASSERT(cond) do {                                                  \
    if (!(cond)) {                                                         \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT(%s)" _TF_RESET "\n", #cond);     \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                   \
    long long _e = (long long)(expected);                                  \
    long long _a = (long long)(actual);                                    \
    if (_e != _a) {                                                        \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_EQ(%s, %s)" _TF_RESET "\n",     \
               #expected, #actual);                                        \
        printf("      expected: %lld\n", _e);                              \
        printf("      actual:   %lld\n", _a);                              \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_NEQ(not_expected, actual) do {                              \
    long long _ne = (long long)(not_expected);                             \
    long long _a  = (long long)(actual);                                   \
    if (_ne == _a) {                                                       \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_NEQ(%s, %s)" _TF_RESET "\n",    \
               #not_expected, #actual);                                    \
        printf("      both are: %lld\n", _a);                             \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_STR_EQ(expected, actual) do {                               \
    const char *_e = (expected);                                           \
    const char *_a = (actual);                                             \
    if (!_e || !_a || strcmp(_e, _a) != 0) {                               \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_STR_EQ(%s, %s)" _TF_RESET "\n", \
               #expected, #actual);                                        \
        printf("      expected: \"%s\"\n", _e ? _e : "(null)");           \
        printf("      actual:   \"%s\"\n", _a ? _a : "(null)");           \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_NOT_NULL(ptr) do {                                          \
    if ((ptr) == NULL) {                                                   \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_NOT_NULL(%s)" _TF_RESET "\n",   \
               #ptr);                                                      \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_NULL(ptr) do {                                              \
    if ((ptr) != NULL) {                                                   \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_NULL(%s)" _TF_RESET "\n",       \
               #ptr);                                                      \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#define ASSERT_GT(a, b) do {                                               \
    long long _a = (long long)(a);                                         \
    long long _b = (long long)(b);                                         \
    if (!(_a > _b)) {                                                      \
        printf(_TF_RED "FAIL" _TF_RESET "\n");                            \
        printf("    " _TF_RED "✗ ASSERT_GT(%s, %s)" _TF_RESET "\n",     \
               #a, #b);                                                    \
        printf("      %lld is NOT > %lld\n", _a, _b);                     \
        printf("      at %s:%d\n", __FILE__, __LINE__);                    \
        _tf_current = 0;                                                   \
        _tf_failed++;                                                      \
        return;                                                            \
    }                                                                      \
} while (0)

#endif /* TEST_FRAMEWORK_H */
