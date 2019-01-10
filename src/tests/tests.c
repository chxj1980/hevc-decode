// Last Update:2019-01-10 11:56:24
/**
 * @file tests.c
 * @brief 
 * @author felix
 * @version 0.1.00
 * @date 2019-01-10
 */

#include <stdio.h>

#include "hevc.h"


void dump_hevc_config( HEVCDecoderConfigurationRecord * config )
{

#define DUMP_MEMBER( member ) printf( #member" : %d\n", config->member )

    DUMP_MEMBER( configurationVersion );
    DUMP_MEMBER( general_profile_space );
    DUMP_MEMBER( general_tier_flag );
    DUMP_MEMBER( general_profile_idc );
    DUMP_MEMBER( general_profile_compatibility_flags );
    DUMP_MEMBER( general_constraint_indicator_flags );
    DUMP_MEMBER( general_constraint_indicator_flags );
    DUMP_MEMBER( general_level_idc );
    DUMP_MEMBER( min_spatial_segmentation_idc );
    DUMP_MEMBER( parallelismType );
    DUMP_MEMBER( chromaFormat );
    DUMP_MEMBER( bitDepthLumaMinus8 );
    DUMP_MEMBER( bitDepthChromaMinus8 );
    DUMP_MEMBER( avgFrameRate );
    DUMP_MEMBER( constantFrameRate );
    DUMP_MEMBER( numTemporalLayers );
    DUMP_MEMBER( temporalIdNested );
    DUMP_MEMBER( lengthSizeMinusOne );
    DUMP_MEMBER( numOfArrays );
}

int main()
{
    HEVCDecoderConfigurationRecord config;

    dump_hevc_config( &config );
    return 0;
}

