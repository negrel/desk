LDFLAGS := $(shell pkg-config --cflags --libs wayland-client wayland-egl egl glesv2) -lm
CFLAGS += -I$(BUILD_DIR)/protocol

WAYLAND_PROTOCOLS_DIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)

PROTOCOLS := \
	$(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml \
	$(WAYLAND_PROTOCOLS_DIR)/stable/presentation-time/presentation-time.xml

# Generated protocol files
PROTOCOL_HEADS := $(patsubst %.xml,%.h,$(PROTOCOLS))
PROTOCOL_HEADS := $(subst $(WAYLAND_PROTOCOLS_DIR),$(BUILD_DIR)/protocol,$(PROTOCOL_HEADS))
PROTOCOL_SRCS := $(patsubst %.xml,%.c,$(PROTOCOLS))
PROTOCOL_SRCS := $(subst $(WAYLAND_PROTOCOLS_DIR),$(BUILD_DIR)/protocol,$(PROTOCOL_SRCS))

build: ./main.c $(SRC_FILES) $(PROTOCOL_HEADS) $(PROTOCOL_SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) \
		./main.c $(SRC_CFILES) $(PROTOCOL_SRCS) -o $(BUILD_DIR)/volumemon

run:
	$(BUILD_DIR)/volumemon

compile_flags:
	@echo $(CFLAGS) $(LDFLAGS)

$(BUILD_DIR)/protocol/%.h: $(WAYLAND_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner client-header $< $@

$(BUILD_DIR)/protocol/%.c: $(WAYLAND_PROTOCOLS_DIR)/%.xml
	mkdir -p $(dir $@)
	wayland-scanner public-code $< $@

