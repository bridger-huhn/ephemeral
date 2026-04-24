#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <math.h>
#include <vector>

#define SD_CS     5
#define TFT_BL   27

#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define TOUCH_INT 21
#define TOUCH_RST 25

#define SWIPE_THRESHOLD  40
#define HOLD_MS         600



// ─── Keyboard layout ──────────────────────────────────────────────────────────
#define KEY_W     32
#define KEY_H     50
#define KEY_PAD    2
#define KB_Y      94

// ─── MODERN UI THEME ─────────────────────────────────────────────────────────
#define UI_BG        tft.color565(10, 12, 18)
#define UI_PANEL     tft.color565(18, 22, 30)
#define UI_PANEL_ALT tft.color565(22, 26, 36)

#define UI_ACCENT       tft.color565(80, 140, 255)
#define UI_ACCENT_SOFT  tft.color565(40, 80, 160)

#define UI_TEXT      TFT_WHITE
#define UI_TEXT_DIM  tft.color565(140, 150, 170)

#define KEY_BG       tft.color565(30, 36, 48)
#define KEY_BG_P     tft.color565(70, 100, 180)

#define KEY_OK       tft.color565(60, 180, 100)
#define KEY_BSP      tft.color565(200, 80, 80)


// ─── Star overlay — card view, bottom-centre ──────────────────────────────────
#define STAR_CX   160
#define STAR_CY   441
#define STAR_OR    15
#define STAR_IR     6
#define STAR_HIT   22

// ─── FAV star button — keyboard screen, top-right ─────────────────────────────
#define KB_FAV_CX  304
#define KB_FAV_CY   20
#define KB_FAV_OR   13
#define KB_FAV_IR    5
#define KB_FAV_HIT  18

// ─── Favorites menu ───────────────────────────────────────────────────────────
// File format: one "path,Card Name" line per favorite.
#define FAVORITES_FILE  "/favorites.txt"
#define FAV_MENU_Y0   44
#define FAV_MENU_Y1  456
#define FAV_ROW_H     52    // (456-44)/52 = 7 rows visible

// ─── Token counter badge ──────────────────────────────────────────────────────
#define TC_X    4
#define TC_Y    4
#define TC_W  140
#define TC_H   56
inline int tcSplitX() { return TC_X + TC_W / 2 + 10; }

// ─── P/T box ─────────────────────────────────────────────────────────────────
#define PT_W      68
#define PT_H      44
#define PT_X      (320 - PT_W - 10)
#define PT_Y      (480 - PT_H - 18)
#define PT_DEC_X  10
#define PT_DEC_Y  PT_Y

// ─────────────────────────────────────────────────────────────────────────────
// OBJECTS
// ─────────────────────────────────────────────────────────────────────────────
TFT_eSPI   tft = TFT_eSPI();
PNG        png;
TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, 320, 480);

// ─────────────────────────────────────────────────────────────────────────────
// STATE
// ─────────────────────────────────────────────────────────────────────────────
String searchText    = "";
String searchName    = "";
int    searchPower   = -1;
int    searchTough   = -1;
String searchColor   = "";
String searchVersion = "";

bool showingCard     = false;
bool inFavoritesMode = false;

// Favorites menu
bool  showFavMenu   = false;
float favScrollPx   = 0;   // pixel scroll offset (smooth)
float favScrollVel  = 0;   // momentum velocity (px/frame)
int   favMenuTotal  = 0;


// KEY OPTIMISATION: fav status cached as a single bool.
// Set once in showCardAt(); drawStarOverlay() reads it — zero SD I/O per frame.
bool currentCardIsFav = false;

// ─── Star animation state ───────────────────────────────────────────
bool starPressed = false;
unsigned long starPressTime = 0;
#define STAR_PRESS_ANIM_MS 120

// Optional (for non-blocking save)
bool pendingFavSave = false;
String pendingPath;
String pendingName;

std::vector<String> matches;
std::vector<String> matchVersions;   // version in search mode; card name in fav-browse mode
std::vector<String> matchNames;      // card name from index (col 1) — search mode only
std::vector<String> favCache;        // display names cached at menu open — no SD per frame
std::vector<String> favPaths;        // file paths cached at menu open (parallel to favCache)


int  matchIndex     = 0;
int  tokenCount     = 1;
bool showTokenCount = false;
int  tappedCount    = 0;
int  displayPower   = 1;
int  displayTough   = 1;

int pressedRow = -1;
int pressedCol = -1;

String keys[4][10] = {
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","<"},
  {"z","x","c","v","b","n","m"," ","OK","."},
  {"1","2","3","4","5","6","7","8","9","0"}
};

// ─────────────────────────────────────────────────────────────────────────────
// PNG FILE CALLBACKS
// ─────────────────────────────────────────────────────────────────────────────
void* myOpen(const char *fn, int32_t *size)
{
  File *f = new File(SD.open(fn));
  if (!f || !*f) { delete f; return NULL; }
  *size = f->size();
  return (void*)f;
}
void    myClose(void *h)                      { ((File*)h)->close(); delete (File*)h; }
int32_t myRead(PNGFILE *h,uint8_t *b,int32_t l){ return ((File*)h->fHandle)->read(b,l); }
int32_t mySeek(PNGFILE *h,int32_t pos)        { ((File*)h->fHandle)->seek(pos); return pos; }

int pngDraw(PNGDRAW *pDraw)
{
  if (pDraw->y >= 480) return 1;
  static uint16_t lb[320];
  png.getLineAsRGB565(pDraw, lb, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(0, pDraw->y, min((int)pDraw->iWidth, 320), 1, lb);
  return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// STAR DRAWING
// ─────────────────────────────────────────────────────────────────────────────
void drawStar(int cx, int cy, int outerR, int innerR, uint16_t col, bool filled)
{
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float a = (i * 36.0f - 90.0f) * DEG_TO_RAD;
    float r = (i % 2 == 0) ? outerR : innerR;
    px[i] = cx + r * cosf(a);
    py[i] = cy + r * sinf(a);
  }
  if (filled) {
    for (int i = 0; i < 10; i++) {
      int j = (i+1)%10;
      tft.fillTriangle(cx,cy,(int)px[i],(int)py[i],(int)px[j],(int)py[j],col);
    }
  } else {
    for (int i = 0; i < 10; i++) {
      int j = (i+1)%10;
      tft.drawLine((int)px[i],(int)py[i],(int)px[j],(int)py[j],col);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// FAVORITES — SD OPERATIONS
// ─────────────────────────────────────────────────────────────────────────────

// Called ONCE per card navigation. Result stored in currentCardIsFav.
bool isFavorite(const String &path)
{
  File f = SD.open(FAVORITES_FILE);
  if (!f) return false;
  bool found = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int c = line.indexOf(',');
    String p = (c > 0) ? line.substring(0, c) : line;
    if (p == path) { found = true; break; }
  }
  f.close();
  return found;
}

// Count entries — used when opening the menu.
int countFavorites()
{
  File f = SD.open(FAVORITES_FILE);
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) n++;
  }
  f.close();
  return n;
}

// Toggle favorite. Rewrites the file (user-initiated action, latency OK).
// Immediately flips currentCardIsFav — no second SD read needed.
void toggleFavorite(const String &path, const String &name)
{
  std::vector<String> kept;
  bool found = false;
  File f = SD.open(FAVORITES_FILE);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int c = line.indexOf(',');
      String p = (c > 0) ? line.substring(0, c) : line;
      if (p == path) found = true;
      else           kept.push_back(line);
    }
    f.close();
  }
  SD.remove(FAVORITES_FILE);
  File out = SD.open(FAVORITES_FILE, FILE_WRITE);
  if (out) {
    for (auto &l : kept) out.println(l);
    if (!found) { out.print(path); out.print(','); out.println(name); }
    out.close();
  }
  currentCardIsFav = !found;   // flip cache — star redraws instantly, no SD re-read
  Serial.printf("Favorite %s: %s\n", found?"removed":"added", path.c_str());
}

// Populate matches[] from favorites file for card-browse mode.
// matchVersions[] is repurposed to hold card names in this mode.
void loadFavoritesAsMatches()
{
  matches.clear();
  matchVersions.clear();
  matchNames.clear();
  File f = SD.open(FAVORITES_FILE);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int c = line.indexOf(',');
    if (c > 0) { matches.push_back(line.substring(0,c)); matchVersions.push_back(line.substring(c+1)); }
    else       { matches.push_back(line);                matchVersions.push_back(line); }
  }
  f.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// HIT-TEST HELPERS
// ─────────────────────────────────────────────────────────────────────────────
bool inStarButton(int x,int y)    { return abs(x-STAR_CX)   <=STAR_HIT   && abs(y-STAR_CY)   <=STAR_HIT; }
bool inKbFavStar(int x,int y)     { return abs(x-KB_FAV_CX) <=KB_FAV_HIT && abs(y-KB_FAV_CY) <=KB_FAV_HIT; }
bool inTokenBadgeLeft(int x,int y) { return showTokenCount && x>=TC_X && x<tcSplitX() && y>=TC_Y && y<TC_Y+TC_H; }
bool inTokenBadgeRight(int x,int y){ return showTokenCount && x>=tcSplitX() && x<TC_X+TC_W && y>=TC_Y && y<TC_Y+TC_H; }
bool inPTBox(int x,int y)    { return x>=PT_X && x<PT_X+PT_W && y>=PT_Y && y<PT_Y+PT_H; }
bool inPTDecBox(int x,int y) { return x>=PT_DEC_X && x<PT_DEC_X+PT_W && y>=PT_DEC_Y && y<PT_DEC_Y+PT_H; }

// Returns absolute list index of tapped row, or -1.
int favMenuRowAt(int x, int y)
{
  if (y < FAV_MENU_Y0 || y >= FAV_MENU_Y1) return -1;
  int absY = (int)favScrollPx + (y - FAV_MENU_Y0);
  int idx  = absY / FAV_ROW_H;
  return (idx >= 0 && idx < favMenuTotal) ? idx : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// FAVORITES MENU  (reads SD on draw — only triggered by user actions)
// ─────────────────────────────────────────────────────────────────────────────
void drawFavMenu()
{
  // ── Palette ───────────────────────────────────────────────────────────────
  uint16_t hdBg   = tft.color565(12,  15,  25);   // header / footer bg
  uint16_t rowBg0 = tft.color565(16,  20,  34);   // even row
  uint16_t rowBg1 = tft.color565(22,  27,  44);   // odd row
  uint16_t gold   = tft.color565(255, 200,  0);
  uint16_t goldDm = tft.color565(120,  90,  0);
  uint16_t divCol = tft.color565( 36,  42,  62);  // row divider
  uint16_t redX   = tft.color565(210,  55,  55);
  uint16_t sbBg   = tft.color565( 28,  33,  50);  // scrollbar track
  uint16_t badge  = tft.color565( 50,  80, 160);  // count badge

  int   maxRows  = (FAV_MENU_Y1 - FAV_MENU_Y0) / FAV_ROW_H;
  float maxPx    = (float)max(0, favMenuTotal - maxRows) * FAV_ROW_H;
  bool  hasScroll = (favMenuTotal > maxRows);
  int   rowW     = hasScroll ? 314 : 320;   // leave 6px for scrollbar

  // ── Rows ──────────────────────────────────────────────────────────────────
  int lastY = FAV_MENU_Y0;

  if (favMenuTotal == 0) {
    tft.fillRect(0, FAV_MENU_Y0, 320, FAV_MENU_Y1 - FAV_MENU_Y0, hdBg);
    tft.setTextColor(tft.color565(70, 75, 110));
    int mw = tft.textWidth("No favorites saved yet.", 2);
    tft.drawString("No favorites saved yet.", (320 - mw) / 2, 218, 2);
    tft.setTextColor(tft.color565(50, 55, 85));
    int hw = tft.textWidth("Tap the star on any card to add one", 1);
    tft.drawString("Tap the star on any card to add one", (320 - hw) / 2, 240, 1);
  } else {
    for (int i = 0; i < (int)favCache.size(); i++) {
      int ry = FAV_MENU_Y0 + i * FAV_ROW_H - (int)favScrollPx;
      if (ry + FAV_ROW_H <= FAV_MENU_Y0) continue;
      if (ry >= FAV_MENU_Y1)             break;

      uint16_t bg  = (i % 2 == 0) ? rowBg0 : rowBg1;
      int clipY    = max(ry, FAV_MENU_Y0);
      int clipH    = min(ry + FAV_ROW_H, FAV_MENU_Y1) - clipY;
      tft.fillRect(0, clipY, rowW, clipH, bg);
      lastY = clipY + clipH;

      if (ry >= FAV_MENU_Y0 && ry + FAV_ROW_H <= FAV_MENU_Y1) {
        // Gold left-accent bar
        tft.fillRect(0, ry, 4, FAV_ROW_H - 1, gold);

        // Bottom divider (skip on last entry)
        tft.drawFastHLine(4, ry + FAV_ROW_H - 1, rowW - 4, divCol);

        // Card name
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(favCache[i], 14, ry + (FAV_ROW_H - 14) / 2, 2);

        // Red X delete button
        int cx = 297, cy = ry + FAV_ROW_H / 2;
        tft.fillCircle(cx, cy, 13, redX);
        // X arms — two lines through center
        tft.drawLine(cx-6, cy-6, cx+6, cy+6, TFT_WHITE);
        tft.drawLine(cx+6, cy-6, cx-6, cy+6, TFT_WHITE);
        tft.drawLine(cx-5, cy-6, cx+7, cy+6, TFT_WHITE);  // bold
        tft.drawLine(cx+7, cy-6, cx-5, cy+6, TFT_WHITE);
      }
    }
    // Gap below last row
    if (lastY < FAV_MENU_Y1)
      tft.fillRect(0, lastY, rowW, FAV_MENU_Y1 - lastY, hdBg);
  }

  // ── Scrollbar ─────────────────────────────────────────────────────────────
  if (hasScroll) {
    int sbX = 314, sbW = 6;
    int sbH = FAV_MENU_Y1 - FAV_MENU_Y0;
    tft.fillRect(sbX, FAV_MENU_Y0, sbW, sbH, sbBg);
    int totalH  = favMenuTotal * FAV_ROW_H;
    int thumbH  = max(24, (int)((float)sbH * sbH / totalH));
    int thumbY  = FAV_MENU_Y0 + (int)((float)(sbH - thumbH) * favScrollPx / maxPx);
    tft.fillRoundRect(sbX + 1, thumbY + 1, sbW - 2, thumbH - 2, 2, gold);
  }

  // ── Header ────────────────────────────────────────────────────────────────
  tft.fillRect(0, 0, 320, FAV_MENU_Y0, hdBg);
  // Star icon (left)
  drawStar(17, 21, 11, 4, gold, true);
  // Title
  tft.setTextColor(TFT_WHITE, hdBg);
  tft.drawString("Favorites", 34, 7, 4);
  // Count badge (top-right)
  char cnt[16]; sprintf(cnt, "%d", favMenuTotal);
  int bw = tft.textWidth(cnt, 2) + 12;
  int bx = 318 - bw, by = 8;
  tft.fillRoundRect(bx, by, bw, 18, 5, badge);
  tft.setTextColor(TFT_WHITE, badge);
  tft.drawString(cnt, bx + 6, by + 2, 2);
  // Double rule
  tft.drawFastHLine(0, FAV_MENU_Y0 - 2, 320, gold);
  tft.drawFastHLine(0, FAV_MENU_Y0 - 1, 320, goldDm);

  // ── Footer ────────────────────────────────────────────────────────────────
  tft.fillRect(0, FAV_MENU_Y1, 320, 480 - FAV_MENU_Y1, hdBg);
  tft.drawFastHLine(0, FAV_MENU_Y1, 320, divCol);
  tft.setTextColor(tft.color565(65, 70, 100), hdBg);
  const char *hint = (favMenuTotal == 0) ? "hold to go back"
                                         : "swipe to scroll   |   hold to go back";
  int hw2 = tft.textWidth(hint, 1);
  tft.drawString(hint, (320 - hw2) / 2, 465, 1);
}

void openFavMenu()
{
  showFavMenu  = true;
  showingCard  = false;
  favScrollPx  = 0;
  favScrollVel = 0;
  favMenuTotal = countFavorites();

  // Cache display names + paths so drawFavMenu() needs zero SD I/O per frame.
  favCache.clear();
  favPaths.clear();
  File f = SD.open(FAVORITES_FILE);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
      int comma = line.indexOf(',');
      String path  = (comma > 0) ? line.substring(0, comma) : line;
      String dname = (comma > 0) ? line.substring(comma+1)  : line;
      while (dname.length() > 1 && tft.textWidth(dname,2) > 252) dname.remove(dname.length()-1);
      favPaths.push_back(path);
      favCache.push_back(dname);
    }
    f.close();
  }

  drawFavMenu();
}

// Tap on a menu row → load all favorites into matches[], start at tapped card.
void openFavoritesFromMenu(int targetIdx)
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Loading...", 80, 220, 4);
  loadFavoritesAsMatches();
  if (matches.empty()) { openFavMenu(); return; }
  inFavoritesMode = true;
  showFavMenu     = false;
  tokenCount=1; tappedCount=0; showTokenCount=false;
  displayPower=1; displayTough=1;
  showCardAt((targetIdx < (int)matches.size()) ? targetIdx : 0);
}

// Delete a favorite by its menu index — rewrites SD, updates caches in-place.
void deleteFavAt(int idx)
{
  if (idx < 0 || idx >= (int)favPaths.size()) return;
  String pathToRemove = favPaths[idx];

  // Rewrite the file without this entry
  std::vector<String> kept;
  File f = SD.open(FAVORITES_FILE);
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n'); line.trim();
      if (line.length() == 0) continue;
      int c = line.indexOf(',');
      String p = (c > 0) ? line.substring(0, c) : line;
      if (p != pathToRemove) kept.push_back(line);
    }
    f.close();
  }
  SD.remove(FAVORITES_FILE);
  File out = SD.open(FAVORITES_FILE, FILE_WRITE);
  if (out) { for (auto &l : kept) out.println(l); out.close(); }

  // Update in-memory caches (no re-open needed)
  favPaths.erase(favPaths.begin() + idx);
  favCache.erase(favCache.begin() + idx);
  favMenuTotal--;
  favScrollVel = 0;

  // Clamp scroll so we don't land past the new end
  int maxRows = (FAV_MENU_Y1 - FAV_MENU_Y0) / FAV_ROW_H;
  float maxPx = (float)max(0, favMenuTotal - maxRows) * FAV_ROW_H;
  if (favScrollPx > maxPx) favScrollPx = maxPx;

  drawFavMenu();
  Serial.printf("Deleted favorite: %s\n", pathToRemove.c_str());
}


void drawKey(int row,int col,bool pressed)
{
  int x = col * KEY_W;
  int y = KB_Y + row * KEY_H;

  if (pressed) y += 2;

  String k = keys[row][col];

  uint16_t bg = KEY_BG;
  if (pressed) bg = KEY_BG_P;
  if (k == "OK") bg = KEY_OK;
  if (k == "<")  bg = KEY_BSP;

  tft.fillRoundRect(x+2, y+2, KEY_W-4, KEY_H-4, 8, tft.color565(0,0,0));
  tft.fillRoundRect(x,   y,   KEY_W-4, KEY_H-4, 8, bg);

  tft.setTextColor(UI_TEXT, bg);

  if (k != " ") {
    int tw = tft.textWidth(k, 2);
    tft.drawString(k, x + (KEY_W - tw)/2 - 2, y + (KEY_H - 14)/2, 2);
  }
}

void drawSearchBar()
{
  tft.fillRoundRect(8, 30, 304, 40, 10, UI_PANEL);
  tft.drawRoundRect(8, 30, 304, 40, 10, UI_ACCENT_SOFT);

  if (searchText.length() == 0) {
    tft.setTextColor(UI_TEXT_DIM, UI_PANEL);
    tft.drawString("Search cards...", 16, 42, 2);
    return;
  }

  tft.setTextColor(UI_TEXT, UI_PANEL);

  String disp = searchText;
  while (disp.length() > 1 && tft.textWidth(disp, 2) > 260)
    disp = disp.substring(1);

  tft.drawString(disp, 16, 42, 2);

  int cx = 16 + tft.textWidth(disp, 2) + 2;

  if (millis() % 1000 < 500)
    tft.drawFastVLine(cx, 40, 20, UI_ACCENT);
}


void drawKeyboard()
{
  tft.fillScreen(UI_BG);

  tft.setTextColor(UI_TEXT);
  tft.drawString("Card Search", 12, 6, 4);

  tft.fillCircle(KB_FAV_CX, KB_FAV_CY, KB_FAV_OR+4, UI_BG);
  drawStar(KB_FAV_CX, KB_FAV_CY, KB_FAV_OR, KB_FAV_IR, tft.color565(255, 200, 0), true);;
  drawSearchBar();

  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 10; c++)
      drawKey(r, c, false);

  pressedRow = -1;
  pressedCol = -1;
}


bool getKeyAtPos(int x,int y,int &row,int &col)
{
  if (y<KB_Y||y>=KB_Y+4*KEY_H) return false;
  row=(y-KB_Y)/KEY_H; col=x/KEY_W;
  if (row<0||row>3||col<0||col>9) return false;
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// TOKEN COUNTER
// ─────────────────────────────────────────────────────────────────────────────
void drawTokenCount()
{
  int unt=tokenCount-tappedCount;
  uint16_t bgC=tft.color565(15,25,15),bordC=tft.color565(0,180,60),
           uC=tft.color565(0,220,80),tC=tft.color565(230,120,0),divC=tft.color565(80,80,80);
  tft.fillRoundRect(TC_X,TC_Y,TC_W,TC_H,6,bgC);
  tft.drawRoundRect(TC_X,TC_Y,TC_W,TC_H,6,bordC);
  int splitX=tcSplitX();
  if (tappedCount>0) tft.drawFastVLine(splitX,TC_Y+6,TC_H-12,divC);
  { String s=String(unt); int avail=(tappedCount>0)?(splitX-TC_X):TC_W;
    tft.setTextColor(uC,bgC); int tw=tft.textWidth(s,6);
    tft.drawString(s,TC_X+(avail-tw)/2,TC_Y+(TC_H-48)/2,6); }
  if (tappedCount>0) {
    String s=String(tappedCount);
    int fw=tft.textWidth(s,6),sprW=fw+4,sprH=52;
    TFT_eSprite spr=TFT_eSprite(&tft);
    spr.createSprite(sprW,sprH); spr.fillSprite(bgC);
    spr.setTextColor(tC,bgC); spr.drawString(s,2,2,6);
    int rW=(TC_X+TC_W)-splitX;
    spr.setPivot(sprW/2,sprH/2); tft.setPivot(splitX+rW/2,TC_Y+TC_H/2);
    spr.pushRotated(90); spr.deleteSprite();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// P/T BOX
// ─────────────────────────────────────────────────────────────────────────────
void drawPTBox()
{
  tft.fillRect(PT_X,PT_Y,PT_W,PT_H,TFT_BLACK);
  tft.drawRect(PT_X,PT_Y,PT_W,PT_H,TFT_WHITE);
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  String s=String(displayPower)+"/"+String(displayTough);
  int tw=tft.textWidth(s,4);
  tft.drawString(s,PT_X+(PT_W-tw)/2,PT_Y+(PT_H-26)/2,4);
}

// ─────────────────────────────────────────────────────────────────────────────
// STAR OVERLAY  ← reads currentCardIsFav only; zero SD I/O
// ─────────────────────────────────────────────────────────────────────────────
void drawStarOverlay()
{
  int outer = STAR_OR;
  int inner = STAR_IR;

  if (showTokenCount) {
    outer = 10;
    inner = 4;
  }

  // ── PRESS ANIMATION ──
  if (starPressed) {
    float t = (millis() - starPressTime) / (float)STAR_PRESS_ANIM_MS;
    if (t > 1.0f) {
      starPressed = false;
    } else {
      outer += (int)(4 * (1.0f - t));
      inner += (int)(2 * (1.0f - t));
    }
  }

  tft.fillCircle(STAR_CX, STAR_CY, outer + 6, TFT_BLACK);

  uint16_t gold = tft.color565(255,200,0);

  // brighter flash while pressed
  if (starPressed) {
    gold = tft.color565(255, 230, 80);
  }

  drawStar(STAR_CX, STAR_CY, outer, inner, gold, currentCardIsFav);

  if (!currentCardIsFav)
    drawStar(STAR_CX, STAR_CY, outer, inner, gold, false);
}

// ─────────────────────────────────────────────────────────────────────────────
// OVERLAY
// ─────────────────────────────────────────────────────────────────────────────
void drawOverlay()
{
  if (showTokenCount) drawTokenCount();

  String label = inFavoritesMode
               ? ("* "+String(matchIndex+1)+"/"+String(matches.size()))
               : (String(matchIndex+1)+" / "+String(matches.size()));
  int lw=tft.textWidth(label,2)+8, lx=320-lw;
  tft.fillRect(lx,0,lw,32,TFT_BLACK);
  tft.setTextColor(inFavoritesMode ? tft.color565(255,200,0) : TFT_YELLOW);
  tft.drawString(label,lx+4,2,2);

  // In fav-browse mode matchVersions holds the card name; show it in full.
  // In search mode show the version tag as before.
  if (matchIndex<(int)matchVersions.size() && matchVersions[matchIndex].length()>0) {
    String tag = inFavoritesMode ? matchVersions[matchIndex]
                                 : ("ver:"+matchVersions[matchIndex]);
    int vw=tft.textWidth(tag,1)+6, vx=320-vw;
    tft.setTextColor(tft.color565(160,160,80));
    tft.drawString(tag,vx+3,18,1);
  }
  tft.setTextColor(TFT_DARKGREY);
  if (matchIndex>0)                       tft.drawString("<",2,230,4);
  if (matchIndex<(int)matches.size()-1)   tft.drawString(">",304,230,4);
  tft.drawString("hold: back",85,463,2);

  drawStarOverlay();   // ← instant, no SD
}

// ─────────────────────────────────────────────────────────────────────────────
// IMAGE / CARD DISPLAY
// ─────────────────────────────────────────────────────────────────────────────
void showImage(String path)
{
  if (!path.startsWith("/")) path="/"+path;
  int rc=png.open(path.c_str(),myOpen,myClose,myRead,mySeek,pngDraw);
  if (rc==PNG_SUCCESS) { tft.fillScreen(TFT_BLACK); png.decode(NULL,0); png.close(); }
  else { tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_RED); tft.drawString("PNG err:"+String(rc),20,200,4); }
}

void showCardAt(int index)
{
  if (index<0) index=0;
  if (index>=(int)matches.size()) index=(int)matches.size()-1;
  matchIndex=index;
  showImage(matches[matchIndex]);
  currentCardIsFav=isFavorite(matches[matchIndex]);   // one SD scan, result cached
  drawOverlay();
  showingCard=true;
}

// ─────────────────────────────────────────────────────────────────────────────
// SEARCH PARSER
// ─────────────────────────────────────────────────────────────────────────────
bool indexLineMatches(const String &line)
{
  int c1=line.indexOf(','),c2=line.indexOf(',',c1+1),
      c3=line.indexOf(',',c2+1),c4=line.indexOf(',',c3+1),c5=line.indexOf(',',c4+1);
  if (c1<0||c2<0||c3<0||c4<0) return false;
  String name=line.substring(c1+1,c2),powerStr=line.substring(c2+1,c3),
         toughStr=line.substring(c3+1,c4),
         colorStr=(c5>0)?line.substring(c4+1,c5):line.substring(c4+1),
         versionStr=(c5>0)?line.substring(c5+1):"";
  String lS=searchName; lS.toLowerCase(); String lN=name; lN.toLowerCase();
  if (lS!="" && !lN.startsWith(lS)) return false;
  if (searchPower!=-1) { if (powerStr=="token"||powerStr.toInt()!=searchPower) return false; }
  if (searchTough!=-1) { if (toughStr=="token"||toughStr.toInt()!=searchTough) return false; }
  if (searchColor!="") { String lC=colorStr; lC.toUpperCase(); String lS2=searchColor; lS2.toUpperCase(); if (lC!=lS2) return false; }
  if (searchVersion!="") { String lV=versionStr; lV.toLowerCase(); String lSr=searchVersion; lSr.toLowerCase(); if (lV.indexOf(lSr)<0) return false; }
  return true;
}

void buildMatchesFromIndex()
{
  matches.clear(); matchVersions.clear(); matchNames.clear();
  File idx=SD.open("/index.csv");
  if (!idx) { tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_RED); tft.drawString("No index.csv!",20,180,4); return; }
  String line="";
  while (idx.available()) {
    char c=idx.read();
    if (c=='\n'||c=='\r') {
      line.trim();
      if (line.length()>0&&indexLineMatches(line)) {
        int c1=line.indexOf(',');
        if (c1>0) {
          matches.push_back(line.substring(0,c1));
          int c2=line.indexOf(',',c1+1),c3=line.indexOf(',',c2+1),
              c4=line.indexOf(',',c3+1),c5=line.indexOf(',',c4+1);
          String cardName=(c2>0)?line.substring(c1+1,c2):""; cardName.trim();
          matchNames.push_back(cardName);
          String ver=(c5>0)?line.substring(c5+1):""; ver.trim();
          matchVersions.push_back(ver);
        }
      }
      line="";
    } else line+=c;
  }
  line.trim();
  if (line.length()>0&&indexLineMatches(line)) {
    int c1=line.indexOf(',');
    if (c1>0) {
      matches.push_back(line.substring(0,c1));
      int c2=line.indexOf(',',c1+1),c3=line.indexOf(',',c2+1),
          c4=line.indexOf(',',c3+1),c5=line.indexOf(',',c4+1);
      String cardName=(c2>0)?line.substring(c1+1,c2):""; cardName.trim();
      matchNames.push_back(cardName);
      String ver=(c5>0)?line.substring(c5+1):""; ver.trim();
      matchVersions.push_back(ver);
    }
  }
  idx.close();
  Serial.printf("Index search: %d matches\n",matches.size());
}

void parseSearch()
{
  searchText.trim();
  searchName=""; searchPower=-1; searchTough=-1; searchColor=""; searchVersion="";
  showTokenCount=false; tokenCount=1; tappedCount=0;
  String working=searchText;
  int fs=working.indexOf(' ');
  if (fs>0) {
    String first=working.substring(0,fs); bool allD=true;
    for (int i=0;i<(int)first.length();i++) if (!isDigit(first[i])){allD=false;break;}
    if (allD){tokenCount=first.toInt();showTokenCount=(tokenCount>0);working=working.substring(fs+1);working.trim();}
  }
  std::vector<String> parts; String tmp=working;
  while (tmp.length()>0){int sp=tmp.indexOf(' ');if(sp<0){parts.push_back(tmp);break;}parts.push_back(tmp.substring(0,sp));tmp=tmp.substring(sp+1);tmp.trim();}
  const String cc="WUBRGC";
  for (int i=1;i<(int)parts.size();i++){String p=parts[i];p.toUpperCase();bool aC=(p.length()>=1);for(int j=0;j<(int)p.length();j++)if(cc.indexOf(p[j])<0){aC=false;break;}if(aC){searchColor=p;parts.erase(parts.begin()+i);break;}}
  for (int i=1;i<(int)parts.size();i++){String p=parts[i];if((p[0]=='v'||p[0]=='V')&&p.length()>=2&&isAlphaNumeric(p[1])){searchVersion=p.substring(1);parts.erase(parts.begin()+i);break;}}
  if((int)parts.size()>=4){searchVersion=parts[3];parts.erase(parts.begin()+3);}
  if(parts.size()>=1) searchName=parts[0];
  if(parts.size()>=2) searchPower=parts[1].toInt();
  if(parts.size()>=3) searchTough=parts[2].toInt();
  displayPower=(searchPower>=0)?searchPower:1;
  displayTough=(searchTough>=0)?searchTough:1;
}

void handleKeyPress(int row,int col)
{
  const String &k=keys[row][col];
  if (k=="<") { if (searchText.length()>0) searchText.remove(searchText.length()-1); }
  else if (k=="OK") { parseSearch(); /* startSearch below */ return; }
  else searchText+=k;
  drawSearchBar();
  if (k=="OK") { /* reached via fall-through? no — explicit call below */ }
}

void startSearch()
{
  inFavoritesMode=false;
  tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_WHITE);
  tft.drawString("Searching...",80,220,4);
  buildMatchesFromIndex();
  if (matches.empty()){drawKeyboard();tft.setTextColor(TFT_RED);tft.drawString("No match found",20,295,4);showingCard=false;return;}
  showCardAt(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup()
{
  Serial.begin(115200); delay(500);
  pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,HIGH);
  pinMode(SD_CS,OUTPUT);  digitalWrite(SD_CS,HIGH);
  SPI.begin(18,19,23,SD_CS);
  if (!SD.begin(SD_CS)){Serial.println("SD INIT FAILED");while(1);}
  tft.init(); tft.setRotation(0);
  Wire.begin(TOUCH_SDA,TOUCH_SCL); touch.begin();
  drawKeyboard();
}

// ─────────────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────────────
int           touchStartX=0,touchStartY=0;
unsigned long touchStartMs=0;
bool          touchActive=false;

void loop()
{
  touch.read();
  if (touch.isTouched)
  {
    int tx=320-touch.points[0].x, ty=480-touch.points[0].y;
    if (!touchActive) {
      touchActive=true; touchStartX=tx; touchStartY=ty; touchStartMs=millis();
      if (!showingCard&&!showFavMenu) {
        int kr,kc;
        if (getKeyAtPos(tx,ty,kr,kc)){
          if(pressedRow>=0) drawKey(pressedRow,pressedCol,false);
          pressedRow=kr;pressedCol=kc;drawKey(kr,kc,true);
        }
      }
    }
  }
  else
  {
    if (touchActive)
    {
      touchActive=false;
      int endX=320-touch.points[0].x,endY=480-touch.points[0].y;
      int dx=endX-touchStartX,dy=endY-touchStartY;
      unsigned long held=millis()-touchStartMs;

      // ══════════════════════════════════════════════════════════════════════
      // CARD VIEW
      // ══════════════════════════════════════════════════════════════════════
      if (showingCard)
      {
        bool onBadge=showTokenCount&&(inTokenBadgeLeft(touchStartX,touchStartY)||inTokenBadgeRight(touchStartX,touchStartY));
        if (onBadge&&held>=350&&tappedCount>0)
        {
          tappedCount=0; drawTokenCount();
        }
        else if (held>=HOLD_MS)
        {
          showingCard=false;
          if (inFavoritesMode) { inFavoritesMode=false; openFavMenu(); }
          else { searchText=""; matches.clear(); matchVersions.clear(); drawKeyboard(); }
        }
        else if (abs(dy)>=SWIPE_THRESHOLD&&abs(dy)>abs(dx))
        {
          if (!showTokenCount&&dy<0){tokenCount=1;tappedCount=0;showTokenCount=true;}
          else if (showTokenCount){if(dy<0)tokenCount++;else if(tokenCount>1){tokenCount--;if(tappedCount>0)tappedCount--;}}
          drawTokenCount();
        }
        else if (abs(dx)>=SWIPE_THRESHOLD&&abs(dx)>abs(dy))
        {
          if(dx<0)showCardAt(matchIndex+1);else showCardAt(matchIndex-1);
        }
        else
        {
          if (inTokenBadgeRight(touchStartX,touchStartY)){if(tappedCount<tokenCount){tappedCount++;drawTokenCount();}}
          else if (inTokenBadgeLeft(touchStartX,touchStartY)){if(tappedCount>0){tappedCount--;drawTokenCount();}}
          // ── Star tap: toggle favorite, redraw star instantly (no PNG reload) ──
          else if (inStarButton(touchStartX,touchStartY))
          {
            // Use the real card name — matchNames[] in search mode,
            // matchVersions[] (already the name) in fav-browse mode.
            String cardName;
            if (inFavoritesMode) {
              cardName = (matchIndex < (int)matchVersions.size() && matchVersions[matchIndex].length()>0)
                       ? matchVersions[matchIndex] : matches[matchIndex];
            } else {
              cardName = (matchIndex < (int)matchNames.size() && matchNames[matchIndex].length()>0)
                       ? matchNames[matchIndex] : matches[matchIndex];
            }

            // instant UI
            currentCardIsFav = !currentCardIsFav;
            starPressed = true;
            starPressTime = millis();
            drawStarOverlay();

            // defer save
            pendingFavSave = true;
            pendingPath = matches[matchIndex];
            pendingName = cardName;
          }
          else if (inPTBox(touchStartX,touchStartY)){displayPower++;displayTough++;drawPTBox();}
          else if (inPTDecBox(touchStartX,touchStartY)){if(displayPower>0)displayPower--;if(displayTough>0)displayTough--;drawPTBox();}
          else{int next=matchIndex+1;if(next>=(int)matches.size())next=0;showCardAt(next);}
        }
      }

      // ══════════════════════════════════════════════════════════════════════
      // FAVORITES MENU
      // ══════════════════════════════════════════════════════════════════════
      else if (showFavMenu)
      {
        int maxRows=(FAV_MENU_Y1-FAV_MENU_Y0)/FAV_ROW_H;
        if (held>=HOLD_MS)
        {
          showFavMenu=false; searchText=""; drawKeyboard();
        }
        else if (abs(dy)>=SWIPE_THRESHOLD&&abs(dy)>abs(dx))
        {
          int maxRows=(FAV_MENU_Y1-FAV_MENU_Y0)/FAV_ROW_H;
          float maxPx=(float)max(0,favMenuTotal-maxRows)*FAV_ROW_H;
          favScrollPx  = constrain(favScrollPx - dy, 0.0f, maxPx);
          favScrollVel = (float)(-dy) / (float)max(1UL,held) * 30.0f;
          drawFavMenu();
        }
        else
        {
          int idx=favMenuRowAt(touchStartX,touchStartY);
          if (idx>=0) {
            if (touchStartX >= 282) deleteFavAt(idx);   // red X zone
            else                    openFavoritesFromMenu(idx);
          }
        }
      }

      // ══════════════════════════════════════════════════════════════════════
      // KEYBOARD
      // ══════════════════════════════════════════════════════════════════════
      else
      {
        if (inKbFavStar(touchStartX,touchStartY))
        {
          pressedRow=-1; pressedCol=-1; openFavMenu();
        }
        else if (pressedRow>=0)
        {
          String k=keys[pressedRow][pressedCol];
          drawKey(pressedRow,pressedCol,false);
          if (k=="<"){if(searchText.length()>0)searchText.remove(searchText.length()-1);drawSearchBar();}
          else if (k=="OK"){parseSearch();startSearch();}
          else{searchText+=k;drawSearchBar();}
          pressedRow=-1; pressedCol=-1;
        }
      }
    }
  }
  // ── Smooth-scroll momentum for favorites menu ──
  if (showFavMenu && fabsf(favScrollVel) > 0.3f) {
    int maxRows = (FAV_MENU_Y1 - FAV_MENU_Y0) / FAV_ROW_H;
    float maxPx = (float)max(0, favMenuTotal - maxRows) * FAV_ROW_H;
    favScrollPx  = constrain(favScrollPx + favScrollVel, 0.0f, maxPx);
    favScrollVel *= 0.82f;  // friction: ~8 frames to stop
    if (favScrollPx <= 0 || favScrollPx >= maxPx) favScrollVel = 0;
    drawFavMenu();
  }

  // ── Deferred favorite save ──
  if (pendingFavSave) {
    pendingFavSave = false;
    toggleFavorite(pendingPath, pendingName);
    }
  delay(20);
}
