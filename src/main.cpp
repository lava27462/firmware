#include "core/main_menu.h"
#include <globals.h>

#include "core/bus_HAL.h"
#include "core/powerSave.h"
#include "core/ram_profile.h"
#include "core/serial_commands/cli.h"
#include "core/utils.h"
#include "current_year.h"
#include "esp32-hal-psram.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <functional>
#include <string>
#include <vector>

io_expander ioExpander;
BruceConfig bruceConfig;
BruceConfigPins bruceConfigPins;

SerialCli serialCli;
USBSerial USBserial;
SerialDevice *serialDevice = &USBserial;

StartupApp startupApp;
String startupAppJSInterpreterFile = "";

MainMenu mainMenu;
SPIClass sdcardSPI;
#ifdef USE_HSPI_PORT
#ifndef VSPI
#define VSPI FSPI
#endif
SPIClass AUX_SPI(VSPI);
#else
SPIClass AUX_SPI(HSPI);
#endif

// Navigation Variables
volatile bool NextPress = false;
volatile bool PrevPress = false;
volatile bool UpPress = false;
volatile bool DownPress = false;
volatile bool SelPress = false;
volatile bool EscPress = false;
volatile bool AnyKeyPress = false;
volatile bool NextPagePress = false;
volatile bool PrevPagePress = false;
volatile bool LongPress = false;
volatile bool SerialCmdPress = false;
volatile int forceMenuOption = -1;
volatile uint8_t menuOptionType = 0;
String menuOptionLabel = "";
#ifdef HAS_ENCODER_LED
volatile int EncoderLedChange = 0;
#endif

TouchPoint touchPoint;
keyStroke KeyStroke;
volatile int32_t RotaryNetSteps = 0;

#ifdef HAS_ENCODER
void __attribute__((weak)) pollEncoder(void) {}

static void taskEncoderPoll(void *parameter) {
    while (true) {
        pollEncoder();
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}
#endif

TaskHandle_t xHandle;
void __attribute__((weak)) taskInputHandler(void *parameter) {
    auto timer = millis();
    while (true) {
        // Вызов checkPowerSaveTime() безопасен, так как мы ранее поставили return в powerSave.cpp
        checkPowerSaveTime();
        
        if (!AnyKeyPress || millis() - timer > 75) {
            NextPress = false;
            PrevPress = false;
            UpPress = false;
            DownPress = false;
            SelPress = false;
            EscPress = false;
            AnyKeyPress = false;
            SerialCmdPress = false;
            NextPagePress = false;
            PrevPagePress = false;
            touchPoint.pressed = false;
            touchPoint.Clear();
            checkAndRecoverSysI2CBus();
#ifndef USE_TFT_eSPI_TOUCH
            InputHandler();
#endif
            timer = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Public Globals Variables
unsigned long previousMillis = millis();
int prog_handler; 
String cachedPassword = "";
int8_t interpreter_state = -1;
bool sdcardMounted = false;
bool gpsConnected = false;

// wifi globals
bool wifiConnected = false;
bool isWebUIActive = false;
String wifiIP;

bool BLEConnected = false;
bool returnToMenu;
bool isSleeping = false;
bool isScreenOff = false;
bool dimmer = false;
char timeStr[16];
time_t localTime;
struct tm *timeInfo;

#if defined(HAS_RTC)
#if defined(HAS_RTC_PCF85063A)
pcf85063_RTC _rtc;
#else
cplus_RTC _rtc;
#endif
RTC_TimeTypeDef _time;
RTC_DateTypeDef _date;
bool clock_set = true;
#else
ESP32Time rtc;
bool clock_set = false;
#endif

std::vector<Option> options;

// Protected global variables
#if defined(HAS_SCREEN)
tft_logger tft = tft_logger(); 
tft_sprite sprite = tft_sprite(&tft);
tft_sprite draw = tft_sprite(&tft);
volatile int tftWidth = TFT_HEIGHT;
#ifdef HAS_TOUCH
volatile int tftHeight = TFT_WIDTH - 20; 
#else
volatile int tftHeight = TFT_WIDTH;
#endif
#else
tft_logger tft;
SerialDisplayClass &sprite = tft;
SerialDisplayClass &draw = tft;
volatile int tftWidth = VECTOR_DISPLAY_DEFAULT_HEIGHT;
volatile int tftHeight = VECTOR_DISPLAY_DEFAULT_WIDTH;
#endif

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/led_control.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/serialcmds.h"
#include "core/settings.h"
#include "core/wifi/webInterface.h"
#include "core/wifi/wifi_common.h"
#include "modules/bjs_interpreter/interpreter.h" 
#include "modules/others/audio.h"                
#include "modules/rf/rf_utils.h"                 
#include <Wire.h>

/*********************************************************************
 **  Function: begin_storage
 **  Config LittleFS and SD storage
 *********************************************************************/
void begin_storage() {
    if (!setupLittleFS()) {
        LittleFS.format();
        setupLittleFS();
    }
    RAM_LOG("after LittleFS");
    bool checkFS = setupSdCard();
    bruceConfig.fromFile(checkFS);
    bruceConfigPins.fromFile(checkFS);
}

/*********************************************************************
 **  Function: _setup_gpio()
 *********************************************************************/
void _setup_gpio() __attribute__((weak));
void _setup_gpio() {}

/*********************************************************************
 **  Function: _post_setup_gpio()
 *********************************************************************/
void _post_setup_gpio() __attribute__((weak));
void _post_setup_gpio() {}

/*********************************************************************
 **  Function: _pre_storage_gpio()
 *********************************************************************/
void _pre_storage_gpio() __attribute__((weak));
void _pre_storage_gpio() {}

/*********************************************************************
 **  Function: setup_gpio
 **  Setup GPIO pins
 *********************************************************************/
void setup_gpio() {
    _setup_gpio();

    // Защита шины I2C: Инициализируем только если Wire запущен, чтобы избежать зависания S3
    if (Wire.getTimeout() > 0) {
        ioExpander.init(IO_EXPANDER_ADDRESS, &Wire);
    }

    initCC1101once(acquireSPIBus(
        bruceConfigPins.CC1101_bus.sck, bruceConfigPins.CC1101_bus.miso, bruceConfigPins.CC1101_bus.mosi
    ));
}

/*********************************************************************
 **  Function: begin_tft
 **  Config tft
 *********************************************************************/
void begin_tft() {
    tft.setRotation(bruceConfigPins.rotation); 
    tft.invertDisplay(bruceConfig.colorInverted);
    tft.setRotation(bruceConfigPins.rotation);
    tftWidth = tft.width();
#ifdef HAS_TOUCH
    tftHeight = tft.height() - 20;
#else
    tftHeight = tft.height();
#endif
    resetTftDisplay();
    setBrightness(bruceConfig.bright, false);
}

/*********************************************************************
 **  Function: setup() и loop()
 **  ДОПИСАНО: Стандартная точка входа для успешной компиляции
 *********************************************************************/
void setup() {
    Serial.begin(115200);
    setup_gpio();
    begin_storage();
    begin_tft();
    
    // Запуск фонового потока опроса кнопок
    xTaskCreatePinnedToCore(taskInputHandler, "InputHandler", 4096, NULL, 1, &xHandle, 1);
    
    mainMenu.begin();
}

void loop() {
    // Вся навигация Bruce Devices крутится во FreeRTOS тасках, основной loop пуст
    vTaskDelay(pdMS_TO_TICKS(1000));
}
