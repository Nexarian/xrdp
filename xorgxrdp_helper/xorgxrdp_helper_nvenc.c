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

#if defined(HAVE_CONFIG_H)
#include <config_ac.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "string_calls.h"

#include <epoxy/gl.h>

#include "encoder_headers/nvEncodeAPI_12_1_14.h"

#include "arch.h"
#include "os_calls.h"
#include "xorgxrdp_helper.h"
#include "xorgxrdp_helper_x11.h"
#include "xorgxrdp_helper_nvenc.h"
#include "log.h"

#define XH_NVENV_DEFAULT_QP 28

typedef NVENCSTATUS
(NVENCAPI *NvEncodeAPICreateInstanceProc)
(NV_ENCODE_API_FUNCTION_LIST *functionList);

static char g_lib_name[] = "libnvidia-encode.so";
static char g_lib_name1[] = "libnvidia-encode.so.1";
static char g_func_name[] = "NvEncodeAPICreateInstance";

static NvEncodeAPICreateInstanceProc g_NvEncodeAPICreateInstance = NULL;

static NV_ENCODE_API_FUNCTION_LIST g_enc_funcs;

static long g_lib = 0;

struct enc_info
{
    int width;
    int height;
    int frameCount;
    int pad0;
    void *enc;
    NV_ENC_OUTPUT_PTR bitstreamBuffer;
    NV_ENC_INPUT_PTR mappedResource;
    NV_ENC_BUFFER_FORMAT mappedBufferFmt;
    NV_ENC_REGISTERED_PTR registeredResource;
};

extern int xrdp_invalidate;

/*****************************************************************************/
int
xorgxrdp_helper_nvenc_init(void)
{
    NVENCSTATUS nv_error;

    g_lib = g_load_library(g_lib_name);
    if (g_lib == 0)
    {
        g_lib = g_load_library(g_lib_name1);
        if (g_lib == 0)
        {
            LOG(LOG_LEVEL_ERROR, "load library for %s/%s failed", g_lib_name, g_lib_name1);
            return 1;
        }
    }
    g_NvEncodeAPICreateInstance = g_get_proc_address(g_lib, g_func_name);
    if (g_NvEncodeAPICreateInstance == NULL)
    {
        LOG(LOG_LEVEL_ERROR, "get proc address for %s failed", g_func_name);
        return 1;
    }
    g_memset(&g_enc_funcs, 0, sizeof(g_enc_funcs));
    g_enc_funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    nv_error = g_NvEncodeAPICreateInstance(&g_enc_funcs);
    LOG(LOG_LEVEL_INFO, "NvEncodeAPICreateInstance rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }
    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_nvenc_create_encoder(int width, int height, int tex,
                                     int tex_format, struct enc_info **ei)
{
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstreamParams;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params;
    NV_ENC_INITIALIZE_PARAMS createEncodeParams;
    NV_ENC_MAP_INPUT_RESOURCE mapInputResource;
    NV_ENC_INPUT_RESOURCE_OPENGL_TEX res;
    NV_ENC_REGISTER_RESOURCE reg_res;
    NV_ENC_CONFIG encCfg;
    NV_ENC_PRESET_CONFIG preset_config;
    NVENCSTATUS nv_error;
    struct enc_info *lei;
    char *rateControlMode_str;
    char *averageBitRate_str;
    char *qp_str;
    int qp_int;
    int averageBitRate_int;
    int rc_set;

    lei = g_new0(struct enc_info, 1);
    if (lei == NULL)
    {
        return 1;
    }

    g_memset(&params, 0, sizeof(params));
    params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.deviceType = NV_ENC_DEVICE_TYPE_OPENGL;
    params.apiVersion = NVENCAPI_VERSION;
    nv_error = g_enc_funcs.nvEncOpenEncodeSessionEx(&params, &(lei->enc));
    LOG(LOG_LEVEL_INFO, "nvEncOpenEncodeSessionEx rv %d enc %p", nv_error, lei->enc);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    g_memset(&createEncodeParams, 0, sizeof(createEncodeParams));
    createEncodeParams.version = NV_ENC_INITIALIZE_PARAMS_VER;
    createEncodeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    createEncodeParams.presetGUID = NV_ENC_PRESET_P4_GUID;
    createEncodeParams.tuningInfo = NV_ENC_TUNING_INFO_LOW_LATENCY;
    createEncodeParams.encodeWidth = width;
    createEncodeParams.encodeHeight = height;
    createEncodeParams.darWidth = width;
    createEncodeParams.darHeight = height;
    createEncodeParams.enablePTD = 1;

    g_memset(&preset_config, 0, sizeof(preset_config));
    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    nv_error = g_enc_funcs.nvEncGetEncodePresetConfigEx(
                   lei->enc, createEncodeParams.encodeGUID,
                   createEncodeParams.presetGUID,
                   createEncodeParams.tuningInfo,
                   &preset_config);

    LOG(LOG_LEVEL_INFO, "nvEncGetEncodePresetConfig rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    g_memset(&encCfg, 0, sizeof(encCfg));

    encCfg.version = NV_ENC_CONFIG_VER;
    memcpy(&encCfg, &preset_config.presetCfg, sizeof(NV_ENC_CONFIG));
    encCfg.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
    encCfg.gopLength = NVENC_INFINITE_GOPLENGTH;
    //encCfg.gopLength = 10;
    encCfg.frameIntervalP = 1;  /* 1 + B_Frame_Count */
    encCfg.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    encCfg.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

    /* these env vars can be added / changed in sesman.ini SessionVariables
       example
       XRDP_NVENC_RATE_CONTROL_MODE=NV_ENC_PARAMS_RC_CONSTQP
       XRDP_NVENC_QP=30
       or
       XRDP_NVENC_RATE_CONTROL_MODE=NV_ENC_PARAMS_RC_VBR
       XRDP_NVENC_AVERAGE_BITRATE=2000000 */
    rateControlMode_str = g_getenv("XRDP_NVENC_RATE_CONTROL_MODE");
    averageBitRate_str = g_getenv("XRDP_NVENC_AVERAGE_BITRATE");
    qp_str = g_getenv("XRDP_NVENC_QP");
    rc_set = 0;
    if (rateControlMode_str != NULL)
    {
        if (g_strcmp(rateControlMode_str, "NV_ENC_PARAMS_RC_CONSTQP") == 0)
        {
            if (qp_str != NULL)
            {
                qp_int = g_atoi(qp_str);
                if ((qp_int >= 0) && (qp_int <= 51))
                {
                    LOG(LOG_LEVEL_INFO,
                        "using NV_ENC_PARAMS_RC_CONSTQP qp %d",
                        qp_int);
                    encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
                    encCfg.rcParams.constQP.qpInterP = qp_int;
                    encCfg.rcParams.constQP.qpInterB = qp_int;
                    encCfg.rcParams.constQP.qpIntra = qp_int;
                    rc_set = 1;
                }
            }
        }
        else if (g_strcmp(rateControlMode_str, "NV_ENC_PARAMS_RC_VBR") == 0)
        {
            if (averageBitRate_str != NULL)
            {
                averageBitRate_int = g_atoi(averageBitRate_str);
                if ((averageBitRate_int >= 5000) &&
                        (averageBitRate_int <= 1000000000))
                {
                    LOG(LOG_LEVEL_INFO,
                        "using NV_ENC_PARAMS_RC_VBR averageBitRate %d",
                        averageBitRate_int);
                    encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
                    encCfg.rcParams.averageBitRate = averageBitRate_int;
                    rc_set = 1;
                }
            }
        }
    }
    if (!rc_set)
    {
        LOG(LOG_LEVEL_INFO,
            "using default NV_ENC_PARAMS_RC_CONSTQP qp %d",
            XH_NVENV_DEFAULT_QP);
        encCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
        encCfg.rcParams.constQP.qpInterP = XH_NVENV_DEFAULT_QP;
        encCfg.rcParams.constQP.qpInterB = XH_NVENV_DEFAULT_QP;
        encCfg.rcParams.constQP.qpIntra = XH_NVENV_DEFAULT_QP;
        rc_set = 1;
    }
    encCfg.rcParams.cbQPIndexOffset = -1;
    encCfg.encodeCodecConfig.h264Config.chromaFormatIDC = 1;
    //encCfg.encodeCodecConfig.h264Config.idrPeriod = 2;
    encCfg.encodeCodecConfig.h264Config.repeatSPSPPS = 1;
    encCfg.encodeCodecConfig.h264Config.disableSPSPPS = 0;
    //encCfg.encodeCodecConfig.h264Config.maxNumRefFrames = 1;
    encCfg.encodeCodecConfig.h264Config.sliceMode = 0;
    encCfg.encodeCodecConfig.h264Config.sliceModeData = 0;
    encCfg.encodeCodecConfig.h264Config.outputAUD = 0;
    // encCfg.encodeCodecConfig.h264Config.outputBufferingPeriodSEI = 1;
    // encCfg.encodeCodecConfig.h264Config.outputPictureTimingSEI = 1;
    // encCfg.encodeCodecConfig.h264Config.level = NV_ENC_LEVEL_AUTOSELECT;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.videoFullRangeFlag = 1;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.videoSignalTypePresentFlag = 1;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.videoFormat = NV_ENC_VUI_VIDEO_FORMAT_UNSPECIFIED;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.bitstreamRestrictionFlag = 1;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.colourDescriptionPresentFlag = 1;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
    // encCfg.encodeCodecConfig.h264Config.h264VUIParameters.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;

    createEncodeParams.encodeConfig = &encCfg;

    nv_error = g_enc_funcs.nvEncInitializeEncoder(lei->enc,
               &createEncodeParams);

    LOG(LOG_LEVEL_INFO, "nvEncInitializeEncoder rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    g_memset(&res, 0, sizeof(res));
    res.texture = tex;
    res.target = GL_TEXTURE_2D;

    g_memset(&reg_res, 0, sizeof(reg_res));
    reg_res.version = NV_ENC_REGISTER_RESOURCE_VER;
    reg_res.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_OPENGL_TEX;
    reg_res.width = width;
    reg_res.height = height;
    if (tex_format == XH_YUV420
        || tex_format == XH_YUV444_V2_MV
        || tex_format == XH_YUV444_V2_AUX)
    {
        reg_res.pitch = width;
        reg_res.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    }
    else
    {
        reg_res.pitch = width * 4;
        reg_res.bufferFormat = NV_ENC_BUFFER_FORMAT_AYUV;
    }
    reg_res.resourceToRegister = &res;
    reg_res.bufferUsage = NV_ENC_INPUT_IMAGE;
    nv_error = g_enc_funcs.nvEncRegisterResource(lei->enc, &reg_res);
    LOG(LOG_LEVEL_INFO, "nvEncRegisterResource rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    g_memset(&mapInputResource, 0, sizeof(mapInputResource));
    mapInputResource.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
    mapInputResource.registeredResource = reg_res.registeredResource;
    nv_error = g_enc_funcs.nvEncMapInputResource(lei->enc, &mapInputResource);
    LOG(LOG_LEVEL_INFO, "nvEncMapInputResource rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    g_memset(&bitstreamParams, 0, sizeof(bitstreamParams));
    bitstreamParams.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    nv_error = g_enc_funcs.nvEncCreateBitstreamBuffer(lei->enc,
               &bitstreamParams);
    LOG(LOG_LEVEL_INFO, "nvEncCreateBitstreamBuffer rv %d", nv_error);
    if (nv_error != NV_ENC_SUCCESS)
    {
        return 1;
    }

    lei->bitstreamBuffer = bitstreamParams.bitstreamBuffer;
    lei->mappedResource = mapInputResource.mappedResource;
    lei->mappedBufferFmt = mapInputResource.mappedBufferFmt;
    lei->registeredResource = reg_res.registeredResource;
    lei->width = width;
    lei->height = height;

    *ei = lei;

    return 0;
}

/*****************************************************************************/
int
xorgxrdp_helper_nvenc_delete_encoder(struct enc_info *ei)
{
    g_enc_funcs.nvEncUnmapInputResource(ei->enc, ei->mappedResource);
    g_enc_funcs.nvEncUnregisterResource(ei->enc, ei->registeredResource);
    g_enc_funcs.nvEncDestroyBitstreamBuffer(ei->enc, ei->bitstreamBuffer);
    g_enc_funcs.nvEncDestroyEncoder(ei->enc);
    g_free(ei);
    return 0;
}

/*****************************************************************************/
enum encoder_result
xorgxrdp_helper_nvenc_encode(struct enc_info *ei, int tex,
                             char *cdata, int *cdata_bytes)
{
    NV_ENC_PIC_PARAMS picParams;
    NV_ENC_LOCK_BITSTREAM lockBitstream;
    NVENCSTATUS nv_error;
    enum encoder_result rv;

    g_memset(&picParams, 0, sizeof(picParams));
    picParams.version = NV_ENC_PIC_PARAMS_VER;
    picParams.inputBuffer = ei->mappedResource;
    picParams.bufferFmt = ei->mappedBufferFmt;
    picParams.inputWidth = ei->width;
    picParams.inputHeight = ei->height;
    picParams.outputBitstream = ei->bitstreamBuffer;
    picParams.inputTimeStamp = g_time3();
    picParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    if (xrdp_invalidate > 0 || ei->frameCount == 0)
    {
        picParams.encodePicFlags = NV_ENC_PIC_FLAG_OUTPUT_SPSPPS | NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_FORCEINTRA;
        picParams.pictureType = NV_ENC_PIC_TYPE_IDR;
        LOG(LOG_LEVEL_INFO, "Forcing NVENC H264 IDR SPSPPS for frame id: %d,"
            "invalidate is: %d", ei->frameCount, xrdp_invalidate);
        xrdp_invalidate = MAX(0, xrdp_invalidate - 1);
    }
    else
    {
        picParams.pictureType = NV_ENC_PIC_TYPE_NONREF_P;
        picParams.encodePicFlags = 0;
    }
    nv_error = g_enc_funcs.nvEncEncodePicture(ei->enc, &picParams);
    rv = ENCODER_ERROR;
    if (nv_error == NV_ENC_SUCCESS)
    {
        g_memset(&lockBitstream, 0, sizeof(lockBitstream));
        lockBitstream.version = NV_ENC_LOCK_BITSTREAM_VER;
        lockBitstream.outputBitstream = ei->bitstreamBuffer;
        lockBitstream.doNotWait = 0;
        nv_error = g_enc_funcs.nvEncLockBitstream(ei->enc, &lockBitstream);
        if (nv_error == NV_ENC_SUCCESS)
        {
            if (*cdata_bytes >= ((int) (lockBitstream.bitstreamSizeInBytes)))
            {
                /* for (int i = 0; i < lockBitstream.bitstreamSizeInBytes; ++i)
                {
                    unsigned char* payload = (unsigned char*)lockBitstream.bitstreamBufferPtr + i;
                    bool b_long_startcode = payload[0] == 0 && payload[1] == 0
                        && payload[2] == 0 && payload[3] == 1;
                    bool b_short_startcode = payload[0] == 0 && payload[1] == 0
                        && payload[2] == 1;
                    int nalUnitType;
                    // 4-byte start code
                    if (b_long_startcode)
                    {
                        nalUnitType = (payload[4] & 0x1F);
                        LOG(LOG_LEVEL_INFO, "Frame: %d: Found long start code at index %d. Type is %d", ei->frameCount, i, nalUnitType);
                        i += 3;
                    }
                    // assume 3-byte start code
                    else if (b_short_startcode)
                    {
                        nalUnitType = (payload[3] & 0x1F);
                        LOG(LOG_LEVEL_INFO, "Frame: %d: Found short start code at index %d. Type is %d", ei->frameCount, i, nalUnitType);
                    }
                } */
                g_memcpy(cdata, lockBitstream.bitstreamBufferPtr,
                         lockBitstream.bitstreamSizeInBytes);
                *cdata_bytes = lockBitstream.bitstreamSizeInBytes;
                rv = INCREMENTAL_FRAME_ENCODED;
            }
            else
            {
                LOG(LOG_LEVEL_INFO, "error not enough room %d %d",
                    *cdata_bytes,
                    (int) (lockBitstream.bitstreamSizeInBytes));
            }
            nv_error = g_enc_funcs.nvEncUnlockBitstream(ei->enc,
                       lockBitstream.outputBitstream);
            if (nv_error != NV_ENC_SUCCESS)
            {
                LOG(LOG_LEVEL_INFO, "unlocking failed");
            }
        }
        else
        {
            LOG(LOG_LEVEL_INFO, "error nvEncLockBitstream %d",
                nv_error);
        }
        ++ei->frameCount;
    }
    else
    {
        LOG(LOG_LEVEL_INFO, "error nvEncEncodePicture %d", nv_error);
    }
    if (rv == INCREMENTAL_FRAME_ENCODED
            && (picParams.encodePicFlags & NV_ENC_PIC_FLAG_FORCEIDR))
    {
        return KEY_FRAME_ENCODED;
    }
    return rv;
}
