export PROJECT_DIR ?= $(shell git rev-parse --show-toplevel)
export PREFIX ?= /usr/local
export BUILD_DIR ?= $(PROJECT_DIR)/build
export SRC_DIR ?= $(PROJECT_DIR)/src
export LIB_DIR ?= $(PROJECT_DIR)/lib

export CC := clang
export CFLAGS ?= -Wall -Wextra -Werror
CFLAGS += -I$(SRC_DIR) -I$(LIB_DIR)

ifeq ($(DEBUG), 1)
	CFLAGS += -g
else
	CFLAGS += -O2
endif

export SRC_CFILES := $(shell find $(PROJECT_DIR)/src -maxdepth 1 -type f -name '*.c')
export SRC_HFILES := $(shell find $(PROJECT_DIR)/src -maxdepth 1 -type f -name '*.h')
export SRC_FILES := $(SRC_CFILES) $(SRC_HFILES)

.PHONY: all
all: clean build/daemon/powermon install

.PHONY: install
install:
	mkdir -p $(PREFIX)/bin
	cp $(BUILD_DIR)/* $(PREFIX)/bin/

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
