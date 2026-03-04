CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -D_GNU_SOURCE
LDFLAGS = -lfl
TARGET = hulk_compiler

# Directorios
LEXER_DIR = generador_analizadores_lexicos
PARSER_DIR = generador_parser_ll1
HULK_AST_DIR = hulk_ast
OUTPUT_DIR = output
TEST_DIR = tests

# Archivo generado por flex
REGEX_LEXER_C = $(LEXER_DIR)/regex_lexer.c

# Objetos del proyecto (sin main.o para poder linkear tests)
LIB_OBJS = hulk_tokens.o \
            hulk_compiler.o \
            $(HULK_AST_DIR)/hulk_ast.o \
            $(HULK_AST_DIR)/hulk_ast_printer.o \
            $(HULK_AST_DIR)/hulk_ast_builder.o \
            error_handler.o \
            $(LEXER_DIR)/ast.o \
            $(LEXER_DIR)/afd.o \
            $(LEXER_DIR)/lexer.o \
            $(LEXER_DIR)/regex_parser.o \
            $(LEXER_DIR)/regex_lexer.o \
            $(PARSER_DIR)/grammar.o \
            $(PARSER_DIR)/grammar_regex.o \
            $(PARSER_DIR)/grammar_hulk.o \
            $(PARSER_DIR)/ll1_table.o \
            $(PARSER_DIR)/parser.o \
            $(PARSER_DIR)/first_follow.o

OBJS = main.o $(LIB_OBJS)

# Binarios de tests
TEST_LEXER       = $(TEST_DIR)/test_lexer
TEST_PARSER      = $(TEST_DIR)/test_parser
TEST_AST         = $(TEST_DIR)/test_ast
TEST_HULK_AST    = $(TEST_DIR)/test_hulk_ast
TEST_AST_BUILDER = $(TEST_DIR)/test_ast_builder
TEST_BINS        = $(TEST_LEXER) $(TEST_PARSER) $(TEST_AST) $(TEST_HULK_AST) $(TEST_AST_BUILDER)

# ============== Regla principal ==============
$(TARGET): $(REGEX_LEXER_C) $(OBJS) | $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Crear directorio de salida
$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

# Generar lexer de regex con flex
$(REGEX_LEXER_C): $(LEXER_DIR)/regex_lexer.l
	flex -o $(REGEX_LEXER_C) $(LEXER_DIR)/regex_lexer.l

# Regla genérica para .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ============== Tests unitarios ==============
# Compilar todos los tests
test-build: $(REGEX_LEXER_C) $(LIB_OBJS) $(TEST_BINS)

$(TEST_LEXER): $(TEST_DIR)/test_lexer.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS)

$(TEST_PARSER): $(TEST_DIR)/test_parser.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS)

$(TEST_AST): $(TEST_DIR)/test_ast.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS)

$(TEST_HULK_AST): $(TEST_DIR)/test_hulk_ast.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS)

$(TEST_AST_BUILDER): $(TEST_DIR)/test_ast_builder.c $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_OBJS) $(LDFLAGS)

# Ejecutar todos los tests
test-all: test-build
	@echo ""
	@echo "═══════════════════════════════════════"
	@echo "  Ejecutando todos los tests..."
	@echo "═══════════════════════════════════════"
	@fail=0; \
	for t in $(TEST_BINS); do \
		$$t || fail=1; \
	done; \
	if [ $$fail -eq 1 ]; then \
		echo "\n❌ Algunos tests fallaron"; exit 1; \
	else \
		echo "\n✅ Todos los tests pasaron"; \
	fi

# Ejecutar tests individuales
test-lexer: $(TEST_LEXER)
	./$(TEST_LEXER)

test-parser: $(TEST_PARSER)
	./$(TEST_PARSER)

test-ast: $(TEST_AST)
	./$(TEST_AST)

test-hulk-ast: $(TEST_HULK_AST)
	./$(TEST_HULK_AST)

test-ast-builder: $(TEST_AST_BUILDER)
	./$(TEST_AST_BUILDER)

# ============== Otros targets ==============
# Test rápido (entrada por defecto)
test: $(TARGET)
	./$(TARGET)

# Test con archivo .hulk
test-file: $(TARGET)
	./$(TARGET) test.hulk

# Limpiar
clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(LEXER_DIR)/*.o $(PARSER_DIR)/*.o $(HULK_AST_DIR)/*.o
	rm -f $(REGEX_LEXER_C)
	rm -f *.ll1.cache
	rm -f $(OUTPUT_DIR)/*.csv $(OUTPUT_DIR)/*.dot $(OUTPUT_DIR)/*.png
	rm -f $(TEST_BINS)

# Reconstruir desde cero
rebuild: clean $(TARGET)

.PHONY: clean test test-file rebuild test-build test-all test-lexer test-parser test-ast test-hulk-ast test-ast-builder