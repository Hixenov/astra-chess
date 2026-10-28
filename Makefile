CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET = astra

.PHONY: all clean test
all: $(TARGET)
$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<
test: $(TARGET)
	./tests/smoke.sh
clean:
	rm -f $(TARGET)
