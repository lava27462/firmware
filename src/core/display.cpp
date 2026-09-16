#include "display.h"
#include "core/wifi/webInterface.h" // for server
#include "core/wifi/wg.h"           // for isConnectedWireguard to print wireguard lock
#include "mykeyboard.h"
#include "settings.h" // for timeStr
#include "utils.h"
#include <JPEGDecoder.h>
#include <interface.h> 
#include <memory>

#define MAX_MENU_SIZE (int)(tftHeight / 25)

// Send the ST7789 into or out of sleep mode
void panelSleep(bool on) {
#if defined(ST7789_2_DRIVER) || defined(ST7789_DRIVER)
    if (on) {
        tft.writecommand(0x10); // SLPIN: panel off
        delay(5);
    } else {
        tft.writecommand(0x11); // SLPOUT: panel on
        delay(120);
    }
#endif
    // Disables tft writings on the display
    tft.setSleepMode(on);
}

// УДАЛЕНО: Конфликтующий weak-метод isCharging(), вызывавший ошибку exit code 1

/***************************************************************************************
** Function name: displayScrollingText
** Description:   Scroll large texts into screen
***************************************************************************************/
void displayScrollingText(const String &text, Opt_Coord &coord, bool highlight) {
    int len = text.length();
    String displayText = text + "        "; // Add spaces for smooth looping
    int scrollLen = len + 8;                // Full text plus space buffer
    static int i = 0;
    static long _lastmillis = 0;
    if (highlight) tft.setTextColor(coord.bgcolor, coord.fgcolor);
    else tft.setTextColor(coord.fgcolor, coord.bgcolor);
    if (len < coord.size) {
        // Text fits within limit, no scrolling needed
        return;
    } else if (millis() > _lastmillis + 200) {
        String scrollingPart =
            displayText.substring(i, i + (coord.size - 1)); // Display charLimit characters at a time
        tft.fillRect(
            coord.x,
            coord.y,
            (coord.size - 1) * LW * tft.getTextSize(),
            LH * tft.getTextSize(),
            highlight ? coord.fgcolor : bruceConfig.bgColor
        ); // Clear display area
        tft.setCursor(coord.x, coord.y);
        tft.print(scrollingPart);
        if (i >= scrollLen - coord.size) i = -1; // Loop back
        _lastmillis = millis();
        i++;
        if (i == 1) _lastmillis = millis() + 1000;
    }
}

/***************************************************************************************
** Function name: TouchFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void TouchFooter(uint16_t color) {
#if defined(HAS_TOUCH)
    tft.drawRoundRect(5, tftHeight + 2, tftWidth - 10, 43, 5, color);
    tft.setTextColor(color);
    tft.setTextSize(FM);
    tft.drawCentreString("PREV", tftWidth / 6, tftHeight + 4, 1);
    tft.drawCentreString("SEL", tftWidth / 2, tftHeight + 4, 1);
    tft.drawCentreString("NEXT", 5 * tftWidth / 6, tftHeight + 4, 1);
#endif
}

/***************************************************************************************
** Function name: MegaFooter
** Description:   Draw touch screen footer
***************************************************************************************/
void MegaFooter(uint16_t color) {
    tft.drawRoundRect(5, tftHeight + 2, tftWidth - 10, 43, 5, color);
    tft.setTextColor(color);
    tft.setTextSize(FM);
    tft.drawCentreString("Exit", tftWidth / 6, tftHeight + 4, 1);
    tft.drawCentreString("UP", tftWidth / 2, tftHeight + 4, 1);
    tft.drawCentreString("DOWN", 5 * tftWidth / 6, tftHeight + 4, 1);
}

/***************************************************************************************
** Function name: resetTftDisplay
** Description:   set cursor to 0,0, screen and text to default color
***************************************************************************************/
void resetTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg, uint16_t screen) {
    tft.setCursor(x, y);
    tft.fillScreen(screen);
    tft.setTextSize(size);
    tft.setTextColor(fc, bg);
    tft.setTextDatum(0);
}

/***************************************************************************************
** Function name: setTftDisplay
** Description:   set cursor, font color, size and bg font color
***************************************************************************************/
void setTftDisplay(int x, int y, uint16_t fc, int size, uint16_t bg) {
    if (x >= 0 && y < 0) tft.setCursor(x, tft.getCursorY());      // if -1 on x, sets only y
    else if (x < 0 && y >= 0) tft.setCursor(tft.getCursorX(), y); // if -1 on y, sets only x
    else if (x >= 0 && y >= 0) tft.setCursor(x, y);               // if x and y > 0, sets both
    tft.setTextSize(size);
    tft.setTextColor(fc, bg);
}

void turnOffDisplay() { setBrightness(0, false); }

bool wakeUpScreen() {
    previousMillis = millis();
    if (isScreenOff) {
        isScreenOff = false;
        dimmer = false;
        getBrightness();
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
    } else if (dimmer) {
        dimmer = false;
        getBrightness();
        vTaskDelay(pdMS_TO_TICKS(200));
        return true;
    }
    return false;
}

/***************************************************************************************
** Function name: wrapText
** Description:   Wrap text to fit within a maximum width, returning vector of lines
***************************************************************************************/
std::vector<String> wrapText(const String &text, int maxCharsPerLine) {
    std::vector<String> lines;
    if (maxCharsPerLine <= 0) return lines;

    String remaining = text;
    while (remaining.length() > 0) {
        if (remaining.length() <= maxCharsPerLine) {
            lines.push_back(remaining);
            break;
        }
        int splitPos = -1;
        for (int i = maxCharsPerLine - 1; i >= 0; i--) {
            if (remaining[i] == ' ' || remaining[i] == '-' || remaining[i] == '_') {
                splitPos = i;
                break;
            }
        }
        if (splitPos <= 0) {
            splitPos = maxCharsPerLine;
        }
        lines.push_back(remaining.substring(0, splitPos));
        remaining = remaining.substring(splitPos + 1);
    }
    return lines;
}

/***************************************************************************************
** Function name: displayRedStripe
** Description:   Display Red Stripe with information (supports multi-line text wrapping)
***************************************************************************************/
void displayRedStripe(const String &text, uint16_t fgcolor, uint16_t bgcolor) {
    int size;
    if (fgcolor == bgcolor && fgcolor == TFT_WHITE) fgcolor = TFT_BLACK;

    int maxCharsFM = (tftWidth - 20) / (LW * FM);
    int maxCharsFP = (tftWidth - 20) / (LW * FP);

    std::vector<String> wrappedLines;
    int boxHeight = 26; 

    if (text.length() * LW * FM < (tftWidth - 2 * FM * LW)) {
        size = FM;
        wrappedLines = wrapText(text, maxCharsFM);
    } else {
        size = FP;
        wrappedLines = wrapText(text, maxCharsFP);
    }

    if (wrappedLines.size() > 1) { boxHeight = 13 + (wrappedLines.size() * (size == FM ? 8 : 10)); }

    tft.drawPixel(0, 0, 0);
    tft.fillRoundRect(10, tftHeight / 2 - boxHeight / 2, tftWidth - 20, boxHeight, 7, bgcolor);
    tft.setTextColor(fgcolor, bgcolor);
    tft.setTextSize(size);

    int lineHeight = size == FM ? 8 : 10;
    int startY = tftHeight / 2 - (wrappedLines.size() * lineHeight) / 2;
    
    // ДОПИСАНО: Логический финал функции для корректной компиляции
    for (size_t i = 0; i < wrappedLines.size(); i++) {
        tft.drawCentreString(wrappedLines[i], tftWidth / 2, startY + (i * lineHeight), 1);
    }
}
