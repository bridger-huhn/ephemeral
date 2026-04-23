#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <math.h>        // NEW: for cos/sin in drawStar
#include <vector>

#define SD_CS     5
#define TFT_BL   27

#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25

#define SWIPE_THRESHOLD  40
#define HOLD_MS         600

// ─── Keyboard layout constants ────────────────────────────────────────────────
#define KEY_W     32
#define KEY_H     50
#define KEY_PAD    2
#define KB_Y      94

// ─── Key colour palette ───────────────────────────────────────────────────────
#define K_LETTER_N  tft.color565( 28,  45,  95)
#define K_LETTER_P  tft.color565( 72, 120, 230)

#define K_NUM_N     tft.color565( 30,  72,  50)
#define K_NUM_P     tft.color565( 65, 165, 100)

#define K_OK_N      tft.color565(  0, 100,   0)
#define K_OK_P      tft.color565(  0, 210,   0)

#define K_BSP_N     tft.color565(115,  22,  22)
#define K_BSP_P     tft.color565(220,  55,  55)

#define K_SPC_N     tft.color565( 50,  50,  55)
#define K_SPC_P     tft.color565(115, 115, 125)

#define K_DOT_N     tft.color565( 70,  55,  10)
#define K_DOT_P     tft.color565(170, 130,  25)

#define K_BORDER    tft.color565( 70,  70,  75)

// Search-bar colours
#define SB_BG       TFT_BLACK
#define SB_BORDER   tft.color565( 90,  90, 200)
#define SB_TEXT     TFT_WHITE
#define SB_HINT     tft.color565( 80,  80,  90)
#define SB_CURSOR   tft.color565(130, 130, 220)

// ─── NEW: Star overlay (bottom-center of card view) ──────────────────────────
#define STAR_CX     160   // horizontal centre of card
#define STAR_CY     441   // sits above the "hold: back to search" footer text
#define STAR_OR      15   // outer radius (points)
#define STAR_IR       6   // inner radius (notches)
#define STAR_HIT     22   // half-side of square hit-test zone

// ─── NEW: FAV star (keyboard screen, top-right corner) ───────────────────────
#define KB_FAV_CX   304   // 8 px from right edge of 320-wide screen
#define KB_FAV_CY    20   // vertically centred in the ~40px header band
#define KB_FAV_OR    13
#define KB_FAV_IR     5
#define KB_FAV_HIT   18

// ─── NEW: Favorites file ──────────────────────────────────────────────────────
#define FAVORITES_FILE  "/favorites.txt"

// ─── Objects ─────────────────────────────────────────────────────────────────
TFT_eSPI   tft = TFT_eSPI();
PNG        png;
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 320, 480);

// ─── State ───────────────────────────────────────────────────────────────────
String searchText    = "";
String searchName    = "";
int    searchPower   = -1;
int    searchTough   = -1;
String searchColor   = "";
String searchVersion = "";
bool   showingCard   = false;
bool   inFavoritesMode = false;   // NEW: true when matches came from favorites

std::vector<String> matches;
std::vector<String> matchVersions;
int  matchIndex     = 0;
int  tokenCount     = 1;
bool showTokenCount = false;
int  tappedCount    = 0;
int  displayPower   = 1;
int  displayTough   = 1;

int pressedRow = -1;
int pressedCol = -1;

// ─── Key table ────────────────────────────────────────────────────────────────
String keys[4][10] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","<"},
  {"z","x","c","v","b","n","m"," ","OK","."},
  {"1","2","3","4","5","6","7","8","9","0"}
};

// ─────────────────────────────────────────────────────────────────────────────
// PNG FILE CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────

void* myOpen(const char *filename, int32_t *size)
{
  File *f = new File(SD.open(filename));
  if (!f || !*f) { delete f; return NULL; }
  *size = f->size();
  return (void*)f;
}

void myClose(void *handle)
{
  File *f = (File*)handle;
  f->close();
  delete f;
}

int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length)
{
  return ((File*)handle->fHandle)->read(buffer, length);
}

int32_t mySeek(PNGFILE *handle, int32_t position)
{
  ((File*)handle->fHandle)->seek(position);
  return position;
}

// ─────────────────────────────────────────────────────────────────────────────
// PNG DRAW
// ─────────────────────────────────────────────────────────────────────────────

int pngDraw(PNGDRAW *pDraw)
{
  if (pDraw->y >= 480) return 1;
  static uint16_t lineBuffer[320];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(0, pDraw->y, min((int)pDraw->iWidth, 320), 1, lineBuffer);
  return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// KEYBOARD DRAWING
// ─────────────────────────────────────────────────────────────────────────────

void drawKey(int row, int col, bool pressed)
{
  int x = col * KEY_W;
  int y = KB_Y + row * KEY_H;
  const String &k = keys[row][col];

  uint16_t bg;
  if      (k == "OK") bg = pressed ? K_OK_P  : K_OK_N;
  else if (k == "<")  bg = pressed ? K_BSP_P : K_BSP_N;
  else if (k == " ")  bg = pressed ? K_SPC_P : K_SPC_N;
  else if (k == ".")  bg = pressed ? K_DOT_P : K_DOT_N;
  else if (row == 3)  bg = pressed ? K_NUM_P : K_NUM_N;
  else                bg = pressed ? K_LETTER_P : K_LETTER_N;

  tft.fillRect(x + KEY_PAD, y + KEY_PAD,
               KEY_W - KEY_PAD * 2, KEY_H - KEY_PAD * 2, bg);
  tft.drawRect(x, y, KEY_W, KEY_H, K_BORDER);

  if (k == " ") return;
  tft.setTextColor(TFT_WHITE, bg);
  if (k == "<") {
    tft.drawString("<", x + KEY_W / 2 - 5, y + KEY_H / 2 - 7, 2);
  } else {
    int tw = tft.textWidth(k, 2);
    tft.drawString(k, x + (KEY_W - tw) / 2, y + (KEY_H - 14) / 2, 2);
  }
}

void drawSearchBar()
{
  tft.fillRect(8, 34, 304, 34, SB_BG);
  tft.drawRoundRect(8, 34, 304, 34, 5, SB_BORDER);

  if (searchText.length() == 0) {
    tft.setTextColor(SB_HINT, SB_BG);
    tft.drawString("[qty] name [pw] [tgh] [color/C] [ver]", 14, 42, 2);
    return;
  }

  tft.setTextColor(SB_TEXT, SB_BG);
  String disp = searchText;
  while (disp.length() > 1 && tft.textWidth(disp, 2) > 278)
    disp = disp.substring(1);

  tft.drawString(disp, 14, 42, 2);
  int cx = 14 + tft.textWidth(disp, 2) + 2;
  if (cx < 304) tft.drawFastVLine(cx, 40, 20, SB_CURSOR);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: STAR DRAWING (5-pointed, filled or outline)
// ─────────────────────────────────────────────────────────────────────────────
//
// Vertices are computed as 10 points alternating between outerR and innerR,
// starting at the top (−90°) and stepping 36° each time.
// Filled: fan of triangles from the centre.
// Outline: polyline through all 10 vertices.

void drawStar(int cx, int cy, int outerR, int innerR, uint16_t color, bool filled)
{
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float angle = (i * 36.0f - 90.0f) * DEG_TO_RAD;
    float r     = (i % 2 == 0) ? outerR : innerR;
    px[i] = cx + r * cosf(angle);
    py[i] = cy + r * sinf(angle);
  }

  if (filled) {
    for (int i = 0; i < 10; i++) {
      int j = (i + 1) % 10;
      tft.fillTriangle(cx, cy,
                       (int)px[i], (int)py[i],
                       (int)px[j], (int)py[j],
                       color);
    }
  } else {
    for (int i = 0; i < 10; i++) {
      int j = (i + 1) % 10;
      tft.drawLine((int)px[i], (int)py[i],
                   (int)px[j], (int)py[j],
                   color);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: FAVORITES — SD FILE OPERATIONS
// ─────────────────────────────────────────────────────────────────────────────

// Returns true if `path` is present in /favorites.txt
bool isFavorite(const String &path)
{
  File f = SD.open(FAVORITES_FILE);
  if (!f) return false;
  bool found = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line == path) { found = true; break; }
  }
  f.close();
  return found;
}

// Adds path if not present; removes it if already present (toggle).
void toggleFavorite(const String &path)
{
  std::vector<String> lines;
  bool found = false;

  File f = SD.open(FAVORITES_FILE);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      if (line == path) { found = true; }   // skip (remove)
      else              { lines.push_back(line); }
    }
    f.close();
  }

  // Delete and rewrite
  SD.remove(FAVORITES_FILE);
  File out = SD.open(FAVORITES_FILE, FILE_WRITE);
  if (out) {
    for (auto &l : lines) out.println(l);
    if (!found) out.println(path);   // add if it was absent
    out.close();
  }

  Serial.printf("Favorite %s: %s\n", found ? "removed" : "added", path.c_str());
}

// Loads every line from /favorites.txt into matches[] (clears existing).
void loadFavoritesAsMatches()
{
  matches.clear();
  matchVersions.clear();
  File f = SD.open(FAVORITES_FILE);
  if (!f) {
    Serial.println("No favorites.txt found.");
    return;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      matches.push_back(line);
      matchVersions.push_back("");   // versions not stored in favorites
    }
  }
  f.close();
  Serial.printf("Favorites loaded: %d entries\n", matches.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: HIT-TEST HELPERS
// ─────────────────────────────────────────────────────────────────────────────

// Square hit zone around the card-view star button
bool inStarButton(int x, int y)
{
  return abs(x - STAR_CX) <= STAR_HIT && abs(y - STAR_CY) <= STAR_HIT;
}

// Square hit zone around the keyboard-screen FAV star
bool inKbFavStar(int x, int y)
{
  return abs(x - KB_FAV_CX) <= KB_FAV_HIT && abs(y - KB_FAV_CY) <= KB_FAV_HIT;
}

// Returns true when /favorites.txt exists and has content
bool hasFavorites()
{
  File f = SD.open(FAVORITES_FILE);
  if (!f) return false;
  bool ok = f.size() > 0;
  f.close();
  return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// MODIFIED: drawKeyboard — adds FAV star in the top-right header area
// ─────────────────────────────────────────────────────────────────────────────

void drawKeyboard()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(tft.color565(160, 160, 180), TFT_BLACK);
  tft.drawString("Card Search", 10, 8, 4);

  // ── NEW: FAV star ──────────────────────────────────────────────────────────
  // Filled gold if favorites exist, dim outline if none saved yet.
  bool favs       = hasFavorites();
  uint16_t kFavC  = favs ? tft.color565(255, 200, 0)    // gold
                          : tft.color565( 90,  80, 30);  // dim
  // Clear area first
  tft.fillRect(KB_FAV_CX - KB_FAV_OR - 4, KB_FAV_CY - KB_FAV_OR - 4,
               (KB_FAV_OR + 4) * 2,        (KB_FAV_OR + 4) * 2, TFT_BLACK);
  drawStar(KB_FAV_CX, KB_FAV_CY, KB_FAV_OR, KB_FAV_IR, kFavC, favs);
  if (!favs) {
    // draw outline even when dim so tap target is visible
    drawStar(KB_FAV_CX, KB_FAV_CY, KB_FAV_OR, KB_FAV_IR, kFavC, false);
  }
  // Tiny label beneath the star
  tft.setTextColor(kFavC, TFT_BLACK);
  int lw = tft.textWidth("FAV", 1);
  tft.drawString("FAV", KB_FAV_CX - lw / 2, KB_FAV_CY + KB_FAV_OR + 2, 1);
  // ── END NEW ───────────────────────────────────────────────────────────────

  drawSearchBar();

  tft.setTextColor(tft.color565(80, 80, 90), TFT_BLACK);
  tft.drawString("[qty] name [pw] [tgh] [W/U/B/R/G/C] [ver]", 4, 72, 1);

  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 10; c++)
      drawKey(r, c, false);

  pressedRow = -1;
  pressedCol = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// KEYBOARD HIT-TEST
// ─────────────────────────────────────────────────────────────────────────────

bool getKeyAtPos(int x, int y, int &row, int &col)
{
  if (y < KB_Y || y >= KB_Y + 4 * KEY_H) return false;
  row = (y - KB_Y) / KEY_H;
  col = x / KEY_W;
  if (row < 0 || row > 3 || col < 0 || col > 9) return false;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// INDEX SEARCH
// ─────────────────────────────────────────────────────────────────────────────

bool indexLineMatches(const String &line)
{
  int c1 = line.indexOf(',');
  int c2 = line.indexOf(',', c1 + 1);
  int c3 = line.indexOf(',', c2 + 1);
  int c4 = line.indexOf(',', c3 + 1);
  int c5 = line.indexOf(',', c4 + 1);
  if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) return false;

  String name     = line.substring(c1 + 1, c2);
  String powerStr = line.substring(c2 + 1, c3);
  String toughStr = line.substring(c3 + 1, c4);
  String colorStr   = (c5 > 0) ? line.substring(c4 + 1, c5) : line.substring(c4 + 1);
  String versionStr = (c5 > 0) ? line.substring(c5 + 1)     : "";

  String lSearch = searchName; lSearch.toLowerCase();
  String lName   = name;       lName.toLowerCase();
  if (lSearch != "" && !lName.startsWith(lSearch)) return false;

  if (searchPower != -1) {
    if (powerStr == "token") return false;
    if (powerStr.toInt() != searchPower) return false;
  }
  if (searchTough != -1) {
    if (toughStr == "token") return false;
    if (toughStr.toInt() != searchTough) return false;
  }

  if (searchColor != "") {
    String lColor   = colorStr;    lColor.toUpperCase();
    String lSearch2 = searchColor; lSearch2.toUpperCase();
    if (lColor != lSearch2) return false;
  }

  if (searchVersion != "") {
    String lVer  = versionStr;    lVer.toLowerCase();
    String lSrch = searchVersion; lSrch.toLowerCase();
    if (lVer.indexOf(lSrch) < 0) return false;
  }

  return true;
}

void buildMatchesFromIndex()
{
  matches.clear();
  matchVersions.clear();

  File idx = SD.open("/index.csv");
  if (!idx) {
    Serial.println("ERROR: index.csv not found.");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("No index.csv!", 20, 180, 4);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Run generate_index.py", 10, 230, 2);
    tft.drawString("and copy to SD card.", 10, 250, 2);
    return;
  }

  String line = "";
  while (idx.available()) {
    char c = idx.read();
    if (c == '\n' || c == '\r') {
      line.trim();
      if (line.length() > 0 && indexLineMatches(line)) {
        int c1 = line.indexOf(',');
        if (c1 > 0) {
          matches.push_back(line.substring(0, c1));
          int cc2 = line.indexOf(',', c1 + 1);
          int cc3 = line.indexOf(',', cc2 + 1);
          int cc4 = line.indexOf(',', cc3 + 1);
          int cc5 = line.indexOf(',', cc4 + 1);
          String ver = (cc5 > 0) ? line.substring(cc5 + 1) : "";
          ver.trim();
          matchVersions.push_back(ver);
        }
      }
      line = "";
    } else {
      line += c;
    }
  }
  line.trim();
  if (line.length() > 0 && indexLineMatches(line)) {
    int c1 = line.indexOf(',');
    if (c1 > 0) {
      matches.push_back(line.substring(0, c1));
      int cc2 = line.indexOf(',', c1 + 1);
      int cc3 = line.indexOf(',', cc2 + 1);
      int cc4 = line.indexOf(',', cc3 + 1);
      int cc5 = line.indexOf(',', cc4 + 1);
      String ver = (cc5 > 0) ? line.substring(cc5 + 1) : "";
      ver.trim();
      matchVersions.push_back(ver);
    }
  }
  idx.close();
  Serial.printf("Index search: %d matches\n", matches.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// IMAGE DISPLAY
// ─────────────────────────────────────────────────────────────────────────────

void showImage(String path)
{
  if (!path.startsWith("/")) path = "/" + path;
  int rc = png.open(path.c_str(), myOpen, myClose, myRead, mySeek, pngDraw);
  if (rc == PNG_SUCCESS) {
    tft.fillScreen(TFT_BLACK);
    png.decode(NULL, 0);
    png.close();
  } else {
    Serial.printf("PNG error %d: %s\n", rc, path.c_str());
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("PNG error: " + String(rc), 20, 200, 4);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// TOKEN COUNTER BADGE  (top-left)
// ─────────────────────────────────────────────────────────────────────────────

#define TC_X    4
#define TC_Y    4
#define TC_W  140
#define TC_H   56

inline int tcSplitX() { return TC_X + TC_W / 2 + 10; }

void drawTokenCount()
{
  int untappedCount = tokenCount - tappedCount;

  uint16_t bgCol    = tft.color565( 15,  25,  15);
  uint16_t bordCol  = tft.color565(  0, 180,  60);
  uint16_t untapCol = tft.color565(  0, 220,  80);
  uint16_t tapCol   = tft.color565(230, 120,   0);
  uint16_t divCol   = tft.color565( 80,  80,  80);

  tft.fillRoundRect(TC_X, TC_Y, TC_W, TC_H, 6, bgCol);
  tft.drawRoundRect(TC_X, TC_Y, TC_W, TC_H, 6, bordCol);

  int splitX = tcSplitX();

  if (tappedCount > 0) {
    tft.drawFastVLine(splitX, TC_Y + 6, TC_H - 12, divCol);
  }

  {
    String s     = String(untappedCount);
    int    availW = (tappedCount > 0) ? (splitX - TC_X) : TC_W;
    tft.setTextColor(untapCol, bgCol);
    int tw = tft.textWidth(s, 6);
    int tx = TC_X + (availW - tw) / 2;
    int ty = TC_Y + (TC_H - 48) / 2;
    tft.drawString(s, tx, ty, 6);
  }

  if (tappedCount > 0) {
    String s  = String(tappedCount);
    int    fw = tft.textWidth(s, 6);
    int    fh = 48;

    int sprW = fw + 4;
    int sprH = fh + 4;

    TFT_eSprite spr = TFT_eSprite(&tft);
    spr.createSprite(sprW, sprH);
    spr.fillSprite(bgCol);
    spr.setTextColor(tapCol, bgCol);
    spr.drawString(s, 2, 2, 6);

    int rightW = (TC_X + TC_W) - splitX;
    int cx     = splitX + rightW / 2;
    int cy     = TC_Y + TC_H / 2;

    spr.setPivot(sprW / 2, sprH / 2);
    tft.setPivot(cx, cy);
    spr.pushRotated(90);
    spr.deleteSprite();
  }
}

bool inTokenBadgeLeft(int x, int y)
{
  if (!showTokenCount) return false;
  return x >= TC_X && x < tcSplitX() && y >= TC_Y && y < TC_Y + TC_H;
}

bool inTokenBadgeRight(int x, int y)
{
  if (!showTokenCount) return false;
  return x >= tcSplitX() && x < TC_X + TC_W && y >= TC_Y && y < TC_Y + TC_H;
}

// ─────────────────────────────────────────────────────────────────────────────
// P/T BOX
// ─────────────────────────────────────────────────────────────────────────────

#define PT_W   68
#define PT_H   44
#define PT_X   (320 - PT_W - 10)
#define PT_Y   (480 - PT_H - 18)

void drawPTBox()
{
  tft.fillRect(PT_X, PT_Y, PT_W, PT_H, TFT_BLACK);
  tft.drawRect(PT_X, PT_Y, PT_W, PT_H, TFT_WHITE);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String s  = String(displayPower) + "/" + String(displayTough);
  int    tw = tft.textWidth(s, 4);
  int    tx = PT_X + (PT_W - tw) / 2;
  int    ty = PT_Y + (PT_H - 26) / 2;
  tft.drawString(s, tx, ty, 4);
}

bool inPTBox(int x, int y)
{
  return x >= PT_X && x < PT_X + PT_W && y >= PT_Y && y < PT_Y + PT_H;
}

#define PT_DEC_X  10
#define PT_DEC_Y  PT_Y

bool inPTDecBox(int x, int y)
{
  return x >= PT_DEC_X && x < PT_DEC_X + PT_W
      && y >= PT_DEC_Y && y < PT_DEC_Y + PT_H;
}

// ─────────────────────────────────────────────────────────────────────────────
// MODIFIED: OVERLAY  — now draws the gold star button at bottom-centre
// ─────────────────────────────────────────────────────────────────────────────

void drawStarOverlay()
{
  // Clear a small black circle so the star sits on a dark background
  // regardless of the card art underneath.
  tft.fillCircle(STAR_CX, STAR_CY, STAR_OR + 5, TFT_BLACK);

  bool fav         = isFavorite(matches[matchIndex]);
  uint16_t starCol = tft.color565(255, 200, 0);   // gold

  drawStar(STAR_CX, STAR_CY, STAR_OR, STAR_IR, starCol, fav);
  if (!fav) {
    // Outline-only when not yet favorited so the silhouette is still clear
    drawStar(STAR_CX, STAR_CY, STAR_OR, STAR_IR, starCol, false);
  }
}

void drawOverlay()
{
  if (showTokenCount) drawTokenCount();

  // Match counter (top-right)
  String label = String(matchIndex + 1) + " / " + String(matches.size());
  // NEW: prefix label for favorites mode so the user knows where they are
  if (inFavoritesMode) label = "★ " + label;

  int lw = tft.textWidth(label, 2) + 8;
  int lx = 320 - lw;
  tft.fillRect(lx, 0, lw, 32, TFT_BLACK);
  tft.setTextColor(inFavoritesMode ? tft.color565(255, 200, 0) : TFT_YELLOW);
  tft.drawString(label, lx + 4, 2, 2);

  // Version label
  if (matchIndex < (int)matchVersions.size() && matchVersions[matchIndex].length() > 0) {
    String verLabel = "ver:" + matchVersions[matchIndex];
    int vw = tft.textWidth(verLabel, 1) + 6;
    int vx = 320 - vw;
    tft.setTextColor(tft.color565(160, 160, 80));
    tft.drawString(verLabel, vx + 3, 18, 1);
  }

  // Navigation arrows
  tft.setTextColor(TFT_DARKGREY);
  if (matchIndex > 0)                        tft.drawString("<", 2,   230, 4);
  if (matchIndex < (int)matches.size() - 1)  tft.drawString(">", 304, 230, 4);
  tft.drawString("hold: back to search", 20, 463, 2);

  // ── NEW: star button ──────────────────────────────────────────────────────
  drawStarOverlay();
}

void showCardAt(int index)
{
  if (index < 0) index = 0;
  if (index >= (int)matches.size()) index = (int)matches.size() - 1;
  matchIndex = index;
  showImage(matches[matchIndex]);
  drawOverlay();
  showingCard = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SEARCH PARSER
// ─────────────────────────────────────────────────────────────────────────────

void parseSearch()
{
  searchText.trim();
  searchName     = "";
  searchPower    = -1;
  searchTough    = -1;
  searchColor    = "";
  searchVersion  = "";
  showTokenCount = false;
  tokenCount     = 1;
  tappedCount    = 0;

  String working    = searchText;
  int    firstSpace = working.indexOf(' ');
  if (firstSpace > 0)
  {
    String first = working.substring(0, firstSpace);
    bool allDigits = true;
    for (int i = 0; i < (int)first.length(); i++)
      if (!isDigit(first[i])) { allDigits = false; break; }
    if (allDigits)
    {
      tokenCount     = first.toInt();
      showTokenCount = (tokenCount > 0);
      working        = working.substring(firstSpace + 1);
      working.trim();
    }
  }

  std::vector<String> parts;
  String tmp = working;
  while (tmp.length() > 0)
  {
    int sp = tmp.indexOf(' ');
    if (sp < 0) { parts.push_back(tmp); break; }
    parts.push_back(tmp.substring(0, sp));
    tmp = tmp.substring(sp + 1);
    tmp.trim();
  }

  const String colorCodes = "WUBRGC";
  for (int i = 1; i < (int)parts.size(); i++)
  {
    String p = parts[i]; p.toUpperCase();
    bool allColor = (p.length() >= 1);
    for (int j = 0; j < (int)p.length(); j++)
      if (colorCodes.indexOf(p[j]) < 0) { allColor = false; break; }
    if (allColor)
    {
      searchColor = p;
      parts.erase(parts.begin() + i);
      break;
    }
  }

  for (int i = 1; i < (int)parts.size(); i++)
  {
    String p = parts[i];
    if ((p[0] == 'v' || p[0] == 'V') && p.length() >= 2 && isAlphaNumeric(p[1]))
    {
      searchVersion = p.substring(1);
      parts.erase(parts.begin() + i);
      break;
    }
  }

  if ((int)parts.size() >= 4)
  {
    searchVersion = parts[3];
    parts.erase(parts.begin() + 3);
  }

  if (parts.size() >= 1) searchName  = parts[0];
  if (parts.size() >= 2) searchPower = parts[1].toInt();
  if (parts.size() >= 3) searchTough = parts[2].toInt();

  displayPower = (searchPower >= 0) ? searchPower : 1;
  displayTough = (searchTough >= 0) ? searchTough : 1;

  Serial.printf("Search: qty=%d name='%s' power=%d tough=%d color='%s' version='%s'\n",
                tokenCount, searchName.c_str(), searchPower, searchTough,
                searchColor.c_str(), searchVersion.c_str());
}

void startSearch()
{
  inFavoritesMode = false;   // NEW: entering a fresh search resets the flag

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Searching...", 80, 220, 4);
  buildMatchesFromIndex();

  if (matches.empty()) {
    drawKeyboard();
    tft.setTextColor(TFT_RED);
    tft.drawString("No match found", 20, 295, 4);
    showingCard = false;
    return;
  }
  showCardAt(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// KEY PRESS HANDLER
// ─────────────────────────────────────────────────────────────────────────────

void handleKeyPress(int row, int col)
{
  const String &k = keys[row][col];

  if (k == "<") {
    if (searchText.length() > 0)
      searchText.remove(searchText.length() - 1);
  } else if (k == "OK") {
    parseSearch();
    startSearch();
    return;
  } else {
    searchText += k;
  }

  drawSearchBar();
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
  Serial.begin(115200);
  delay(500);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS)) { Serial.println("SD INIT FAILED"); while (1); }

  if (!SD.exists("/index.csv"))
    Serial.println("WARNING: /index.csv not found. Run generate_index.py!");
  else
    Serial.println("index.csv found. Ready.");

  tft.init();
  tft.setRotation(0);

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  touch.begin();

  drawKeyboard();
}

// ─────────────────────────────────────────────────────────────────────────────
// MODIFIED: LOOP — handles star tap (card view) and FAV star tap (keyboard)
// ─────────────────────────────────────────────────────────────────────────────

int           touchStartX  = 0;
int           touchStartY  = 0;
unsigned long touchStartMs = 0;
bool          touchActive  = false;

void loop()
{
  touch.read();

  if (touch.isTouched)
  {
    int tx = 320 - touch.points[0].x;
    int ty = 480 - touch.points[0].y;

    if (!touchActive)
    {
      touchActive  = true;
      touchStartX  = tx;
      touchStartY  = ty;
      touchStartMs = millis();

      if (!showingCard)
      {
        int kr, kc;
        if (getKeyAtPos(tx, ty, kr, kc))
        {
          if (pressedRow >= 0) drawKey(pressedRow, pressedCol, false);
          pressedRow = kr;
          pressedCol = kc;
          drawKey(kr, kc, true);
        }
      }
    }
  }
  else
  {
    if (touchActive)
    {
      touchActive = false;

      int endX = 320 - touch.points[0].x;
      int endY = 480 - touch.points[0].y;
      int dx   = endX - touchStartX;
      int dy   = endY - touchStartY;
      unsigned long held = millis() - touchStartMs;

      if (showingCard)
      {
        // ── Long-press on token badge → untap ALL ─────────────────────────────
        bool onBadge = showTokenCount
            && (inTokenBadgeLeft(touchStartX, touchStartY)
             || inTokenBadgeRight(touchStartX, touchStartY));

        if (onBadge && held >= 350 && tappedCount > 0)
        {
          tappedCount = 0;
          drawTokenCount();
        }
        // ── Long-press anywhere else → back to search ─────────────────────────
        else if (held >= HOLD_MS)
        {
          showingCard     = false;
          inFavoritesMode = false;   // NEW: reset mode on exit
          searchText      = "";
          matches.clear();
          matchVersions.clear();
          drawKeyboard();
        }
        // ── Vertical swipe → add / remove tokens ─────────────────────────────
        else if (abs(dy) >= SWIPE_THRESHOLD && abs(dy) > abs(dx))
        {
          if (!showTokenCount && dy < 0)
          {
            tokenCount     = 1;
            tappedCount    = 0;
            showTokenCount = true;
          }
          else if (showTokenCount)
          {
            if (dy < 0)
            {
              tokenCount++;
            }
            else if (tokenCount > 1)
            {
              tokenCount--;
              if (tappedCount > 0) tappedCount--;
            }
          }
          drawTokenCount();
        }
        // ── Horizontal swipe → next / previous card ───────────────────────────
        else if (abs(dx) >= SWIPE_THRESHOLD && abs(dx) > abs(dy))
        {
          if (dx < 0) showCardAt(matchIndex + 1);
          else        showCardAt(matchIndex - 1);
        }
        // ── Tap ───────────────────────────────────────────────────────────────
        else
        {
          if (inTokenBadgeRight(touchStartX, touchStartY))
          {
            if (tappedCount < tokenCount)
            {
              tappedCount++;
              drawTokenCount();
            }
          }
          else if (inTokenBadgeLeft(touchStartX, touchStartY))
          {
            if (tappedCount > 0)
            {
              tappedCount--;
              drawTokenCount();
            }
          }
          // ── NEW: star button tap — toggle favorite ─────────────────────────
          else if (inStarButton(touchStartX, touchStartY))
          {
            toggleFavorite(matches[matchIndex]);
            // Redraw only the star so we don't re-decode the PNG
            drawStarOverlay();
          }
          // ── END NEW ──────────────────────────────────────────────────────────
          else if (inPTBox(touchStartX, touchStartY))
          {
            displayPower++;
            displayTough++;
            drawPTBox();
          }
          else if (inPTDecBox(touchStartX, touchStartY))
          {
            if (displayPower > 0) displayPower--;
            if (displayTough > 0) displayTough--;
            drawPTBox();
          }
          else
          {
            int next = matchIndex + 1;
            if (next >= (int)matches.size()) next = 0;
            showCardAt(next);
          }
        }
      }
      else
      {
        // ── Keyboard screen ───────────────────────────────────────────────────

        // ── NEW: FAV star button → load favorites list ─────────────────────
        if (inKbFavStar(touchStartX, touchStartY))
        {
          tft.fillScreen(TFT_BLACK);
          tft.setTextColor(TFT_WHITE);
          tft.drawString("Loading favorites...", 40, 220, 4);

          loadFavoritesAsMatches();

          if (matches.empty()) {
            drawKeyboard();
            tft.setTextColor(tft.color565(255, 200, 0));
            tft.drawString("No favorites saved yet", 10, 295, 2);
          } else {
            inFavoritesMode = true;
            tokenCount      = 1;
            tappedCount     = 0;
            showTokenCount  = false;
            displayPower    = 1;
            displayTough    = 1;
            showCardAt(0);
          }

          pressedRow = -1;
          pressedCol = -1;
        }
        // ── END NEW ──────────────────────────────────────────────────────────
        else if (pressedRow >= 0)
        {
          // Regular keyboard key: un-highlight then fire
          drawKey(pressedRow, pressedCol, false);
          handleKeyPress(pressedRow, pressedCol);
          pressedRow = -1;
          pressedCol = -1;
        }
      }
    }
  }

  delay(20);
}
