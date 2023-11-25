/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2020-2022
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _XORGXRDP_HELPER_H
#define _XORGXRDP_HELPER_H

#define XH_YUV420        1
#define XH_YUV422        2
#define XH_YUV444        3

// This should 4, but 4 doesn't work (graphical glitches). Fix it!
#define XH_YUV444_V2_MV  1

#define XH_YUV444_V2_AUX 6

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
