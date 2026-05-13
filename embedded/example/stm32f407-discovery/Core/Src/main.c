/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "rsid.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RX_BUF_SIZE 512 /* Must be power of 2 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;

I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
volatile uint8_t rx_buf[RX_BUF_SIZE];
volatile uint32_t rx_head = 0, rx_tail = 0;

#define CMD_BUF_SIZE 64
volatile uint8_t cmd_rx_buf[CMD_BUF_SIZE];
volatile uint32_t cmd_rx_head = 0;
static uint32_t cmd_rx_tail = 0;

static rsid_ctx_t g_ctx;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * ============================================================================
 *  UART Plumbing
 * ============================================================================
 *
 * Two UARTs are used:
 *   USART2 (PA2 TX / PA3 RX) — Data channel to RealSenseID module (115200 8N1).
 *                                 ISR-driven RX ring buffer (rx_buf) for the SDK.
 *   USART3 (PD8 TX / PB11 RX) — Debug console to host PC via USB-TTL (115200 8N1).
 *                                 printf() output + interactive command input.
 *
 * The RealSenseID SDK communicates through five platform callbacks wired in main():
 *   send, recv, purge, get_time_ms, sleep_ms
 */

/* Redirect printf to USART3 (debug console) */
int _write(int file, char *ptr, int len)
{
  (void)file;
  HAL_UART_Transmit(&huart3, (uint8_t*)ptr, (uint16_t)len, 1000);
  return len;
}

/*
 * ============================================================================
 *  RealSenseID Platform Callbacks (rsid_platform_t)
 * ============================================================================
 */

/* Debug: route SDK diagnostic messages to the debug console (only called with -DRSID_DEBUG=1) */
static void rsid_debug_callback(const char* msg, void* app_ctx)
{
  (void)app_ctx;
  printf("[RSID] %s\r\n", msg);
}

/* Send: blocking UART transmit to the RealSenseID module */
static int stm32_send(const uint8_t* data, uint32_t len, void* app_ctx)
{
  (void)app_ctx;
  return HAL_UART_Transmit(&huart2, (uint8_t*)data, (uint16_t)len, 1000) == HAL_OK ? 0 : -1;
}

/* Recv: pull bytes from the ISR ring buffer with timeout */
static int stm32_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx)
{
  (void)app_ctx;
  uint32_t start = HAL_GetTick();
  uint32_t received = 0;
  while (received < len)
  {
    if ((HAL_GetTick() - start) >= timeout_ms)
      return -1;
    if (rx_head != rx_tail)
    {
      data[received++] = rx_buf[rx_tail & (RX_BUF_SIZE - 1)];
      rx_tail++;
    }
  }
  return 0;
}

/* Purge: discard any stale bytes in the RX ring buffer */
static void stm32_purge(void* app_ctx)
{
  (void)app_ctx;
  __disable_irq();
  rx_head = rx_tail = 0;
  __enable_irq();
}

/* Timestamp: millisecond clock for SDK timeouts */
static uint32_t stm32_get_time_ms(void* app_ctx)
{
  (void)app_ctx;
  return HAL_GetTick();
}

/* Sleep: blocking delay for SDK retry logic */
static void stm32_sleep_ms(uint32_t ms, void* app_ctx)
{
  (void)app_ctx;
  HAL_Delay(ms);
}

/*
 * ============================================================================
 *  LED Helpers
 * ============================================================================
 *
 *  LD4 (PD12) Green  — success indicator
 *  LD5 (PD14) Red    — failure indicator
 *  LD3 (PD13) Orange — USART3 RX activity (toggled in ISR)
 *  LD6 (PD15) Blue   — available
 */

static void led_green(int on)
{
  HAL_GPIO_WritePin(GPIOD, LD4_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void led_red(int on)
{
  HAL_GPIO_WritePin(GPIOD, LD5_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*
 * ============================================================================
 *  Debug Console — Line Editor
 * ============================================================================
 *
 * Reads characters from the USART3 ISR ring buffer (cmd_rx_buf), echoes them
 * back, and assembles a complete line when CR or LF is received.
 * Returns the line length, or 0 if no complete line is available yet.
 */

static char cmd_line[64];
static uint32_t cmd_line_pos = 0;

static int cmd_readline(char* out, int max_len)
{
  while (cmd_rx_head != cmd_rx_tail)
  {
    uint8_t ch = cmd_rx_buf[cmd_rx_tail & (CMD_BUF_SIZE - 1)];
    cmd_rx_tail++;

    if (ch == '\r' || ch == '\n')
    {
      if (cmd_line_pos > 0)
      {
        int len = (int)cmd_line_pos;
        if (len >= max_len) len = max_len - 1;
        memcpy(out, cmd_line, len);
        out[len] = '\0';
        cmd_line_pos = 0;
        printf("\r\n");
        return len;
      }
    }
    else if (cmd_line_pos < sizeof(cmd_line) - 1)
    {
      cmd_line[cmd_line_pos++] = ch;
      HAL_UART_Transmit(&huart3, (uint8_t*)&ch, 1, 10);
    }
  }
  return 0;
}

/*
 * ============================================================================
 *  SDK Callbacks — Authentication & Enrollment
 * ============================================================================
 */

static rsid_ctx_t* g_cancel_ctx = NULL; /* set during auth loop for button cancel */

static int blue_button_pressed(void)
{
  return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_SET;
}

static void on_auth_result(rsid_auth_status status, const char* user_id, short score, void* ctx)
{
  (void)ctx;
  if (status == RSID_Auth_Success)
  {
    printf("AUTH OK: %s (score=%d)\r\n", user_id, (int)score);
    led_green(1);
    led_red(0);
  }
  else
  {
    printf("AUTH FAIL: %d\r\n", (int)status);
    led_green(0);
    led_red(1);
  }
}

static void on_auth_hint(rsid_auth_status hint, float frame_score, void* ctx)
{
  (void)ctx;
//  printf("  hint: %d (%.2f)\r\n", (int)hint, (float)frame_score);

  /* Blue button cancels auth loop */
  if (g_cancel_ctx && blue_button_pressed())
  {
    printf("  [CANCEL requested]\r\n");
    rsid_cancel(g_cancel_ctx);
    g_cancel_ctx = NULL;
  }
}

static void on_enroll_result(rsid_enroll_status status, void* ctx)
{
  (void)ctx;
  if (status == RSID_Enroll_Success)
  {
    printf("ENROLL OK\r\n");
    led_green(1);
    led_red(0);
  }
  else
  {
    printf("ENROLL FAIL: %d\r\n", (int)status);
    led_green(0);
    led_red(1);
  }
}

static void on_enroll_progress(rsid_face_pose pose, void* ctx)
{
  (void)ctx;
  const char* names[] = {"Center", "Up", "Down", "Left", "Right"};
  if (pose <= RSID_Face_Right)
    printf("  pose: %s\r\n", names[pose]);
}

/*
 * ============================================================================
 *  Command Handler
 * ============================================================================
 *
 * Commands (type 'help' at the console):
 *   p/ping           Ping the device (binary packet echo test)
 *   v/version        Query firmware module versions (text protocol)
 *   a/auth           Single authentication attempt
 *   l/loop           Continuous auth loop (BLUE BUTTON to cancel)
 *   e <name>         Enroll a new user
 *   r <name>         Remove a user by name
 *   ra/remove-all    Remove all enrolled users
 *   u/users          Query enrolled user count
 *   sn/serial        Query device serial number
 *   c/config         Query device configuration
 *   reboot           Reboot the device
 */

static void handle_command(rsid_ctx_t* ctx, const char* cmd)
{
  rsid_status st;

  if (strcmp(cmd, "p") == 0 || strcmp(cmd, "ping") == 0)
  {
    uint32_t t0 = HAL_GetTick();
    st = rsid_ping(ctx);
    uint32_t dt = HAL_GetTick() - t0;
    printf("ping: %s (%lu ms)\r\n", st == RSID_Ok ? "OK" : "FAIL", (unsigned long)dt);
    led_green(st == RSID_Ok);
    led_red(st != RSID_Ok);
  }
  else if (strcmp(cmd, "v") == 0 || strcmp(cmd, "version") == 0)
  {
    char ver[256];
    st = rsid_query_firmware_version(ctx, ver, sizeof(ver));
    if (st == RSID_Ok)
      printf("FW: %s\r\n", ver);
    else
      printf("version FAIL (%d)\r\n", (int)st);
  }
  else if (strcmp(cmd, "a") == 0 || strcmp(cmd, "auth") == 0)
  {
    rsid_auth_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_auth_result;
    cb.on_hint = on_auth_hint;
    printf("Authenticating...\r\n");
    rsid_authenticate(ctx, &cb, NULL);
  }
  else if (strcmp(cmd, "l") == 0 || strcmp(cmd, "loop") == 0)
  {
    rsid_auth_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_auth_result;
    cb.on_hint = on_auth_hint;
    g_cancel_ctx = ctx;
    printf("Auth loop (BLUE BUTTON to stop)...\r\n");
    rsid_authenticate_loop(ctx, &cb, NULL);
    g_cancel_ctx = NULL;
    printf("Auth loop ended\r\n");
  }
  else if (strncmp(cmd, "e ", 2) == 0 || strncmp(cmd, "enroll ", 7) == 0)
  {
    const char* name = (cmd[0] == 'e' && cmd[1] == ' ') ? cmd + 2 : cmd + 7;
    rsid_enroll_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.on_result = on_enroll_result;
    cb.on_progress = on_enroll_progress;
    printf("Enrolling '%s'...\r\n", name);
    rsid_enroll(ctx, name, &cb, NULL);
  }
  else if (strcmp(cmd, "u") == 0 || strcmp(cmd, "users") == 0)
  {
    unsigned int count = 0;
    st = rsid_query_number_of_users(ctx, &count);
    if (st == RSID_Ok)
      printf("Users: %u\r\n", count);
    else
      printf("users FAIL (%d)\r\n", (int)st);
  }
  else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "config") == 0)
  {
    rsid_device_config_t cfg;
    st = rsid_query_device_config(ctx, &cfg);
    if (st == RSID_Ok)
      printf("Config OK (rotation=%d, security=%d)\r\n",
             (int)cfg.camera_rotation, (int)cfg.security_level);
    else
      printf("config FAIL (%d)\r\n", (int)st);
  }
  else if (strncmp(cmd, "r ", 2) == 0 || strncmp(cmd, "remove ", 7) == 0)
  {
    const char* name = (cmd[0] == 'r' && cmd[1] == ' ') ? cmd + 2 : cmd + 7;
    st = rsid_remove_user(ctx, name);
    printf("remove '%s': %s (%d)\r\n", name, st == RSID_Ok ? "OK" : "FAIL", (int)st);
  }
  else if (strcmp(cmd, "ra") == 0 || strcmp(cmd, "remove-all") == 0)
  {
    printf("Removing all users...\r\n");
    st = rsid_remove_all(ctx);
    printf("remove-all: %s (%d)\r\n", st == RSID_Ok ? "OK" : "FAIL", (int)st);
  }
  else if (strcmp(cmd, "sn") == 0 || strcmp(cmd, "serial") == 0)
  {
    char sn[64];
    st = rsid_query_serial_number(ctx, sn, sizeof(sn));
    if (st == RSID_Ok)
      printf("SN: %s\r\n", sn);
    else
      printf("serial FAIL (%d)\r\n", (int)st);
  }
  else if (strcmp(cmd, "reboot") == 0)
  {
    rsid_reboot(ctx);
    printf("Rebooting device...\r\n");
  }
  else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0)
  {
    printf("Commands:\r\n");
    printf("  p/ping        Ping device\r\n");
    printf("  v/version     Firmware version\r\n");
    printf("  a/auth        Authenticate once\r\n");
    printf("  l/loop        Auth loop (BLUE BUTTON to cancel)\r\n");
    printf("  e <name>      Enroll user\r\n");
    printf("  r <name>      Remove user\r\n");
    printf("  ra/remove-all Remove all users\r\n");
    printf("  u/users       User count\r\n");
    printf("  sn/serial     Device serial number\r\n");
    printf("  c/config      Query config\r\n");
    printf("  reboot        Reboot device\r\n");
  }
  else
  {
    printf("Unknown: '%s' (type 'help')\r\n", cmd);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART2_UART_Init();
  MX_CRC_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  /* Enable RX interrupts for both UARTs (ISR fills ring buffers) */
  __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);  /* Device data */
  __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);  /* Debug console */

  /* Wire RealSenseID SDK platform callbacks */
  g_ctx.platform.send       = stm32_send;
  g_ctx.platform.recv       = stm32_recv;
  g_ctx.platform.purge      = stm32_purge;
  g_ctx.platform.get_time_ms = stm32_get_time_ms;
  g_ctx.platform.sleep_ms   = stm32_sleep_ms;
  g_ctx.platform.debug      = rsid_debug_callback;
  g_ctx.platform.app_ctx    = NULL;
  rsid_init(&g_ctx);

  printf("\r\n=== RealSenseID STM32 Demo ===\r\n");

  /*
   * F460 DCD UART: the TX channel is disabled by default on the device.
   * "init 1" enables bidirectional communication on the debug UART so the
   * device sends responses back to us (not just to its own debug console).
   * Wait 500ms for the device to process the command, then flush any
   * boot/init noise from the RX buffer before accepting user commands.
   */
  {
    const char init_cmd[] = "\r\ninit 1\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)init_cmd, sizeof(init_cmd) - 1, 1000);
    HAL_Delay(500);
    stm32_purge(NULL);
    printf("Device UART initialized\r\n");
  }

  printf("Type 'help' for commands\r\n> ");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    char cmd[64];
    if (cmd_readline(cmd, sizeof(cmd)) > 0)
    {
      handle_command(&g_ctx, cmd);
      printf("> ");
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port, OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : CS_I2C_SPI_Pin */
  GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PDM_OUT_Pin */
  GPIO_InitStruct.Pin = PDM_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : I2S3_WS_Pin */
  GPIO_InitStruct.Pin = I2S3_WS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(I2S3_WS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CLK_IN_Pin */
  GPIO_InitStruct.Pin = CLK_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
  HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD4_Pin LD3_Pin LD5_Pin LD6_Pin
                           Audio_RST_Pin */
  GPIO_InitStruct.Pin = LD4_Pin|LD3_Pin|LD5_Pin|LD6_Pin
                          |Audio_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : I2S3_MCK_Pin I2S3_SCK_Pin I2S3_SD_Pin */
  GPIO_InitStruct.Pin = I2S3_MCK_Pin|I2S3_SCK_Pin|I2S3_SD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : VBUS_FS_Pin */
  GPIO_InitStruct.Pin = VBUS_FS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(VBUS_FS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : OTG_FS_ID_Pin OTG_FS_DM_Pin OTG_FS_DP_Pin */
  GPIO_InitStruct.Pin = OTG_FS_ID_Pin|OTG_FS_DM_Pin|OTG_FS_DP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : OTG_FS_OverCurrent_Pin */
  GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : MEMS_INT2_Pin */
  GPIO_InitStruct.Pin = MEMS_INT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
