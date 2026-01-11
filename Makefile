cflags = -g -I/usr/local/include/
ldflags = -lm
cc = gcc

src = $(shell find src/ -name '*.c' -not -name '*main.c')
obj = $(src:.c=.o)

all: server client

server: $(obj) src/server-main.o
	$(cc) $(cflags) $(ldflags) -o $@ $^

client: $(obj) src/client-main.o
	$(cc) $(cflags) $(ldflags) -o $@ $^

test: $(tests_bin)
	@echo
	@echo "Run tests:"
	@scripts/runall.sh $^

$(obj):%.o:%.c
	$(cc) -c $(cflags) $< -MD -MF $@.d -o $@

src/server-main.o:src/server-main.c
	$(cc) -c $(cflags) $< -MD -MF $@.d -o $@

src/client-main.o:src/client-main.c
	$(cc) -c $(cflags) $< -MD -MF $@.d -o $@

clean:
	-rm $(shell find tests/ -name '*.bin')
	-rm $(shell find . -name '*.o' -or -name '*.d')
	-rm client server

fmt:
	sh scripts/formats.sh

DEPS := $(shell find . -name *.d)
ifneq ($(DEPS),)
include $(DEPS)
endif
