/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "ringbuff/ringbuff.h"

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

/* LPUART related functions */
void usart_init(void);
void usart_rx_check(void);
void usart_process_data(const void* data, size_t len);
void usart_send_string(const char* str);

/**
 * \brief           Calculate length of statically allocated array
 */
#define ARRAY_LEN(x)            (sizeof(x) / sizeof((x)[0]))

/**
 * \brief           Create ring buffer for TX DMA
 */
static ringbuff_t
usart_tx_dma_ringbuff;

/**
 * \brief           Ring buffer data array for TX DMA
 */
static uint8_t
usart_tx_dma_ringbuff_data[128];

/**
 * \brief           Length of TX DMA transfer
 */
static size_t
usart_tx_dma_current_len;

/**
 * \brief           Buffer for USART DMA
 * \note            Contains RAW unprocessed data received by UART and transfered by DMA
 */
static uint8_t
usart_rx_dma_buffer[64];

/**
 * \brief           Application entry point
 */
int
main(void) {
    /* MCU Configuration--------------------------------------------------------*/

    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
    NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    LL_PWR_DisableDeadBatteryPD();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize ringbuff */
    ringbuff_init(&usart_tx_dma_ringbuff, usart_tx_dma_ringbuff_data, sizeof(usart_tx_dma_ringbuff_data));

    /* Initialize all configured peripherals */
    usart_init();

    /* Notify user to start sending data */
    usart_send_string("USART DMA example: DMA HT & TC + USART IDLE LINE IRQ + RTOS processing\r\n");
    usart_send_string("Start sending data to STM32\r\n");

    while (1) {

    }
}

/**
 * \brief           Check for new data received with DMA
 */
void
usart_rx_check(void) {
    static size_t old_pos;
    size_t pos;

    /* Calculate current position in buffer */
    pos = ARRAY_LEN(usart_rx_dma_buffer) - LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_1);
    if (pos != old_pos) {                       /* Check change in received data */
        if (pos > old_pos) {                    /* Current position is over previous one */
            /* We are in "linear" mode */
            /* Process data directly by subtracting "pointers" */
            usart_process_data(&usart_rx_dma_buffer[old_pos], pos - old_pos);
        } else {
            /* We are in "overflow" mode */
            /* First process data to the end of buffer */
            usart_process_data(&usart_rx_dma_buffer[old_pos], ARRAY_LEN(usart_rx_dma_buffer) - old_pos);
            /* Check and continue with beginning of buffer */
            if (pos > 0) {
                usart_process_data(&usart_rx_dma_buffer[0], pos);
            }
        }
    }
    old_pos = pos;                              /* Save current position as old */

    /* Check and manually update if we reached end of buffer */
    if (old_pos == ARRAY_LEN(usart_rx_dma_buffer)) {
        old_pos = 0;
    }
}

/**
 * \brief           Check if DMA is active and if not try to send data
 */
uint8_t
usart_start_tx_dma_transfer(void) {
    uint32_t old_primask;
    uint8_t started = 0;

    /* Check if DMA is active */
    /* Must be set to 0 */
    old_primask = __get_PRIMASK();
    __disable_irq();

    /* Check if transfer is not active */
    if (usart_tx_dma_current_len == 0) {
        /* Check if something to send  */
        usart_tx_dma_current_len = ringbuff_get_linear_block_read_length(&usart_tx_dma_ringbuff);
        if (usart_tx_dma_current_len > 0) {
            /* Disable channel if enabled */
            LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);

            /* Clear all flags */
            LL_DMA_ClearFlag_TC2(DMA1);
            LL_DMA_ClearFlag_HT2(DMA1);
            LL_DMA_ClearFlag_GI2(DMA1);
            LL_DMA_ClearFlag_TE2(DMA1);

            /* Start DMA transfer */
            LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_2, usart_tx_dma_current_len);
            LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)ringbuff_get_linear_block_read_address(&usart_tx_dma_ringbuff));

            /* Start new transfer */
            LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_2);
            started = 1;
        }
    }

    __set_PRIMASK(old_primask);
    return started;
}

/**
 * \brief           Process received data over UART
 * \note            Either process them directly or copy to other bigger buffer
 * \param[in]       data: Data to process
 * \param[in]       len: Length in units of bytes
 */
void
usart_process_data(const void* data, size_t len) {
    ringbuff_write(&usart_tx_dma_ringbuff, data, len);  /* Write data to TX buffer for loopback */
    usart_start_tx_dma_transfer();              /* Then try to start transfer */
}

/**
 * \brief           Send string to USART
 * \param[in]       str: String to send
 */
void
usart_send_string(const char* str) {
    ringbuff_write(&usart_tx_dma_ringbuff, str, strlen(str));   /* Write data to TX buffer for loopback */
    usart_start_tx_dma_transfer();              /* Then try to start transfer */
}

/**
 * \brief           LPUART1 Initialization Function
 */
void
usart_init(void) {
    LL_LPUART_InitTypeDef LPUART_InitStruct = {0};
    LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Peripheral clock enable */
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_LPUART1);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMAMUX1);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

    /*
     * LPUART1 GPIO Configuration
     *
     * PA2   ------> LPUART1_TX
     * PA3   ------> LPUART1_RX
     */
    GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_12;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
    GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
    GPIO_InitStruct.Alternate = LL_GPIO_AF_12;
    LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* LPUART1 RX DMA init */
    LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_1, LL_DMAMUX_REQ_LPUART1_RX);
    LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_1, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PRIORITY_LOW);
    LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MODE_CIRCULAR);
    LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)&LPUART1->RDR);
    LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_1, (uint32_t)usart_rx_dma_buffer);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, ARRAY_LEN(usart_rx_dma_buffer));

    /* LPUART1 TX DMA init */
    LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_2, LL_DMAMUX_REQ_LPUART1_TX);
    LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_2, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
    LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PRIORITY_LOW);
    LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MODE_NORMAL);
    LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PERIPH_NOINCREMENT);
    LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MEMORY_INCREMENT);
    LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PDATAALIGN_BYTE);
    LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MDATAALIGN_BYTE);
    LL_DMA_SetPeriphAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)&LPUART1->TDR);

    /* Enable HT & TC interrupts for RX */
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);

    /* Enable HT & TC interrupts for TX */
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_2);

    /* DMA interrupt init for RX & TX */
    NVIC_SetPriority(DMA1_Channel1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    NVIC_SetPriority(DMA1_Channel2_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
    NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    /* Initialize LPUART1 */
    LPUART_InitStruct.PrescalerValue = LL_LPUART_PRESCALER_DIV1;
    LPUART_InitStruct.BaudRate = 115200;
    LPUART_InitStruct.DataWidth = LL_LPUART_DATAWIDTH_8B;
    LPUART_InitStruct.StopBits = LL_LPUART_STOPBITS_1;
    LPUART_InitStruct.Parity = LL_LPUART_PARITY_NONE;
    LPUART_InitStruct.TransferDirection = LL_LPUART_DIRECTION_TX_RX;
    LPUART_InitStruct.HardwareFlowControl = LL_LPUART_HWCONTROL_NONE;
    LL_LPUART_Init(LPUART1, &LPUART_InitStruct);
    LL_LPUART_SetTXFIFOThreshold(LPUART1, LL_LPUART_FIFOTHRESHOLD_1_8);
    LL_LPUART_SetRXFIFOThreshold(LPUART1, LL_LPUART_FIFOTHRESHOLD_1_8);
    LL_LPUART_DisableFIFO(LPUART1);
    LL_LPUART_EnableDMAReq_RX(LPUART1);
    LL_LPUART_EnableDMAReq_TX(LPUART1);
    LL_LPUART_EnableIT_IDLE(LPUART1);

    /* LPUART interrupt */
    NVIC_SetPriority(LPUART1_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
    NVIC_EnableIRQ(LPUART1_IRQn);

    /* Enable LPUART and DMA */
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_LPUART_Enable(LPUART1);
    while (!LL_LPUART_IsActiveFlag_TEACK(LPUART1) || !LL_LPUART_IsActiveFlag_REACK(LPUART1)) {}
}


/* Interrupt handlers here */

/**
 * \brief           DMA1 channel1 interrupt handler for LPUART1 RX
 */
void
DMA1_Channel1_IRQHandler(void) {
    /* Check half-transfer complete interrupt */
    if (LL_DMA_IsEnabledIT_HT(DMA1, LL_DMA_CHANNEL_1) && LL_DMA_IsActiveFlag_HT1(DMA1)) {
        LL_DMA_ClearFlag_HT1(DMA1);             /* Clear half-transfer complete flag */
        usart_rx_check();                       /* Check data */
    }

    /* Check transfer-complete interrupt */
    if (LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_1) && LL_DMA_IsActiveFlag_TC1(DMA1)) {
        LL_DMA_ClearFlag_TC1(DMA1);             /* Clear transfer complete flag */
        usart_rx_check();                       /* Check data */
    }

    /* Implement other events when needed */
}

/**
 * \brief           DMA1 channel2 interrupt handler for LPUART1 TX
 */
void
DMA1_Channel2_IRQHandler(void) {
    /* Check transfer-complete interrupt */
    if (LL_DMA_IsEnabledIT_TC(DMA1, LL_DMA_CHANNEL_2) && LL_DMA_IsActiveFlag_TC2(DMA1)) {
        LL_DMA_ClearFlag_TC2(DMA1);             /* Clear transfer complete flag */
        ringbuff_skip(&usart_tx_dma_ringbuff, usart_tx_dma_current_len);/* Skip buffer, it has been successfully sent out */
        usart_tx_dma_current_len = 0;           /* Reset data length */
        usart_start_tx_dma_transfer();          /* Start new transfer */
    }

    /* Implement other events when needed */
}


/**
 * \brief           LPUART1 global interrupt handler
 */
void
LPUART1_IRQHandler(void) {
    /* Check for IDLE line interrupt */
    if (LL_LPUART_IsEnabledIT_IDLE(LPUART1) && LL_LPUART_IsActiveFlag_IDLE(LPUART1)) {
        LL_LPUART_ClearFlag_IDLE(LPUART1);      /* Clear IDLE line flag */
        usart_rx_check();                       /* Check data */
    }

    /* Implement other events when needed */
}

/**
 * \brief           System Clock Configuration
 */
void
SystemClock_Config(void) {
    /* Set flash latency */
    LL_FLASH_SetLatency(LL_FLASH_LATENCY_8);
    if (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_8) {
        while (1) {}
    }

    /* Configure voltage range */
    LL_PWR_EnableRange1BoostMode();

    /* Configure HSI */
    LL_RCC_HSI_Enable();
    while (LL_RCC_HSI_IsReady() != 1) {}
    LL_RCC_HSI_SetCalibTrimming(64);

    /* Configure PLL */
    LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSI, LL_RCC_PLLM_DIV_4, 85, LL_RCC_PLLR_DIV_2);
    LL_RCC_PLL_EnableDomain_SYS();
    LL_RCC_PLL_Enable();
    while (LL_RCC_PLL_IsReady() != 1) {}

    /* Configure system clock */
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_2);
    while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {}

    /* Insure 1s transition state at intermediate medium speed clock based on DWT */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
    while (DWT->CYCCNT < 100) {}

    /* Configure prescalers */
    LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB1_DIV_1);

    /* Configure systick */
    LL_Init1msTick(170000000);
    LL_SYSTICK_SetClkSource(LL_SYSTICK_CLKSOURCE_HCLK);
    LL_SetSystemCoreClock(170000000);
    LL_RCC_SetLPUARTClockSource(LL_RCC_LPUART1_CLKSOURCE_PCLK1);
}

