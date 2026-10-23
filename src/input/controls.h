#ifndef DSPSP_CONTROLS_H
#define DSPSP_CONTROLS_H
#include <stdint.h>
/* PSP button bits; the platform adapter checks these against the SDK. */
enum { DS_PAD_SELECT=1,DS_PAD_START=8,DS_PAD_UP=16,DS_PAD_RIGHT=32,
 DS_PAD_DOWN=64,DS_PAD_LEFT=128,DS_PAD_L=256,DS_PAD_R=512,
 DS_PAD_TRIANGLE=4096,DS_PAD_CIRCLE=8192,DS_PAD_CROSS=16384,DS_PAD_SQUARE=32768 };
typedef struct { uint32_t buttons; uint8_t x,y; } DsPad;
typedef enum { DS_INPUT_KEY_DOWN,DS_INPUT_KEY_UP,DS_INPUT_TOUCH_DOWN,
 DS_INPUT_TOUCH_MOVE,DS_INPUT_TOUCH_UP } DsInputType;
typedef struct { DsInputType type; uint32_t module,code; float x,y; } DsInputEvent;
#define DS_INPUT_MAX_EVENTS 24
typedef struct { DsInputEvent events[DS_INPUT_MAX_EVENTS]; unsigned count; } DsInputBatch;
typedef struct {
 uint32_t held_keys;
 int moving,select_pending,select_used;
 float last_x,last_y;
} DsControls;
/* Gameplay mapping only. Caller must release input on focus loss/pause and
 * supply the actual guest display size. No menu-state detection is implied. */
int ds_controls_update(DsControls *state,DsPad pad,unsigned width,unsigned height,
                       int focused,DsInputBatch *batch);
/* ARM soft-float JNI argument words, including env/object and stack arguments.
 * Returns argument count, or zero for an invalid event. */
unsigned ds_controls_pack(const DsInputEvent *event,uint32_t env,uint32_t object,
                          const char **export_name,uint32_t words[7]);
#endif
