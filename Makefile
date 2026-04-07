CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -I include
SRCD     = src/server

SERVER_SRCS = $(SRCD)/server.cpp $(SRCD)/parser.cpp \
              $(SRCD)/storage.cpp $(SRCD)/cache.cpp $(SRCD)/executor.cpp

all: flexql-server flexql-client benchmark_exec

flexql-server: $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

flexql-client: src/client/flexql_api.cpp src/client/repl.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@

benchmark_exec: src/client/flexql_api.cpp benchmark/benchmark_flexql.cpp
	$(CXX) $(CXXFLAGS) $^ -o benchmark_exec

clean:
	rm -f flexql-server flexql-client benchmark_exec

clean-data:
	rm -f data/tables/*.dat data/tables/*.schema

.PHONY: all clean clean-data
