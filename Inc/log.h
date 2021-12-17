/**
 * @file log.h
 * @author Ildefonso Aspera
 * @brief Logger library for FreeRTOS and Cortex M with input FIFO
 * @version 1.0.0
 * @date 2021-12-06
 *
 * @copyright Copyright (c) 2021
 *
 *
 * This library allows printing strings, characters and variables in decimal and hexadecimal to
 * a backend that will process the data. The library is isolated from it in order to allow any
 * type of backend to be used, whether it is a serial port, a virtual serial port or a file.
 * It is designed to be used with a low priority thread that will process its input FIFO,
 * which has been implemented so that application execution time is impacted as little as possible.
 *
 * Several macros have been implemented to automatically print with the appropiate format by
 * making use of _Generic from C11, meaning that signed variables are printed with minus sign if
 * negative and hexadecimal printing automatically adjust the number of digits if printing a
 * 8 bit variable, a 16 bit or a 32 bit one.
 *
 * Strings are stored in the input FIFO by reference, so they have to be constant between storage
 * and processing of the FIFO.
 * This FIFO is atomic, meaning that it disables all interrupts during insertions and extractions.
 * Measurements in a Cortex M0+ shows that it takes around 100 cycles to insert a new data, with
 * around 50 cycles required for the actual FIFO insertion (with interrupts disabled). Usage of a
 * RTOS queue was discarded because it would take around 550 cycles for the insertions in FreeRTOS.
 *
 * In order to process the data, its thread wakes up periodically to check if the input FIFO
 * contains data to process and converts it to strings that are sent to the backend. The library
 * only needs a callback function pointer during initialization to know where to send the
 * processed data.
 *
 *
 * Usage
 *
 * - For initialization call log_init() and pass a pointer to a function that will process and send
 * the output strings. The second pointer is optional (can be NULL) and allows the library to call the
 * backend's flush function when log_flush() is called.
 *
 * - log_thread() must be called from a low priority thread. The function does not return. The
 * function requires a stack of 144 bytes plus the backend requirement stack, so a FreeRTOS stack size
 * of 128 words should be enough for the thread.
 *
 * - To print constant strings call log_str() or logc_str() if a condition check is needed. These
 * macros automatically extract the string size at compile time to optimize processing time.
 *
 * - To print independent characters, call log_char() or logc_char(). These characters are read at
 * call time, so they do not need to be constant, unlike the strings.
 *
 * - To print variables with a decimal format, call log_dec() or logc_dec(). These variables will
 * be printing without leading zeroes and with '-' sign if variable is signed and negative, ie: -126
 *
 * - To print variables with a hexadecimal format, call log_hex() or logc_hex(). The variables will
 * be printed as unsigned and with 2 digits for 8 bit variables, 4 digits for 16 bit and 8 digits
 * for 32 bits, with leading zeroes if needed, but without the '0x' prefix. Ex: 0F3E00FF
 *
 * - To print arrays use log_array_dec(), logc_array_dec(), log_array_hex(), logc_array_hex(),
 * depending on the desired format. These functions require a pointer to the memory area with the
 * data and a number of elements to print. The size of each item is automatically extracted thanks
 * to _Generic. The separator used between each element is a space (' '). The data must not be
 * modified until the function returns (an interrupt that writes on the array could be problematic).
 *
 * Apart from that define, there is also LOG_INPUT_FIFO_N_ELEM, which defines the size of the input
 * FIFO in number of items, and LOG_DELAY_LOOPS_MS, which defines how often the logger thread
 * should wake up to check and process the input queue.
 *
 * A flush function of the input FIFO is also available in case the system needs to reset and all
 * remaining data must be processed outside of the logger thread. If during initialization,
 * a pointer was provided for backend flushing, this function calls it after processing input FIFO.
 *
 *
 * Public defines
 *
 * LOG_INPUT_FIFO_N_ELEM
 * LOG_DELAY_LOOPS_MS
 *
 *
 * Public functions/macros
 *
 * - log_init()
 * - log_thread()
 * - log_flush()
 *
 * - log_str()
 * - log_char()
 * - log_dec()
 * - log_hex()
 * - log_array_dec()
 * - log_array_hex()
 *
 * - logc_str()
 * - logc_char()
 * - logc_dec()
 * - logc_hex()
 * - logc_array_dec()
 * - logc_array_hex()
 *
 *
 * Usage example
 *
 * - log_init(uart_print, NULL);
 *
 * - log_str("Test");
 * - log_str("Test2");
 * - logc_str(PRINT_FSM_STATE, "Test2");
 *
 * - log_flush()
 *
 * - log_char('\r');
 * - logc_char(PRINT_CR, '\r');
 *
 * - log_dec(u32Var);
 * - log_dec(s8Var);
 * - log_hex(s16Var);
 * - log_dec(12556);          <-- NOTE: remember that literals are "signed int" by default
 * - log_dec(25000123UL);     <-- Will be printed as unsigned long thanks to "UL" suffix
 *
 * - uint16_t data[3] = {23, 156, 0};
 *   log_array_dec(data, ARRAY_N_ELEM(data));     <-- Assumes that a ARRAY_N_ELEM() macro exists
 *
 * - uint16_t data[3] = {23, 156, 0};
 *   logc_array_hex(PRINT_DATA, &data[1], 2);     <-- Prints only last two array elements
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
#include <stdbool.h>


/*********************** User configurable definitions ***********************/

#define LOG_INPUT_FIFO_N_ELEM   256     // Defines log input FIFO size in number of elements (const strings, variables, etc)
#define LOG_DELAY_LOOPS_MS      100     // Delay between log thread pollings to check if input queue contains data

/*****************************************************************************/


enum log_data_type {
    _LOG_STRING,
    _LOG_UINT_DEC,
    _LOG_INT_DEC_1,
    _LOG_INT_DEC_2,
    _LOG_INT_DEC_4,
    _LOG_HEX_1,
    _LOG_HEX_2,
    _LOG_HEX_4,
    LOG_CHAR
};


typedef void (*log_out_handler)(void* p_data, uint32_t length);
typedef void (*log_out_flush_handler)(void);



// Macro that returns the second element.
// Used to count the number of variable arguments
#define GET_MACRO(_1, NAME, ...) NAME

//  GET_MACRO(__VA_ARGS__ __VA_OPT__(,) _log_str((str), strlen(str) __VA_OPT__(,) __VA_ARGS__), _log_str((str), strlen(str)))

#define log_str(str)                    _log_str((str), strlen(str))
#define log_char(chr)                   _log_char(chr)
#define log_flush()                     _log_flush(true)


#define log_dec(number)                 _log_var((uint32_t)(number), _Generic((number),     \
                                                            unsigned char:  _LOG_UINT_DEC,  \
                                                            unsigned short: _LOG_UINT_DEC,  \
                                                            unsigned long:  _LOG_UINT_DEC,  \
                                                            unsigned int:   _LOG_UINT_DEC,  \
                                                            char:           _LOG_INT_DEC_1, \
                                                            signed char:    _LOG_INT_DEC_1, \
                                                            signed short:   _LOG_INT_DEC_2, \
                                                            signed long:    _LOG_INT_DEC_4, \
                                                            signed int:     _LOG_INT_DEC_4))


#define log_hex(number)                 _log_var((uint32_t)(number), _Generic((number),  \
                                                            unsigned char:  _LOG_HEX_1,  \
                                                            unsigned short: _LOG_HEX_2,  \
                                                            unsigned long:  _LOG_HEX_4,  \
                                                            unsigned int:   _LOG_HEX_4,  \
                                                            char:           _LOG_HEX_1,  \
                                                            signed char:    _LOG_HEX_1,  \
                                                            signed short:   _LOG_HEX_2,  \
                                                            signed long:    _LOG_HEX_4,  \
                                                            signed int:     _LOG_HEX_4))


#define log_array_dec(array, nItems)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],            \
                                                            unsigned char:  _LOG_UINT_DEC,  \
                                                            unsigned short: _LOG_UINT_DEC,  \
                                                            unsigned long:  _LOG_UINT_DEC,  \
                                                            unsigned int:   _LOG_UINT_DEC,  \
                                                            char:           _LOG_INT_DEC_1, \
                                                            signed char:    _LOG_INT_DEC_1, \
                                                            signed short:   _LOG_INT_DEC_2, \
                                                            signed long:    _LOG_INT_DEC_4, \
                                                            signed int:     _LOG_INT_DEC_4))


#define log_array_hex(array, nItems)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],         \
                                                            unsigned char:  _LOG_HEX_1,  \
                                                            unsigned short: _LOG_HEX_2,  \
                                                            unsigned long:  _LOG_HEX_4,  \
                                                            unsigned int:   _LOG_HEX_4,  \
                                                            char:           _LOG_HEX_1,  \
                                                            signed char:    _LOG_HEX_1,  \
                                                            signed short:   _LOG_HEX_2,  \
                                                            signed long:    _LOG_HEX_4,  \
                                                            signed int:     _LOG_HEX_4))


#define logc_str(cond, string)                do{ if(cond){ log_str(string); } } while(0)
#define logc_dec(cond, number)                do{ if(cond){ log_dec(number); } } while(0)
#define logc_hex(cond, number)                do{ if(cond){ log_hex(number); } } while(0)
#define logc_char(cond, chr)                  do{ if(cond){ log_char(chr);   } } while(0)
#define logc_array_dec(cond, array, nItems)   do{ if(cond){ log_array_dec((array), (nItems)); } } while(0)
#define logc_array_hex(cond, array, nItems)   do{ if(cond){ log_array_hex((array), (nItems)); } } while(0)



void _log_var(uint32_t number, enum log_data_type type);
void _log_str(char *string,    uint32_t length);
void _log_char(char chr);
void _log_array(void *pArray,  uint32_t nItems, uint8_t nBytesPerItem, enum log_data_type type);
void _log_flush(bool isPublicCall);


void log_thread(void const * argument);
void log_init(log_out_handler printHandler, log_out_flush_handler flushHandler);



#ifdef  __cplusplus
}
#endif


#endif
