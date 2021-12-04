
#include "log.h"

#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "main.h"
#include "cmsis_os.h"
#include "vcp.h"



#define LOG_ARRAY_N_ELEM(x)     (sizeof(x)/sizeof((x)[0]))


#if LOG_SUPPORT_ANSI_COLOR
#define LOG_ANSI_PREFIX         "\x1B["
#define LOG_ANSI_SUFFIX         'm'

static char ansi_colors[_LOG_COLOR_LEN][2] = {
    {'0', ' '},
    {'3', '0'},
    {'3', '1'},
    {'3', '2'},
    {'3', '3'},
    {'3', '4'},
    {'3', '5'},
    {'3', '6'},
    {'3', '7'},
};
#endif


typedef struct log_fifo_item_s
{
    union
    {
        uint32_t uData;
        int32_t  sData;
        char *   str;
        char     chr[4];
    };
    union
    {
        uint16_t str_len;
        uint8_t  nChars;
    };
    enum log_data_type type;
#if LOG_SUPPORT_ANSI_COLOR
    enum log_color     color;
#endif
} log_fifo_item_t;


typedef struct log_fifo_s
{
    log_fifo_item_t buffer[LOG_INPUT_FIFO_N_ELEM];
    uint32_t wrIdx;
    uint32_t rdIdx;
    uint32_t nItems;
} log_fifo_t;



static log_fifo_t logFifo;



static inline void log_fifo_put(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    uint32_t primask_bit;

    primask_bit = __get_PRIMASK();
    __disable_irq();

    if(pFifo->nItems < LOG_ARRAY_N_ELEM(pFifo->buffer))
    {
        pFifo->buffer[pFifo->wrIdx++] = *pItem;
        pFifo->wrIdx &= LOG_INPUT_FIFO_N_ELEM - 1;
        pFifo->nItems++;
    }

    __set_PRIMASK(primask_bit);
}


static inline bool log_fifo_get(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    bool retVal = false;
    uint32_t primask_bit;

    primask_bit = __get_PRIMASK();
    __disable_irq();

    if(pFifo->nItems)
    {
        *pItem = pFifo->buffer[pFifo->rdIdx++];
        pFifo->rdIdx &= LOG_INPUT_FIFO_N_ELEM - 1;
        pFifo->nItems--;
        retVal = true;
    }

    __set_PRIMASK(primask_bit);
    return retVal;
}


static void log_fifo_reset(log_fifo_t *pFifo)
{
    pFifo->rdIdx  = 0;
    pFifo->wrIdx  = 0;
    pFifo->nItems = 0;
}



#if LOG_SUPPORT_ANSI_COLOR
static void set_color(enum log_color color)
{
    if(color != LOG_COLOR_NONE)
    {
        char str[5] = LOG_ANSI_PREFIX;

        str[2] = ansi_colors[color][0];
        if(color != LOG_COLOR_DEFAULT)
        {
            str[3] = ansi_colors[color][1];
            str[4] = LOG_ANSI_SUFFIX;
            vcp_send(str, 5);
        }
        else
        {
            str[3] = LOG_ANSI_SUFFIX;
            vcp_send(str, 4);
        }
    }
}
#endif


static void process_string(char *string, uint32_t length)
{
    vcp_send(string, length);
}


static void process_hexadecimal(uint32_t number, uint8_t nDigits)
{
    const char hexVals[16] = {'0','1','2','3','4','5','6','7',
                              '8','9','A','B','C','D','E','F'};
    char output[nDigits];
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

    while(number >= 10)
    {
        output[i++] = 0x30 + number/divider;
        number %= divider;
        divider /= 10;
    }

    output[i++] = 0x30 + number;
    process_string(output, i);
}


void _log_var(uint32_t number, enum log_data_type type, enum log_color color)
{
    log_fifo_item_t item = {.type = type, .uData = number};

#if LOG_SUPPORT_ANSI_COLOR
        item.color = color;
#endif

    log_fifo_put(&item, &logFifo);
}


void _log_str(char *string, uint32_t length, enum log_color color)
{
    log_fifo_item_t item = {.type = LOG_STRING, .str = string, .str_len = length};

#if LOG_SUPPORT_ANSI_COLOR
        item.color = color;
#endif

    log_fifo_put(&item, &logFifo);
}


void _log_char(char chr, enum log_color color)
{
    log_fifo_item_t item = {.type = LOG_CHAR, .chr[0] = chr, .nChars = 1};

#if LOG_SUPPORT_ANSI_COLOR
        item.color = color;
#endif

    log_fifo_put(&item, &logFifo);
}


void _log_array(void *pArray, uint32_t nItems, uint8_t nBytesPerItem, enum log_data_type type, enum log_color color)
{
    uint8_t* pData = (uint8_t*) pArray;
    uint32_t offset = 0;

    while(nItems--)
    {
        _log_var(pData[offset], type, color);
        offset += nBytesPerItem;
        if(nItems)                      // Skips separator after last array item
            _log_char(' ', color);
    }
}


void log_flush(void)
{
    log_fifo_item_t item;

    while(log_fifo_get(&item, &logFifo))
    {
#if LOG_SUPPORT_ANSI_COLOR
        set_color(item.color);
#endif
        switch(item.type)
        {
        case LOG_STRING:
            process_string(item.str, item.str_len);
            break;
        case LOG_UINT_DEC:
            process_decimal(item.uData, false);
            break;
        case LOG_INT_DEC:
            if(item.sData < 0)
                process_decimal((uint32_t)(-item.sData), true);
            else
                process_decimal(item.uData, false);
            break;
        case LOG_HEX_2:
            process_hexadecimal(item.uData, 2);
            break;
        case LOG_HEX_4:
            process_hexadecimal(item.uData, 4);
            break;
        case LOG_HEX_8:
            process_hexadecimal(item.uData, 8);
            break;
        case LOG_CHAR:
            process_string(item.chr, item.nChars);
        }
    }
}


void log_thread(void const * argument)
{

    while(1)
    {
        log_flush();
        osDelay(LOG_DELAY_LOOPS_MS);
    }
}


void log_init(void)
{
    static_assert(!(LOG_INPUT_FIFO_N_ELEM & (LOG_INPUT_FIFO_N_ELEM - 1)), "Log input queue must be power of 2");
    log_fifo_reset(&logFifo);
}
