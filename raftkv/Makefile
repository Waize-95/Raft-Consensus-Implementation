CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: raftkv raftkv-cli

# Server binary — src/main.cpp includes server.cpp which includes
# the full chain: statemachine.cpp -> storage.cpp -> log.cpp -> command.cpp
raftkv: src/main.cpp src/server.cpp src/statemachine.cpp src/storage.cpp src/log.cpp src/command.cpp
	$(CXX) $(CXXFLAGS) -o raftkv src/main.cpp

# Client binary — cli/main.cpp includes client.cpp
raftkv-cli: cli/main.cpp cli/client.cpp
	$(CXX) $(CXXFLAGS) -o raftkv-cli cli/main.cpp

clean:
	rm -f raftkv raftkv-cli
	rm -rf data/ d1/

.PHONY: all clean
