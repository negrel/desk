CFLAGS := $(G_CFLAGS)
LDFLAGS := $(G_LDFLAGS) $(shell pkg-config --cflags --libs libsystemd)

SRC_FILES := ./main.c

build: $(SRC_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(SRC_FILES) -o $(BUILD_DIR)/powermon

run:
	$(BUILD_DIR)/powermon

compile_flags:
	@echo $(CFLAGS) $(LDFLAGS)
