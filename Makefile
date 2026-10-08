BUILD_DIR ?= build
all:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j2
clean:
	rm -rf $(BUILD_DIR)
.PHONY: all clean
