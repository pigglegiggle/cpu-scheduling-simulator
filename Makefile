CXX := clang++
FLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
PYTHON ?= .venv/bin/python
CORE := src/scheduler.cpp
HEADERS := include/das/scheduler.hpp include/das/indexed_heap.hpp
.PHONY: all test sanitize native setup ui benchmark demo
all: build/core_tests build/benchmark
build/core_tests: tests/core_test.cpp $(CORE) $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) tests/core_test.cpp $(CORE) -o $@
build/benchmark: tests/benchmark.cpp $(CORE) $(HEADERS)
	mkdir -p build
	$(CXX) $(FLAGS) tests/benchmark.cpp $(CORE) -o $@
test: build/core_tests native
	./build/core_tests
	$(PYTHON) -m pytest tests/test_python.py -q
sanitize:
	mkdir -p build
	$(CXX) $(FLAGS) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer tests/core_test.cpp $(CORE) -o build/core_sanitize
	./build/core_sanitize
native: $(CORE) $(HEADERS) bindings/python.cpp
	$(PYTHON) scripts/build_native.py
setup:
	python3 -m venv .venv
	$(PYTHON) -m pip install -r requirements.txt
	$(MAKE) native
ui: native
	$(PYTHON) -m streamlit run web/ui.py --server.address 127.0.0.1 --server.port 8502
demo: native
	$(PYTHON) -m scheduler compare examples/frame_pipeline.json
benchmark: build/benchmark native
	$(PYTHON) scripts/benchmark.py
