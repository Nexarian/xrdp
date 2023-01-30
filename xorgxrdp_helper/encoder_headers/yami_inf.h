
#if !defined( __YAMI_INF_H__)
#define __YAMI_INF_H__ 1

#if !defined(YI_INT64)
#define YI_INT64 long long
#endif

#define YI_VERSION_INT(_major, _minor) (((_major) << 16) | (_minor))

#define YI_MAJOR                        0
#define YI_MINOR                        2

#define YI_SUCCESS                      0
#define YI_ERROR_MEMORY                 1
#define YI_ERROR_UNIMP                  2
#define YI_ERROR_TYPE                   3
#define YI_ERROR_OTHER                  4
#define YI_ERROR_VAINITIALIZE           100
#define YI_ERROR_VACREATECONFIG         101
#define YI_ERROR_VACREATECONTEXT        102
#define YI_ERROR_VACREATESURFACES       103
#define YI_ERROR_VACREATEIMAGE          104
#define YI_ERROR_VAMAPBUFFER            105
#define YI_ERROR_VAUNMAPBUFFER          106
#define YI_ERROR_VAPUTIMAGE             107
#define YI_ERROR_VASYNCSURFACE          108
#define YI_ERROR_VAPUTSURFACE           109
#define YI_ERROR_VACREATEBUFFER         110
#define YI_ERROR_VABEGINPICTURE         111
#define YI_ERROR_VARENDERPICTURE        112
#define YI_ERROR_VAENDPICTURE           113
#define YI_ERROR_VAEXPORTSURFACE        114
#define YI_ERROR_CREATEENCODER          200
#define YI_ERROR_ENCODEGETPARAMETERS    201
#define YI_ERROR_ENCODESETPARAMETERS    202
#define YI_ERROR_ENCODESTART            203
#define YI_ERROR_ENCODEENCODE           204
#define YI_ERROR_ENCODEGETOUTPUT        205
#define YI_ERROR_CREATEDECODER          300
#define YI_ERROR_DECODESTART            301
#define YI_ERROR_DECODEDECODE           302
#define YI_ERROR_DECODEGETOUTPUT        303
#define YI_ERROR_DECODEGETFORMATINFO    304

#define YI_TYPE_H264                    1
#define YI_TYPE_MPEG2                   2

#define YI_H264_ENC_FLAGS_PROFILE_MASK  0x0000000F
#define YI_H264_ENC_FLAGS_PROFILE_MAIN  (1 << 0)
#define YI_H264_ENC_FLAGS_PROFILE_HIGH  (2 << 0)

#define YI_TYPE_DRM                     1
#define YI_TYPE_X11                     2

#define YI_H264_DEC_FLAG_LOWLATENCY     1

#define YI_NV12                         0x10C
#define YI_YUY2                         0x110

#ifdef __cplusplus
extern "C"
{
#endif

int
yami_get_version(int *version);
int
yami_init(int type, void *display);
int
yami_deinit(void);

int
yami_encoder_create(void **obj, int width, int height, int type, int flags);
int
yami_encoder_delete(void *obj);
int
yami_encoder_get_width(void *obj, int *width);
int
yami_encoder_get_height(void *obj, int *height);
int
yami_encoder_resize(void *obj, int width, int height);
int
yami_encoder_get_ybuffer(void *obj, void **ydata, int *ydata_stride_bytes);
int
yami_encoder_get_uvbuffer(void *obj, void **uvdata, int *uvdata_stride_bytes);
int
yami_encoder_set_fd_src(void *obj, int fd, int fd_width, int fd_height,
                        int fd_stride, int fd_size, int fd_bpp);
int
yami_encoder_encode(void *obj, void *cdata,
                    int *cdata_max_bytes, int force_key_frame);

int
yami_decoder_create(void **obj, int width, int height, int type, int flags);
int
yami_decoder_delete(void *obj);
int
yami_decoder_decode(void *obj, void *cdata, int cdata_bytes);
int
yami_decoder_decode_time(void *obj, void *cdata, int cdata_bytes,
                         YI_INT64 time);
int
yami_decoder_get_pixmap(void *obj, void *display,
                        int width, int height, int *pixmap);
int
yami_decoder_get_fd_dst(void *obj, int *fd, int *fd_width, int *fd_height,
                        int *fd_stride, int *fd_size, int *fd_bpp,
                        YI_INT64 *fd_time);

int
yami_surface_create(void **obj, int width, int height, int type, int flags);
int
yami_surface_delete(void *obj);
int
yami_surface_get_ybuffer(void *obj, void **ydata, int *ydata_stride_bytes);
int
yami_surface_get_uvbuffer(void *obj, void **uvdata, int *uvdata_stride_bytes);
int
yami_surface_get_fd_dst(void *obj, int *fd, int *fd_width, int *fd_height,
                        int *fd_stride, int *fd_size, int *fd_bpp);

struct yami_funcs
{
    int (*yami_get_version)(int *version);
    int (*yami_init)(int type, void *display);
    int (*yami_deinit)(void);
    size_t pad0[20 - 3];
    /* encoder */
    int (*yami_encoder_create)(void **obj, int width, int height, int type, int flags);
    int (*yami_encoder_delete)(void *obj);
    int (*yami_encoder_get_width)(void *obj, int *width);
    int (*yami_encoder_get_height)(void *obj, int *height);
    int (*yami_encoder_resize)(void *obj, int width, int height);
    int (*yami_encoder_get_ybuffer)(void *obj, void **ydata, int *ydata_stride_bytes);
    int (*yami_encoder_get_uvbuffer)(void *obj, void **uvdata, int *uvdata_stride_bytes);
    int (*yami_encoder_set_fd_src)(void *obj, int fd, int fd_width, int fd_height,
                                   int fd_stride, int fd_size, int fd_bpp);
    int (*yami_encoder_encode)(void *obj, void *cdata,
                               int *cdata_max_bytes, int force_key_frame);
    size_t pad1[20 - 9];
    /* decoder */
    int (*yami_decoder_create)(void **obj, int width, int height, int type, int flags);
    int (*yami_decoder_delete)(void *obj);
    int (*yami_decoder_decode)(void *obj, void *cdata, int cdata_bytes);
    int (*yami_decoder_decode_time)(void *obj, void *cdata, int cdata_bytes,
                                    YI_INT64 time);
    int (*yami_decoder_get_pixmap)(void *obj, void *display,
                                   int width, int height, int *pixmap);
    int (*yami_decoder_get_fd_dst)(void *obj, int *fd, int *fd_width, int *fd_height,
                                   int *fd_stride, int *fd_size, int *fd_bpp,
                                   YI_INT64 *fd_time);
    size_t pad2[20 - 6];
    /* surface */
    int (*yami_surface_create)(void **obj, int width, int height, int type, int flags);
    int (*yami_surface_delete)(void *obj);
    int (*yami_surface_get_ybuffer)(void *obj, void **ydata, int *ydata_stride_bytes);
    int (*yami_surface_get_uvbuffer)(void *obj, void **uvdata, int *uvdata_stride_bytes);
    int (*yami_surface_get_fd_dst)(void *obj, int *fd, int *fd_width, int *fd_height,
                                   int *fd_stride, int *fd_size, int *fd_bpp);
    size_t pad3[20 - 5];
};

typedef int
(*yami_get_funcs_proc)(struct yami_funcs *funcs, int version);
int
yami_get_funcs(struct yami_funcs *funcs, int version);

#ifdef __cplusplus
}
#endif

#endif
