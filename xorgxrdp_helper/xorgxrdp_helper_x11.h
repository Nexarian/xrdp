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

#ifndef _XORGXRDP_HELPER_X11_H
#define _XORGXRDP_HELPER_X11_H

/* generic type that can hold either a GLXPixmap(XID, unsigned int or long)
 * or EGLSurface(void*) */
typedef intptr_t inf_image_t;

/* forward declaration used in xorgxrdp_helper_nvenc and xorgxrdp_helper_yami */
struct enc_info;

int
xorgxrdp_helper_x11_init(void);
int
xorgxrdp_helper_x11_get_wait_objs(intptr_t *objs, int *obj_count);
int
xorgxrdp_helper_x11_check_wait_objs(void);
int
xorgxrdp_helper_x11_delete_all_pixmaps(void);
int
xorgxrdp_helper_x11_create_pixmap(int width, int height, int magic,
                                  int con_id, int mon_id);
enum encoder_result
xorgxrdp_helper_x11_encode_pixmap(int left, int top, int width, int height,
                                  int mon_id, int num_crects,
                                  struct xh_rect *crects,
                                  char *cdata, int *cdata_bytes);

#endif
