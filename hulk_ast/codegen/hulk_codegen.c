/*
 * hulk_codegen.c — Punto de entrada del generador de código LLVM IR
 *
 * Implementa hulk_codegen() y hulk_codegen_to_executable():
 *   1. Crea contexto LLVM (Context, Module, Builder)
 *   2. Declara funciones C del runtime (printf, malloc, math, etc.)
 *   3. Define helpers del runtime HULK (print, concat, conversiones)
 *   4. Llama a cg_emit_program() para emitir el programa completo
 *   5. Verifica el módulo con LLVMVerifyModule
 *   6. Escribe el IR a archivo .ll (o compila a ejecutable)
 *
 * SRP: Solo orquestación de la pipeline de codegen.
 */

#include "hulk_codegen_internal.h"

/* ============================================================
 *  Declarar funciones externas del runtime C
 * ============================================================ */

void cg_declare_runtime(CodegenContext *c) {
    /*
     * Cada función de C se declara como extern en el módulo LLVM.
     * Cuando se enlace con libc/libm, se resolverán automáticamente.
     */

    /* int printf(const char *fmt, ...) */
    {
        LLVMTypeRef params[] = { c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i32, params, 1, 1);
        c->fn_printf = LLVMAddFunction(c->module, "printf", ft);
    }
    /* int snprintf(char *buf, size_t n, const char *fmt, ...) */
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMTypeRef params[] = { c->t_i8ptr, i64, c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i32, params, 3, 1);
        c->fn_snprintf = LLVMAddFunction(c->module, "snprintf", ft);
    }
    /* size_t strlen(const char *) */
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMTypeRef params[] = { c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(i64, params, 1, 0);
        c->fn_strlen = LLVMAddFunction(c->module, "strlen", ft);
    }
    /* char *strcpy(char *dst, const char *src) */
    {
        LLVMTypeRef params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 2, 0);
        c->fn_strcpy = LLVMAddFunction(c->module, "strcpy", ft);
    }
    /* char *strcat(char *dst, const char *src) */
    {
        LLVMTypeRef params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 2, 0);
        c->fn_strcat = LLVMAddFunction(c->module, "strcat", ft);
    }
    /* void *malloc(size_t) */
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMTypeRef params[] = { i64 };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
        c->fn_malloc = LLVMAddFunction(c->module, "malloc", ft);
    }
    /* double sqrt(double) */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_sqrt = LLVMAddFunction(c->module, "sqrt", ft);
    }
    /* double sin(double) */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_sin = LLVMAddFunction(c->module, "sin", ft);
    }
    /* double cos(double) */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_cos = LLVMAddFunction(c->module, "cos", ft);
    }
    /* double exp(double) */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_exp = LLVMAddFunction(c->module, "exp", ft);
    }
    /* double log(double) */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_log = LLVMAddFunction(c->module, "log", ft);
    }
    /* double pow(double, double) */
    {
        LLVMTypeRef params[] = { c->t_double, c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 2, 0);
        c->fn_pow = LLVMAddFunction(c->module, "pow", ft);
    }
    /* double fmod(double, double) */
    {
        LLVMTypeRef params[] = { c->t_double, c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 2, 0);
        c->fn_fmod = LLVMAddFunction(c->module, "fmod", ft);
    }
    /* int rand(void) */
    {
        LLVMTypeRef ft = LLVMFunctionType(c->t_i32, NULL, 0, 0);
        c->fn_rand = LLVMAddFunction(c->module, "rand", ft);
    }
    /* void srand(unsigned int) */
    {
        LLVMTypeRef params[] = { c->t_i32 };
        LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
        c->fn_srand = LLVMAddFunction(c->module, "srand", ft);
    }
    /* time_t time(time_t *) */
    {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);
        LLVMTypeRef params[] = { c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(i64, params, 1, 0);
        c->fn_time = LLVMAddFunction(c->module, "time", ft);
    }
    /* double atof(const char *) */
    {
        LLVMTypeRef params[] = { c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_double, params, 1, 0);
        c->fn_atof = LLVMAddFunction(c->module, "atof", ft);
    }

    /* ---- Registrar funciones matemáticas built-in en scope global ---- */
    {
        LLVMTypeRef p1[] = { c->t_double };
        LLVMTypeRef ft1 = LLVMFunctionType(c->t_double, p1, 1, 0);
        cg_define_in(c, c->global, "sqrt", c->fn_sqrt, ft1, 1);
        cg_define_in(c, c->global, "sin",  c->fn_sin,  ft1, 1);
        cg_define_in(c, c->global, "cos",  c->fn_cos,  ft1, 1);
        cg_define_in(c, c->global, "exp",  c->fn_exp,  ft1, 1);
        cg_define_in(c, c->global, "log",  c->fn_log,  ft1, 1);

        LLVMTypeRef p2[] = { c->t_double, c->t_double };
        LLVMTypeRef ft2 = LLVMFunctionType(c->t_double, p2, 2, 0);
        cg_define_in(c, c->global, "pow",  c->fn_pow,  ft2, 1);
    }
}

/* ============================================================
 *  Definir helpers del runtime HULK
 *
 *  Estas son funciones auxiliares emitidas en IR:
 *    hulk_print(double)     → imprime número con printf
 *    hulk_concat(i8*, i8*)  → concat strings con malloc+strcpy+strcat
 *    hulk_concat_ws(i8*,i8*)→ concat con espacio intermedio
 *    hulk_num_to_str(double)→ convierte número a string
 *    hulk_bool_to_str(i1)   → retorna "true" o "false"
 * ============================================================ */

void cg_define_runtime_helpers(CodegenContext *c) {

    LLVMTypeRef i64 = LLVMInt64TypeInContext(c->llvm_ctx);

    /* ---- hulk_print: void hulk_print(double val) ---- */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_void, params, 1, 0);
        LLVMValueRef fn = LLVMAddFunction(c->module, "hulk_print", ft);
        c->fn_hulk_print = fn;

        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, bb);

        /* Comprobar si es entero: trunc(val) == val */
        LLVMValueRef val = LLVMGetParam(fn, 0);
        /* Simplificación: siempre imprimir con %g para formato limpio */
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(c->builder, "%g\n", "pfmt");
        LLVMValueRef args[] = { fmt, val };
        LLVMTypeRef printf_params[] = { c->t_i8ptr };
        LLVMTypeRef printf_ft = LLVMFunctionType(c->t_i32, printf_params, 1, 1);
        LLVMBuildCall2(c->builder, printf_ft, c->fn_printf, args, 2, "");
        LLVMBuildRetVoid(c->builder);

        cg_define_in(c, c->global, "print", fn, ft, 1);
    }

    /* ---- hulk_num_to_str: i8* hulk_num_to_str(double val) ---- */
    {
        LLVMTypeRef params[] = { c->t_double };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
        LLVMValueRef fn = LLVMAddFunction(c->module, "hulk_num_to_str", ft);
        c->fn_hulk_num_to_str = fn;

        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, bb);

        /* malloc(32) */
        LLVMValueRef buf_size = LLVMConstInt(i64, 32, 0);
        LLVMTypeRef malloc_params[] = { i64 };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
        LLVMValueRef buf = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &buf_size, 1, "buf");

        /* snprintf(buf, 32, "%g", val) */
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(c->builder, "%g", "nfmt");
        LLVMValueRef val = LLVMGetParam(fn, 0);
        LLVMValueRef sn_args[] = { buf, buf_size, fmt, val };
        LLVMTypeRef sn_params[] = { c->t_i8ptr, i64, c->t_i8ptr };
        LLVMTypeRef sn_ft = LLVMFunctionType(c->t_i32, sn_params, 3, 1);
        LLVMBuildCall2(c->builder, sn_ft, c->fn_snprintf, sn_args, 4, "");
        LLVMBuildRet(c->builder, buf);
    }

    /* ---- hulk_bool_to_str: i8* hulk_bool_to_str(i1 val) ---- */
    {
        LLVMTypeRef params[] = { c->t_bool };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 1, 0);
        LLVMValueRef fn = LLVMAddFunction(c->module, "hulk_bool_to_str", ft);
        c->fn_hulk_bool_to_str = fn;

        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, bb);

        LLVMValueRef val = LLVMGetParam(fn, 0);
        LLVMValueRef str_true  = LLVMBuildGlobalStringPtr(c->builder, "true", "strue");
        LLVMValueRef str_false = LLVMBuildGlobalStringPtr(c->builder, "false", "sfalse");
        LLVMValueRef result = LLVMBuildSelect(c->builder, val,
                                               str_true, str_false, "bstr");
        LLVMBuildRet(c->builder, result);
    }

    /* ---- hulk_concat: i8* hulk_concat(i8* a, i8* b) ---- */
    {
        LLVMTypeRef params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 2, 0);
        LLVMValueRef fn = LLVMAddFunction(c->module, "hulk_concat", ft);
        c->fn_hulk_concat = fn;

        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, bb);

        LLVMValueRef a = LLVMGetParam(fn, 0);
        LLVMValueRef b = LLVMGetParam(fn, 1);

        /* len_a = strlen(a), len_b = strlen(b) */
        LLVMTypeRef strlen_params[] = { c->t_i8ptr };
        LLVMTypeRef strlen_ft = LLVMFunctionType(i64, strlen_params, 1, 0);
        LLVMValueRef len_a = LLVMBuildCall2(c->builder, strlen_ft,
                                             c->fn_strlen, &a, 1, "la");
        LLVMValueRef len_b = LLVMBuildCall2(c->builder, strlen_ft,
                                             c->fn_strlen, &b, 1, "lb");

        /* total = len_a + len_b + 1 */
        LLVMValueRef total = LLVMBuildAdd(c->builder, len_a, len_b, "sum");
        total = LLVMBuildAdd(c->builder, total,
                             LLVMConstInt(i64, 1, 0), "total");

        /* buf = malloc(total) */
        LLVMTypeRef malloc_params[] = { i64 };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
        LLVMValueRef buf = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &total, 1, "buf");

        /* strcpy(buf, a); strcat(buf, b); */
        LLVMTypeRef str2_params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef str2_ft = LLVMFunctionType(c->t_i8ptr, str2_params, 2, 0);
        LLVMValueRef cpy_args[] = { buf, a };
        LLVMBuildCall2(c->builder, str2_ft, c->fn_strcpy, cpy_args, 2, "");
        LLVMValueRef cat_args[] = { buf, b };
        LLVMBuildCall2(c->builder, str2_ft, c->fn_strcat, cat_args, 2, "");

        LLVMBuildRet(c->builder, buf);
    }

    /* ---- hulk_concat_ws: i8* hulk_concat_ws(i8* a, i8* b) ---- */
    /*     Concat con espacio: a + " " + b */
    {
        LLVMTypeRef params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef ft = LLVMFunctionType(c->t_i8ptr, params, 2, 0);
        LLVMValueRef fn = LLVMAddFunction(c->module, "hulk_concat_ws", ft);
        c->fn_hulk_concat_ws = fn;

        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(
            c->llvm_ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(c->builder, bb);

        LLVMValueRef a = LLVMGetParam(fn, 0);
        LLVMValueRef b = LLVMGetParam(fn, 1);

        LLVMTypeRef strlen_params[] = { c->t_i8ptr };
        LLVMTypeRef strlen_ft = LLVMFunctionType(i64, strlen_params, 1, 0);
        LLVMValueRef len_a = LLVMBuildCall2(c->builder, strlen_ft,
                                             c->fn_strlen, &a, 1, "la");
        LLVMValueRef len_b = LLVMBuildCall2(c->builder, strlen_ft,
                                             c->fn_strlen, &b, 1, "lb");

        /* total = len_a + len_b + 2 (espacio + nulo) */
        LLVMValueRef total = LLVMBuildAdd(c->builder, len_a, len_b, "sum");
        total = LLVMBuildAdd(c->builder, total,
                             LLVMConstInt(i64, 2, 0), "total");

        LLVMTypeRef malloc_params[] = { i64 };
        LLVMTypeRef malloc_ft = LLVMFunctionType(c->t_i8ptr, malloc_params, 1, 0);
        LLVMValueRef buf = LLVMBuildCall2(c->builder, malloc_ft,
                                           c->fn_malloc, &total, 1, "buf");

        LLVMTypeRef str2_params[] = { c->t_i8ptr, c->t_i8ptr };
        LLVMTypeRef str2_ft = LLVMFunctionType(c->t_i8ptr, str2_params, 2, 0);
        LLVMValueRef cpy_args[] = { buf, a };
        LLVMBuildCall2(c->builder, str2_ft, c->fn_strcpy, cpy_args, 2, "");
        LLVMValueRef space = LLVMBuildGlobalStringPtr(c->builder, " ", "sp");
        LLVMValueRef cat1_args[] = { buf, space };
        LLVMBuildCall2(c->builder, str2_ft, c->fn_strcat, cat1_args, 2, "");
        LLVMValueRef cat2_args[] = { buf, b };
        LLVMBuildCall2(c->builder, str2_ft, c->fn_strcat, cat2_args, 2, "");

        LLVMBuildRet(c->builder, buf);
    }
}

/* ============================================================
 *  hulk_codegen — API pública: generar archivo .ll
 * ============================================================ */

int hulk_codegen(HulkNode *program, const char *out_file) {
    CodegenContext c;
    memset(&c, 0, sizeof(c));

    /* Crear contexto LLVM */
    c.llvm_ctx = LLVMContextCreate();
    c.module   = LLVMModuleCreateWithNameInContext("hulk_module", c.llvm_ctx);
    c.builder  = LLVMCreateBuilderInContext(c.llvm_ctx);

    /* Inicializar tipos básicos, scopes */
    cg_types_init(&c);
    c.global  = cg_scope_create(&c, NULL);
    c.current = c.global;

    /* Declarar runtime */
    cg_declare_runtime(&c);
    cg_define_runtime_helpers(&c);

    /* Emitir programa */
    cg_emit_program(&c, program);

    /* Verificar módulo */
    char *error_msg = NULL;
    if (LLVMVerifyModule(c.module, LLVMReturnStatusAction, &error_msg)) {
        fprintf(stderr, "[CODEGEN ERROR] LLVM module verification failed:\n%s\n",
                error_msg);
        LLVMDisposeMessage(error_msg);

        /* Aún así, escribir el IR para debugging */
        if (out_file) {
            char *ir = LLVMPrintModuleToString(c.module);
            FILE *f = fopen(out_file, "w");
            if (f) { fputs(ir, f); fclose(f); }
            LLVMDisposeMessage(ir);
        }

        int errors = c.error_count + 1;
        cg_context_free(&c);
        return errors;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    /* Escribir IR */
    if (out_file) {
        char *ir = LLVMPrintModuleToString(c.module);
        FILE *f = fopen(out_file, "w");
        if (f) {
            fputs(ir, f);
            fclose(f);
        } else {
            fprintf(stderr, "[CODEGEN ERROR] No se pudo abrir '%s'\n", out_file);
        }
        LLVMDisposeMessage(ir);
    } else {
        /* Imprimir a stdout */
        char *ir = LLVMPrintModuleToString(c.module);
        printf("%s", ir);
        LLVMDisposeMessage(ir);
    }

    int errors = c.error_count;
    cg_context_free(&c);
    return errors;
}

/* ============================================================
 *  hulk_codegen_to_executable — generar ejecutable nativo
 * ============================================================ */

int hulk_codegen_to_executable(HulkNode *program, const char *out_file) {
    CodegenContext c;
    memset(&c, 0, sizeof(c));

    c.llvm_ctx = LLVMContextCreate();
    c.module   = LLVMModuleCreateWithNameInContext("hulk_module", c.llvm_ctx);
    c.builder  = LLVMCreateBuilderInContext(c.llvm_ctx);

    cg_types_init(&c);
    c.global  = cg_scope_create(&c, NULL);
    c.current = c.global;

    cg_declare_runtime(&c);
    cg_define_runtime_helpers(&c);
    cg_emit_program(&c, program);

    /* Verificar */
    char *error_msg = NULL;
    if (LLVMVerifyModule(c.module, LLVMReturnStatusAction, &error_msg)) {
        fprintf(stderr, "[CODEGEN ERROR] Module verification failed:\n%s\n",
                error_msg);
        LLVMDisposeMessage(error_msg);
        cg_context_free(&c);
        return 1;
    }
    if (error_msg) LLVMDisposeMessage(error_msg);

    /* Inicializar targets */
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    /* Obtener target triple */
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(c.module, triple);

    LLVMTargetRef target;
    char *err = NULL;
    if (LLVMGetTargetFromTriple(triple, &target, &err)) {
        fprintf(stderr, "[CODEGEN ERROR] Target not found: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeMessage(triple);
        cg_context_free(&c);
        return 1;
    }
    if (err) LLVMDisposeMessage(err);

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, "generic", "",
        LLVMCodeGenLevelDefault,
        LLVMRelocDefault,
        LLVMCodeModelDefault);
    LLVMDisposeMessage(triple);

    /* Emitir .o temporal */
    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path), "%s.o", out_file);

    err = NULL;
    if (LLVMTargetMachineEmitToFile(tm, c.module, obj_path,
                                     LLVMObjectFile, &err)) {
        fprintf(stderr, "[CODEGEN ERROR] Emit object failed: %s\n", err);
        LLVMDisposeMessage(err);
        LLVMDisposeTargetMachine(tm);
        cg_context_free(&c);
        return 1;
    }
    if (err) LLVMDisposeMessage(err);

    LLVMDisposeTargetMachine(tm);
    cg_context_free(&c);

    /* Link: cc -o output output.o -lm */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cc -o %s %s -lm", out_file, obj_path);
    int ret = system(cmd);

    /* Limpiar .o temporal */
    remove(obj_path);

    return ret;
}
