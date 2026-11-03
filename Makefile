CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -O2 -g
BUILD   := build

all: $(BUILD)/tsrun

$(BUILD)/tsrun: src/tsrun.c | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

test: all
	TSRUN=$(BUILD)/tsrun bash tests/test_m1.sh

# run the tests under valgrind-free sanitizers while developing
asan: CFLAGS := -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined
asan: clean all test

clean:
	rm -rf $(BUILD)

.PHONY: all test asan clean
