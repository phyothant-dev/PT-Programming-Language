CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra
SRC      := src/main.cpp src/lexer.cpp src/parser.cpp src/interpreter.cpp src/http.cpp
TARGET   := pt

.PHONY: all clean test install uninstall windows

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

windows:
	x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o pt.exe $(SRC)

clean:
	rm -f $(TARGET) pt.exe

test: $(TARGET)
	./$(TARGET) test.pt

install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/pt

uninstall:
	rm -f /usr/local/bin/pt
