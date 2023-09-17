#!/bin/bash
set -ex
# cp ~/dev/libdragon-gfx/tools/mksprite/baseline/stone.sprite filesystem/stone_baseline.sprite
# cp ~/dev/libdragon-gfx/tools/mksprite/yuverror/stone.sprite filesystem/stone_yuverror.sprite
# cp ~/dev/libdragon-gfx/tools/mksprite/yuverror2/stone.sprite filesystem/stone_yuverror2.sprite
# cp ~/dev/libdragon-gfx/tools/mksprite/ci4/stone.sprite filesystem/stone_ci4.sprite
# cp ~/dev/libdragon-gfx/tools/mksprite/rgba16/stone.sprite filesystem/stone_rgba16.sprite

for image in stone desert circuit grass castle; do
    for dir in baseline yuverror ci4 rgba16; do
        mkdir -p filesystem/$dir
        cp ~/dev/libdragon-gfx/tools/mksprite/$dir/$image/$image.sprite filesystem/$dir/$image.sprite
    done
done
