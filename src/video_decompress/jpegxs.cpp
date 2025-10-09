#include <svt-jpegxs/SvtJpegxsDec.h>
#include <assert.h> 

#include "debug.h"
#include "lib_common.h"
#include "video.h"
#include "video_decompress.h"
#include "jpegxs/jpegxs_conv.h"

#define MOD_NAME "[JPEG XS dec.] "

struct state_decompress_jpegxs {
        ~state_decompress_jpegxs() {
                for (uint8_t i = 0; i < image_config.components_num; ++i) {
                        free(image_buffer.data_yuv[i]);
                }
                svt_jpeg_xs_decoder_close(&decoder);
        }
        svt_jpeg_xs_decoder_api_t decoder;
        bool configured = 0;
        svt_jpeg_xs_bitstream_buffer_t bitstream; // in_buf
        svt_jpeg_xs_image_buffer_t image_buffer; // out_buf
        svt_jpeg_xs_image_config_t image_config;

        void (*convert_from_planar)(const svt_jpeg_xs_image_buffer_t *src, int width, int height, uint8_t *dst);
        struct video_desc desc;
        int rshift, gshift, bshift;
        int pitch;
        codec_t out_codec;
};

static void *jpegxs_decompress_init(void) {
        struct state_decompress_jpegxs *s = new state_decompress_jpegxs();

        return s;
}

static bool setup_image_output_buffer(svt_jpeg_xs_image_buffer_t *image_buffer, const svt_jpeg_xs_image_config_t *image_config) {
        
        uint32_t pixel_size = image_config->bit_depth <= 8 ? 1 : 2;

        for (uint8_t i = 0; i < 3; ++i) {
                image_buffer->stride[i] = image_config->components[i].width;
                image_buffer->alloc_size[i] = image_buffer->stride[i] * image_config->components[i].height * pixel_size;
                image_buffer->data_yuv[i] = malloc(image_buffer->alloc_size[i]);
                if (!image_buffer->data_yuv[i]) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate plane %d\n", i);
                        return false;
                }
        }

        return true;
}

static bool configure_with(struct state_decompress_jpegxs *s, unsigned char *bitstream_buffer, size_t codestream_size) {

        s->decoder.verbose = VERBOSE_SYSTEM_INFO;
        s->decoder.threads_num = 10;
        s->decoder.use_cpu_flags = CPU_FLAGS_ALL;
        s->decoder.proxy_mode = proxy_mode_full;

        SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &s->decoder, bitstream_buffer, codestream_size, &s->image_config);
        if (err != SvtJxsErrorNone) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to initialize JPEG XS decoder\n");
                return false;
        }

        if (s->out_codec == VIDEO_CODEC_NONE) {
                const struct jpegxs_to_uv_conversion *conv = get_default_jpegxs_to_uv_conversion(s->image_config.format);
                if (!conv || !conv->convert) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "No default conversion for colour format: %d\n", s->image_config.format);
                        return false;
                }
                s->convert_from_planar = conv->convert;
        }

        if (!setup_image_output_buffer(&s->image_buffer, &s->image_config)) {
                return false;
        }

        s->configured = true;
        return true;
}

static int jpegxs_decompress_reconfigure(void *state, struct video_desc desc,
        int rshift, int gshift, int bshift, int pitch, codec_t out_codec)
{
        struct state_decompress_jpegxs *s = (struct state_decompress_jpegxs *) state;

        assert(out_codec == UYVY || out_codec == VIDEO_CODEC_NONE);

        if (s->out_codec == out_codec &&
                s->pitch == pitch &&
                s->rshift == rshift &&
                s->gshift == gshift &&
                s->bshift == bshift &&
                video_desc_eq_excl_param(s->desc, desc, PARAM_INTERLACING)) {
                return true;
        }

        s->out_codec = out_codec;
        s->pitch = pitch;
        s->rshift = rshift;
        s->gshift = gshift;
        s->bshift = bshift;
        s->desc = desc;

        if (s->out_codec != VIDEO_CODEC_NONE) {
                const struct jpegxs_to_uv_conversion *conv = get_jpegxs_to_uv_conversion(s->out_codec);
                if (!conv || !conv->convert) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unsupported codec: %s\n", get_codec_name(s->out_codec));
                        return false;
                }
                s->convert_from_planar = conv->convert;
        }

        if (s->configured) {
                svt_jpeg_xs_decoder_close(&s->decoder);
                s->configured = false;
        }

        return true;
}

static decompress_status jpegxs_probe_internal_codec(struct state_decompress_jpegxs *s, struct pixfmt_desc *internal_prop)
{
        internal_prop->depth = 8;
        internal_prop->rgb = false;

        switch (s->image_config.format) {
        case COLOUR_FORMAT_PLANAR_YUV420:
                internal_prop->subsampling = 4200;
                break;
        case COLOUR_FORMAT_PLANAR_YUV422:
                internal_prop->subsampling = 4220;
                break;
        case COLOUR_FORMAT_PLANAR_YUV444_OR_RGB:
                internal_prop->subsampling = 4440;
                break;
        default:
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unsupported colour format\n");
                abort();
        }
        
        return DECODER_GOT_CODEC;
}

static decompress_status jpegxs_decompress(void *state, unsigned char *dst, unsigned char *buffer, 
        unsigned int src_len, int frame_seq, struct video_frame_callbacks *callbacks, struct pixfmt_desc *internal_prop)
{
        UNUSED(frame_seq);
        UNUSED(callbacks);
        auto *s = (struct state_decompress_jpegxs *) state;

        if (!s->configured) {
                if (!configure_with(s, buffer, src_len)) {
                        return DECODER_NO_FRAME;
                }
        }

        if (s->out_codec == VIDEO_CODEC_NONE) {
                return jpegxs_probe_internal_codec(s, internal_prop);
        }

        s->bitstream.buffer = buffer;
        s->bitstream.used_size = src_len;

        svt_jpeg_xs_frame_t dec_input;
        dec_input.bitstream = s->bitstream;
        dec_input.image = s->image_buffer;

        SvtJxsErrorType_t err;  
        err = svt_jpeg_xs_decoder_send_frame(&s->decoder, &dec_input, 1 /*blocking*/);
        if (err) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to send frame to decoder, error code: %x\n", err);
                return DECODER_NO_FRAME;
        }

        svt_jpeg_xs_frame_t dec_output;
        err = svt_jpeg_xs_decoder_get_frame(&s->decoder, &dec_output, 1 /*blocking*/);
        if (err) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to get encoded packet, error code: %x\n", err);
                return DECODER_NO_FRAME;
        }

        s->convert_from_planar(&dec_output.image, s->image_config.width, s->image_config.height, dst);

        return DECODER_GOT_FRAME;
}

static int jpegxs_decompress_get_property(void *state, int property, void *val, size_t *len)
{
        struct state_decompress *s = (struct state_decompress *) state;
        UNUSED(s);
        int ret = false;

        switch(property) {
                case DECOMPRESS_PROPERTY_ACCEPTS_CORRUPTED_FRAME:
                        if(*len >= sizeof(int)) {
                                *(int *) val = false;
                                *len = sizeof(int);
                                ret = true;
                        }
                        break;
                default:
                        ret = false;
        }

        return ret;
}

static void jpegxs_decompress_done(void *state) {
       delete (struct state_decompress_jpegxs *) state;
}

static int jpegxs_decompress_get_priority(codec_t compression, struct pixfmt_desc internal, codec_t ugc)
{
        UNUSED(internal);

        if (compression != JPEG_XS) {
                return VDEC_PRIO_NA;
        }

        if (ugc == VC_NONE) {
                return VDEC_PRIO_PROBE_HI;
        }

        // supported output formats
        if (ugc == UYVY) {
                return VDEC_PRIO_PREFERRED;
        }

        return VDEC_PRIO_NA;
}

static const struct video_decompress_info jpegxs_info = {
        jpegxs_decompress_init, // jpegxs_decompress_init
        jpegxs_decompress_reconfigure, // jpegxs_decompress_reconfigure
        jpegxs_decompress, // jpegxs_decompress
        jpegxs_decompress_get_property, // jpegxs_decompress_get_property
        jpegxs_decompress_done, // jpegxs_decompress_done
        jpegxs_decompress_get_priority, // jpegxs_decompress_get_priority
};

REGISTER_MODULE(jpegxs, &jpegxs_info, LIBRARY_CLASS_VIDEO_DECOMPRESS, VIDEO_DECOMPRESS_ABI_VERSION);