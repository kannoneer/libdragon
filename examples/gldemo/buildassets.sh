set -ex
../../tools/mkmodel/mkmodel -v -o filesystem/ assets/gemstone.glb
../../tools/audioconv64/audioconv64 -o filesystem/ sourceassets/20230728_6_neo_occult_wave_blessed1.wav

