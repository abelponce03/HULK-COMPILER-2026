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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


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
        LLVMRelocPIC,           /* PIC para que el .o linkee con cc PIE */
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

    /* Link: cc -o output output.o -lm (sin system() para evitar injection) */
    int ret = 1;
    {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            remove(obj_path);
            return 1;
        }
        if (pid == 0) {
            execlp("cc", "cc", "-o", out_file, obj_path, "-lm", (char*)NULL);
            perror("exec cc");
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        ret = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    /* Limpiar .o temporal */
    remove(obj_path);

    return ret;
}
