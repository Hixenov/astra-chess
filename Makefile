CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O3 -DNDEBUG -Wall -Wextra -pedantic
LDFLAGS  ?= -pthread
ifeq ($(OS),Windows_NT)
TARGET   = astra.exe
else
TARGET   = astra
endif
SRC      = $(wildcard src/*.cpp)
OBJ      = $(SRC:src/%.cpp=build/%.o)

.PHONY: all clean test native static
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

# Fully static binary (Linux / MinGW): runs on machines without matching runtime libraries
static: LDFLAGS += -static
static: clean $(TARGET)

test: $(TARGET)
	./$(TARGET) --selftest
	./tests/uci_test.sh

clean:
	rm -rf build astra astra.exe
