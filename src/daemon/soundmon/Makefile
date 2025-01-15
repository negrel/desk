LDFLAGS := $(shell pkg-config --cflags --libs libpipewire-0.3 libsystemd) -lm

SRC_FILES := ./main.c $(SRC_DIR)/log.c $(SRC_DIR)/xsnprintf.c $(SRC_DIR)/debug.c

build: $(SRC_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		$(SRC_FILES) -o $(BUILD_DIR)/soundmon

run:
	$(BUILD_DIR)/soundmon

compile_flags:
	@echo $(CFLAGS) $(LDFLAGS)

$(BUILD_DIR)/protocol/wlr/%.h: $(WLR_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner client-header $< $@

$(BUILD_DIR)/protocol/%.h: $(WAYLAND_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner client-header $< $@

$(BUILD_DIR)/protocol/wlr/%.c: $(WLR_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner public-code $< $@

$(BUILD_DIR)/protocol/%.c: $(WAYLAND_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner public-code $< $@

