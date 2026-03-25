CC      ?= cc
RM      ?= rm -f
MKDIR_P ?= mkdir -p

BUILD   := build
SRC     := src/orangepool.c
INC     := -Iinclude -Isrc
WARN    := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes
STD     := -std=c11
OPT     ?= -O2
DBG     ?= -g3
THREAD  := -pthread
BASE_CFLAGS := $(STD) $(WARN) $(OPT) $(DBG) $(THREAD) $(INC)

TESTS := \
	test_drain \
	test_poison_semantics \
	test_queue_overflow \
	test_immediate_shutdown \
	test_timeout \
	test_blocking_submit \
	test_stats

.PHONY: all clean test demo asan ubsan tsan docs help

all: demo

$(BUILD):
	$(MKDIR_P) $(BUILD)

demo: $(BUILD)
	$(CC) $(BASE_CFLAGS) $(SRC) tests/poc_orangepool_demo.c -o $(BUILD)/poc_orangepool_demo

$(BUILD)/%: tests/%.c $(SRC) include/orangepool.h | $(BUILD)
	$(CC) $(BASE_CFLAGS) $(SRC) $< -o $@

test: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; \
	for t in $(TESTS); do \
		echo "[TEST] $$t"; \
		./$(BUILD)/$$t; \
	done; \
	echo "[OK] all tests passed"

docs:
	doxygen docs/Doxyfile

asan: CFLAGS_EXTRA := -fsanitize=address,undefined -fno-omit-frame-pointer -O1
asan:
	$(MAKE) clean
	$(MAKE) BASE_CFLAGS="$(STD) $(WARN) $(DBG) $(THREAD) $(INC) $(CFLAGS_EXTRA)" test demo

ubsan: CFLAGS_EXTRA := -fsanitize=undefined -fno-omit-frame-pointer -O1
ubsan:
	$(MAKE) clean
	$(MAKE) BASE_CFLAGS="$(STD) $(WARN) $(DBG) $(THREAD) $(INC) $(CFLAGS_EXTRA)" test demo

tsan: CFLAGS_EXTRA := -fsanitize=thread -fno-omit-frame-pointer -O1
tsan:
	$(MAKE) clean
	$(MAKE) BASE_CFLAGS="$(STD) $(WARN) $(DBG) $(THREAD) $(INC) $(CFLAGS_EXTRA)" test demo

clean:
	$(RM) -r $(BUILD)

help:
	@echo "Targets:"
	@echo "  make        - build demo"
	@echo "  make test   - build and run all tests"
	@echo "  make docs   - generate Doxygen HTML"
	@echo "  make asan   - run tests with ASan+UBSan"
	@echo "  make ubsan  - run tests with UBSan"
	@echo "  make tsan   - run tests with TSan"
	@echo "  make clean  - remove build artifacts"
