// Last Update:2019-01-10 11:41:32
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
#include "hevc.h"

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field
#define AV_INPUT_BUFFER_PADDING_SIZE 32
#define NALU_MAX 16
#define HEVC_MAX_SUB_LAYERS 7
#define HEVC_MAX_SHORT_TERM_RPS_COUNT 64

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) > (b) ? (b) : (a))

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

typedef struct HVCCProfileTierLevel {
    uint8_t  profile_space;
    uint8_t  tier_flag;
    uint8_t  profile_idc;
    uint32_t profile_compatibility_flags;
    uint64_t constraint_indicator_flags;
    uint8_t  level_idc;
} HVCCProfileTierLevel;


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
        config->general_level_idc = MAX(config->general_level_idc, ptl->level_idc);
    config->general_tier_flag = MAX(config->general_tier_flag, ptl->tier_flag);
    config->general_profile_idc = MAX(config->general_profile_idc, ptl->profile_idc);
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
    hevc_update_ptl( config, &general_ptl );

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

static int hevc_parse_vps( bs_t *bs, HEVCDecoderConfigurationRecord *config )
{
    unsigned int vps_max_sub_layers_minus1;

    bs_skip_u( bs, 12 );
    vps_max_sub_layers_minus1 = bs_read_u( bs, 3 );
    config->numTemporalLayers = MAX(config->numTemporalLayers, vps_max_sub_layers_minus1 + 1);
    bs_skip_u( bs, 17 );
    hevc_parse_ptl( bs, config, vps_max_sub_layers_minus1 );

    return 0;
}

static void skip_scaling_list_data( bs_t *bs )
{
    int i, j, k, num_coeffs;

    for (i = 0; i < 4; i++)
        for (j = 0; j < (i == 3 ? 2 : 6); j++)
            if (!bs_read_u1( bs ))         // scaling_list_pred_mode_flag[i][j]
                bs_read_ue(bs); // scaling_list_pred_matrix_id_delta[i][j]
            else {
                num_coeffs = MIN(64, 1 << (4 + (i << 1)));

                if (i > 1)
                    bs_read_se(bs); // scaling_list_dc_coef_minus8[i-2][j]

                for (k = 0; k < num_coeffs; k++)
                    bs_read_se(bs); // scaling_list_delta_coef
            }
}

static void skip_sub_layer_ordering_info(bs_t *bs)
{
    bs_read_ue(bs); // max_dec_pic_buffering_minus1
    bs_read_ue(bs); // max_num_reorder_pics
    bs_read_ue(bs); // max_latency_increase_plus1
}

static int parse_rps(bs_t *bs, unsigned int rps_idx,
                     unsigned int num_rps,
                     unsigned int num_delta_pocs[HEVC_MAX_SHORT_TERM_RPS_COUNT])
{
    unsigned int i;

    if (rps_idx && bs_read_u1(bs)) { // inter_ref_pic_set_prediction_flag
        /* this should only happen for slice headers, and this isn't one */
        if (rps_idx >= num_rps)
            return -1;

        bs_skip_u1        (bs); // delta_rps_sign
        bs_read_ue(bs); // abs_delta_rps_minus1

        num_delta_pocs[rps_idx] = 0;

        for (i = 0; i <= num_delta_pocs[rps_idx - 1]; i++) {
            uint8_t use_delta_flag = 0;
            uint8_t used_by_curr_pic_flag = bs_read_u1(bs);
            if (!used_by_curr_pic_flag)
                use_delta_flag = bs_read_u1(bs);

            if (used_by_curr_pic_flag || use_delta_flag)
                num_delta_pocs[rps_idx]++;
        }
    } else {
        unsigned int num_negative_pics = bs_read_ue(bs);
        unsigned int num_positive_pics = bs_read_ue(bs);

        if ((num_positive_pics + (uint64_t)num_negative_pics) * 2 > bs_bits_left(bs))
            return -1;

        num_delta_pocs[rps_idx] = num_negative_pics + num_positive_pics;

        for (i = 0; i < num_negative_pics; i++) {
            bs_read_ue(bs); // delta_poc_s0_minus1[rps_idx]
            bs_skip_u1        (bs); // used_by_curr_pic_s0_flag[rps_idx]
        }

        for (i = 0; i < num_positive_pics; i++) {
            bs_read_ue(bs); // delta_poc_s1_minus1[rps_idx]
            bs_skip_u1        (bs); // used_by_curr_pic_s1_flag[rps_idx]
        }
    }

    return 0;
}

static void skip_timing_info(bs_t *bs)
{
    bs_skip_bytes(bs, 4); // num_units_in_tick
    bs_skip_bytes(bs, 32); // time_scale

    if (bs_read_u1(bs))          // poc_proportional_to_timing_flag
        bs_read_ue(bs); // num_ticks_poc_diff_one_minus1
}

static void skip_sub_layer_hrd_parameters(bs_t *bs,
                                          unsigned int cpb_cnt_minus1,
                                          uint8_t sub_pic_hrd_params_present_flag)
{
    unsigned int i;

    for (i = 0; i <= cpb_cnt_minus1; i++) {
        bs_read_ue(bs); // bit_rate_value_minus1
        bs_read_ue(bs); // cpb_size_value_minus1

        if (sub_pic_hrd_params_present_flag) {
            bs_read_ue(bs); // cpb_size_du_value_minus1
            bs_read_ue(bs); // bit_rate_du_value_minus1
        }

        bs_skip_u1(bs); // cbr_flag
    }
}

static int skip_hrd_parameters(bs_t *bs, uint8_t cprms_present_flag,
                                unsigned int max_sub_layers_minus1)
{
    unsigned int i;
    uint8_t sub_pic_hrd_params_present_flag = 0;
    uint8_t nal_hrd_parameters_present_flag = 0;
    uint8_t vcl_hrd_parameters_present_flag = 0;

    if (cprms_present_flag) {
        nal_hrd_parameters_present_flag = bs_read_u1(bs);
        vcl_hrd_parameters_present_flag = bs_read_u1(bs);

        if (nal_hrd_parameters_present_flag ||
            vcl_hrd_parameters_present_flag) {
            sub_pic_hrd_params_present_flag = bs_read_u1(bs);

            if (sub_pic_hrd_params_present_flag)
                bs_skip_u(bs, 19);

            bs_skip_u(bs, 8);

            if (sub_pic_hrd_params_present_flag)
                bs_skip_u(bs, 4); // cpb_size_du_scale

            bs_skip_u(bs, 15);
        }
    }

    for (i = 0; i <= max_sub_layers_minus1; i++) {
        unsigned int cpb_cnt_minus1            = 0;
        uint8_t low_delay_hrd_flag             = 0;
        uint8_t fixed_pic_rate_within_cvs_flag = 0;
        uint8_t fixed_pic_rate_general_flag    = bs_read_u1(bs);

        if (!fixed_pic_rate_general_flag)
            fixed_pic_rate_within_cvs_flag = bs_read_u1(bs);

        if (fixed_pic_rate_within_cvs_flag)
            bs_read_ue(bs); // elemental_duration_in_tc_minus1
        else
            low_delay_hrd_flag = bs_read_u1(bs);

        if (!low_delay_hrd_flag) {
            cpb_cnt_minus1 = bs_read_ue(bs);
            if (cpb_cnt_minus1 > 31)
                return -1;
        }

        if (nal_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(bs, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);

        if (vcl_hrd_parameters_present_flag)
            skip_sub_layer_hrd_parameters(bs, cpb_cnt_minus1,
                                          sub_pic_hrd_params_present_flag);
    }

    return 0;
}

static void hevc_parse_vui( bs_t *bs,
                           HEVCDecoderConfigurationRecord *config,
                           unsigned int max_sub_layers_minus1)
{
    unsigned int min_spatial_segmentation_idc;

    if (bs_read_u1(bs))              // aspect_ratio_info_present_flag
        if (bs_read_u(bs, 8) == 255) // aspect_ratio_idc
            bs_skip_bytes(bs, 32); // sar_width u(16), sar_height u(16)

    if (bs_read_u1(bs))  // overscan_info_present_flag
        bs_skip_u1(bs); // overscan_appropriate_flag

    if (bs_read_u1(bs)) {  // video_signal_type_present_flag
        bs_skip_u(bs, 4); // video_format u(3), video_full_range_flag u(1)

        if (bs_read_u1(bs)) // colour_description_present_flag
            bs_skip_u(bs, 24);
    }

    if (bs_read_u1(bs)) {        // chroma_loc_info_present_flag
        bs_read_ue(bs); // chroma_sample_loc_type_top_field
        bs_read_ue(bs); // chroma_sample_loc_type_bottom_field
    }

    bs_skip_u(bs, 3);

    if (bs_read_u1(bs)) {        // default_display_window_flag
        bs_read_ue(bs); // def_disp_win_left_offset
        bs_read_ue(bs); // def_disp_win_right_offset
        bs_read_ue(bs); // def_disp_win_top_offset
        bs_read_ue(bs); // def_disp_win_bottom_offset
    }

    if (bs_read_u1(bs)) { // vui_timing_info_present_flag
        skip_timing_info(bs);

        if (bs_read_u1(bs)) // vui_hrd_parameters_present_flag
            skip_hrd_parameters(bs, 1, max_sub_layers_minus1);
    }

    if (bs_read_u1(bs)) { // bitstream_restriction_flag
        bs_skip_u(bs, 3);

        min_spatial_segmentation_idc = bs_read_ue(bs);

        config->min_spatial_segmentation_idc = MIN(config->min_spatial_segmentation_idc,
                                                   min_spatial_segmentation_idc);

        bs_read_ue(bs); // max_bytes_per_pic_denom
        bs_read_ue(bs); // max_bits_per_min_cu_denom
        bs_read_ue(bs); // log2_max_mv_length_horizontal
        bs_read_ue(bs); // log2_max_mv_length_vertical
    }
}

static int hevc_parse_sps( bs_t *bs, HEVCDecoderConfigurationRecord *config )
{
    unsigned int i, sps_max_sub_layers_minus1, log2_max_pic_order_cnt_lsb_minus4;
    unsigned int num_short_term_ref_pic_sets, num_delta_pocs[HEVC_MAX_SHORT_TERM_RPS_COUNT];

    bs_skip_u( bs, 4 ); // sps_video_parameter_set_id

    sps_max_sub_layers_minus1 = bs_read_u ( bs, 3 );
    config->numTemporalLayers = MAX(config->numTemporalLayers,
                                    sps_max_sub_layers_minus1 + 1);

    config->temporalIdNested = bs_read_u1( bs );

    hevc_parse_ptl( bs, config, sps_max_sub_layers_minus1);

    bs_read_ue( bs );// sps_seq_parameter_set_id

    config->chromaFormat = bs_read_ue( bs );

    if (config->chromaFormat == 3)
        bs_skip_u1( bs ); // separate_colour_plane_flag

    bs_read_ue(bs); // pic_width_in_luma_samples
    bs_read_ue(bs); // pic_height_in_luma_samples

    if (bs_read_u1(bs)) {        // conformance_window_flag
        bs_read_ue(bs); // conf_win_left_offset
        bs_read_ue(bs); // conf_win_right_offset
        bs_read_ue(bs); // conf_win_top_offset
        bs_read_ue(bs); // conf_win_bottom_offset
    }

    config->bitDepthLumaMinus8          = bs_read_ue(bs);
    config->bitDepthChromaMinus8        = bs_read_ue(bs);
    log2_max_pic_order_cnt_lsb_minus4 = bs_read_ue(bs);

    /* sps_sub_layer_ordering_info_present_flag */
    i = bs_read_u1(bs) ? 0 : sps_max_sub_layers_minus1;
    for (; i <= sps_max_sub_layers_minus1; i++)
        skip_sub_layer_ordering_info(bs);

    bs_read_ue(bs); // log2_min_luma_coding_block_size_minus3
    bs_read_ue(bs); // log2_diff_max_min_luma_coding_block_size
    bs_read_ue(bs); // log2_min_transform_block_size_minus2
    bs_read_ue(bs); // log2_diff_max_min_transform_block_size
    bs_read_ue(bs); // max_transform_hierarchy_depth_inter
    bs_read_ue(bs); // max_transform_hierarchy_depth_intra

    if (bs_read_u1(bs) && // scaling_list_enabled_flag
        bs_read_u1(bs))   // sps_scaling_list_data_present_flag
        skip_scaling_list_data(bs);

    bs_skip_u1(bs); // amp_enabled_flag
    bs_skip_u1(bs); // sample_adaptive_offset_enabled_flag

    if (bs_read_u1(bs)) {           // pcm_enabled_flag
        bs_skip_u         (bs, 4); // pcm_sample_bit_depth_luma_minus1
        bs_skip_u         (bs, 4); // pcm_sample_bit_depth_chroma_minus1
        bs_read_ue(bs);    // log2_min_pcm_luma_coding_block_size_minus3
        bs_read_ue(bs);    // log2_diff_max_min_pcm_luma_coding_block_size
        bs_skip_u1        (bs);    // pcm_loop_filter_disabled_flag
    }

    num_short_term_ref_pic_sets = bs_read_ue(bs);
    if (num_short_term_ref_pic_sets > HEVC_MAX_SHORT_TERM_RPS_COUNT)
        return -1;

    for (i = 0; i < num_short_term_ref_pic_sets; i++) {
        int ret = parse_rps(bs, i, num_short_term_ref_pic_sets, num_delta_pocs);
        if (ret < 0)
            return ret;
    }

    if (bs_read_u1(bs)) {                               // long_term_ref_pics_present_flag
        unsigned num_long_term_ref_pics_sps = bs_read_ue(bs);
        if (num_long_term_ref_pics_sps > 31U)
            return -1;
        for (i = 0; i < num_long_term_ref_pics_sps; i++) { // num_long_term_ref_pics_sps
            int len = MIN(log2_max_pic_order_cnt_lsb_minus4 + 4, 16);
            bs_skip_u (bs, len); // lt_ref_pic_poc_lsb_sps[i]
            bs_skip_u1(bs);      // used_by_curr_pic_lt_sps_flag[i]
        }
    }

    bs_skip_u1(bs); // sps_temporal_mvp_enabled_flag
    bs_skip_u1(bs); // strong_intra_smoothing_enabled_flag

    if (bs_read_u1(bs)) // vui_parameters_present_flag
        hevc_parse_vui(bs, config, sps_max_sub_layers_minus1);

    /* nothing useful for config past this point */
    return 0;
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
        nalu_list[i].nalu_type = nalu_type;
        nalu_list[i].size = nal_end - nal_start;
        nalu_list[i++].addr = nal_start;
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

static int hevc_parse_pps(bs_t *bs,
                          HEVCDecoderConfigurationRecord *config)
{
    uint8_t tiles_enabled_flag, entropy_coding_sync_enabled_flag;

    bs_read_ue(bs); // pps_pic_parameter_set_id
    bs_read_ue(bs); // pps_seq_parameter_set_id

    bs_skip_u(bs, 7);

    bs_read_ue(bs); // num_ref_idx_l0_default_active_minus1
    bs_read_ue(bs); // num_ref_idx_l1_default_active_minus1
    bs_read_se(bs); // init_qp_minus26

    bs_skip_u(bs, 2);

    if (bs_read_u1(bs))          // cu_qp_delta_enabled_flag
        bs_read_ue(bs); // diff_cu_qp_delta_depth

    bs_read_se(bs); // pps_cb_qp_offset
    bs_read_se(bs); // pps_cr_qp_offset

    bs_skip_u(bs, 4);

    tiles_enabled_flag               = bs_read_u1(bs);
    entropy_coding_sync_enabled_flag = bs_read_u1(bs);

    if (entropy_coding_sync_enabled_flag && tiles_enabled_flag)
        config->parallelismType = 0; // mixed-type parallel decoding
    else if (entropy_coding_sync_enabled_flag)
        config->parallelismType = 3; // wavefront-based parallel decoding
    else if (tiles_enabled_flag)
        config->parallelismType = 2; // tile-based parallel decoding
    else
        config->parallelismType = 1; // slice-based parallel decoding

    /* nothing useful for hvcC past this point */
    return 0;
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

