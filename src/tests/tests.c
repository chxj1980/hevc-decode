// Last Update:2019-01-10 13:27:20
/**
 * @file tests.c
 * @brief 
 * @author felix
 * @version 0.1.00
 * @date 2019-01-10
 */

#include <stdio.h>

#include "hevc.h"

static char gbuffer[ MAX_BUF_LEN ];

#define HEVC_RAW_FILE "../src/tests/media/surfing.265"
#define BUFFER_SIZE (10*1024*1024)

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

char *test_hevc_parse_config()
{
    FILE *fp = fopen( HEVC_RAW_FILE, "r" );

    ASSERT_NOT_EQUAL( fp, NULL );
    return NULL;
}

static char *all_tests()
{
    RUN_TEST_CASE( test_hevc_parse_config );

    return NULL;
}

int main()
{
    HEVCDecoderConfigurationRecord config;
    char *res = AllTests();

    dump_hevc_config( &config );

    if ( res ) {
        printf("%s\n", res );
    } else {
        printf("[ AdtsDecodeTest ] test pass\n");
    }
    return 0;
}

