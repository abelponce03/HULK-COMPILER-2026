/*
 * error_handler.c — Implementación del handler centralizado de errores
 */

#include "error_handler.h"
#include <stdio.h>

// ============== HANDLER POR DEFECTO ==============

static void default_handler(LogLevel level, const char *module,
                            const char *fmt, va_list args) {
    const char *prefix;
    switch (level) {
        case LOG_INFO:    prefix = "INFO";    break;
        case LOG_WARNING: prefix = "WARN";    break;
        case LOG_ERROR:   prefix = "ERROR";   break;
        case LOG_FATAL:   prefix = "FATAL";   break;
        default:          prefix = "???";     break;
    }
    fprintf(stderr, "[%s][%s] ", prefix, module);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

// ============== ESTADO INTERNO ==============

// Único dato mutable del módulo: el handler activo.
// No es reentrante, pero el logging global rara vez necesita serlo.
static ErrorHandlerFn current_handler = default_handler;

// ============== IMPLEMENTACIÓN API ==============

void error_handler_set(ErrorHandlerFn handler) {
    current_handler = handler ? handler : default_handler;
}

ErrorHandlerFn error_handler_get(void) {
    return current_handler;
}

void compiler_log(LogLevel level, const char *module,
                  const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    current_handler(level, module, fmt, args);
    va_end(args);
}
