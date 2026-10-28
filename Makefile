CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -pedantic
LDFLAGS  ?= -pthread
TARGET   = astra
SRC      = $(wildcard src/*.cpp)
OBJ      = $(SRC:src/%.cpp=build/%.o)

.PHONY: all clean test native
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp $(wildcard src/*.h) | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

# Faster binary for your own machine (not portable): make native
native: CXXFLAGS += -march=native
native: clean $(TARGET)

test: $(TARGET)
	./$(TARGET) --selftest
	./tests/uci_test.sh

clean:
	rm -rf build $(TARGET)
