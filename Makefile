PROJECT := plzj

LIBS := zlib libpng

include mk/flags.mk

CPPFLAGS += -DHAVE_THREADS -DHAVE_PTHREAD_NAME
CPPFLAGS += -DPLZJ_BUILDING_DLL
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
