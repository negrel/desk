export PROJECT_DIR := $(shell git rev-parse --show-toplevel)
export PREFIX ?= /usr/local
export BUILD_DIR ?= $(PROJECT_DIR)/build
export SRC_DIR ?= $(PROJECT_DIR)/src
export LIB_DIR ?= $(PROJECT_DIR)/lib

# We use the G_ prefix as some makefile under src/ adds more flags to CFLAGS
# and calls this makefile recursively.
export G_CFLAGS ?= -Wall -Wextra -Werror
G_CFLAGS += -I$(SRC_DIR) -I$(LIB_DIR)
export G_LDFLAGS :=

export CC := clang
export AR := ar

ifeq ($(DEBUG), 1)
	G_CFLAGS += -g
else
	G_CFLAGS += -O2
endif

.PHONY: all
all: clean build/bin/powermon build/bin/usound

build/%: $(BUILD_DIR)/%
	@true

.PHONY: clean
clean:
	-rm -rf $(BUILD_DIR)

.PHONY: format
format:
	find $(PROJECT_DIR)/src \
		\( -name '*.c' -or -name '*.h' \) -exec clang-format -i {} \;

.PHONY: compile_flags.txt
compile_flags.txt:
	@grep compile_flags -R $(SRC_DIR) -l \
		| xargs -I{} sh -c 'make -s -C $$(dirname {}) compile_flags' \
		| tr ' ' '\n' | sort | uniq \
		> $(PROJECT_DIR)/compile_flags.txt

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: $(BUILD_DIR)
	$(MAKE) -C $(PROJECT_DIR)/src/$* build

run/%:
	$(MAKE) -C $(PROJECT_DIR)/src/$* run
