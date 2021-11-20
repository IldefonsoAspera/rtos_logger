
#include "logger.h"

#include <string.h>

#include "main.h"
#include "cmsis_os.h"



UART_HandleTypeDef *mp_huart = NULL;


void logger_thread(void const * argument)
{

    for(;;)
    {
        HAL_UART_Transmit(mp_huart, (uint8_t*)"Hello world\n", strlen("Hello world\n"), HAL_MAX_DELAY);
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
        osDelay(500);
    }

}

void logger_init(UART_HandleTypeDef *p_huart)
{
    mp_huart = p_huart;
}
