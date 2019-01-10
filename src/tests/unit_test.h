// Last Update:2018-12-14 17:43:30
/**
 * @file unittest.h
 * @brief 
 * @author liyq
 * @version 0.1.00
 * @date 2018-10-26
 */

#ifndef UNITTEST_H
#define UNITTEST_H

#include <string.h>

#define mu_assert(test) do { \
    if ( !(test) ) { \
        memset( gbuffer, 0, sizeof(gbuffer) );\
        sprintf( gbuffer, "[ FAIL ] run test case : %s, line : %d, "#test, __FUNCTION__, __LINE__ ); \
        return gbuffer;\
    } \
} while (0)

#define RUN_TEST_CASE(test) do { \
    char *message = test();  \
    if (message) {  return message; } else { printf( "[ PASS ] run test case : "#test"\n"); }\
} while (0)

#define ASSERT_EQUAL(a,b) do { \
    if ( !(a == b) ) { \
        memset( gbuffer, 0, sizeof(gbuffer) );\
        sprintf( gbuffer, "[ FAIL ] run test case : %s, line : %d, "#a" = %d, "#b" = %d", __FUNCTION__,  __LINE__, a, b ); \
        return gbuffer;\
    } \
} while (0)

#define ASSERT_NOT_EQUAL(a,b) do { \
    if ( !(a != b) ) { \
        memset( gbuffer, 0, sizeof(gbuffer) );\
        sprintf( gbuffer, "[ FAIL ] run test case : %s, line : %d, "#a" = %ld, "#b" = %ld", __FUNCTION__, __LINE__, (long)a, (long)b ); \
        return gbuffer;\
    } \
} while (0)


#define ASSERT_STR_EQUAL( a, b ) mu_assert( strcmp(a, b) == 0 )

#define ASSERT_MEM_EQUAL( a, b, len ) do { \
    if ( !(memcmp( a, b , len ) == 0) ) { \
        DumpBuffer( #a, a, len, __LINE__ ); \
        DumpBuffer( #b, b, len, __LINE__ ); \
        memset( gbuffer, 0, sizeof(gbuffer) );\
        sprintf( gbuffer, "[ FAIL ] run test case : %s line : %d "#a" = "#b"\n", __FUNCTION__, __LINE__ ); \
        return gbuffer;\
    } \
} while (0)



#endif  /*UNITTEST_H*/

