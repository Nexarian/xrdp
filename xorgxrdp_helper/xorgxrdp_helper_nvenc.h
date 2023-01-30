/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2022
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

#ifndef _XORGXRDP_HELPER_NVENC_H
#define _XORGXRDP_HELPER_NVENC_H

int
xorgxrdp_helper_nvenc_init(void);
int
xorgxrdp_helper_nvenc_create_encoder(int width, int height, int tex,
                                     int tex_format, struct enc_info **ei);
int
xorgxrdp_helper_nvenc_delete_encoder(struct enc_info *ei);
enum encoder_result
xorgxrdp_helper_nvenc_encode(struct enc_info *ei, int tex,
                             void *cdata, int *cdata_bytes);

#endif
