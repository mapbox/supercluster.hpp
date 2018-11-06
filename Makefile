CFLAGS += -I include --std=c++14 -Wall -Wextra -Werror -O3

export MASON_DIR = $(shell pwd)/.mason
export MASON = $(MASON_DIR)/mason

VARIANT = variant 1.1.5
GEOMETRY = geometry 1.0.0
KDBUSH = kdbush 0.1.3
RAPIDJSON = rapidjson 1.1.0

DEPS = `$(MASON) cflags $(VARIANT)` `$(MASON) cflags $(GEOMETRY)` `$(MASON) cflags $(KDBUSH)`
RAPIDJSON_DEP = `$(MASON) cflags $(RAPIDJSON)`

default:
	make run-test

$(MASON_DIR):
	git submodule update --init $(MASON_DIR)

mason_packages: $(MASON_DIR)
	$(MASON) install $(VARIANT)
	$(MASON) install $(GEOMETRY)
	$(MASON) install $(KDBUSH)
	$(MASON) install $(RAPIDJSON)

build/bench: bench.cpp include/* mason_packages Makefile
	mkdir -p build
	$(CXX) bench.cpp $(CFLAGS) $(DEPS) $(RAPIDJSON_DEP) -o build/bench

build/test: test/test.cpp include/* mason_packages Makefile
	mkdir -p build
	$(CXX) test/test.cpp $(CFLAGS) $(DEPS) $(RAPIDJSON_DEP) -o build/test

run-bench: build/bench
	./build/bench

run-test: build/test
	./build/test

format:
	clang-format include/*.hpp *.cpp test/*.cpp -i

clean:
	rm -rf build
