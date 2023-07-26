set -ex
../../tools/mkmodel/mkmodel -v -o filesystem/ assets/gemstone.glb
../../tools/audioconv64/audioconv64 -o filesystem/ sourceassets/music2.wav
