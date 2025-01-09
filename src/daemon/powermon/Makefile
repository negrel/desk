LDFLAGS := $(shell pkg-config --cflags --libs libsystemd)

build: ./main.c $(SRC_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		./main.c $(SRC_CFILES) -o $(BUILD_DIR)/powermon

run:
	$(BUILD_DIR)/powermon

compile_flags:
	@echo $(CFLAGS) $(LDFLAGS)
