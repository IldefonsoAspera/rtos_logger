
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


enum log_data_type {
    LOG_STRING,
    LOG_UINT_DEC,
    LOG_INT_DEC,
    LOG_HEX_2,
    LOG_HEX_4,
    LOG_HEX_8,
    LOG_CHAR
};


#define log_str(str)    _log_const_string(str, strlen(str))

#define log_char(chr)    _log_char(chr)


#define log_dec(number) _log_var((uint32_t)number, _Generic((number),                  \
                                                        unsigned char:  LOG_UINT_DEC,  \
                                                        unsigned short: LOG_UINT_DEC,  \
                                                        unsigned long:  LOG_UINT_DEC,  \
                                                        unsigned int:   LOG_UINT_DEC,  \
                                                        char:           LOG_INT_DEC,   \
                                                        signed char:    LOG_INT_DEC,   \
                                                        signed short:   LOG_INT_DEC,   \
                                                        signed long:    LOG_INT_DEC,   \
                                                        signed int:     LOG_INT_DEC))


#define log_hex(number) _log_var((uint32_t)(number), _Generic((number),             \
                                                        unsigned char:  LOG_HEX_2,  \
                                                        unsigned short: LOG_HEX_4,  \
                                                        unsigned long:  LOG_HEX_8,  \
                                                        unsigned int:   LOG_HEX_8,  \
                                                        char:           LOG_HEX_2,  \
                                                        signed char:    LOG_HEX_2,  \
                                                        signed short:   LOG_HEX_4,  \
                                                        signed long:    LOG_HEX_8,  \
                                                        signed int:     LOG_HEX_8))


#define logc_str(cond, string)  do{ if(cond){ log_str(string); } } while(0)
#define logc_dec(cond, number)  do{ if(cond){ log_dec(number); } } while(0)
#define logc_hex(cond, number)  do{ if(cond){ log_hex(number); } } while(0)



void _log_var(uint32_t number, enum log_data_type type);
void _log_const_string(const char *string, uint32_t length);
void _log_char(char chr);



void logger_thread(void const * argument);
void logger_init(UART_HandleTypeDef *p_husart);



#ifdef  __cplusplus
}
#endif


#endif
