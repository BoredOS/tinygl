// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <poll.h>
#include <pthread.h>

#include <TGL/gl.h>
#include <zbuffer.h>
#include <novaproto.h>

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define NORMAL_LAYER  2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int nova_fd = -1;
static uint32_t surface_id = 0;
static uint32_t *shm_pixels = NULL;
static size_t shm_size = 0;
static char shm_path[128];

static GLfloat view_rotx = 20.0f, view_roty = 30.0f, view_rotz = 0.0f;
static GLfloat angle = 0.0f;

/* Material Colors */
static GLfloat red[4]   = {0.8f, 0.1f, 0.1f, 1.0f};
static GLfloat green[4] = {0.0f, 0.8f, 0.2f, 1.0f};
static GLfloat blue[4]  = {0.2f, 0.2f, 1.0f, 1.0f};
static GLfloat white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
static GLfloat shininess = 5.0f;

/* FPS & Benchmarking variables */
static uint32_t current_fps = 0;
static uint32_t frame_count = 0;
static uint64_t last_fps_time = 0;

static uint64_t accumulated_render_ms = 0;
static uint64_t accumulated_ipc_ms = 0;

static uint64_t get_time_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
    return 0;
}

static void gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width, GLint teeth, GLfloat tooth_depth) {
    GLfloat r0, r1, r2;
    GLfloat angle_step, da;
    GLfloat u, v, len;

    r0 = inner_radius;
    r1 = outer_radius - tooth_depth / 2.0f;
    r2 = outer_radius + tooth_depth / 2.0f;

    da = 2.0f * (GLfloat)M_PI / teeth / 4.0f;

    glNormal3f(0.0f, 0.0f, 1.0f);

    glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), width * 0.5f);
        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), width * 0.5f);
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), width * 0.5f);
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), width * 0.5f);
    }
    glEnd();

    glBegin(GL_QUADS);
    for (GLint i = 0; i < teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + da), r2 * sinf(angle_step + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + 2 * da), r2 * sinf(angle_step + 2 * da), width * 0.5f);
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), width * 0.5f);
    }
    glEnd();

    glNormal3f(0.0f, 0.0f, -1.0f);

    glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), -width * 0.5f);
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), -width * 0.5f);
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), -width * 0.5f);
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), -width * 0.5f);
    }
    glEnd();

    glBegin(GL_QUADS);
    for (GLint i = 0; i < teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + 2 * da), r2 * sinf(angle_step + 2 * da), -width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + da), r2 * sinf(angle_step + da), -width * 0.5f);
        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), -width * 0.5f);
    }
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i < teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;

        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), width * 0.5f);
        glVertex3f(r1 * cosf(angle_step), r1 * sinf(angle_step), -width * 0.5f);
        u = r2 * cosf(angle_step + da) - r1 * cosf(angle_step);
        v = r2 * sinf(angle_step + da) - r1 * sinf(angle_step);
        len = sqrtf(u * u + v * v);
        u /= len;
        v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * cosf(angle_step + da), r2 * sinf(angle_step + da), width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + da), r2 * sinf(angle_step + da), -width * 0.5f);
        glNormal3f(cosf(angle_step), sinf(angle_step), 0.0f);
        glVertex3f(r2 * cosf(angle_step + 2 * da), r2 * sinf(angle_step + 2 * da), width * 0.5f);
        glVertex3f(r2 * cosf(angle_step + 2 * da), r2 * sinf(angle_step + 2 * da), -width * 0.5f);
        u = r1 * cosf(angle_step + 3 * da) - r2 * cosf(angle_step + 2 * da);
        v = r1 * sinf(angle_step + 3 * da) - r2 * sinf(angle_step + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), width * 0.5f);
        glVertex3f(r1 * cosf(angle_step + 3 * da), r1 * sinf(angle_step + 3 * da), -width * 0.5f);
        glNormal3f(cosf(angle_step), sinf(angle_step), 0.0f);
    }

    glVertex3f(r1 * cosf(0), r1 * sinf(0), width * 0.5f);
    glVertex3f(r1 * cosf(0), r1 * sinf(0), -width * 0.5f);
    glEnd();

    glBegin(GL_QUAD_STRIP);
    for (GLint i = 0; i <= teeth; i++) {
        angle_step = i * 2.0f * (GLfloat)M_PI / teeth;
        glNormal3f(-cosf(angle_step), -sinf(angle_step), 0.0f);
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), -width * 0.5f);
        glVertex3f(r0 * cosf(angle_step), r0 * sinf(angle_step), width * 0.5f);
    }
    glEnd();
}

static void draw(void) {
    angle += 2.0f;

    glPushMatrix();
    glRotatef(view_rotx, 1.0f, 0.0f, 0.0f);
    glRotatef(view_roty, 0.0f, 1.0f, 0.0f);
    glRotatef(view_rotz, 0.0f, 0.0f, 1.0f);

    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, red);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glPopMatrix();

    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, blue);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glPopMatrix();

    glPopMatrix();
}

static void init_scene(void) {
    static GLfloat pos[4] = {5.0f, 5.0f, 10.0f, 0.0f};

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);

    glTextSize(GL_TEXT_SIZE24x24);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("[GLGears] Connecting to Nova window server...\n");
    nova_fd = nova_connect(NULL);
    if (nova_fd < 0) {
        fprintf(stderr, "[GLGears] Error: Failed to connect to Nova window server.\n");
        return 1;
    }

    if (nova_create_surface(nova_fd, WINDOW_WIDTH, WINDOW_HEIGHT, NORMAL_LAYER, SURFACE_FLAG_NO_RESIZE, &surface_id, shm_path) < 0) {
        fprintf(stderr, "[GLGears] Error: Failed to create Nova surface.\n");
        close(nova_fd);
        return 1;
    }

    nova_set_title(nova_fd, surface_id, "GLGears)");

    shm_size = (size_t)WINDOW_WIDTH * WINDOW_HEIGHT * sizeof(uint32_t);
    int shm_fd = open(shm_path, O_RDWR);
    if (shm_fd < 0) {
        fprintf(stderr, "[GLGears] Error: Failed to open SHM buffer '%s'.\n", shm_path);
        close(nova_fd);
        return 1;
    }

    shm_pixels = (uint32_t *)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (shm_pixels == MAP_FAILED) {
        fprintf(stderr, "[GLGears] Error: mmap failed.\n");
        close(nova_fd);
        return 1;
    }

    ZBuffer *frameBuffer = ZB_open(WINDOW_WIDTH, WINDOW_HEIGHT, ZB_MODE_RGBA, 0);
    if (!frameBuffer) {
        fprintf(stderr, "[GLGears] Error: ZB_open failed.\n");
        close(nova_fd);
        return 1;
    }

    glInit(frameBuffer);

    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    glShadeModel(GL_FLAT);

    GLfloat h = (GLfloat)WINDOW_HEIGHT / (GLfloat)WINDOW_WIDTH;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0f, 1.0f, -h, h, 5.0f, 60.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);

    init_scene();

    last_fps_time = get_time_ms();
    frame_count = 0;
    current_fps = 0;

    int running = 1;
    NovaEvent evt;
    struct pollfd pfd;
    pfd.fd = nova_fd;
    pfd.events = POLLIN;

    while (running) {
        while (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            if (nova_poll_event(nova_fd, &evt) != 0) break;
            if (evt.type == EVT_CLOSE_REQUEST) {
                running = 0;
            } else if (evt.type == EVT_KEY && evt.data.key.pressed) {
                if (evt.data.key.keycode == KEY_ESCAPE) {
                    running = 0;
                } else if (evt.data.key.keycode == KEY_LEFT) {
                    view_roty -= 5.0f;
                } else if (evt.data.key.keycode == KEY_RIGHT) {
                    view_roty += 5.0f;
                } else if (evt.data.key.keycode == KEY_UP) {
                    view_rotx -= 5.0f;
                } else if (evt.data.key.keycode == KEY_DOWN) {
                    view_rotx += 5.0f;
                }
            }
        }

        uint64_t t0 = get_time_ms();

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw();

        char fps_text[32];
        snprintf(fps_text, sizeof(fps_text), "FPS: %u", current_fps);
        glDrawText((const unsigned char *)fps_text, 16, 16, 0x00FF00);
        uint64_t t1 = get_time_ms();

        memcpy(shm_pixels, frameBuffer->pbuf, shm_size);

        nova_damage_surface(nova_fd, surface_id, 0, NULL);
        uint64_t t2 = get_time_ms();

        accumulated_render_ms += (t1 - t0);
        accumulated_ipc_ms    += (t2 - t1);
        frame_count++;

        uint64_t now = get_time_ms();
        if (now - last_fps_time >= 1000) {
            uint64_t time_delta = now - last_fps_time;
            current_fps = (uint32_t)((frame_count * 1000) / time_delta);
            
            if (frame_count > 0) {
                printf("[GLGears Bench] FPS: %u | Avg Render: %lu ms | Avg Nova IPC: %lu ms\n",
                       current_fps,
                       (unsigned long)(accumulated_render_ms / frame_count),
                       (unsigned long)(accumulated_ipc_ms / frame_count));
            }

            frame_count = 0;
            accumulated_render_ms = 0;
            accumulated_ipc_ms = 0;
            last_fps_time = now;
        }
    }

    ZB_close(frameBuffer);
    glClose();

    nova_destroy_surface(nova_fd, surface_id);
    close(nova_fd);

    printf("[GLGears] Exited cleanly.\n");
    return 0;
}