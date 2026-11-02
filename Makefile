CC ?= gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -I.
LDFLAGS = -lrt
AR = ar
ARFLAGS = rcs

# Source modules for static library libsigfw.a
LIB_SRCS = src/sigreg.c \
           src/selfpipe.c \
           src/childtab.c \
           src/log.c \
           src/statefile.c \
           src/config.c \
           src/shm_hb.c

LIB_OBJS = $(LIB_SRCS:.c=.o)
STATIC_LIB = libsigfw.a

# Executables
BINARIES = sigctld worker sigctl

.PHONY: all debug asan test valgrind clean

all: $(STATIC_LIB) $(BINARIES)

$(STATIC_LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

sigctld: src/sigctld.o $(STATIC_LIB)
	$(CC) $(CFLAGS) src/sigctld.o $(STATIC_LIB) $(LDFLAGS) -o $@

worker: src/worker.o $(STATIC_LIB)
	$(CC) $(CFLAGS) src/worker.o $(STATIC_LIB) $(LDFLAGS) -o $@

sigctl: src/sigctl.o $(STATIC_LIB)
	$(CC) $(CFLAGS) src/sigctl.o $(STATIC_LIB) $(LDFLAGS) -o $@

debug: CFLAGS += -g3 -O0 -DDEBUG
debug: clean all

asan: CFLAGS += -fsanitize=address,undefined -g
asan: clean all

test: all
	@echo "============================================="
	@echo "       RUNNING SIGCTL TEST SUITE             "
	@echo "============================================="
	@mkdir -p tests/logs tests/tmp
	@passed=0; total=0; failed=0; \
	for t in tests/test_t*.sh; do \
		total=$$((total + 1)); \
		if bash "$$t"; then \
			passed=$$((passed + 1)); \
		else \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "---------------------------------------------"; \
	echo "Test Summary: Total: $$total | Passed: $$passed | Failed: $$failed"; \
	echo "---------------------------------------------"; \
	if [ $$failed -ne 0 ]; then \
		echo "ERROR: One or more tests failed!"; \
		exit 1; \
	else \
		echo "SUCCESS: All tests passed with 100% success!"; \
	fi

valgrind: all
	@echo "Running valgrind memory leak check on T12..."
	@bash tests/test_t12.sh

chaos: all
	@echo "Running automated chaos engineering test..."
	@bash tests/chaos.sh

clean:
	rm -f src/*.o *.o $(STATIC_LIB) $(BINARIES)
	rm -rf tests/tmp/ tests/logs/*.log
	rm -f *.pid *.status *.fifo *.log /tmp/sigctl_*
