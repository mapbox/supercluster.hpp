CFLAGS += -I include --std=c++14 -Wall -Wextra -Werror -O3

export MASON_DIR = $(shell pwd)/.mason
export MASON = $(MASON_DIR)/mason

VARIANT = variant 1.1.0
GEOMETRY = geometry 0.3.0
RAPIDJSON = rapidjson 1.0.2

GEOMETRY_DEP = `$(MASON) cflags $(VARIANT)` `$(MASON) cflags $(GEOMETRY)`
RAPIDJSON_DEP = `$(MASON) cflags $(RAPIDJSON)`

default:
	make run-bench

$(MASON_DIR):
	git submodule update --init $(MASON_DIR)

mason_packages: $(MASON_DIR)
	$(MASON) install $(VARIANT)
	$(MASON) install $(GEOMETRY)
	$(MASON) install $(RAPIDJSON)

build/bench: bench.cpp include/* mason_packages Makefile
	mkdir -p build
	$(CXX) bench.cpp $(CFLAGS) $(GEOMETRY_DEP) $(RAPIDJSON_DEP) -o build/bench

run-bench: build/bench
	./build/bench

format:
	clang-format include/*.hpp *.cpp -i

clean:
	rm -rf build
