#include <stdio.h>
#include <stdbool.h>

#include "ff.h"
#include "diskio.h"
#include "adc.h"
#include "tim.h"
#include "main.h"
#include "energymeter.h"

extern header_t header;
extern uint32_t error_status;
extern RTC_HandleTypeDef hrtc;

volatile uint32_t timer_flag; // 10 ms timer flag
volatile uint32_t sync_flag;  // 1000 ms timer flag
volatile uint32_t tim_cnt;    // 1000 ms counter

volatile uint32_t adc_flag;   // adc conversion flag
uint32_t adc[ADC_CH_CNT];     // adc conversion buffer
int16_t adc_calc[ADC_CH_CNT]; // adc real value buffer
float adc_mv[ADC_CH_CNT];     // adc mV calc buffer

float hv_voltage_cal;
float hv_current_cal;

static FIL totalFile;
static FIL raceFile;
static FIL lapFile;

static bool raceRunning = false;

static uint16_t raceNumber = 0;
static uint8_t currentLap = 0;
static uint32_t lastStartStopTime = 0;
static uint32_t lastLapTime = 0;

static void startRace(void);
static void stopRace(void);
static void nextLap(void);

static volatile bool startStopRequest = false;
static volatile bool lapRequest = false;


// size must be aligned to 4 bytes
static uint16_t checksum(uint16_t *data, size_t size) {
  uint16_t checksum = 0;

  for (int i = 0; i < (size >> 1); i++) {
    checksum ^= data[i];
  }

  return checksum;
}

static void getCurrentDateTime(RTC_DateTypeDef *date, RTC_TimeTypeDef *time)
{
    HAL_RTC_GetTime(&hrtc, time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, date, RTC_FORMAT_BIN);
}

static void startRace(void)
{
    char filename[_MAX_LFN];
    FRESULT ret;
    UINT written;

    raceNumber++;
    currentLap = 1;

    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    getCurrentDateTime(&sDate, &sTime);

    sprintf(filename,
            "race-%d-20%02d-%02d-%02d-%02d-%02d-%02d-%03d.log",
            raceNumber,
            sDate.Year,
            sDate.Month,
            sDate.Date,
            sTime.Hours,
            sTime.Minutes,
            sTime.Seconds,
            (int)(HAL_GetTick() % 1000));

    ret = f_open(&raceFile, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (ret != FR_OK) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }

    sprintf(filename,
            "race-%d-lap-%d-20%02d-%02d-%02d-%02d-%02d-%02d-%03d.log",
            raceNumber,
            currentLap,
            sDate.Year,
            sDate.Month,
            sDate.Date,
            sTime.Hours,
            sTime.Minutes,
            sTime.Seconds,
            (int)(HAL_GetTick() % 1000));

    ret = f_open(&lapFile, filename, FA_CREATE_ALWAYS | FA_WRITE);
    if (ret != FR_OK) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }

    // race.log 헤더 작성
    ret = f_write(&raceFile, &header, sizeof(header_t), &written);
    if (ret != FR_OK || written != sizeof(header_t)) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }

    // lap_001.log 헤더 작성
    ret = f_write(&lapFile, &header, sizeof(header_t), &written);
    if (ret != FR_OK || written != sizeof(header_t)) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }

    raceRunning = true;
}

static void stopRace(void)
{
    if (!raceRunning)
        return;

    f_sync(&raceFile);
    f_sync(&lapFile);

    f_close(&raceFile);
    f_close(&lapFile);

    raceRunning = false;
}

static void nextLap(void)
{
    char filename[_MAX_LFN];
    FRESULT ret;
    UINT written;

    if (!raceRunning)
        return;

    f_sync(&lapFile);
    f_close(&lapFile);

    currentLap++;

    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    getCurrentDateTime(&sDate, &sTime);

    sprintf(filename,
            "race-%d-lap-%d-20%02d-%02d-%02d-%02d-%02d-%02d-%03d.log",
            raceNumber,
            currentLap,
            sDate.Year,
            sDate.Month,
            sDate.Date,
            sTime.Hours,
            sTime.Minutes,
            sTime.Seconds,
            (int)(HAL_GetTick() % 1000));

    ret = f_open(&lapFile, filename,
                 FA_CREATE_ALWAYS | FA_WRITE);

    if (ret != FR_OK) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }

    // 새 Lap 파일에도 Header 작성
    ret = f_write(&lapFile, &header, sizeof(header_t), &written);
    if (ret != FR_OK || written != sizeof(header_t)) {
        error_status = EEM_ERR_SD_CARD;
        Error_Handler();
    }
}

void energymeter_record(void) {
  disk_initialize((BYTE) 0);

  FATFS fat;
  FRESULT ret = f_mount(&fat, "", 0);

  if (ret != FR_OK) {
    error_status = EEM_ERR_SD_CARD;
    Error_Handler();
  }

  char filename[_MAX_LFN];

  sprintf(filename, "total-20%02d-%02d-%02d-%02d-%02d-%02d-%03d %08lX-%08lX-%08lX.log",
          header.year, header.month, header.day,
          header.hour, header.minute, header.second, header.millisecond,
          header.uid[0], header.uid[1], header.uid[2]);

  ret = f_open(&totalFile, filename, FA_OPEN_APPEND | FA_WRITE);

  if (ret != FR_OK) {
    error_status = EEM_ERR_SD_CARD;
    Error_Handler();
  }

  header.timestamp = HAL_GetTick(); // typically 550 ms here
  header.checksum = 0;
  header.checksum = checksum((uint16_t *)&header, sizeof(header_t));

  // write log header
  UINT written;
  ret = f_write(&totalFile, &header, sizeof(header_t), &written);
  if (ret != FR_OK || written != sizeof(header_t)) {
    error_status = EEM_ERR_SD_CARD;
    Error_Handler();
  }

  DEBUG_MSG("LOG : %s\r\n", filename);

  log_t log;
  log.type = LOG_TYPE_RECORD;
  log.magic = LOG_MAGIC;

  int32_t adc_avg[ADC_CH_CNT];

  HAL_TIM_Base_Start_IT(&htim5); // start 10 ms timer

  while (TRUE) {
    if (startStopRequest)
    {
        startStopRequest = false;

        if (raceRunning)
            stopRace();
        else
            startRace();
    }

    if (lapRequest)
    {
        lapRequest = false;

        if (raceRunning)
            nextLap();
    }
    // 10 ms timer
    if (timer_flag) {
      // reset all average buffers
      adc_avg[ADC_LV_VOLTAGE] = 0;
      adc_avg[ADC_HV_CURRENT] = 0;
      adc_avg[ADC_HV_VOLTAGE] = 0;
      adc_avg[ADC_TEMP] = 0;

      // ADC measurement average calculation
      for (int i = 0; i < ADC_AVG_CNT; i++) {
        HAL_ADC_Start_DMA(&hadc1, adc, ADC_CH_CNT);

        uint32_t timeout = HAL_GetTick();

        while (adc_flag != TRUE) {
            if (HAL_GetTick() - timeout > 10) {
                break;
            }
        }

        adc_avg[ADC_LV_VOLTAGE] += adc_calc[ADC_LV_VOLTAGE];
        adc_avg[ADC_HV_CURRENT] += adc_calc[ADC_HV_CURRENT];
        adc_avg[ADC_HV_VOLTAGE] += adc_calc[ADC_HV_VOLTAGE];
        adc_avg[ADC_TEMP] += adc_calc[ADC_TEMP];

        adc_flag = FALSE;
      }

      log.timestamp = HAL_GetTick();
      log.packet.record.hv_voltage = (int16_t)(adc_avg[ADC_HV_VOLTAGE] >> ADC_AVG_EXP);
      log.packet.record.hv_current = (int16_t)(adc_avg[ADC_HV_CURRENT] >> ADC_AVG_EXP);
      log.packet.record.lv_voltage = (int16_t)(adc_avg[ADC_LV_VOLTAGE] >> ADC_AVG_EXP);
      log.packet.record.temperature = (int16_t)(adc_avg[ADC_TEMP] >> ADC_AVG_EXP);

      // checksum calculation
      log.checksum = 0;
      log.checksum = checksum((uint16_t *)&log, sizeof(log_t));

      // won't handle error; better keep retrying on failure
      ret = f_write(&totalFile, &log, sizeof(log_t), &written);
      if (ret != FR_OK || written != sizeof(log_t)) {
          error_status = EEM_ERR_SD_CARD;
          Error_Handler();
      }

      if (raceRunning)
      {
        ret = f_write(&raceFile, &log, sizeof(log_t), &written);
        if (ret != FR_OK || written != sizeof(log_t)) {
            error_status = EEM_ERR_SD_CARD;
            Error_Handler();
        }

        ret = f_write(&lapFile, &log, sizeof(log_t), &written);
        if (ret != FR_OK || written != sizeof(log_t)) {
            error_status = EEM_ERR_SD_CARD;
            Error_Handler();
        }
      }

      adc_flag = FALSE;
      timer_flag = FALSE;
    }

    // 100 ms timer
    if (sync_flag) {

        ret = f_sync(&totalFile);
        if (ret != FR_OK) {
            error_status = EEM_ERR_SD_CARD;
            Error_Handler();
        }

        if (raceRunning)
        {
            ret = f_sync(&raceFile);
            if (ret != FR_OK) {
                error_status = EEM_ERR_SD_CARD;
                Error_Handler();
            }

            ret = f_sync(&lapFile);
            if (ret != FR_OK) {
                error_status = EEM_ERR_SD_CARD;
                Error_Handler();
            }
        }

        sync_flag = FALSE;

        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }
  }
}

// get ADC values at 0V, 0A
void energymeter_calibrate(void) {
  float v = 0, c = 0;

  for (int i = 0; i < ADC_AVG_CNT; i++) {
    HAL_ADC_Start_DMA(&hadc1, adc, ADC_CH_CNT);
    while (adc_flag != TRUE);

    v += (float)adc_calc[ADC_HV_VOLTAGE];
    c += (float)adc_calc[ADC_HV_CURRENT];

    adc_flag = FALSE;
    HAL_Delay(1);
  }

  hv_voltage_cal = v / ADC_AVG_CNT;
  hv_current_cal = c / ADC_AVG_CNT;

  header.v_cal = (uint16_t)(hv_voltage_cal * 100.0f);
  header.c_cal = (uint16_t)(hv_current_cal * 100.0f);

  #ifdef DEBUG
  DEBUG_MSG("CAL: x%d, %.2f V, %.2f A\r\n", ADC_AVG_CNT, hv_voltage_cal / 10.0f, hv_current_cal / 10.0f);
  #endif
}

// 10ms TIM5 callback
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance != TIM5) {
    return;
  }

  timer_flag = TRUE;

  if (++tim_cnt >= 10) {
    sync_flag = TRUE;
    tim_cnt = 0;
  }
}

// ADC conversion callback
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  if (hadc->Instance != ADC1) {
    return;
  }
  // Vref calculation
  adc_mv[ADC_VREFINT] = (float)VREFINT_CAL_VREF * (float)(*VREFINT_CAL_ADDR) / (float)adc[ADC_VREFINT];

  // ADC channel voltage calculation
  adc_mv[ADC_LV_VOLTAGE] = adc_mv[ADC_VREFINT] * (float)adc[ADC_LV_VOLTAGE] / (float)((1 << ADC_RES) - 1);
  adc_mv[ADC_5V_REF] = adc_mv[ADC_VREFINT] * (float)adc[ADC_5V_REF] / (float)((1 << ADC_RES) - 1);
  adc_mv[ADC_HV_CURRENT] = adc_mv[ADC_VREFINT] * (float)adc[ADC_HV_CURRENT] / (float)((1 << ADC_RES) - 1);
  adc_mv[ADC_HV_VOLTAGE] = adc_mv[ADC_VREFINT] * (float)adc[ADC_HV_VOLTAGE] / (float)((1 << ADC_RES) - 1);

  // actual value calculation
  adc_calc[ADC_LV_VOLTAGE] = (int16_t)(adc_mv[ADC_LV_VOLTAGE] * VOLTAGE_DIVIDER_RATIO_LV / 10.0f);
  adc_calc[ADC_HV_CURRENT] = (int16_t)((((adc_mv[ADC_HV_CURRENT] * VOLTAGE_DIVIDER_RATIO_HV_C) - adc_mv[ADC_5V_REF]) * 4.0f) - hv_current_cal);
  adc_calc[ADC_HV_VOLTAGE] = (int16_t)((adc_mv[ADC_HV_VOLTAGE] * VOLTAGE_DIVIDER_RATIO_HV / 100.0f) - hv_voltage_cal);

  // temperature calculation
  adc_calc[ADC_TEMP] = (int16_t)(((float)(TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) / (float)(*TEMPSENSOR_CAL2_ADDR - *TEMPSENSOR_CAL1_ADDR) * (adc[ADC_TEMP] - (float)(*TEMPSENSOR_CAL1_ADDR)) + (float)(TEMPSENSOR_CAL1_TEMP)) * 100.0f);

  adc_flag = TRUE;

  // #ifdef DISABLED
  #ifdef DEBUG
  if (tim_cnt == 0) {
    DEBUG_MSG("[%8lu] Vref: %f mV\r\n%*cLV  : %.2f V\r\n%*cHV  : %.1f V / %.1f A\r\n%*cVREF: %f mV\r\n%*cTEMP: %.2f °C\r\n",
              HAL_GetTick(), adc_mv[ADC_VREFINT],
              11, ' ', adc_calc[ADC_LV_VOLTAGE] / 100.0f,
              11, ' ', (int16_t)adc_calc[ADC_HV_VOLTAGE] / 10.0f, (int16_t)adc_calc[ADC_HV_CURRENT] / 10.0f,
              11, ' ', adc_mv[ADC_5V_REF],
              11, ' ', adc_calc[ADC_TEMP] / 100.0f);
  }
  #endif
  // #endif
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin == GPIO_PIN_7)
    {
        if ((now - lastStartStopTime) < 500)
            return;

        lastStartStopTime = now;
        startStopRequest = true;
    }
    else if (GPIO_Pin == GPIO_PIN_6)
    {
        if ((now - lastLapTime) < 300)
            return;

        lastLapTime = now;
        lapRequest = true;
    }
}