#!/bin/bash
set -ex
MKMODEL=../../tools/mkmodel/mkmodel
$MKMODEL -o filesystem -v  '/home/user/Documents/blender/rooms/room4.glb'
#$MKMODEL -o filesystem -v  '/home/user/Documents/blender/rooms/icosphere.glb'
