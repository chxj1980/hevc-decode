// Last Update:2019-01-07 19:33:32
/**
 * @file hevc.c
 * @brief 
 * @author felix
 * @version 0.1.00
 * @date 2019-01-07
 */

#include <stdint.h>

#define MAX_SPATIAL_SEGMENTATION 4096 // max. value of u(12) field

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


int hevc_get_config( const uint8_t *data, int size, HEVCDecoderConfigurationRecord *config )
{
    if ( !data || !config ) {
        goto err;
    }

err:
    return -1;
}

