CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -D_GNU_SOURCE
LDFLAGS = -lfl
TARGET = hulk_compiler

# Directorios
LEXER_DIR = generador_analizadores_lexicos
PARSER_DIR = generador_parser_ll1

# Archivo generado por flex
REGEX_LEXER_C = $(LEXER_DIR)/regex_lexer.c

# Objetos
OBJS = main.o \
       hulk_tokens.o \
       hulk_compiler.o \
       $(LEXER_DIR)/ast.o \
       $(LEXER_DIR)/afd.o \
       $(LEXER_DIR)/lexer.o \
       $(LEXER_DIR)/regex_parser.o \
       $(LEXER_DIR)/regex_lexer.o \
       $(PARSER_DIR)/grammar.o \
       $(PARSER_DIR)/parser.o \
       $(PARSER_DIR)/first_follow.o

# Regla principal
$(TARGET): $(REGEX_LEXER_C) $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Generar lexer de regex con flex
$(REGEX_LEXER_C): $(LEXER_DIR)/regex_lexer.l
	flex -o $(REGEX_LEXER_C) $(LEXER_DIR)/regex_lexer.l

# Regla genérica para .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar
clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(LEXER_DIR)/*.o $(PARSER_DIR)/*.o
	rm -f $(REGEX_LEXER_C)
	rm -f *.ll1.cache

# Test rápido
test: $(TARGET)
	./$(TARGET)

# Test con archivo
test-file: $(TARGET)
	./$(TARGET) test.hulk

# Reconstruir todo
rebuild: clean $(TARGET)

.PHONY: clean test test-file rebuild ff_obj