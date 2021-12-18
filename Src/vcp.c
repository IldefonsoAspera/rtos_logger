/*
 * vcp.c
 *
 *  Created on: Nov 28, 2021
 *      Author: Ilde
 */


#include "vcp.h"
#include <stdint.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "main.h"


static UART_HandleTypeDef*  mp_huart = NULL;

static uint8_t inputStreamBuffer[VCP_INPUT_BUFFER_SIZE];
static StaticStreamBuffer_t inputStreamCb;
static StreamBufferHandle_t inputStream;


void vcp_flush(void)
{
    uint8_t rxBuffer[16];
    uint32_t nChars;

    do
    {
        nChars = xStreamBufferReceive(inputStream, rxBuffer, sizeof(rxBuffer), 0);
        HAL_UART_Transmit(mp_huart, rxBuffer, nChars, HAL_MAX_DELAY);
    } while (nChars == sizeof(rxBuffer));
}


void vcp_th(void const * argument)
{

    while(1)
    {
        vcp_flush();
        HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    }
}


void vcp_send(const char* p_data, uint32_t length)
{
    xStreamBufferSend(inputStream, p_data, length, 0);
}


void vcp_init(UART_HandleTypeDef *p_huart)
{
    mp_huart = p_huart;
    inputStream = xStreamBufferCreateStatic(sizeof(inputStreamBuffer), 1, inputStreamBuffer, &inputStreamCb);
}
