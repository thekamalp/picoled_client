// picoled_client.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <k3.h>
#ifdef _WIN32
// socket stuff
#include <WinSock2.h>
#include <WS2tcpip.h>
typedef unsigned int sock_t;
#define VALID_SOCKET(s) ((s) != INVALID_SOCKET)

// These are function in k3 library
#ifdef CreateFont
#undef CreateFont
#endif
#ifdef DrawText
#undef DrawText
#endif
#else
// socket stuff
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
typedef int sock_t;
#define VALID_SOCKET(s) ((s) >= 0)
#define INVALID_SOCET (~0)
#endif

static const char* WINDOW_TITLE = "picoled client";
static const char* PORT = "4242";
static const uint32_t DEFAULT_IMAGE_DIM = 8;
static const uint32_t DEFAULT_WIN_WIDTH = 512;
static const uint32_t DEFAULT_WIN_HEIGHT = 512;
static const uint32_t NUM_VIEWS = 8;

static const uint32_t MAX_IMAGE_SIZE = (128 * 1024);

#define MAX_ANIM 8

struct anim_t {
    uint32_t start_src_data;
    int16_t width;
    int16_t pitch;
    int16_t height;
    int16_t start_x;
    int16_t start_y;
    int32_t delta_src_data;
    int16_t delta_x;
    int16_t num_frames_x;
    int16_t delta_y;
    int16_t num_frames_y;
    int16_t num_src_inc;
    int16_t num_frames;
    int16_t num_loops;
};

class picoled {
public:
    picoled();
    ~picoled();

private:
    k3win win;
    k3gfx gfx;
    k3cmdBuf cmd;
    k3fence fence;
    k3surf gpu_image;
    k3uploadImage cpu_image;
    k3buffer fullscreen_vb;
    k3gfxState main_state;
    k3sampler main_sampler;
    k3font main_font;

    static const uint32_t MAX_TEXT_LEN = 256;
    uint32_t text_len;
    char text[MAX_TEXT_LEN];

    void Display();
    void Keyboard(k3key k, char c, k3keyState state);
    void ExecuteCommand();
    void SendArray8(uint32_t len, const uint8_t* data);
    void SendArray16(uint32_t len, const uint16_t* data);
    void SendArray32(uint32_t len, const uint32_t* data);
    void CloseSocket();

    static void K3CALLBACK DisplayCallback(void* ptr);
    static void K3CALLBACK KeyboardCallback(void* ptr, k3key k, char c, k3keyState state);

    uint32_t image_width;
    uint32_t image_height;

    sock_t sock;
    uint8_t num_back_anim;
    anim_t back_anim[MAX_ANIM];
};
