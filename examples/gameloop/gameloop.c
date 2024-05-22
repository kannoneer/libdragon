#include <libdragon.h>
#include <math.h>
#include <float.h>
#include "n64sys.h"

#define PLAYER_NUM_FRAMES (12)
#define PLAYER_SPRITE_SIZE (32)

static sprite_t *player_sprite;
static sprite_t *apple_sprite;

static wav64_t sfx_hit;

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

static struct {
    vector2d_t pos;
} apple;

static struct {
    vector2d_t pos;
    int facing;
    int frame;
    int anim_counter;
} player;

// Mixer channel allocation
#define CHANNEL_SFX1    0
#define CHANNEL_SFX2    2
#define CHANNEL_SFX3    4
#define CHANNEL_MUSIC   6

static float distance_squared(vector2d_t *a, vector2d_t *b)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    return dx*dx + dy*dy;
}

void update(float deltatime)
{
    joypad_poll();
    joypad_buttons_t buttons = joypad_get_buttons(JOYPAD_PORT_1);

    const float player_speed = 150.0f; // Unit is pixels per second

    if (buttons.d_left) {
        player.pos.x -= deltatime * player_speed;
        player.facing = FACING_LEFT;
    }
    if (buttons.d_right) {
        player.pos.x += deltatime * player_speed;
        player.facing = FACING_RIGHT;
    }
    if (buttons.d_up) {
        player.pos.y -= deltatime * player_speed;
    }
    if (buttons.d_down) {
        player.pos.y += deltatime * player_speed;
    }

    const int hit_radius = 25;

    if (distance_squared(&player.pos, &apple.pos) < hit_radius * hit_radius) {
        wav64_play(&sfx_hit, CHANNEL_SFX1);
        const int margin = 20;
        apple.pos.x = margin + rand() % (display_get_width() - 2*margin);
        apple.pos.y = margin + rand() % (display_get_height() - 2*margin);
    }
}

void render()
{
    surface_t *disp = display_get();
    rdpq_attach_clear(disp, NULL);

    rdpq_set_mode_standard();
    rdpq_sprite_blit(apple_sprite, apple.pos.x, apple.pos.y, &(rdpq_blitparms_t){});
    rdpq_sprite_blit(player_sprite, player.pos.x, player.pos.y, &(rdpq_blitparms_t){
        .s0 = player.frame * PLAYER_SPRITE_SIZE,
        .width = PLAYER_SPRITE_SIZE,
        .flip_x = player.facing == FACING_LEFT
    });

    player.anim_counter++;
    if (player.anim_counter > 5) {
        player.frame = (player.frame + 1) % PLAYER_NUM_FRAMES;
        player.anim_counter = 0;
    }


    rdpq_detach_show();
}

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);

    joypad_init();
    timer_init();

    uint32_t display_width = display_get_width();
    uint32_t display_height = display_get_height();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    rdpq_init();

	audio_init(44100, 4);
	mixer_init(20);

	wav64_open(&sfx_hit, "rom:/blip.wav64");

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
