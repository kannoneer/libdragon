BUILD_DIR=build
include $(N64_INST)/include/n64.mk

src = occdemo.c
assets_png = $(wildcard assets/*.png)

assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite)))

MKSPRITE_FLAGS ?=

all: occdemo.z64

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 --compress -o "$(dir $@)" "$<"

$(BUILD_DIR)/occdemo.dfs: $(assets_conv)
$(BUILD_DIR)/occdemo.elf: $(src:%.c=$(BUILD_DIR)/%.o)

occdemo.z64: N64_ROM_TITLE="Occlusion Demo"
occdemo.z64: $(BUILD_DIR)/occdemo.dfs

clean:
	rm -rf $(BUILD_DIR) filesystem/ occdemo.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
