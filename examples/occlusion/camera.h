#ifndef CAMERA_H
#define CAMERA_H

typedef struct {
    float distance;
    float rotation;
} camera_t;

typedef struct {
    float pos[3];
    float angle;
    float pitch;
} fps_camera_t;

void camera_transform(const camera_t *camera)
{
    // Set the camera transform
    glLoadIdentity();
    gluLookAt(
        0, -camera->distance, -camera->distance,
        0, 0, 0,
        0, 1, 0);
    glRotatef(camera->rotation, 0, 1, 0);
}

// void fps_camera_transform(const fps_camera_t *camera)
// {
//     // Set the camera transform
//     glLoadIdentity();
//     gluLookAt(
//         camera->pos[0], camera->pos[1], camera->pos[2],
//         camera->pos[0] + cos(camera.angle), camera->pos[1], camera->pos[2] + sin(camera.angle),
//         0, 1, 0);
// }
#endif
