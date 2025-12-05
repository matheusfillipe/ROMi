#pragma once

#define VITA_WIDTH  848
#define VITA_HEIGHT 512

#define ROMI_COLOR(R, G, B)		(((R)<<16) | ((G)<<8) | (B))
#define RGBA_COLOR(C, ALPHA)	((C<<8) | ALPHA)


#define ROMI_UTF8_O "\xfa" // "\xe2\x97\x8b" // 0x25cb
#define ROMI_UTF8_X "\xfb" // "\xe2\x95\xb3" // 0x2573
#define ROMI_UTF8_T "\xfc" // "\xe2\x96\xb3" // 0x25b3
#define ROMI_UTF8_S "\xfd" // "\xe2\x96\xa1" // 0x25a1


#define ROMI_UTF8_INSTALLED "\x04"//"\xe2\x97\x8f" // 0x25cf
#define ROMI_UTF8_PARTIAL   "\x09"//"\xe2\x97\x8b" // 0x25cb

#define ROMI_UTF8_B  "B"
#define ROMI_UTF8_KB "Kb" // "\xe3\x8e\x85" // 0x3385
#define ROMI_UTF8_MB "Mb" // "\xe3\x8e\x86" // 0x3386
#define ROMI_UTF8_GB "Gb" // "\xe3\x8e\x87" // 0x3387

#define ROMI_UTF8_CLEAR "\xaf" // 0x00d7

#define ROMI_UTF8_SORT_ASC  "\x1e" //"\xe2\x96\xb2" // 0x25b2
#define ROMI_UTF8_SORT_DESC "\x1f" //"\xe2\x96\xbc" // 0x25bc

#define ROMI_UTF8_CHECK_ON  "\x04"//"\xe2\x97\x8f" // 0x25cf
#define ROMI_UTF8_CHECK_OFF "\x09"//"\xe2\x97\x8b" // 0x25cb

#define ROMI_UTF8_ARROW_DOWN "\x1f" // Down arrow (reuse sort desc)
#define ROMI_UTF8_CHECK      "\x04" // Check mark (reuse check on)
#define ROMI_UTF8_SQUARE     "\xfd" // Square (reuse S button)

#define ROMI_COLOR_DIALOG_BACKGROUND    ROMI_COLOR(48, 48, 48)
#define ROMI_COLOR_MENU_BORDER          ROMI_COLOR(80, 80, 255)
#define ROMI_COLOR_MENU_BACKGROUND      ROMI_COLOR(48, 48, 48)
#define ROMI_COLOR_TEXT_MENU            ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_TEXT_MENU_SELECTED   ROMI_COLOR(0, 255, 0)
#define ROMI_COLOR_TEXT                 ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_TEXT_HEAD            ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_TEXT_TAIL            ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_TEXT_DIALOG          ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_TEXT_ERROR           ROMI_COLOR(255, 50, 50)
#define ROMI_COLOR_TEXT_SHADOW          ROMI_COLOR(0, 0, 0)
#define ROMI_COLOR_HLINE                ROMI_COLOR(200, 200, 200)
#define ROMI_COLOR_SCROLL_BAR           ROMI_COLOR(255, 255, 255)
#define ROMI_COLOR_BATTERY_LOW          ROMI_COLOR(255, 50, 50)
#define ROMI_COLOR_BATTERY_CHARGING     ROMI_COLOR(50, 255, 50)
#define ROMI_COLOR_SELECTED_BACKGROUND  ROMI_COLOR(60, 60, 60)
#define ROMI_COLOR_PROGRESS_BACKGROUND  ROMI_COLOR(128, 128, 128)
#define ROMI_COLOR_PROGRESS_BAR         ROMI_COLOR(128, 255, 0)

#define ROMI_ANIMATION_SPEED 4000 // px/second

#define ROMI_FONT_Z      1000
#define ROMI_FONT_WIDTH  10
#define ROMI_FONT_HEIGHT 16
#define ROMI_FONT_SHADOW 2

#define ROMI_MAIN_COLUMN_PADDING    10
#define ROMI_MAIN_HLINE_EXTRA       5
#define ROMI_MAIN_ROW_PADDING       2
#define ROMI_MAIN_HLINE_HEIGHT      2
#define ROMI_MAIN_TEXT_PADDING      5
#define ROMI_MAIN_SCROLL_WIDTH      2
#define ROMI_MAIN_SCROLL_PADDING    2
#define ROMI_MAIN_SCROLL_MIN_HEIGHT 50
#define ROMI_MAIN_HMARGIN           20
#define ROMI_MAIN_VMARGIN           20

#define ROMI_DIALOG_TEXT_Z  800
#define ROMI_DIALOG_HMARGIN 100
#define ROMI_DIALOG_VMARGIN 150
#define ROMI_DIALOG_PADDING 30
#define ROMI_DIALOG_WIDTH (VITA_WIDTH - 2*ROMI_DIALOG_HMARGIN)
#define ROMI_DIALOG_HEIGHT (VITA_HEIGHT - 2*ROMI_DIALOG_VMARGIN)

#define ROMI_DIALOG_PROCESS_BAR_HEIGHT  10
#define ROMI_DIALOG_PROCESS_BAR_PADDING 10
#define ROMI_DIALOG_PROCESS_BAR_CHUNK   200

#define ROMI_MENU_Z            900
#define ROMI_MENU_TEXT_Z       800
#define ROMI_MENU_WIDTH        150
#define ROMI_MENU_HEIGHT       (VITA_HEIGHT - 2*ROMI_MAIN_VMARGIN)
#define ROMI_MENU_LEFT_PADDING 20
#define ROMI_MENU_TOP_PADDING  30
