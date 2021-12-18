/**
 * @defgroup log Logger library
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
 * Additionally, there are also functions to log arrays of data. They insert the individual items
 * in the input FIFO along with separators in between each item.
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
 * - log_char('\\r');
 * - logc_char(PRINT_CR, '\\r');
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
 *
 * @file log.h
 * @author Ildefonso Aspera
 * @brief Logger library for FreeRTOS and Cortex M with input FIFO
 * @version 0.2.0
 * @date 2021-12-18
 *
 * @copyright Copyright (c) 2021
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

#include <stdbool.h>

/*********************** User configurable definitions ***********************/

#define LOG_INPUT_FIFO_N_ELEM   256     ///< Defines log input FIFO size in number of elements (const strings, variables, etc)
#define LOG_DELAY_LOOPS_MS      100     ///< Delay between log thread pollings to check if input FIFO contains data
#define LOG_DEF_ARRAY_SEPARATOR ' '		///< Default separator for arrays
#define LOG_MSG_START_SYMBOL    '<'     ///< Symbol to be used when starting a message. If start label is used, it goes after the symbol
#define LOG_MSG_STOP_SYMBOL     '>'     ///< Symbol to be used when ending a message. If stop label is used, it goes before the symbol
#define LOG_MSG_LABEL_SEPARATOR ' '     ///< If label is used, declares what char to put between label and message body

/*****************************************************************************/



/**
 * @brief Internal use
 *
 */
enum log_data_type {
    _LOG_STRING,
    _LOG_UINT_DEC,
    _LOG_INT_DEC_1,
    _LOG_INT_DEC_2,
    _LOG_INT_DEC_4,
    _LOG_HEX_1,
    _LOG_HEX_2,
    _LOG_HEX_4,
    _LOG_CHAR,
    _LOG_MSG_START,
    _LOG_MSG_STOP,
};


/**
 * @brief Prototype for backend print handler
 *
 */
typedef void (*log_out_handler)(char* str, uint32_t length);


/**
 * @brief Prototype for backend flush handler
 *
 */
typedef void (*log_out_flush_handler)(void);


/**
 * @brief Internal use. Returns the second element of the arg list
 *
 */
#define GET_MACRO(_1, NAME, ...) NAME


/**
 * @brief Prints list of variables in decimal format. Removes leading zeroes when printing
 * This string output contains a separator between each item and the next one
 *
 * @param[in] array Pointer to array of variables to print
 * @param[in] nItems Number of variables to print
 * @param[in] ... Optional parameter to define what separator to use.
 *                Accepts chars and falls back to #LOG_DEF_ARRAY_SEPARATOR
 *
 */
#define log_array_dec(array, nItems, ...)   GET_MACRO(__VA_ARGS__ __VA_OPT__(,) \
                                                    _log_array_dec((array), (nItems), __VA_OPT__(,) __VA_ARGS__), \
                                                    _log_array_dec((array), (nItems), LOG_DEF_ARRAY_SEPARATOR))


/**
 * @brief Prints list of variables in hexadecimal format without removing leading zeroes
 * This string output contains a separator between each item and the next one
 * Auto formats each variable depending on its type (8 bit, 16 bit, 32 bit)
 *
 * @param[in] array Pointer to array of variables to print
 * @param[in] nItems Number of variables to print
 * @param[in] ... Optional parameter to define what separator to use.
 *                Accepts chars and falls back to #LOG_DEF_ARRAY_SEPARATOR
 *
 */
#define log_array_hex(array, nItems, ...)   GET_MACRO(__VA_ARGS__ __VA_OPT__(,) \
                                                    _log_array_hex((array), (nItems), __VA_OPT__(,) __VA_ARGS__), \
                                                    _log_array_hex((array), (nItems), LOG_DEF_ARRAY_SEPARATOR))


/**
 * @brief Prints passed constant string
 *
 * @param[in] str String to print
 */
#define log_str(str)                    _log_str((str), sizeof(str)-1)


/**
 * @brief Prints passed char
 *
 * @param[in] chr Character to print
 */
#define log_char(chr)                   _log_char(chr)


/**
 * @brief Flushes input FIFO to empty it. Usually called just before a reset
 *
 */
#define log_flush()                     _log_flush(true)


/**
 * @brief Prints starting symbol of a message (#LOG_MSG_START_SYMBOL) along with a passed label
 *
 * @param[in] label String to print right after starting symbol. Can be NULL for no string
 */
#define log_msg_start(label)            _log_msg_start((label), sizeof(label)-1)


/**
 * @brief Prints passed label along with an ending symbol of a message (#LOG_MSG_STOP_SYMBOL)
 *
 * @param[in] label String to print right before ending symbol. Can be NULL for no string
 */
#define log_msg_stop(label)             _log_msg_stop((label), sizeof(label)-1)


/**
 * @brief Prints variable in decimal format. Removes leading zeroes when printing
 *
 * @param[in] number Variable to print
 *
 */
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


/**
 * @brief Prints variable in hexadecimal format. Does not remove leading zeroes when printing
 *
 * @param[in] number Variable to print
 *
 */
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


/**
 * @brief Internal use
 *
 */
#define _log_array_dec(array, nItems, separator)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],            \
                                                            unsigned char:  _LOG_UINT_DEC,  \
                                                            unsigned short: _LOG_UINT_DEC,  \
                                                            unsigned long:  _LOG_UINT_DEC,  \
                                                            unsigned int:   _LOG_UINT_DEC,  \
                                                            char:           _LOG_INT_DEC_1, \
                                                            signed char:    _LOG_INT_DEC_1, \
                                                            signed short:   _LOG_INT_DEC_2, \
                                                            signed long:    _LOG_INT_DEC_4, \
                                                            signed int:     _LOG_INT_DEC_4), (separator))


/**
 * @brief Internal use
 *
 */
#define _log_array_hex(array, nItems, separator)    _log_array((uint32_t*)(array), (nItems), sizeof((array)[0]), \
                                                            _Generic((array)[0],         \
                                                            unsigned char:  _LOG_HEX_1,  \
                                                            unsigned short: _LOG_HEX_2,  \
                                                            unsigned long:  _LOG_HEX_4,  \
                                                            unsigned int:   _LOG_HEX_4,  \
                                                            char:           _LOG_HEX_1,  \
                                                            signed char:    _LOG_HEX_1,  \
                                                            signed short:   _LOG_HEX_2,  \
                                                            signed long:    _LOG_HEX_4,  \
                                                            signed int:     _LOG_HEX_4), (separator))


/**
 * @brief Conditional print. If condition is true, calls #log_str
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] string String to print
 *
 */
#define logc_str(cond, string)                do{ if(cond){ log_str(string); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_dec
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] number Variable to print
 *
 */
#define logc_dec(cond, number)                do{ if(cond){ log_dec(number); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_hex
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] number Variable to print
 *
 */
#define logc_hex(cond, number)                do{ if(cond){ log_hex(number); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_char
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] chr    Character to print
 *
 */
#define logc_char(cond, chr)                  do{ if(cond){ log_char(chr);   } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_array_dec
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] array  Array of variables to print
 * @param[in] nItems Number of variables to print
 *
 */
#define logc_array_dec(cond, array, nItems)   do{ if(cond){ log_array_dec((array), (nItems)); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_array_hex
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] array  Array of variables to print
 * @param[in] nItems Number of variables to print
 *
 */
#define logc_array_hex(cond, array, nItems)   do{ if(cond){ log_array_hex((array), (nItems)); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_msg_start
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] label  String to print
 *
 */
#define logc_msg_start(cond, label)           do{ if(cond){ log_msg_start(label); } } while(0)


/**
 * @brief Conditional print. If condition is true, calls #log_msg_stop
 *
 * @param[in] cond   Condition that must be evaluated true to execute print
 * @param[in] label  String to print
 *
 */
#define logc_msg_stop(cond, label)                   do{ if(cond){ log_msg_stop(label);  } } while(0)



void _log_var(uint32_t number, enum log_data_type type);
void _log_str(const char *str, uint32_t length);
void _log_char(char chr);
void _log_array(void *pArray, uint32_t nItems, uint8_t nBytesPerItem, enum log_data_type type, char separator);
void _log_flush(bool isPublicCall);
void _log_msg_start(const char *label, uint32_t length);
void _log_msg_stop(const char *label, uint32_t length);


/**
 * @brief Periodically checks the input FIFO to stringify items and send them to backend.
 * Must be executed from a thread because it never returns
 *
 * @param[in] argument Unused
 */
void log_thread(void const * argument);


/**
 * @brief Initializer for this library
 *
 * @param[in] printHandler Callback function to backend that will actually print stringified items
 * @param[in] flushHandler Optional callback to flush backend. Can be NULL
 */
void log_init(log_out_handler printHandler, log_out_flush_handler flushHandler);



#ifdef  __cplusplus
}
#endif


#endif
