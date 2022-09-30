#ifndef _XORGXRDP_HELPER_H
#define _XORGXRDP_HELPER_H

#define XH_YUV420   1
#define XH_YUV422   2
#define XH_YUV444   3

#define XH_BT601    0
#define XH_BT709FR  1
#define XH_BTRFX    2

struct xh_rect
{
    short x;
    short y;
    short w;
    short h;
};

enum encoder_result
{
    INCREMENTAL_FRAME_ENCODED,
    KEY_FRAME_ENCODED,
    ENCODER_ERROR
};

#endif
