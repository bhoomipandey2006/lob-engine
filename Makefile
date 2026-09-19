CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iinclude

SRC := src/order_book.cpp src/main.cpp
BIN := lob_bench

SCALING_SRC := src/order_book.cpp src/naive_order_book.cpp src/scaling_bench.cpp
SCALING_BIN := scaling_bench

REPLAY_SRC := src/order_book.cpp src/market_replay.cpp
REPLAY_BIN := market_replay

all: $(BIN) $(SCALING_BIN) $(REPLAY_BIN)

$(BIN): $(SRC) include/order_book.hpp
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

$(SCALING_BIN): $(SCALING_SRC) include/order_book.hpp include/naive_order_book.hpp
	$(CXX) $(CXXFLAGS) $(SCALING_SRC) -o $(SCALING_BIN)

$(REPLAY_BIN): $(REPLAY_SRC) include/order_book.hpp
	$(CXX) $(CXXFLAGS) $(REPLAY_SRC) -o $(REPLAY_BIN)

run: $(BIN)
	./$(BIN) 1000000

run-scaling: $(SCALING_BIN)
	./$(SCALING_BIN)

run-replay: $(REPLAY_BIN)
	./$(REPLAY_BIN) data/NIFTY50_15sec_20260917.csv

clean:
	rm -f $(BIN) $(SCALING_BIN) $(REPLAY_BIN)

.PHONY: all run run-scaling run-replay clean