CFLAGS += -I include --std=c++14 -Wall -Wextra -Werror -O3

export MASON_DIR = $(shell pwd)/.mason
export MASON = $(MASON_DIR)/mason

GEOMETRY_DEP = `$(MASON) cflags variant 1.1.0` `$(MASON) cflags geometry 0.2.0`
RAPIDJSON_DEP = `$(MASON) cflags rapidjson 1.0.2`

default:
	make run-bench

$(MASON_DIR):
	git submodule update --init $(MASON_DIR)

mason_packages: $(MASON_DIR)
	$(MASON) install variant 1.1.0
	$(MASON) install rapidjson 1.0.2
	$(MASON) install geometry 0.2.0

build/bench: bench.cpp include/* mason_packages Makefile
	mkdir -p build
	$(CXX) bench.cpp $(CFLAGS) $(GEOMETRY_DEP) $(RAPIDJSON_DEP) -o build/bench

run-bench: build/bench
	./build/bench

format:
	clang-format include/*.hpp *.cpp -i

clean:
	rm -rf build
