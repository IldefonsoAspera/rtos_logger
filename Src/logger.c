
#include "logger.h"

#include <string.h>
#include <stdbool.h>

#include "main.h"
#include "cmsis_os.h"



#define LOG_INPUT_QUEUE_SIZE    128
#define LOG_OUTPUT_BUFFER_SIZE  512

struct queue_item
{
    uint32_t           data;
    uint16_t           str_len;
    enum log_data_type type;
};


static TIM_HandleTypeDef  *mp_htim  = NULL;

static QueueHandle_t qInput;
static StaticQueue_t qInputStatic;
static uint8_t qInputBuffer[LOG_INPUT_QUEUE_SIZE * sizeof(struct queue_item)];

static char out_buf[LOG_OUTPUT_BUFFER_SIZE];
static uint32_t outBuf_idx = 0;


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


void _log_const_string(const char *string, uint32_t length)
{
    struct queue_item item = {.type = LOG_STRING, .data = (uint32_t)string, .str_len = length};
    xQueueSendToBack(qInput, &item, 0);
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
    struct queue_item item = {.type = type, .data = number};
    xQueueSendToBack(qInput, &item, 0);
}


void logger_thread(void const * argument)
{
    volatile uint32_t i, j, result;

    HAL_TIM_Base_Start(mp_htim);
    while(1)
    {
        struct queue_item item;

        if(xQueueReceive(qInput, &item, portMAX_DELAY))
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

        if(!uxQueueMessagesWaiting(qInput))
        {
            uint32_t idx;
            __HAL_TIM_SET_COUNTER(mp_htim, 0);
            i = mp_htim->Instance->CNT;
            //HAL_UART_Transmit(mp_huart, (uint8_t*)out_buf, outBuf_idx, HAL_MAX_DELAY);
            for(idx = 0; idx < outBuf_idx; idx++)
            {
                LL_USART_TransmitData8(USART2, out_buf[idx]);
                while(!LL_USART_IsActiveFlag_TC(USART2));
            }

            HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
            j = mp_htim->Instance->CNT;
            result = j-i;                   // Number of CPU cycles to execute this section
            outBuf_idx = 0;

            //osDelay(500);
        }
    }
}


void logger_init(TIM_HandleTypeDef *p_htim)
{
    mp_htim  = p_htim;
    qInput   = xQueueCreateStatic(LOG_INPUT_QUEUE_SIZE, sizeof(struct queue_item), qInputBuffer, &qInputStatic);
}
