CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -D_GNU_SOURCE
LDFLAGS = -lfl
TARGET = hulk_compiler

# Directorios
LEXER_DIR = generador_analizadores_lexicos
PARSER_DIR = generador_parser_ll1

# Archivo generado por flex
REGEX_LEXER_C = $(LEXER_DIR)/regex_lexer.c

# Objetos directamente
OBJS = main.o \
       $(LEXER_DIR)/ast.o \
       $(LEXER_DIR)/afd.o \
       $(LEXER_DIR)/lexer.o \
       $(LEXER_DIR)/regex_parser.o \
       $(LEXER_DIR)/regex_lexer.o \
       $(PARSER_DIR)/grammar.o \
       $(PARSER_DIR)/parser.o

# Compilación especial para first_&_follow.c
FF_SRC = "$(PARSER_DIR)/first_&_follow.c"
FF_OBJ = "$(PARSER_DIR)/first_&_follow.o"

# Regla principal
$(TARGET): $(REGEX_LEXER_C) $(OBJS) ff_obj
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(PARSER_DIR)/first_\&_follow.o $(LDFLAGS)

ff_obj:
	$(CC) $(CFLAGS) -c $(FF_SRC) -o $(FF_OBJ)

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