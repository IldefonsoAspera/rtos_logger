
#ifndef VCP_TH_H_
#define VCP_TH_H_


#include "main.h"


#define VCP_INPUT_BUFFER_SIZE       1024


void vcp_flush(void);
void vcp_th(void const * argument);
void vcp_send(char* pData, uint32_t nBytes);
void vcp_init(UART_HandleTypeDef *p_huart);


#endif
