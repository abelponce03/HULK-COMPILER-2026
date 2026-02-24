/*
 * error_handler.h — Patrón Observer para manejo centralizado de errores
 *
 * Reemplaza las llamadas dispersas a fprintf(stderr, ...) por un sistema
 * de logging con niveles y handler configurable.  Por defecto imprime
 * a stderr, pero el consumidor puede instalar su propio callback.
 */

#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include <stdarg.h>

// ============== NIVELES DE SEVERIDAD ==============

typedef enum {
    LOG_INFO,       // Mensajes informativos
    LOG_WARNING,    // Advertencias que no impiden continuar
    LOG_ERROR,      // Errores recuperables
    LOG_FATAL       // Errores que abortan la operación
} LogLevel;

// ============== TIPO DEL HANDLER ==============

// Callback: recibe nivel, módulo origen, formato printf y va_list.
// El consumidor puede redirigir a archivo, GUI, buffer, etc.
typedef void (*ErrorHandlerFn)(LogLevel level, const char *module,
                               const char *fmt, va_list args);

// ============== API ==============

// Instala un handler personalizado.  Si handler==NULL restaura el default.
void error_handler_set(ErrorHandlerFn handler);

// Obtiene el handler actual (nunca NULL).
ErrorHandlerFn error_handler_get(void);

// Función principal de logging (usa el handler instalado).
void compiler_log(LogLevel level, const char *module,
                  const char *fmt, ...);

// Atajos por nivel (delegan en compiler_log)
#define LOG_INFO_MSG(mod, ...)    compiler_log(LOG_INFO,    (mod), __VA_ARGS__)
#define LOG_WARN_MSG(mod, ...)    compiler_log(LOG_WARNING, (mod), __VA_ARGS__)
#define LOG_ERROR_MSG(mod, ...)   compiler_log(LOG_ERROR,   (mod), __VA_ARGS__)
#define LOG_FATAL_MSG(mod, ...)   compiler_log(LOG_FATAL,   (mod), __VA_ARGS__)

#endif /* ERROR_HANDLER_H */
