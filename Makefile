CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude

BUILD    := build
BIN      := $(BUILD)/journey
TEST_BIN := $(BUILD)/test_graph
GEN_BIN  := $(BUILD)/gen_network

CORE_SRC := src/graph.cpp src/cli.cpp src/server.cpp

.PHONY: all run serve test ui-test gen clean

all: $(BIN)

$(BUILD):
	mkdir -p $(BUILD)

$(BIN): src/main.cpp $(CORE_SRC) include/journey.hpp include/stats.hpp include/server.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) src/main.cpp $(CORE_SRC) -o $@

$(TEST_BIN): tests/test_graph.cpp src/graph.cpp include/journey.hpp include/stats.hpp tests/check.hpp | $(BUILD)
	$(CXX) $(CXXFLAGS) tests/test_graph.cpp src/graph.cpp -o $@

$(GEN_BIN): tools/gen_network.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) tools/gen_network.cpp -o $@

run: $(BIN)
	./$(BIN) < samples/example1.txt

serve: $(BIN)
	./$(BIN) --serve

test: $(TEST_BIN)
	./$(TEST_BIN)

ui-test: $(BIN)
	bash tools/ui_smoke.sh

gen: $(GEN_BIN)
	./$(GEN_BIN) > samples/big.txt
	@echo "wrote samples/big.txt"

clean:
	rm -rf $(BUILD)
