PROJECT := plzj

LIBS := zlib libpng

include mk/flags.mk

CPPFLAGS += -DHAVE_PTHREAD_NAME -D_FILE_OFFSET_BITS=64 -DPLZJ_BUILDING_STATIC
CPPFLAGS += -pthread
LDFLAGS += -pthread

SOURCES := $(sort $(wildcard lib/*.c lib/*/*.c src/debug.c src/plzj.c))
OBJS := $(SOURCES:.c=.o)
EXE := $(PROJECT)

.PHONY: all
all: $(EXE)

.PHONY: clean
clean:
	$(RM) $(EXE) $(OBJS) $(PREREQUISITES)

$(EXE): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

include mk/prerequisties.mk
