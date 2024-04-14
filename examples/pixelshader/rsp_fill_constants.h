#ifndef RSP_FILLER_CONSTANTS_H
#define RSP_FILLER_CONSTANTS_H
#define ASSERT_FAIL   0x0101

enum {
    // Overlay commands. This must match the command table in the RSP code
    RSP_FILL_CMD_SET_SCREEN_SIZE   = 0x0,
    RSP_FILL_CMD_DRAW_CONSTANT          = 0x1,
    RSP_FILL_CMD_DOWNSAMPLE          = 0x2,
    RSP_FILL_CMD_CACHETEST          = 0x3,
    RSP_FILL_CMD_GATHERTEST          = 0x4,
    RSP_FILL_CMD_LOAD_TEXTURE          = 0x5,
    RSP_FILL_CMD_STORE_TILE          = 0x6,
    RSP_FILL_CMD_STORE_TEXTURE          = 0x7,
};
#endif