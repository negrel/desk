CFLAGS := $(G_CFLAGS)
LDFLAGS := $(G_LDFLAGS) $(shell pkg-config --cflags --libs libpipewire-0.3 libsystemd) -lm

SRC_FILES := ./main.c

build: $(SRC_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(SRC_FILES) -o $(BUILD_DIR)/usound

compile_flags:
	@echo $(CFLAGS) $(LDFLAGS)

