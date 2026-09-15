#include "core/bus_HAL.h"
#include "core/powerSave.h"

/***************************************************************************************
** Function name: _setup_gpio()
** Location: main.cpp
** Description:   initial setup for the device
***************************************************************************************/

// Удалена библиотека XPowersLib и объект PPM, чтобы избежать сбоев I2C

void _setup_gpio() {
    // ВАЖНО: Переводим пины кнопок в режим INPUT_PULLUP.
    // Теперь они принудительно подтянуты к 3.3V, и нажатием считается замыкание на GND (LOW).
    pinMode(UP_BTN, INPUT_PULLUP);
    pinMode(SEL_BTN, INPUT_PULLUP);
    pinMode(DW_BTN, INPUT_PULLUP);
    pinMode(R_BTN, INPUT_PULLUP);
    pinMode(L_BTN, INPUT_PULLUP);

    pinMode(CC1101_SS_PIN, OUTPUT);
    pinMode(NRF24_SS_PIN, OUTPUT);

    digitalWrite(CC1101_SS_PIN, HIGH);
    digitalWrite(NRF24_SS_PIN, HIGH);

    bruceConfigPins.rfModule = CC1101_SPI_MODULE;
    bruceConfigPins.irRx = RXLED;

    // Включение питания периферии и дисплея (для плат ESP32-S3 пин 15 часто активирует экран)
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    // Удалена вся логика опроса и настройки несуществующего чипа BQ25896
    Serial.println("PMU BQ25896 disabled. Running on Generic Power Mode.");
}

bool isCharging() {
    // Чипа питания нет, возвращаем false, чтобы прошивка не думала, что идет зарядка
    return false;
}

int getBattery() {
    // Всегда возвращаем 100%, чтобы прошивка никогда не уходила в экстренный сон из-за "разряда"
    return 100;
}

/*********************************************************************
** Function: setBrightness
** location: settings.cpp
** set brightness value
**********************************************************************/
void _setBrightness(uint8_t brightval) {
    if (brightval == 0) {
        analogWrite(TFT_BL, brightval);
    } else {
        int bl = MINBRIGHT + round(((255 - MINBRIGHT) * brightval / 100));
        analogWrite(TFT_BL, bl);
    }
}

/*********************************************************************
** Function: InputHandler
** Handles the variables PrevPress, NextPress, SelPress, AnyKeyPress and EscPress
**********************************************************************/
void InputHandler(void) {
    static unsigned long tm = 0;
    if (millis() - tm < 200 && !LongPress) return;

    // Считываем физическое состояние кнопок (LOW/false = кнопка зажата)
    bool _u = (digitalRead(UP_BTN) == LOW);
    bool _d = (digitalRead(DW_BTN) == LOW);
    bool _l = (digitalRead(L_BTN) == LOW);
    bool _r = (digitalRead(R_BTN) == LOW);
    bool _s = (digitalRead(SEL_BTN) == LOW);

    // Если нажата хотя бы одна кнопка
    if (_s || _u || _d || _r || _l) {
        tm = millis();
        if (!wakeUpScreen()) AnyKeyPress = true;
        else return;
    }

    if (_l) { PrevPress = true; }
    if (_r) { NextPress = true; }
    if (_u) {
        UpPress = true;
        PrevPagePress = true;
    }
    if (_d) {
        DownPress = true;
        NextPagePress = true;
    }
    if (_s) { SelPress = true; }

    // Выход назад (ESC) при одновременном нажатии Влево и Вправо
    if (_l && _r) {
        EscPress = true;
        NextPress = false;
        PrevPress = false;
    }
}

/*********************************************************************
** Function: powerOff
** location: mykeyboard.cpp
** Turns off the device (or try to)
**********************************************************************/
void powerOff() {
    // Используем родную для ESP32-S3 логику пробуждения по низкому уровню (LOW) на кнопке ОК
    esp_sleep_enable_ext0_wakeup((gpio_num_t)SEL_BTN, 0);
    esp_deep_sleep_start();
}

/*********************************************************************
** Function: checkReboot
** location: mykeyboard.cpp
** Btn logic to turn off the device
**********************************************************************/
void checkReboot() {
    // Полностью очищаем эту функцию, чтобы предотвратить случайные ложные выключения
    // Удержание кнопок больше не вызовет сбой "pwr 1/3" и экстренный сон
}
