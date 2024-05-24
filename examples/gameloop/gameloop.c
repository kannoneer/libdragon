#include <libdragon.h>
#include <math.h>
#include <float.h>
#include "n64sys.h"

// Player spritesheet constants
#define PLAYER_NUM_FRAMES (12)              // Total number of frames in the spritesheet
#define PLAYER_SPRITE_SIZE (32)             // 32x32 pixel frames
#define PLAYER_ANIM_FRAMES_PER_SECOND (20)  // Player animation speed

// Mixer channel allocation, we have just one for now
#define CHANNEL_SFX (0)

enum {
    FONT_PACIFICO = 1,
};

typedef struct vector2d_s {
    float x;
    float y;
} vector2d_t;

enum {
    FACING_RIGHT = 0,
    FACING_LEFT = 1,
};

// Sprites
static sprite_t *player_sprite;
static sprite_t *apple_sprite;

// Sound effects
static wav64_t sfx_hit;

// Keep game object state in their own anonymous structs.
// This way their variables are kept in their own namespaces.

static struct {
    vector2d_t pos;
    float drop;
} apple;

static struct {
    vector2d_t pos;
    int facing;
    int frame;
    float anim_counter;
} player;

static float distance_squared(vector2d_t *a, vector2d_t *b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return dx*dx + dy*dy;
}

void update(float dt)
{
    joypad_poll();
    joypad_buttons_t buttons = joypad_get_buttons(JOYPAD_PORT_1);

    const float player_speed = 150.0f; // Unit is pixels per second

    if (buttons.d_left) {
        player.pos.x -= dt * player_speed;
        player.facing = FACING_LEFT;
    }
    if (buttons.d_right) {
        player.pos.x += dt * player_speed;
        player.facing = FACING_RIGHT;
    }
    if (buttons.d_up) {
        player.pos.y -= dt * player_speed;
    }
    if (buttons.d_down) {
        player.pos.y += dt * player_speed;
    }

    const int hit_radius = 20;

    if (distance_squared(&player.pos, &apple.pos) < (hit_radius * hit_radius)) {
        wav64_play(&sfx_hit, CHANNEL_SFX);
        const int margin = 20;
        apple.pos.x = margin + rand() % (display_get_width() - 2 * margin);
        apple.pos.y = margin + rand() % (display_get_height() - 2 * margin);
        apple.drop = 6.0f;
    }

    // Update animations

    player.anim_counter += dt;
    if (player.anim_counter > 1.0f/PLAYER_ANIM_FRAMES_PER_SECOND) {
        player.frame = (player.frame + 1) % PLAYER_NUM_FRAMES;
        player.anim_counter = 0.0f;
    }

    apple.drop -= dt * 150.0f;
    if (apple.drop < 0.0f) {
        apple.drop = 0.0;
    }
}

void render()
{
    surface_t *disp = display_get();
    rdpq_attach_clear(disp, NULL);

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_sprite_blit(apple_sprite, apple.pos.x, apple.pos.y - apple.drop, NULL);
    rdpq_sprite_blit(player_sprite, player.pos.x, player.pos.y, &(rdpq_blitparms_t){
        .s0 = player.frame * PLAYER_SPRITE_SIZE,
        .width = PLAYER_SPRITE_SIZE,
        .flip_x = player.facing == FACING_LEFT
    });

    rdpq_detach_show();
}

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

    joypad_init();
    timer_init();

    dfs_init(DFS_DEFAULT_LOCATION);

    rdpq_init();

	audio_init(44100, 4);
	mixer_init(10); // The number of concurrently playing channels

	wav64_open(&sfx_hit, "rom:/blip.wav64");

    // Sprites drawn by Pixel Frog and licensed under CC0
    // at https://pixelfrog-assets.itch.io/pixel-adventure-1
    player_sprite = sprite_load("rom:/ninjafrog.sprite");
    apple_sprite = sprite_load("rom:/apple.sprite");

    // rdpq_font_t *fnt1 = rdpq_font_load("rom:/Pacifico.font64");
    // rdpq_font_style(fnt1, 0, &(rdpq_fontstyle_t){
    //     .color = RGBA32(0xFD, 0xFE, 0x99, 0xFF),
    // });
    // rdpq_font_style(fnt1, 1, &(rdpq_fontstyle_t){
    //     .color = RGBA32(0xFD, 0x9E, 0x99, 0xFF),
    // });
    // rdpq_text_register_font(FONT_PACIFICO, fnt1);

    player.pos.x = 100;
    player.pos.y = 50;
    apple.pos.x = 200;
    apple.pos.y = 75;

    // Make the first frame have a 33 ms delta.
    uint32_t last_ticks = get_ticks() - TICKS_FROM_MS(33);

    while (1)
    {
        uint32_t now_ticks = get_ticks();
        float deltatime = TICKS_DISTANCE(last_ticks, now_ticks) / (float)TICKS_PER_SECOND;
        last_ticks = now_ticks;

        update(deltatime);
        render();

        // Update audio if needed
        mixer_try_play();
    }
}
