/*******************************************************************************
 *
 * Description: Assignment2
 * Project name: TouchSwitch_MCU_V1.0.1
 *
 *
 * Last Changed By:  $Author: TrungNT $
 * Revision:         $Revision: 1.0 $
 * Last Changed:     $Date: 20/20/2021 $
 *
 ******************************************************************************/
/**Libraries******************************************************************/
#include <stdint.h>
#include <system_stm32f4xx.h>
#include <timer.h>
#include <eventman.h>
#include <led.h>
#include <melody.h>
#include <lightsensor.h>
#include <temhumsensor.h>
#include <eventbutton.h>
#include <button.h>
#include <Ucglib.h>
#include <stdio.h>
#include <string.h>
#include <uartcmd.h>
#include <serial.h>


/*	@brief Events APIs*/
//define state of program
typedef enum {
	EVENT_EMPTY, EVENT_APP_INIT, EVENT_APP_FLUSHEM_READY,
} event_api_t, *event_api_p;

typedef enum {
	STATE_APP_STARTUP, STATE_APP_IDLE, STATE_APP_RESET
} state_app_t;

/* Private variables ---------------------------------------------------------*/
static state_app_t eCurrentState;
static ucg_t ucg;
uint8_t state = 0;

#define MODE_HOME			(0)
#define MODE_ALARM			(1)

// declare variable use in program
static char timeT[20];
static char DateT[30];
static char AlarmT[30];
int8_t mode = MODE_HOME;
int8_t changeMode = 0;
int8_t setItem = 0;
int8_t locateChoose = 0;
static int btn = -1;

//declare array locate
char *locates[2] = {"Vi", "Cn"};

enum{
	DISPLAY_TIME,
	NOT_DISPLAY,
};

static uint8_t idUpdateTime = NO_TIMER;
static uint8_t idAlarmModeTime = NO_TIMER;
static uint8_t idClearLCDTime = NO_TIMER;
static uint8_t idDisplayTime = NO_TIMER;
static uint8_t idChangeLocateTime = NO_TIMER;

// Cấu trúc thời gian bao gồm giờ , tháng , năm , ngày , tháng , địa điểm , trạng thái hiển thị
struct date_time
{
	int32_t tick;
	int32_t tickD;
	int16_t h;
	int16_t m;
	int16_t s;
	int16_t day;
	int16_t month;
	int32_t year;
	char *locate;
	int8_t format;
	int8_t display; // không cho phép hiển thị ra màn hình để dùng tính năng khác
};
static struct date_time dateTime; // Đối tượng thời gian ở chế độ HOME
static struct date_time alarmClock; // Đối tượng bó thức ở chế độ SETUP


/* Private function prototypes****************************************************/
static void appInitCommon(void);
static state_app_t getStateApp(void);
static void setStateApp(state_app_t state);
static void appStateManager(uint8_t event);
static void loadConfiguration(void);
void deviceStateMachine(uint8_t event);
static void updateTimeHandler(void);
void intTime(void);
void alarmModeHandler (void);
void clearTimeLCDHandler(void);
//--------------------------------------------------------------------------------
int main(void) {
	appInitCommon();
	setStateApp(STATE_APP_STARTUP); // Khởi động ở chế độ STATE_APP_STARTUP
	EventSchedulerAdd(EVENT_APP_INIT); //Khởi tạo bộ đệm buffer để quản lý các sự kiện của chương trình chính

	/* Loop forever */
	while (1) {
		processTimerScheduler(); // Xử lý các bộ hẹn giờ đã được tạo
		processEventScheduler(); // Xử lý các sự kiện của chương trình chính do bộ quản lý sự kiện quản lý.
		alarm(); // Gọi hàm chạy báo thức
	}
}

/**
 * @func   appInitCommon
 * @brief  Initialize app
 * @param  None
 * @retval None
 */
static void appInitCommon(void) {
	// Initializes system clock
	SystemCoreClockUpdate(); //Cấu hình clock của hệ thống
	// Initializes system tick để xử lý các sự kiện thời gian.
	TimerInit();
	intTime();
	intAlarm();
	EventSchedulerInit(appStateManager); // Khởi tạo bộ đệm buffer để quản lý các sự kiện của chương trình chính
	EventButton_Init();	 	// Cấu hình chân GPIO của các nút nhấn trên mạch.
	BuzzerControl_Init(); 	// Cấu hình chân GPIO của còi trên mạch.
	LedControl_Init(); 		// Cấu hình chân GPIO của các led RGB trên mạch.
	// Initializes glcd
	Ucglib4WireSWSPI_begin(&ucg, UCG_FONT_MODE_SOLID);
	ucg_ClearScreen(&ucg);
	ucg_SetColor(&ucg, 0, 255, 255, 255);
	ucg_SetColor(&ucg, 1, 0, 0, 0);
	ucg_SetRotate180(&ucg);



	// Update Time theo chu kì
	callUpdateTimeCyCle();

}

/**
 * @func   appStateManager
 * @brief  Manage state app
 * @param  uint8_t
 * @retval None
 */
static void appStateManager(uint8_t event) {
    switch (getStateApp())
    {
        case STATE_APP_STARTUP:
            if (event == EVENT_APP_INIT)
            {
                loadConfiguration();
                setStateApp(STATE_APP_IDLE);
            }
            break;

        case STATE_APP_IDLE:
            deviceStateMachine(event);
            break;

        case STATE_APP_RESET:
            break;

        default:
            break;
    }
}

/**
 * @func   setStateApp
 * @brief  set state app
 * @param  state_app_t
 * @retval None
 */
static void setStateApp(state_app_t state) {
	/* Set state of application */
	eCurrentState = state;
}

/**
 * @func   getStateApp
 * @brief  get state app
 * @param  None
 * @retval state_app_t
 */
static state_app_t getStateApp(void) {
	/*Return state of application */
	return eCurrentState;
}

/**
 * @func   loadConfiguration
 * @brief  Display parameter fixed
 * @param  None
 * @retval None
 */
static void loadConfiguration(void) {
	// Display output
	ucg_SetFont(&ucg, ucg_font_ncenR08_hf);
	ucg_DrawString(&ucg, 42, 20, 0, "GROUP 3");
	ucg_DrawString(&ucg, 50, 40,0, "HOME");
	writeDate();
	ucg_DrawString(&ucg, 20, 110, 0, "Locate: ");
	writeLocate();
}

/**
 * @func   deviceStateMachine
 * @brief  Handler button
 * @param  uint8_t
 * @retval None
 */
void deviceStateMachine(uint8_t event) {
	switch (event) {

	case EVENT_OF_BUTTON_3_PRESS_LOGIC: {
		BuzzerControl_SetMelody(pbeep);

			mode = MODE_ALARM;
			ucg_DrawString(&ucg, 50, 40, 0, "SETUP");
			stopDisplayUpdateTime();
			alarmClock.display = DISPLAY_TIME;
			setItem = 0;

			if (idAlarmModeTime != NO_TIMER)
			{
				TimerStop(idAlarmModeTime);
			}
			idAlarmModeTime = TimerStart("MODE_ALARM ", 1000, TIMER_REPEAT_FOREVER, (void*) alarmModeHandler, NULL);
		break;
	}
	case EVENT_OF_BUTTON_3_PRESS_2_TIMES:

		mode = MODE_HOME;
		if (idAlarmModeTime != NO_TIMER)
		{
			TimerStop(idAlarmModeTime);
		}
		alarmClock.display = NOT_DISPLAY;
		ucg_DrawString(&ucg, 50, 40, 0, "HOME    ");


		if (idClearLCDTime != NO_TIMER)
		{
			TimerStop(idClearLCDTime);
		}
		idClearLCDTime = TimerStart("MODE_ALARM ", 1000, TIMER_REPEAT_ONE_TIME, (void*) clearTimeLCDHandler, NULL);


		break;
	case EVENT_OF_BUTTON_5_PRESS_LOGIC: {
		BuzzerControl_SetMelody(pbeep);
		if(mode == MODE_ALARM)
		{
			if(setItem == 0)
			{
				alarmClock.h--;
				if(alarmClock.h < 0)
				{
					alarmClock.h = 23;
				}
			}
			if(setItem == 1)
			{
				alarmClock.m--;
				if(alarmClock.m < 0)
				{
					alarmClock.m = 59;
				}
			}
			if(setItem == 2)
			{
				alarmClock.s--;
				if(alarmClock.s < 0)
				{
					alarmClock.s = 59;
				}
			}

		}
		break;
	}
	case EVENT_OF_BUTTON_2_PRESS_LOGIC: {
		BuzzerControl_SetMelody(pbeep);
		if(mode == MODE_ALARM){
			setItem--;
			if(setItem < 0){
				setItem = 2;
			}
			break;
		}
		if(mode == MODE_HOME){
			locateChoose = !locateChoose;
			dateTime.locate = locates[locateChoose];
			if(locateChoose == 1){
				dateTime.h += 1 ;
			}else{
				dateTime.h -= 1 ;
			}
			changeLocate();
			break;
		}
		break;
}
	case EVENT_OF_BUTTON_1_PRESS_LOGIC: {
		BuzzerControl_SetMelody(pbeep);
		if(mode == MODE_ALARM)
		{
			if(setItem == 0)
			{
				alarmClock.h++;
				if(alarmClock.h > 23)
				{
					alarmClock.h = 0;
				}
			}
			if(setItem == 1)
			{
				alarmClock.m++;
				if(alarmClock.m > 59)
				{
					alarmClock.m = 0;
				}
			}
			if(setItem == 2)
			{
				alarmClock.s++;
				if(alarmClock.s > 60)
				{
					alarmClock.s = 0;
				}
			}
		}
		break;
	}
	case EVENT_OF_BUTTON_4_PRESS_LOGIC: {
		BuzzerControl_SetMelody(pbeep);
		if(mode == MODE_ALARM){
			setItem++;
			if(setItem > 2){
				setItem = 0;
			}
			break;
		}
		if(mode == MODE_HOME){
			locateChoose = !locateChoose;
			dateTime.locate = locates[locateChoose];
			if(locateChoose == 1){
				dateTime.h += 1 ;
			}else{
				dateTime.h -= 1 ;
			}
			changeLocate();
			break;
		}

		break;
	}
	default:
		break;

}
}

/**
 * @func   alarm
 * @brief  Handler alarm
 * @param  None
 * @retval None
 */
void alarm()
{
	if(dateTime.h == alarmClock.h && dateTime.m == alarmClock.m)
	{
		if(dateTime.s == alarmClock.s)
		{
			BuzzerControl_SetMelody(pbeep);
		}
	}
}

/**
 * @func   intTime
 * @brief  Init time in mode Home
 * @param  None
 * @retval None
 */
void intTime(void)
{
    dateTime.h = 01;
    dateTime.m = 01;
    dateTime.s = 01;
    dateTime.day = 9;
    dateTime.month = 6;
    dateTime.year = 2024;
    dateTime.locate = locates[locateChoose];
    dateTime.format = 0;
    dateTime.tick = GetMilSecTick();
    dateTime.tickD = GetMilSecTick();
}

/**
 * @func   intAlarm
 * @brief  Init alarm in mode SETUP
 * @param  None
 * @retval None
 */
void intAlarm(void){
    alarmClock.h = 00;
	alarmClock.m = 00;
	alarmClock.s = 00;
}

/**
 * @func   callUpdateTimeCyCle
 * @brief  Display time in cycles
 * @param  None
 * @retval None
 */
void callUpdateTimeCyCle(void){
	if (idUpdateTime != NO_TIMER)
	{
		TimerStop(idUpdateTime);
	}
	dateTime.display = DISPLAY_TIME;
	idUpdateTime = TimerStart("TimerUpCycle", 1000, TIMER_REPEAT_FOREVER, (void*) updateTimeHandler, NULL);
}

/**
 * @func   displayUpdateTime
 * @brief  Display time
 * @param  None
 * @retval None
 */
void displayUpdateTime(void){
	dateTime.display = DISPLAY_TIME;

}

/**
 * @func   stopDisplayUpdateTime
 * @brief  None display time
 * @param  None
 * @retval None
 */
void stopDisplayUpdateTime(void){
	dateTime.display = NOT_DISPLAY;
}

/**
 * @func   updateTimeHandler
 * @brief  Handler display time
 * @param  None
 * @retval None
 */
static void updateTimeHandler(void){
	// call function writeTime
	writeTime(&dateTime);
	// call function writeDate after 1 day
	if(dateTime.h == 23 && dateTime.m ==59 && dateTime.s == 59){
		writeDate();
	}
}

/**
 * @func   writeTime
 * @brief  write time in LCD
 * @param  date_time*
 * @retval None
 */
void writeTime(struct date_time* timeP)
{
		timeP->s+=1;
		if(timeP->s >= 60)
		{
			timeP->s = 0;
			timeP->m+=1;
		}
		if(timeP->m >= 60)
		{
			timeP->m = 0;
			timeP->h+=1;
		}
		if(timeP->h >= 24)
		{
			timeP->h = 0;
		}
	sprintf(timeT, "Time: %02d : %02d : %02d", timeP->h, timeP->m, timeP->s);
	if(timeP->display == DISPLAY_TIME){
		ucg_DrawString(&ucg, 20, 70, 0, &timeT);
	}
}

/**
 * @func   writeLocate
 * @brief  write locate in LCD
 * @param  Node
 * @retval None
 */
void writeLocate(void){
		const char *formatStr = dateTime.locate;
		ucg_DrawString(&ucg, 60, 110,0, formatStr);
}

/**
 * @func   writeLocateHandler
 * @brief  Handler write locate in LCD
 * @param  Node
 * @retval None
 */
void writeLocateHandler(void){
	static uint8_t count = 0;
	count++;
	if(count == 1){
		ucg_ClearScreenCustom(&ucg, 60, 110, 20, 128);
	}else{
		const char *formatStr = dateTime.locate;
		ucg_DrawString(&ucg, 60, 110,0, formatStr);
		count = 0;
	}
}

/**
 * @func   writeDate
 * @brief  write date in LCD
 * @param  Node
 * @retval None
 */
void writeDate(){
	switch(dateTime.month){
		// Month 2 has 29 day
		case 2 :{
			if(dateTime.h == 23 && dateTime.m == 59 && dateTime.s == 59)
				{
					dateTime.day+=1;
				}
			if(dateTime.day > 29)
				{
					dateTime.day = 1;
					dateTime.month+=1;
				}
			if(dateTime.month > 12)
				{
					dateTime.month = 1;
					dateTime.year+=1;
				}
			sprintf(DateT, "Date: %02d : %02d : %04d", dateTime.day, dateTime.month, dateTime.year);
			ucg_DrawString(&ucg, 20, 90, 0, &DateT);
			break;
		}
		// Month 1,3,5,7,8,10,12 has 31 day
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
			{
			if(dateTime.h == 23 && dateTime.m == 59 && dateTime.s == 59)
				{
					dateTime.day+=1;
				}
			if(dateTime.day > 31)
				{
					dateTime.day = 1;
					dateTime.month+=1;
				}
			if(dateTime.month > 12)
				{
					dateTime.month = 1;
					dateTime.year+=1;
				}
			sprintf(DateT, "Date: %02d : %02d : %04d", dateTime.day, dateTime.month, dateTime.year);
			ucg_DrawString(&ucg, 20, 90, 0, &DateT);
			break;
		}
		// Month 4,6,9,11 has 30 day
		case 4 :
		case 6 :
		case 9 :
		case 11:
		{
			if(dateTime.h == 23 && dateTime.m == 59 && dateTime.s == 59)
				{
					dateTime.day+=1;
				}
			if(dateTime.day > 30)
				{
					dateTime.day = 1;
					dateTime.month+=1;
				}
			if(dateTime.month > 12)
				{
					dateTime.month = 1;
					dateTime.year+=1;
				}
			sprintf(DateT, "Date: %02d : %02d : %04d", dateTime.day, dateTime.month, dateTime.year);
			ucg_DrawString(&ucg, 20, 90, 0, &DateT);
			break;

		}

	}
}

/**
 * @func   alarmModeHandler
 * @brief  handler display alarm in LCD
 * @param  Node
 * @retval None
 */
void alarmModeHandler(void){
	if(setItem == 0)
	{
		sprintf(AlarmT, "Alarm: [%02d]:%02d:%02d", alarmClock.h, alarmClock.m, alarmClock.s);
	}
	if(setItem == 1)
	{
		sprintf(AlarmT, "Alarm: %02d:[%02d]:%02d", alarmClock.h, alarmClock.m, alarmClock.s);
	}
	if(setItem == 2)
	{

		sprintf(AlarmT, "Alarm: %02d:%02d:[%02d]", alarmClock.h, alarmClock.m, alarmClock.s);
	}
	if(alarmClock.display == DISPLAY_TIME){
		ucg_DrawString(&ucg, 20, 70, 0, &AlarmT);
	}

}

/**
 * @func   displayTimeHandler
 * @brief  call  function displayUpdateTime
 * @param  Node
 * @retval None
 */
void displayTimeHandler(void){
	displayUpdateTime();
}

/**
 * @func   changeLocate
 * @brief  change locate
 * @param  Node
 * @retval None
 */
void changeLocate(void){

	if (idChangeLocateTime != NO_TIMER)
	{
		TimerStop(idChangeLocateTime);
	}
	idChangeLocateTime = TimerStart("DISPLAY_LOCATE ", 1000, 2, (void*) writeLocateHandler, NULL);
}

/**
 * @func   clearTimeLCDHandler
 * @brief  clear time in mode SETUP in LCD
 * @param  Node
 * @retval None
 */
void clearTimeLCDHandler(void){
	ucg_ClearScreenCustom(&ucg, 20, 70, 0,128);

	if (idDisplayTime != NO_TIMER)
	{
		TimerStop(idDisplayTime);
	}
	//Display time in mode Home after 1s
	idDisplayTime = TimerStart("MODE_ALARM ", 1000, TIMER_REPEAT_ONE_TIME, (void*) displayTimeHandler, NULL);
}
