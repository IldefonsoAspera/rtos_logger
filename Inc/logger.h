
#ifndef __LOGGER_H
#define __LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"


void logger_thread(void const * argument);

void logger_init(UART_HandleTypeDef *p_huart);



#ifdef  __cplusplus
}
#endif


#endif
