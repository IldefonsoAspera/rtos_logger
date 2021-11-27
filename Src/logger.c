
#include "logger.h"

#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include "main.h"
#include "cmsis_os.h"


#define LOG_INPUT_FIFO_N_ELEM   128
#define LOG_OUTPUT_BUFFER_SIZE  512
#define LOG_DELAY_LOOPS_MS      100     // Delay between log thread pollings to check if input queue contains data

static USART_TypeDef *mp_husart = NULL;

static char out_buf[LOG_OUTPUT_BUFFER_SIZE];
static uint32_t outBuf_idx = 0;



//      FIFO

typedef struct log_fifo_item_s
{
    uint32_t           data;
    uint16_t           str_len;
    enum log_data_type type;
} log_fifo_item_t;


typedef struct log_fifo_s
{
    log_fifo_item_t buffer[LOG_INPUT_FIFO_N_ELEM];
    uint32_t wrIdx;
    uint32_t rdIdx;
    uint32_t nItems;
} log_fifo_t;


static log_fifo_t logFifo;



static bool log_fifo_put(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    bool retVal = false;
    uint32_t primask_bit;

    primask_bit = __get_PRIMASK();
    __disable_irq();

    // Queue is not full if read and write indices are different or, 
    // if having the same value, the item counter is 0
    if(pFifo->wrIdx != pFifo->rdIdx || !pFifo->nItems)
    {
        pFifo->buffer[pFifo->wrIdx] = *pItem;
        pFifo->wrIdx = (pFifo->wrIdx + 1) & (LOG_INPUT_FIFO_N_ELEM - 1);
        pFifo->nItems++;
        retVal = true;    
    }

    __set_PRIMASK(primask_bit);
    return retVal;
}


static bool log_fifo_get(log_fifo_item_t *pItem, log_fifo_t *pFifo)
{
    bool retVal = false;
    uint32_t primask_bit;

    primask_bit = __get_PRIMASK();
    __disable_irq();

    // Queue is not empty if read and write indices are different or,
    // if having the same value, the item counter is not 0
    if(pFifo->rdIdx != pFifo->wrIdx || pFifo->nItems)
    {
        *pItem = pFifo->buffer[pFifo->rdIdx];
        pFifo->rdIdx = (pFifo->rdIdx + 1) & (LOG_INPUT_FIFO_N_ELEM - 1);
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


//      /FIFO



static inline uint8_t process_number_decimal(uint32_t number, char *output)
{
    uint32_t divider = 1000000000UL;
    uint8_t i;

    for(i=0; i<10; i++)
    {
        output[i] = 0x30 + number/divider;
        number %= divider;
        divider /= 10;
    }

    i = 0;
    while(i < 10 && output[i] == 0x30)
        i++;

    if(i == 10)     // Case when all digits are 0 should print one digit ('0')
        return 1;
    else            // Return number of digits to print without leading zeroes
        return 10-i;
}


static inline void process_number_hex(uint32_t number, char *output, uint8_t n_digits)
{
    char hexVals[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    int8_t i;

    for(i = sizeof(number)*2 - 1; i > -1; i--)
    {
        output[i] = hexVals[number & 0x0F];
        number >>= 4;
    }
}


static void proc_string(char *string, uint32_t length)
{
    while(length--)
        out_buf[outBuf_idx++] = *string++;
}


static void proc_uint_dec(uint32_t number)
{
    char output[10];
    uint8_t n_digits;

    n_digits = process_number_decimal(number, output);
    proc_string(&output[10 - n_digits], n_digits);
}


static void proc_hex(uint32_t number, uint8_t n_digits)
{
    char output[sizeof(number)*2];

    process_number_hex(number, output, n_digits);
    proc_string(&output[8 - n_digits], n_digits);
}


static void proc_sint_dec(int32_t number)
{
    char output[11];
    bool is_negative = number < 0;
    uint8_t n_digits;

    if(is_negative)
        number = -number;

    n_digits = process_number_decimal(number, &output[1]);

    if(is_negative)
        output[10 - n_digits++] = '-';

    proc_string(&output[11 - n_digits], n_digits);
}


void _log_var(uint32_t number, enum log_data_type type)
{
    log_fifo_item_t item = {.type = type, .data = number};
    log_fifo_put(&item, &logFifo);
}


void _log_const_string(const char *string, uint32_t length)
{
    log_fifo_item_t item = {.type = LOG_STRING, .data = (uint32_t)string, .str_len = length};
    log_fifo_put(&item, &logFifo);
}


void logger_thread(void const * argument)
{

    while(1)
    {
        log_fifo_item_t item;

        while(log_fifo_get(&item, &logFifo))
        {
            switch(item.type)
            {
            case LOG_STRING:
                proc_string((char*)item.data, item.str_len);
                break;
            case LOG_UINT_DEC:
                proc_uint_dec(item.data);
                break;
            case LOG_INT_DEC:
                proc_sint_dec((int32_t)item.data);
                break;
            case LOG_HEX_2:
                proc_hex(item.data, 2);
                break;
            case LOG_HEX_4:
                proc_hex(item.data, 4);
                break;
            case LOG_HEX_8:
                proc_hex(item.data, 8);
                break;
            }
        }

        for(int idx = 0; idx < outBuf_idx; idx++)
        {
            LL_USART_TransmitData8(USART2, out_buf[idx]);
            while(!LL_USART_IsActiveFlag_TC(USART2));
        }
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
        outBuf_idx = 0;
        

        osDelay(LOG_DELAY_LOOPS_MS);
    }
}


void logger_init(USART_TypeDef *p_husart)
{
    mp_husart = p_husart;

    static_assert(!(LOG_INPUT_FIFO_N_ELEM & (LOG_INPUT_FIFO_N_ELEM - 1)), "Log input queue must be power of 2");
    log_fifo_reset(&logFifo);
}
