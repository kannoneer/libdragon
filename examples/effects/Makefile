BUILD_DIR=build
include $(N64_INST)/include/n64.mk

src = gldemo.c
assets_png = $(wildcard assets/*.png)
assets_xm1 = $(wildcard assets/*.xm)

assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite))) \
			  $(addprefix filesystem/,$(notdir $(assets_xm1:%.xm=%.xm64)))

MKSPRITE_FLAGS ?=
CFLAGS += -Wno-unused-variable -Wno-unused-function -O3

all: gldemo.z64

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 -o "$(dir $@)" "$<"

filesystem/%.xm64: assets/%.xm
	@mkdir -p $(dir $@)
	@echo "    [AUDIO] $@"
	@$(N64_AUDIOCONV) $(AUDIOCONV_FLAGS) -o filesystem "$<"

$(BUILD_DIR)/gldemo.dfs: $(assets_conv)
$(BUILD_DIR)/gldemo.elf: $(src:%.c=$(BUILD_DIR)/%.o)

gldemo.z64: N64_ROM_TITLE="GL Demo"
gldemo.z64: $(BUILD_DIR)/gldemo.dfs

clean:
	rm -rf $(BUILD_DIR) gldemo.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
