CC = gcc
CFLAGS = -Wall -Wextra -std=c99
TARGET = pstree
SOURCE = pstree.c

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Testing basic functionality:"
	./$(TARGET) | head -10
	@echo "\nTesting with PID display:"
	./$(TARGET) -p | head -5
	@echo "\nTesting with arguments:"
	./$(TARGET) -a | head -5
	@echo "\nTesting ASCII mode:"
	./$(TARGET) -A | head -5
	@echo "\nTesting numeric sort:"
	./$(TARGET) -n -p | head -5

.PHONY: clean test