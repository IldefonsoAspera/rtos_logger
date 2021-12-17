
#include "log.h"

#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "main.h"
#include "cmsis_os.h"



#define LOG_ARRAY_N_ELEM(x)     (sizeof(x)/sizeof((x)[0]))



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
        uint8_t  nChars;
    };
    enum log_data_type type;
} log_fifo_item_t;


typedef struct log_fifo_s
{
    log_fifo_item_t buffer[LOG_INPUT_FIFO_N_ELEM];
    uint32_t wrIdx;
    uint32_t rdIdx;
    uint32_t nItems;
} log_fifo_t;



static log_fifo_t            logFifo;
static log_out_handler       mPrintHandler = NULL;
static log_out_flush_handler mFlushHandler = NULL;



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


static void log_fifo_reset(log_fifo_t *pFifo)
{
    pFifo->rdIdx  = 0;
    pFifo->wrIdx  = 0;
    pFifo->nItems = 0;
}


static void process_string(const char *str, uint32_t length)
{
    if(mPrintHandler)
        mPrintHandler(str, length);
}


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


void _log_var(uint32_t number, enum log_data_type type)
{
    log_fifo_item_t item = {.type = type, .uData = number};
    log_fifo_put(&item, &logFifo);
}


void _log_str(const char *str, uint32_t length)
{
    log_fifo_item_t item = {.type = _LOG_STRING, .str = str, .strLen = length};
    log_fifo_put(&item, &logFifo);
}


void _log_char(char chr)
{
    log_fifo_item_t item = {.type = LOG_CHAR, .chr[0] = chr, .nChars = 1};
    log_fifo_put(&item, &logFifo);
}


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

        _log_var(value, type);
        offset += nBytesPerItem;
        if(nItems)                      // Skips separator after last array item
            _log_char(separator);
    }
}


void _log_flush(bool isPublicCall)
{
    log_fifo_item_t item;

    if(logFifo.nItems == LOG_ARRAY_N_ELEM(logFifo.buffer))
        process_string("\r\nLog input FIFO full\r\n", strlen("\r\nLog input FIFO full\r\n"));

    while(log_fifo_get(&item, &logFifo))
    {
        switch(item.type)
        {
        case _LOG_STRING:
            process_string(item.str, item.strLen);
            break;
        case _LOG_UINT_DEC:
            process_decimal(item.uData, false);
            break;
        case _LOG_INT_DEC_1:
            if((int8_t)item.sData < 0)
                process_decimal((uint32_t)-((int8_t)item.sData), true);
            else
                process_decimal(item.uData, false);
            break;
        case _LOG_INT_DEC_2:
            if((int16_t)item.sData < 0)
                process_decimal((uint32_t)-((int16_t)item.sData), true);
            else
                process_decimal(item.uData, false);
            break;
        case _LOG_INT_DEC_4:
            if((int32_t)item.sData < 0)
                process_decimal((uint32_t)-((int32_t)item.sData), true);
            else
                process_decimal(item.uData, false);
            break;
        case _LOG_HEX_1:
            process_hexadecimal(item.uData, 2);
            break;
        case _LOG_HEX_2:
            process_hexadecimal(item.uData, 4);
            break;
        case _LOG_HEX_4:
            process_hexadecimal(item.uData, 8);
            break;
        case LOG_CHAR:
            process_string(item.chr, item.nChars);
        }
    }

    if(isPublicCall && mFlushHandler)
        mFlushHandler();
}


void log_thread(void const * argument)
{

    while(1)
    {
        _log_flush(false);
        osDelay(LOG_DELAY_LOOPS_MS);
    }
}


void log_init(log_out_handler printHandler, log_out_flush_handler flushHandler)
{
    mPrintHandler = printHandler;
    mFlushHandler = flushHandler;
    static_assert(!(LOG_INPUT_FIFO_N_ELEM & (LOG_INPUT_FIFO_N_ELEM - 1)), "Log input queue must be power of 2");
    log_fifo_reset(&logFifo);
}
