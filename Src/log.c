/**
 * @ingroup log Logger library
 * @file log.c
 *
 *
 */

#include "log.h"

#include <stdbool.h>
#include <assert.h>

#include "main.h"
#include "cmsis_os.h"



#define LOG_ARRAY_N_ELEM(x)     (sizeof(x)/sizeof((x)[0]))  ///< Returns the number of items in an array

#define LOG_FIFO_FULL_MSG       "\r\nLog input FIFO full\r\n"


/**
 * @brief Structure for each item in the input FIFO
 *
 */
typedef struct log_fifo_item_s
{
    union
    {
        uint32_t    uData;
        int32_t     sData;
        const char *str;
        char        chr[4];
    };
    union
    {
        uint16_t strLen;
        uint8_t nBytes;
        struct
        {
            uint8_t nChars;
            char msgSymbol;
        };
    };
    enum log_data_type type;    ///< Type of item to store in the FIFO
} log_fifo_item_t;


/************************************** Input FIFO ******************************************/

/**
 * @brief Struct for the input FIFO control block
 *
 */
typedef struct log_fifo_s
{
    log_fifo_item_t buffer[LOG_INPUT_FIFO_N_ELEM];  ///< Buffer where data will be stored
    uint32_t wrIdx;                                 ///< Index of next position to write
    uint32_t rdIdx;                                 ///< Index of next position to read
    uint32_t nItems;                                ///< Number of items in buffer
} log_fifo_t;



static log_fifo_t logFifo;


/**
 * @brief Inserts new item atomically in input FIFO if there is space
 *
 * @param[in]     pItem Pointer to item to insert
 * @param[in,out] pFifo Pointer to FIFO where to insert the item
 */
static inline void log_fifo_put(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    uint32_t primaskBit;

    primaskBit = __get_PRIMASK();
    __disable_irq();

    if(pFifo->nItems < LOG_ARRAY_N_ELEM(pFifo->buffer))
    {
        pFifo->buffer[pFifo->wrIdx++] = *pItem;
        pFifo->wrIdx &= LOG_INPUT_FIFO_N_ELEM - 1;
        pFifo->nItems++;
    }

    __set_PRIMASK(primaskBit);
}


/**
 * @brief Gets item atomically from FIFO if it was not empty
 *
 * @param[in]     pItem Pointer to memory where item will be stored
 * @param[in,out] pFifo Pointer to FIFO from where the item will be extracted
 * @return true   If an item has been extracted
 * @return false  If FIFO was empty and no item was extracted
 */
static inline bool log_fifo_get(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    bool retVal = false;
    uint32_t primaskBit;

    primaskBit = __get_PRIMASK();
    __disable_irq();

    if(pFifo->nItems)
    {
        *pItem = pFifo->buffer[pFifo->rdIdx++];
        pFifo->rdIdx &= LOG_INPUT_FIFO_N_ELEM - 1;
        pFifo->nItems--;
        retVal = true;
    }

    __set_PRIMASK(primaskBit);
    return retVal;
}


/**
 * @brief Resets FIFO's indices, effectively emptying it
 *
 * @param[out] pFifo Pointer to FIFO to reset
 */
static void log_fifo_reset(log_fifo_t *pFifo)
{
    pFifo->rdIdx  = 0;
    pFifo->wrIdx  = 0;
    pFifo->nItems = 0;
}


/************************************** END Input FIFO ******************************************/


/**
 * @brief Weak (default) function for flushing the backend (UART, USB, etc)
 *
 */
__weak void log_cb_backend_flush(void)
{
    /* NOTE : This function should not be modified, when the callback is needed,
            the log_cb_backend_flush could be implemented in the user file
    */
}


/**
 * @brief Weak (default) function to write string in backend input buffer
 *
 * @param str String to send to backend
 * @param length Number of bytes of the string
 */
__weak void log_cb_backend_write(char* str, uint32_t length)
{
    /* NOTE : This function should not be modified, when the callback is needed,
            the log_cb_backend_write could be implemented in the user file
    */
}


/**
 * @brief Sends string to backend
 *
 * @param[in] str    Pointer to string to print
 * @param[in] length Number of characters of the string, not including the null terminator
 */
static void process_string(char *str, uint32_t length)
{
    log_cb_backend_write(str, length);
}


/**
 * @brief Converts number to string with hexadecimal format. Does not remove leading zeroes
 *
 * @param[in] number  Number to be stringified
 * @param[in] nDigits Number of digits to generate. Depends on the type (for max value) of the to be converted number
 */
static void process_hexadecimal(uint32_t number, uint8_t nDigits)
{
    const char hexVals[16] = {'0','1','2','3','4','5','6','7',
                              '8','9','A','B','C','D','E','F'};
    char output[8];
    int8_t i;

    // Fill char array starting at the end
    for(i = nDigits-1 ; i > -1; i--)
    {
        output[i] = hexVals[number & 0x0F];
        number >>= 4;
    }

    process_string(output, nDigits);
}


/**
 * @brief Converts number to string and removes leading zeroes
 *
 * @param[in] number     Number to print
 * @param[in] isNegative True if number is signed and negative
 */
static void process_decimal(uint32_t number, bool isNegative)
{
    char output[11];
    uint32_t divider = 1000000000UL;
    uint8_t i = 0;

    if(isNegative)
        output[i++] = '-';

    while(number < divider)
        divider /= 10;

    while(divider >= 10)
    {
        output[i++] = 0x30 + number/divider;
        number %= divider;
        divider /= 10;
    }

    output[i++] = 0x30 + number;
    process_string(output, i);
}


/**
 * @brief Stores number in input FIFO
 *
 * @param[in] number Number to store. Can be any type except float or larger than 32 bits
 * @param[in] nBytes Size of variable inserted in @p number in bytes
 * @param[in] type   Type of representation of the number to store
 */
void _log_var(uint32_t number, uint8_t nBytes, enum log_data_type type)
{
    log_fifo_item_t item = {.type = type, .uData = number, .nBytes = nBytes};
    log_fifo_put(&item, &logFifo);
}


/**
 * @brief Stores pointer to constant string in input FIFO
 *
 * @param[in] str    Pointer to string
 * @param[in] length String length without the null terminator. Must be smaller than 65536
 */
void _log_str(const char *str, uint16_t length)
{
    log_fifo_item_t item = {.type = _LOG_STRING, .str = str, .strLen = length};
    if(str)
        log_fifo_put(&item, &logFifo);
}


/**
 * @brief Stores char in input FIFO
 *
 * @param[in] chr Character to store
 */
void _log_char(char chr)
{
    log_fifo_item_t item = {.type = _LOG_CHAR, .chr[0] = chr, .nChars = 1};
    log_fifo_put(&item, &logFifo);
}


/**
 * @brief Stores an array in the input FIFO.
 * Insertions are not atomic, meaning that between two different numbers an interrupt
 * could take place because this function stores items iteratively
 *
 * @param[in] pArray        Pointer to input array where data to print is stored
 * @param[in] nItems        Number of variables in the array to print
 * @param[in] nBytesPerItem Number of bytes per item, i.e. int16_t items would have a value of 2
 * @param[in] type          Type of each element of the array
 * @param[in] separator     Separator to be used between elements of the array
 */
void _log_array(void *pArray, uint32_t nItems, uint8_t nBytesPerItem, enum log_data_type type, char separator)
{
    uint8_t *pData = (uint8_t*) pArray;
    uint32_t offset = 0;
    uint32_t value;

    while(nItems--)
    {
        if(nBytesPerItem == 4)
            value = *((uint32_t*)&pData[offset]);
        else if(nBytesPerItem == 2)
            value = *((uint16_t*)&pData[offset]);
        else
            value = *((uint8_t*)&pData[offset]);

        _log_var(value, nBytesPerItem, type);
        offset += nBytesPerItem;
        if(nItems)                      // Skips separator after last array item
            _log_char(separator);
    }
}


/**
 * @brief Stores msg start symbol along with pointer to const string in input FIFO
 *
 * @param[in] label     String containing the label
 * @param[in] length    Number of characters of the label, without the null terminator. Must be smaller than 256
 */
void _log_msg_start(const char *label, uint8_t length)
{
    log_fifo_item_t item = {.type = _LOG_MSG_START, .str = label, .nChars = length, .msgSymbol = LOG_MSG_START_SYMBOL};
    log_fifo_put(&item, &logFifo);
}


/**
 * @brief Stores msg stop symbol along with pointer to const string in input FIFO
 *
 * @param[in] label     String containing the label
 * @param[in] length    Number of characters of the label, without the null terminator. Must be smaller than 256
 */
void _log_msg_stop(const char *label, uint8_t length)
{
    log_fifo_item_t item = {.type = _LOG_MSG_STOP, .str = label, .nChars = length, .msgSymbol = LOG_MSG_STOP_SYMBOL};
    log_fifo_put(&item, &logFifo);
}



/**
 * @brief Processes all items in the input FIFO, sending their outputs to the backend in a blocking way
 *
 * @param[in] isPublicCall  True if function is called from outside this library
 */
void _log_flush(bool isPublicCall)
{
    log_fifo_item_t item;

    if(logFifo.nItems == LOG_ARRAY_N_ELEM(logFifo.buffer))
        process_string(LOG_FIFO_FULL_MSG, sizeof(LOG_FIFO_FULL_MSG)-1);

    while(log_fifo_get(&item, &logFifo))
    {
        switch(item.type)
        {
        case _LOG_STRING:
            process_string((char*)item.str, item.strLen);
            break;
        case _LOG_UINT_DEC:
            process_decimal(item.uData, false);
            break;
        case _LOG_INT_DEC:
            if(item.sData < 0)
                process_decimal(-item.uData, true);
            else
                process_decimal(item.uData, false);
            break;
        case _LOG_HEX:
            process_hexadecimal(item.uData, item.nBytes*2);
            break;
        case _LOG_CHAR:
            process_string(item.chr, item.nChars);
            break;
        case _LOG_MSG_START:
            process_string(&item.msgSymbol, 1);
            if(item.str)
            {
                process_string((char*)item.str, item.nChars);
                char separator = LOG_MSG_LABEL_SEPARATOR;
                process_string(&separator, 1);
            }
            break;
        case _LOG_MSG_STOP:
            if(item.str)
            {
                char separator = LOG_MSG_LABEL_SEPARATOR;
                process_string(&separator, 1);
                process_string((char*)item.str, item.nChars);
            }
            process_string(&item.msgSymbol, 1);
            break;
        }
    }

    if(isPublicCall)
        log_cb_backend_flush();
}


void log_thread(void const * argument)
{

    while(1)
    {
        _log_flush(false);
        osDelay(LOG_DELAY_LOOPS_MS);
    }
}


void log_init(void)
{
    static_assert(!(LOG_INPUT_FIFO_N_ELEM & (LOG_INPUT_FIFO_N_ELEM - 1)), "Log input queue must be power of 2");
    log_fifo_reset(&logFifo);
}