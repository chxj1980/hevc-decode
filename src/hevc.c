// Last Update:2019-01-09 21:22:48
/**
 * @file hevc.c
 * @brief 
 * @author felix
 * @version 0.1.00
 * @date 2019-01-07
 */

#include <stdint.h>
#include <string.h>
#include "bs.h"

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field
#define AV_INPUT_BUFFER_PADDING_SIZE 32
#define NALU_MAX 16
#define HEVC_MAX_SUB_LAYERS 7

#define MAX(a,b) ((a) > (b) ? (a) : (b))

typedef struct NalUnit {
    uint8_t nalu_type;
    uint8_t *addr;
    int size;
} NalUnit;

typedef struct HVCCNALUnitArray {
    uint8_t  array_completeness;
    uint8_t  NAL_unit_type;
    uint16_t numNalus;
    uint16_t *nalUnitLength;
    uint8_t  **nalUnit;
} HVCCNALUnitArray;

typedef struct HEVCDecoderConfigurationRecord {
    uint8_t  configurationVersion;
    uint8_t  general_profile_space;
    uint8_t  general_tier_flag;
    uint8_t  general_profile_idc;
    uint32_t general_profile_compatibility_flags;
    uint64_t general_constraint_indicator_flags;
    uint8_t  general_level_idc;
    uint16_t min_spatial_segmentation_idc;
    uint8_t  parallelismType;
    uint8_t  chromaFormat;
    uint8_t  bitDepthLumaMinus8;
    uint8_t  bitDepthChromaMinus8;
    uint16_t avgFrameRate;
    uint8_t  constantFrameRate;
    uint8_t  numTemporalLayers;
    uint8_t  temporalIdNested;
    uint8_t  lengthSizeMinusOne;
    uint8_t  numOfArrays;
    HVCCNALUnitArray *array;
} HEVCDecoderConfigurationRecord;

static const uint8_t *hevc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
    const uint8_t *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(const uint32_t*)p;
        if ((x - 0x01010101) & (~x) & 0x80808080) { // generic
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p < end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return end + 3;
}

const uint8_t *hevc_find_startcode( const uint8_t *p, const uint8_t *end )
{
    const uint8_t *out= hevc_find_startcode_internal(p, end);

    if(p<out && out<end && !out[-1]) out--;

    return out;
}

static void hevc_update_ptl(HEVCDecoderConfigurationRecord *config,
                            HVCCProfileTierLevel *ptl)
{
    config->general_profile_space = ptl->profile_space;
    if (config->general_tier_flag < ptl->tier_flag)
        config->general_level_idc = ptl->level_idc;
    else
        config->general_level_idc = FFMAX(hvcc->general_level_idc, ptl->level_idc);
    config->general_tier_flag = MAX(hvcc->general_tier_flag, ptl->tier_flag);
    config->general_profile_idc = MAX(hvcc->general_profile_idc, ptl->profile_idc);
    config->general_profile_compatibility_flags &= ptl->profile_compatibility_flags;
    config->general_constraint_indicator_flags &= ptl->constraint_indicator_flags;
}

static void hevc_parse_ptl(bs_t *bs, HEVCDecoderConfigurationRecord *config,
                 unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    HVCCProfileTierLevel general_ptl;
    uint8_t sub_layer_profile_present_flag[HEVC_MAX_SUB_LAYERS];
    uint8_t sub_layer_level_present_flag[HEVC_MAX_SUB_LAYERS];

    general_ptl.profile_space               = bs_read_u( bs, 2 );
    general_ptl.tier_flag                   = bs_read_u1( bs );
    general_ptl.profile_idc                 = bs_read_u( bs, 5 );
    general_ptl.profile_compatibility_flags = bs_read_u( bs, 32 );
    general_ptl.constraint_indicator_flags  = bs_read_u( bs, 48 );
    general_ptl.level_idc                   = bs_read_u8( bs );
    hvcc_update_ptl( config, &general_ptl );

    for (i = 0; i < max_sub_layers_minus1; i++) {
        sub_layer_profile_present_flag[i] = bs_read_u1( bs );
        sub_layer_level_present_flag[i]   = bs_read_u1( bs );
    }

    if (max_sub_layers_minus1 > 0)
        for (i = max_sub_layers_minus1; i < 8; i++)
            bs_skip_u( bs, 2 ); // reserved_zero_2bits[i]

    for (i = 0; i < max_sub_layers_minus1; i++) {
        if (sub_layer_profile_present_flag[i]) {
            bs_skip_bytes( bs, 4 );
            bs_skip_bytes( bs, 4 );
            bs_skip_bytes( bs, 3);
        }

        if (sub_layer_level_present_flag[i])
            bs_skip_bytes( bs, 1 );
    }
}

static int hevc_parse_vps( bs_t *bs, int size, HEVCDecoderConfigurationRecord *config )
{
    unsigned int vps_max_sub_layers_minus1;

    bs_skip_u( bs, 12 );

    vps_max_sub_layers_minus1 = bs_read_u( bs, 3 );

    config->numTemporalLayers = MAX(config->numTemporalLayers, vps_max_sub_layers_minus1 + 1);

    bs_skip_u( bs, 17 );

    return 0;
err:
    return -1;
}

static int hevc_parse_pps( bs_t *bs, int size, HEVCDecoderConfigurationRecord *config )
{
    return 0;
err:
    return -1;
}

static int hevc_parse_sps( bs_t *bs, int size, HEVCDecoderConfigurationRecord *config )
{
    return 0;
err:
    return -1;
}

int hevc_parse_nalu( const uint8_t *data_in, int size, NalUnit *nalu_list )
{
    int i = 0;
    const uint8_t *p = data_in;
    const uint8_t *end = p + size;
    const uint8_t *nal_start = NULL, *nal_end = NULL;
    uint8_t nalu_type = 0;

    nal_start = hevc_find_startcode(p, end);
    for (;;) {
        while (nal_start < end && !*(nal_start++));
        if (nal_start == end)
            break;

        nal_end = hevc_find_startcode(nal_start, end);
        nalu_type = (nal_start[4] >> 1) & 0x3f;
        if ( nalu_type == HEVC_NAL_VPS ||
             nalu_type == HEVC_NAL_SPS ||
             nalu_type == HEVC_NAL_PPS ||
             nalu_type == HEVC_NAL_SEI_PREFIX ||
             nalu_type == HEVC_NAL_SEI_SUFFIX ) {
            nalu_list[i].nalu_type = nalu_type;
            nalu_list[i].size = nal_end - nal_start;
            nalu_list[i++].addr = nal_start;
        }
        nal_start = nal_end;
    }

    return i;
}

static uint8_t *nalu_extract_rbsp(const uint8_t *src, uint32_t src_len,
                                      uint32_t *dst_len)
{
    uint8_t *dst;
    uint32_t i, len;

    dst = malloc(src_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!dst)
        return NULL;

    /* NAL unit header (2 bytes) */
    i = len = 0;
    while (i < 2 && i < src_len)
        dst[len++] = src[i++];

    while (i + 2 < src_len)
        if (!src[i] && !src[i + 1] && src[i + 2] == 3) {
            dst[len++] = src[i++];
            dst[len++] = src[i++];
            i++; // remove emulation_prevention_three_byte
        } else
            dst[len++] = src[i++];

    while (i < src_len)
        dst[len++] = src[i++];

    *dst_len = len;
    return dst;
}

int hevc_get_config( const uint8_t *data_in, int size, HEVCDecoderConfigurationRecord *config )
{
    int ret = 0, i = 0;
    NalUnit nalu_list[NALU_MAX];

    if ( !data_in || !config ) {
        goto err;
    }
    
    memset( nalu_list, 0, sizeof(nalu_list) );
    ret = hevc_parse_nalu( data_in, size, nalu_list );
    if ( ret <= 0 ) {
        goto err; 
    }

    for ( i=0; i<ret; i++ ) {
        uint32_t rbsp_size = 0;
        uint8_t *rbsp_buf = NULL;
        bs_t *bs = NULL;

        if ( nalu_list[i].nalu_type != HEVC_NAL_VPS &&
             nalu_list[i].nalu_type != HEVC_NAL_SPS &&
             nalu_list[i].nalu_type != HEVC_NAL_PPS ) {
            continue;
        }

        rbsp_buf = nalu_extract_rbsp( nalu_list[i].addr, nalu_list[i].size, &rbsp_size );
        if ( !rbsp_buf || rbsp_size <= 0 ) {
            goto err;
        }

        rbsp_buf += 2;// skip nal unit header,2bytes
        bs = bs_new( rbsp_buf, rbsp_size );
        if ( !bs ) {
            goto err;
        }

        switch( nalu_list[i].nalu_type ) {
        case HEVC_NAL_VPS:
            ret = hevc_parse_vps( bs, config );
            if ( ret < 0 ) {
                free( rbsp_buf );
                goto err;
            }
            free( rbsp_buf );
            break;
        case HEVC_NAL_SPS:
            ret = hevc_parse_sps( bs, config );
            if ( ret < 0 ) {
                free( rbsp_buf );
                goto err;
            }
            free( rbsp_buf );
            break;
        case HEVC_NAL_PPS:
            ret = hevc_parse_pps( bs, config );
            if ( ret < 0 ) {
                free( rbsp_buf );
                goto err;
            }
            free( rbsp_buf );
            break;
        default:
            free( rbsp_buf );
            break;
        }
    }

    return 0;

err:
    return -1;
}

