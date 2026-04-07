CC      = gcc
CFLAGS  = -Wall -Wextra -O2

SRCS    = compiler/main.c \
          compiler/lexer.c \
          compiler/parser.c \
          compiler/parse_table.c \
          compiler/ast.c \
          compiler/symbol_table.c \
          compiler/optimizer.c \
          compiler/json_output.c

# Detect Windows (MSYS/MinGW/Cygwin or plain cmd)
ifeq ($(OS),Windows_NT)
    TARGET = tinylang_compiler.exe
    RM     = del /Q
else
    TARGET = tinylang_compiler
    RM     = rm -f
endif

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	$(RM) tinylang_compiler tinylang_compiler.exe

.PHONY: all clean
