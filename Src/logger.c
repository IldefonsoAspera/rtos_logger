
#include "logger.h"

#include "main.h"
#include "cmsis_os.h"





void logger_thread(void const * argument)
{

    for(;;)
    {
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
        osDelay(50);
    }

}
