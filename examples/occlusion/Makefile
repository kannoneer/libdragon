BUILD_DIR=build
include $(N64_INST)/include/n64.mk

common_src = bvh.c occult.c cpumath.c cpu_3d.c profiler.c frustum.c aabb.c
lib_src = lib/microui.c lib/microuiN64.c
demo_src = occdemo.c $(common_src)
tests_src = tests.c $(common_src)

assets_png = $(wildcard assets/*.png)
assets_ttf = $(wildcard assets/*.ttf)

assets_conv = $(addprefix filesystem/,$(notdir $(assets_png:%.png=%.sprite))) \
			  $(addprefix filesystem/,$(notdir $(assets_ttf:%.ttf=%.font64)))

MKSPRITE_FLAGS ?=
N64_C_AND_CXX_FLAGS += -Wno-error=unused-function

all: occdemo.z64

filesystem/%.font64: assets/%.ttf
	@mkdir -p $(dir $@)
	@echo "    [FONT] $@"
	@$(N64_MKFONT) $(MKFONT_FLAGS) -o filesystem "$<"

filesystem/%.sprite: assets/%.png
	@mkdir -p $(dir $@)
	@echo "    [SPRITE] $@"
	@$(N64_MKSPRITE) -f RGBA16 --compress -o "$(dir $@)" "$<"

$(BUILD_DIR)/occdemo.dfs: $(assets_conv)
$(BUILD_DIR)/occtests.dfs: $(assets_conv)
$(BUILD_DIR)/occdemo.elf: $(demo_src:%.c=$(BUILD_DIR)/%.o) $(lib_src:%.c=$(BUILD_DIR)/%.o)
$(BUILD_DIR)/occtests.elf: $(tests_src:%.c=$(BUILD_DIR)/%.o)

filesystem/FerriteCoreDX.font64: MKFONT_FLAGS+=--size 12
filesystem/VCR_OSD_MONO.font64: MKFONT_FLAGS+=--size 9

occdemo.z64: N64_ROM_TITLE="Occlusion Demo"
occdemo.z64: $(BUILD_DIR)/occdemo.dfs

occtests.z64: N64_ROM_TITLE="Occlusion Tests"
occtests.z64: $(BUILD_DIR)/occtests.dfs

run: occdemo.z64
	ares occdemo.z64

clean:
	rm -rf $(BUILD_DIR) filesystem/ occdemo.z64 occtests.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean run
