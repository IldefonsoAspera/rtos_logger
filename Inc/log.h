
#ifndef __LOGGER_H
#define __LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

// Eclipse CDT parser does not support C11, so suppress
// warning when parsing but not when compiling
#ifdef __CDT_PARSER__
    #define _Generic(...) 0
#endif


#include "main.h"

#include <string.h>


/*********************** User configurable definitions ***********************/

#define LOG_SUPPORT_ANSI_COLOR  1   // Activating colors increase element size

/*****************************************************************************/


enum log_data_type {
    LOG_STRING,
    LOG_UINT_DEC,
    LOG_INT_DEC,
    LOG_HEX_2,
    LOG_HEX_4,
    LOG_HEX_8,
    LOG_CHAR
};

enum log_color {
    LOG_COLOR_DEFAULT,
    LOG_COLOR_BLACK,
    LOG_COLOR_RED,
    LOG_COLOR_GREEN,
    LOG_COLOR_YELLOW,
    LOG_COLOR_BLUE,
    LOG_COLOR_MAGENTA,
    LOG_COLOR_CYAN,
    LOG_COLOR_WHITE,
    _LOG_COLOR_LEN,
    LOG_COLOR_NONE
};


#define GET_MACRO(_1, NAME, ...) NAME





#define log_str(str, ...)     GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_str(str, strlen(str) __VA_OPT__(,) __VA_ARGS__), \
                                                                  _log_str(str, strlen(str), LOG_COLOR_NONE))

#define log_char(chr, ...)    GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_char(chr __VA_OPT__(,) __VA_ARGS__),     \
                                                                  _log_char(chr, LOG_COLOR_NONE))

#define log_dec(number, ...)  GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_dec(number __VA_OPT__(,) __VA_ARGS__), \
                                                                  _log_dec(number, LOG_COLOR_NONE))

#define log_hex(number, ...)  GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_hex(number __VA_OPT__(,) __VA_ARGS__), \
                                                                  _log_hex(number, LOG_COLOR_NONE))


#define _log_dec(number, color) _log_var((uint32_t)number, _Generic((number),              \
                                                            unsigned char:  LOG_UINT_DEC,  \
                                                            unsigned short: LOG_UINT_DEC,  \
                                                            unsigned long:  LOG_UINT_DEC,  \
                                                            unsigned int:   LOG_UINT_DEC,  \
                                                            char:           LOG_INT_DEC,   \
                                                            signed char:    LOG_INT_DEC,   \
                                                            signed short:   LOG_INT_DEC,   \
                                                            signed long:    LOG_INT_DEC,   \
                                                            signed int:     LOG_INT_DEC), color)


#define _log_hex(number, color) _log_var((uint32_t)(number), _Generic((number),         \
                                                            unsigned char:  LOG_HEX_2,  \
                                                            unsigned short: LOG_HEX_4,  \
                                                            unsigned long:  LOG_HEX_8,  \
                                                            unsigned int:   LOG_HEX_8,  \
                                                            char:           LOG_HEX_2,  \
                                                            signed char:    LOG_HEX_2,  \
                                                            signed short:   LOG_HEX_4,  \
                                                            signed long:    LOG_HEX_8,  \
                                                            signed int:     LOG_HEX_8), color)

#define logc_str(cond, string, ...)  do{ if(cond){ log_str(string __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_dec(cond, number, ...)  do{ if(cond){ log_dec(number __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_hex(cond, number, ...)  do{ if(cond){ log_hex(number __VA_OPT__(,) __VA_ARGS__); } } while(0)


void _log_var(uint32_t number, enum log_data_type type, enum log_color color);
void _log_str(char *string,    uint32_t length,         enum log_color color);
void _log_char(char chr,       enum log_color color);


void log_flush(void);
void log_thread(void const * argument);
void log_init(UART_HandleTypeDef *p_husart);



#ifdef  __cplusplus
}
#endif


#endif
