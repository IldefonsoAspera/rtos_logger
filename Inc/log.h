/**
 * @file log.h
 * @author Ildefonso Aspera
 * @brief Logger library for FreeRTOS and Cortex M with input FIFO
 * @version 1.0
 * @date 2021-12-05
 *
 * @copyright Copyright (c) 2021
 *
 *
 * This library allows printing strings, variables in decimal and hexadecimal and characters.
 * It is design around a low priority FreeRTOS thread that will process its input FIFO, which
 * is used so that application execution time is impacted as little as possible.
 *
 * Several macros have been implemented to automatically print with the appropiate format by
 * making use of _Generic from C11, meaning that signed variables are printed with minus sign if
 * negative and hexadecimal printing automatically adjust the number of digits if printing a
 * 8 bit variable, a 16 bit or a 32 bit one.
 * Strings are stored in the input FIFO by reference, so they have to be constant between storage
 * and processing of the FIFO
 *
 * In order to process the data, its thread wakes up periodically to check if the input FIFO
 * contains data to process and converts it to strings that are sent to the backend. The library
 * only needs a callback function pointer during initialization to know where to send the
 * processed data.
 * ANSI coloring is also supported by activating a flag to provide colored text if needed.
 *
 * Usage:
 *
 * - log_thread() must be called from a thread. The function does not return.
 *
 * - For initialization call log_init() and pass a pointer to a function that will process and send
 * the output strings.
 *
 * - To print constant strings call log_str() or logc_str() if a condition check is needed. These
 * macros automatically extract the string size at compile time to optimize processing time.
 *
 * - To print independent characters, call log_char() or logc_char(). These characters are read at
 * call time, so they do not need to be constant, unlike the strings.
 *
 * - To print variables with a decimal format, call log_dec() or logc_dec(). These variables will
 * be printing without leading zeroes, and with '-' sign if variable is signed and negative.
 *
 * - To print variables with a hexadecimal format, call log_hex() or logc_hex(). The variables will
 * be printed as unsigned and with 2 digits for 8 bit variables, 4 digits for 16 bit and 8 digits
 * for 32 bits, with leading zeroes if needed, but without the '0x' prefix.
 *
 * - To print arrays use log_array_dec(), logc_array_dec(), log_array_hex(), logc_array_hex(),
 * depending on the desired format. These functions require a pointer to the memory area with the
 * data and a number of elements to print. The size of each item is automatically extracted thanks
 * to _Generic. The separator used between each element is a space (' ').
 *
 * All functions support an optional last parameter in the function call to configure the desired
 * ANSI color to print the item. It is supported (but ignored) even if LOG_SUPPORT_ANSI_COLOR is
 * set to 0.
 *
 * Apart from that define, there is also LOG_INPUT_FIFO_N_ELEM, which defines the size of the input
 * FIFO in number of items, and LOG_DELAY_LOOPS_MS, which defines how often the logger thread
 * should wake up to check and process the input queue.
 *
 * A flush function of the input FIFO is also available in case the system needs to reset and all
 * remaining data must be processed outside of the logger thread.
 *
 *
 * Public defines:
 *
 * LOG_INPUT_FIFO_N_ELEM
 * LOG_DELAY_LOOPS_MS
 * LOG_SUPPORT_ANSI_COLOR
 *
 *
 * Public functions/macros:
 *
 * log_init()
 * log_thread()
 * log_flush()
 *
 * log_str()
 * log_char()
 * log_dec()
 * log_hex()
 * log_array_dec()
 * log_array_hex()
 *
 * logc_str()
 * logc_char()
 * logc_dec()
 * logc_hex()
 * logc_array_dec()
 * logc_array_hex()
 *
 *
 * Usage example:
 *
 * log_init(uart_print);
 *
 * log_str("Test");
 * log_str("Test2", LOG_COLOR_RED);
 * logc_str(PRINT_FSM_STATE, "Test2", LOG_COLOR_YELLOW);
 *
 * log_char('\r');
 * logc_char(PRINT_CR, '\r');
 *
 * log_dec(u32Var);
 * log_dec(s8Var);
 * log_hex(s16Var);
 * log_dec(12556);          <-- NOTE: remember that number literals are "signed int" by default
 * log_dec(25000123UL);     <-- Will be printed as unsigned long thanks to "UL" suffix
 *
 * uint16_t data[3] = {23, 156, 0};
 * log_array_dec(data, ARRAY_N_ELEM(data));     <-- Assumes that a ARRAY_N_ELEM() macro exists
 * logc_array_hex(PRINT_DATA, &data[1], 2);     <-- Prints only last two array elements
 *
 */


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

#define LOG_INPUT_FIFO_N_ELEM   128     // Defines log input FIFO size in number of elements (const strings, variables, etc)
#define LOG_DELAY_LOOPS_MS      100     // Delay between log thread pollings to check if input queue contains data
#define LOG_SUPPORT_ANSI_COLOR  1       // Activating colors increase element size

/*****************************************************************************/


enum log_data_type {
    LOG_STRING,
    LOG_UINT_DEC,
    LOG_INT_DEC_1,
    LOG_INT_DEC_2,
    LOG_INT_DEC_4,
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


typedef void (*log_out_handler)(void* p_data, uint32_t length);



// Macro that returns the second element.
// Used to count the number of variable arguments
#define GET_MACRO(_1, NAME, ...) NAME



#define log_str(str, ...)           GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_str((str), strlen(str) __VA_OPT__(,) __VA_ARGS__), \
                                                                        _log_str((str), strlen(str), LOG_COLOR_NONE))

#define log_char(chr, ...)          GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_char((chr) __VA_OPT__(,) __VA_ARGS__),   \
                                                                        _log_char((chr), LOG_COLOR_NONE))

#define log_dec(number, ...)        GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_dec((number) __VA_OPT__(,) __VA_ARGS__), \
                                                                        _log_dec((number), LOG_COLOR_NONE))

#define log_hex(number, ...)        GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_hex((number) __VA_OPT__(,) __VA_ARGS__), \
                                                                        _log_hex((number), LOG_COLOR_NONE))

#define log_array_dec(array, nItems, ...)   GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_array_dec((array), (nItems) __VA_OPT__(,) __VA_ARGS__), \
                                                                                _log_array_dec((array), (nItems), LOG_COLOR_NONE))

#define log_array_hex(array, nItems, ...)   GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_array_hex((array), (nItems) __VA_OPT__(,) __VA_ARGS__), \
                                                                                _log_array_hex((array), (nItems), LOG_COLOR_NONE))



#define _log_dec(number, color) _log_var((uint32_t)(number), _Generic((number),            \
                                                            unsigned char:  LOG_UINT_DEC,  \
                                                            unsigned short: LOG_UINT_DEC,  \
                                                            unsigned long:  LOG_UINT_DEC,  \
                                                            unsigned int:   LOG_UINT_DEC,  \
                                                            char:           LOG_INT_DEC_1, \
                                                            signed char:    LOG_INT_DEC_1, \
                                                            signed short:   LOG_INT_DEC_2, \
                                                            signed long:    LOG_INT_DEC_4, \
                                                            signed int:     LOG_INT_DEC_4), (color))


#define _log_hex(number, color) _log_var((uint32_t)(number), _Generic((number),         \
                                                            unsigned char:  LOG_HEX_2,  \
                                                            unsigned short: LOG_HEX_4,  \
                                                            unsigned long:  LOG_HEX_8,  \
                                                            unsigned int:   LOG_HEX_8,  \
                                                            char:           LOG_HEX_2,  \
                                                            signed char:    LOG_HEX_2,  \
                                                            signed short:   LOG_HEX_4,  \
                                                            signed long:    LOG_HEX_8,  \
                                                            signed int:     LOG_HEX_8), (color))


#define _log_array_dec(array, nItems, color)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],           \
                                                            unsigned char:  LOG_UINT_DEC,  \
                                                            unsigned short: LOG_UINT_DEC,  \
                                                            unsigned long:  LOG_UINT_DEC,  \
                                                            unsigned int:   LOG_UINT_DEC,  \
                                                            char:           LOG_INT_DEC_1, \
                                                            signed char:    LOG_INT_DEC_1, \
                                                            signed short:   LOG_INT_DEC_2, \
                                                            signed long:    LOG_INT_DEC_4, \
                                                            signed int:     LOG_INT_DEC_4), (color))


#define _log_array_hex(array, nItems, color)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],        \
                                                            unsigned char:  LOG_HEX_2,  \
                                                            unsigned short: LOG_HEX_4,  \
                                                            unsigned long:  LOG_HEX_8,  \
                                                            unsigned int:   LOG_HEX_8,  \
                                                            char:           LOG_HEX_2,  \
                                                            signed char:    LOG_HEX_2,  \
                                                            signed short:   LOG_HEX_4,  \
                                                            signed long:    LOG_HEX_8,  \
                                                            signed int:     LOG_HEX_8), (color))


// Suppress syntax error for conditional logs when parsing with IntelliSense or CDT parser
#if defined(__INTELLISENSE__) || defined(__CDT_PARSER__)
#define logc_str(cond, string, ...)  0
#define logc_dec(cond, number, ...)  0
#define logc_hex(cond, number, ...)  0
#define logc_char(cond, chr, ...)    0
#define logc_array_dec(cond, array, nItems, ...)    0
#define logc_array_hex(cond, array, nItems, ...)    0
#else
#define logc_str(cond, string, ...)  do{ if(cond){ log_str((string) __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_dec(cond, number, ...)  do{ if(cond){ log_dec((number) __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_hex(cond, number, ...)  do{ if(cond){ log_hex((number) __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_char(cond, chr, ...)    do{ if(cond){ log_char((chr)   __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_array_dec(cond, array, nItems, ...)   do{ if(cond){ log_array_dec((array), (nItems) __VA_OPT__(,) __VA_ARGS__); } } while(0)
#define logc_array_hex(cond, array, nItems, ...)   do{ if(cond){ log_array_hex((array), (nItems) __VA_OPT__(,) __VA_ARGS__); } } while(0)
#endif


void _log_var(uint32_t number, enum log_data_type type, enum log_color color);
void _log_str(char *string,    uint32_t length,         enum log_color color);
void _log_char(char chr,       enum log_color color);
void _log_array(void *pArray, uint32_t nItems, uint8_t nBytesPerItem, enum log_data_type type, enum log_color color);


void log_flush(void);
void log_thread(void const * argument);
void log_init(log_out_handler callback);



#ifdef  __cplusplus
}
#endif


#endif
