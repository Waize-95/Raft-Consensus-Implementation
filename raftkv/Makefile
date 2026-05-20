CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

all: raftkv raftkv-cli unit_tests

# Server binary — src/main.cpp includes server.cpp which includes
# the full chain: statemachine.cpp -> storage.cpp -> log.cpp -> command.cpp
raftkv: src/main.cpp src/server.cpp src/rcp.cpp src/statemachine.cpp src/storage.cpp src/log.cpp src/command.cpp
	$(CXX) $(CXXFLAGS) -o raftkv src/main.cpp

# Client binary — cli/main.cpp includes client.cpp
raftkv-cli: cli/main.cpp cli/client.cpp
	$(CXX) $(CXXFLAGS) -o raftkv-cli cli/main.cpp

# Unit tests for serializer, RequestVote / AppendEntries logic, commitment rule
unit_tests: tests/unit_tests.cpp src/rcp.cpp src/log.cpp src/command.cpp
	$(CXX) $(CXXFLAGS) -o unit_tests tests/unit_tests.cpp

# Run the unit tests
test: unit_tests
	./unit_tests

# Run the full six-scenario chaos test (override N: make chaos N=200)
N ?= 1000
chaos: all
	./chaos/chaos_test.sh $(N)

clean:
	rm -f raftkv raftkv-cli unit_tests
	rm -rf d1/ d2/ d3/ d4/ d5/ data/

.PHONY: all clean test chaos
