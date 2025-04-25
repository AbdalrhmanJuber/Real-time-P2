CC = gcc
CFLAGS = -Wall -g -I/usr/include
LDFLAGS = -lGL -lGLU -lglut -lm

SRC_DIR = src
INC_DIR = include
OBJ_DIR = src
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:.c=.o)
DEPS = $(wildcard $(INC_DIR)/*.h)

TARGET = $(BIN_DIR)/bakery_simulation

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Compilation successful!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) config/bakery_config.txt

clean:
	rm -f $(OBJ_DIR)/*.o $(TARGET)
	@echo "Cleaned up object files and executables"