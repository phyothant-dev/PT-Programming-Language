CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
SRC      := src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp
TARGET   := pt

.PHONY: all clean test install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

test: $(TARGET)
	./$(TARGET) test.pt

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/pt

uninstall:
	rm -f /usr/local/bin/pt
