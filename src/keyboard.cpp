#include "globals.h"

// 8x5 on-screen keyboard grid: rows 0-3 are character cells (32 per page),
// row 4 is a control row of 4 double-wide buttons.
static const char KB_PAGES[3][32] = {
  { 'a','b','c','d','e','f','g','h',
    'i','j','k','l','m','n','o','p',
    'q','r','s','t','u','v','w','x',
    'y','z','0','1','2','3','4','5' },
  { 'A','B','C','D','E','F','G','H',
    'I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X',
    'Y','Z','6','7','8','9','-','_' },
  { '0','1','2','3','4','5','6','7',
    '8','9','.',',','!','@','#','$',
    '%','^','&','*','(',')','+','=',
    ':',';','\'','"','<','>','?','/' },
};

static const char *const KB_PAGE_NAMES[3]  = { "abc", "ABC", "123" };
static const char *const KB_CTRL_LABELS[4] = { "SPACE", "BKSP", "MODE", "DONE" };

static const int16_t GRID_Y  = 56;
static const int16_t CELL_W  = 40;
static const int16_t CELL_H  = 26;

static void drawCharCell(int row, int col, int page, bool selected)
{
  int16_t x = col * CELL_W;
  int16_t y = GRID_Y + row * CELL_H;
  uint16_t bg = selected ? tft.color565(0, 130, 190)
                          : (row & 1) ? tft.color565(12, 15, 38) : C_BG;
  uint16_t fg = selected ? TFT_WHITE : 0xDEFB;

  tft.fillRect(x, y, CELL_W - 1, CELL_H - 1, bg);
  tft.drawFastHLine(x, y + CELL_H - 1, CELL_W, tft.color565(20, 25, 55));
  tft.drawFastVLine(x + CELL_W - 1, y, CELL_H, tft.color565(20, 25, 55));

  tft.setTextColor(fg);
  tft.setTextSize(2);
  tft.setCursor(x + (CELL_W - 12) / 2, y + (CELL_H - 16) / 2);
  tft.print(KB_PAGES[page][row * 8 + col]);
}

static void drawCtrlCell(int idx, int page, bool selected)
{
  int16_t x = idx * 80;
  int16_t y = GRID_Y + 4 * CELL_H;
  uint16_t bg, fg;

  if (idx == 3) { // DONE - green accent
    bg = selected ? tft.color565(0, 220, 110) : tft.color565(0, 110, 60);
    fg = TFT_WHITE;
  } else {
    bg = selected ? tft.color565(0, 130, 190) : tft.color565(12, 15, 38);
    fg = selected ? TFT_WHITE : 0xDEFB;
  }

  tft.fillRect(x, y, 79, CELL_H - 1, bg);
  tft.drawFastHLine(x, y + CELL_H - 1, 80, tft.color565(20, 25, 55));
  tft.drawFastVLine(x + 79, y, CELL_H, tft.color565(20, 25, 55));

  const char *label = (idx == 2) ? KB_PAGE_NAMES[page] : KB_CTRL_LABELS[idx];
  tft.setTextColor(fg);
  tft.setTextSize(1);
  tft.setCursor(x + (80 - (int)strlen(label) * 6) / 2, y + (CELL_H - 8) / 2);
  tft.print(label);
}

static void drawKeyboardGrid(int page, int selRow, int selCol)
{
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 8; ++c)
      drawCharCell(r, c, page, (r == selRow && c == selCol));
  for (int i = 0; i < 4; ++i)
    drawCtrlCell(i, page, (selRow == 4 && selCol / 2 == i));
}

static void redrawCell(int row, int col, int page, bool selected)
{
  if (row < 4) drawCharCell(row, col, page, selected);
  else         drawCtrlCell(col / 2, page, selected);
}

// Masks all characters except (optionally) the most recently typed one,
// which is shown in plain text for ~1s so a long passphrase can be verified
// keystroke-by-keystroke without fully exposing it.
static void drawPreviewRow(const char *text, bool isPassword, bool showLastPlain)
{
  tft.fillRect(0, 37, TFT_W, 18, C_BG);
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(1);

  int len = (int)strlen(text);
  const int maxChars = 50;
  int startIdx = (len > maxChars) ? len - maxChars : 0;

  char display[64];
  int n = 0;
  for (int i = startIdx; i < len; ++i)
  {
    char c = text[i];
    if (isPassword && !(showLastPlain && i == len - 1)) c = '*';
    display[n++] = c;
  }
  display[n++] = '_'; // cursor
  display[n] = '\0';

  tft.setCursor(4, 42);
  tft.print(display);
}

bool showTextKeyboard(const char *title, char *buf, size_t bufLen, bool isPassword)
{
  char text[64];
  strncpy(text, buf, sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';
  size_t maxLen = (bufLen - 1 < sizeof(text) - 1) ? bufLen - 1 : sizeof(text) - 1;

  int page = 0;
  int selRow = 0, selCol = 0;
  unsigned long lastMoveMs = 0;
  unsigned long lastTypedMs = 0;
  bool maskPending = false;

  while (isJoystickButtonPressed()) delay(10);

  tft.fillScreen(C_BG);
  drawHeader(title);
  drawHints("Joy: move/select  Btn: type", "Hold Btn 0.5s: cancel");
  drawKeyboardGrid(page, selRow, selCol);
  drawPreviewRow(text, isPassword, false);

  while (true)
  {
    unsigned long now = millis();

    if (maskPending && now - lastTypedMs >= 1000)
    {
      maskPending = false;
      drawPreviewRow(text, isPassword, false);
    }

    int x = readJoystickAxis(JOY_X_PIN);
    int y = readJoystickAxis(JOY_Y_PIN);

    if ((x != 0 || y != 0) && now - lastMoveMs > 200)
    {
      int prevRow = selRow, prevCol = selCol;
      if (x != 0) selCol = (selCol + x + 8) % 8;
      if (y != 0) selRow = (selRow + y + 5) % 5;
      lastMoveMs = now;

      if (prevRow != selRow || prevCol != selCol)
      {
        redrawCell(prevRow, prevCol, page, false);
        redrawCell(selRow, selCol, page, true);
      }
    }

    if (isJoystickButtonPressed())
    {
      unsigned long pressStart = millis();
      while (isJoystickButtonPressed()) delay(10);
      if (millis() - pressStart >= 500) return false; // cancel, buf unchanged

      size_t len = strlen(text);
      char typedChar = 0;

      if (selRow < 4)
      {
        typedChar = KB_PAGES[page][selRow * 8 + selCol];
      }
      else
      {
        switch (selCol / 2)
        {
          case 0: typedChar = ' '; break;
          case 1: // BKSP
            if (len > 0)
            {
              text[len - 1] = '\0';
              maskPending = false;
              drawPreviewRow(text, isPassword, false);
            }
            break;
          case 2: // MODE - cycle character page
            page = (page + 1) % 3;
            drawKeyboardGrid(page, selRow, selCol);
            break;
          case 3: // DONE
            strncpy(buf, text, bufLen - 1);
            buf[bufLen - 1] = '\0';
            return true;
        }
      }

      if (typedChar && len < maxLen)
      {
        text[len] = typedChar;
        text[len + 1] = '\0';
        lastTypedMs = millis();
        maskPending = isPassword;
        drawPreviewRow(text, isPassword, maskPending);
      }
    }

    delay(10);
  }
}
