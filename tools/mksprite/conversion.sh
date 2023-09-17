#!/bin/bash
set -ex

for image in stone desert circuit grass castle; do
    mkdir -p yuverror/$image
    pushd yuverror/$image
    ../../mksprite -v --format IHQ --debug ../../$image.png
    popd
    mkdir -p baseline/$image
    pushd baseline/$image
    ../../mksprite_4483de -v --format IHQ --debug ../../$image.png
    popd

    mkdir -p rgba16/$image
    pushd rgba16/$image
    ../../mksprite -v --format rgba16 --debug ../../$image.png
    popd

    mkdir -p ci4/$image
    pushd ci4/$image
    ../../mksprite -v --format CI4 --debug ../../$image.png
    popd

done