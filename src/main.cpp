#include <Keyboard.h>
#include <EEPROM.h>

// --------------------------------------------------------------
// Uncomment ONLY if you want to force reset on NEXT boot
// (then comment it out again after upload)
// --------------------------------------------------------------
// #define FORCE_RESET_ON_BOOT

// Pin layout
const int pins[] = {7, 4, 6, 2, 5, 3};
const int NUM_KEYS = 6;

// EEPROM layout
const int MAGIC_ADDR = 0;
const int PRESS_NAMES_ADDR = 1;                                  // 6 * 16 = 96
const int RELEASE_NAMES_ADDR = PRESS_NAMES_ADDR + NUM_KEYS * 16; // 96
const int MODE_ADDR = RELEASE_NAMES_ADDR + NUM_KEYS * 16;        // 6 bytes
const int SOCD_COUNT_ADDR = MODE_ADDR + NUM_KEYS;                // 1 byte
const int SOCD_DATA_ADDR = SOCD_COUNT_ADDR + 1;                  // 2 * MAX_PAIRS
const byte MAGIC_VALUE = 0xDD;                                   // NEW magic – forces reset if old EEPROM data exists

const int MAX_SOCD_PAIRS = 2;

// Defaults: all keys, no release keys, all Key mode
const char defaultPressKeys[NUM_KEYS][16] = {"q", "w", "e", "a", "s", "d"};
const char defaultReleaseKeys[NUM_KEYS][16] = {"", "", "", "", "", ""};
const uint8_t defaultMode[NUM_KEYS] = {0, 0, 0, 0, 0, 0};

// Runtime data
char pressKeys[NUM_KEYS][16];
char releaseKeys[NUM_KEYS][16];
uint8_t mode[NUM_KEYS]; // 0=Key, 1=Dual

// SOCD
int socdPairs[MAX_SOCD_PAIRS][2];
int socdCount = 0;

// Debounce & state
unsigned long lastDebounceTime[NUM_KEYS] = {0};
int lastState[NUM_KEYS] = {HIGH};
int currentState[NUM_KEYS] = {HIGH};
unsigned long pressTime[NUM_KEYS] = {0};
bool activeState[NUM_KEYS] = {false};
const unsigned long debounceDelay = 5;

// ----- parseKey (supports F13-F18) -----
int parseKey(String s)
{
  if (s.length() == 1)
  {
    char c = s.charAt(0);
    if (c >= 32 && c <= 126)
      return c;
  }
  s.toUpperCase();
  if (s == "TAB")
    return KEY_TAB;
  if (s == "CAPS")
    return KEY_CAPS_LOCK;
  if (s == "LSHIFT")
    return KEY_LEFT_SHIFT;
  if (s == "RSHIFT")
    return KEY_RIGHT_SHIFT;
  if (s == "LCTRL")
    return KEY_LEFT_CTRL;
  if (s == "RCTRL")
    return KEY_RIGHT_CTRL;
  if (s == "LALT")
    return KEY_LEFT_ALT;
  if (s == "RALT")
    return KEY_RIGHT_ALT;
  if (s == "LGUI")
    return KEY_LEFT_GUI;
  if (s == "RGUI")
    return KEY_RIGHT_GUI;
  if (s == "ENTER")
    return KEY_RETURN;
  if (s == "ESC")
    return KEY_ESC;
  if (s == "BACK")
    return KEY_BACKSPACE;
  if (s == "DEL")
    return KEY_DELETE;
  if (s == "INS")
    return KEY_INSERT;
  if (s == "HOME")
    return KEY_HOME;
  if (s == "END")
    return KEY_END;
  if (s == "PGUP")
    return KEY_PAGE_UP;
  if (s == "PGDN")
    return KEY_PAGE_DOWN;
  if (s == "UP")
    return KEY_UP_ARROW;
  if (s == "DOWN")
    return KEY_DOWN_ARROW;
  if (s == "LEFT")
    return KEY_LEFT_ARROW;
  if (s == "RIGHT")
    return KEY_RIGHT_ARROW;
  if (s == "SPACE")
    return ' ';
  if (s == "PRTSC")
    return KEY_PRINT_SCREEN;
  if (s == "SCRLK")
    return KEY_SCROLL_LOCK;
  if (s == "PAUSE")
    return KEY_PAUSE;
  if (s == "NUMLK")
    return KEY_NUM_LOCK;
  if (s.startsWith("F") && s.length() <= 3)
  {
    int num = s.substring(1).toInt();
    if (num >= 1 && num <= 12)
      return KEY_F1 + num - 1;
    if (num >= 13 && num <= 18)
      return KEY_F13 + (num - 13);
  }
  return ' ';
}

bool isValidPressKey(int idx) { return (parseKey(String(pressKeys[idx])) != ' '); }
bool isValidReleaseKey(int idx) { return (parseKey(String(releaseKeys[idx])) != ' '); }

void removeButtonFromSOCD(int idx)
{
  for (int p = socdCount - 1; p >= 0; p--)
  {
    if (socdPairs[p][0] == idx || socdPairs[p][1] == idx)
    {
      for (int q = p; q < socdCount - 1; q++)
      {
        socdPairs[q][0] = socdPairs[q + 1][0];
        socdPairs[q][1] = socdPairs[q + 1][1];
      }
      socdCount--;
      socdPairs[socdCount][0] = -1;
      socdPairs[socdCount][1] = -1;
    }
  }
  if (activeState[idx])
  {
    int key = parseKey(String(pressKeys[idx]));
    Keyboard.release(key);
    activeState[idx] = false;
  }
}

// ----- EEPROM -----
void saveToEEPROM()
{
  for (int i = 0; i < NUM_KEYS; i++)
  {
    int addr = PRESS_NAMES_ADDR + i * 16;
    for (int j = 0; j < 16; j++)
    {
      EEPROM.write(addr + j, pressKeys[i][j]);
      if (pressKeys[i][j] == '\0')
        break;
    }
    addr = RELEASE_NAMES_ADDR + i * 16;
    for (int j = 0; j < 16; j++)
    {
      EEPROM.write(addr + j, releaseKeys[i][j]);
      if (releaseKeys[i][j] == '\0')
        break;
    }
    EEPROM.write(MODE_ADDR + i, mode[i]);
  }
  EEPROM.write(SOCD_COUNT_ADDR, socdCount);
  int addr = SOCD_DATA_ADDR;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    EEPROM.write(addr + p * 2, socdPairs[p][0] + 1);
    EEPROM.write(addr + p * 2 + 1, socdPairs[p][1] + 1);
  }
  EEPROM.write(MAGIC_ADDR, MAGIC_VALUE);
}

void loadFromEEPROM()
{
#ifdef FORCE_RESET_ON_BOOT
  for (int i = 0; i < NUM_KEYS; i++)
  {
    strcpy(pressKeys[i], defaultPressKeys[i]);
    strcpy(releaseKeys[i], defaultReleaseKeys[i]);
    mode[i] = defaultMode[i];
  }
  socdCount = 0;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    socdPairs[p][0] = -1;
    socdPairs[p][1] = -1;
  }
  saveToEEPROM();
  return;
#endif
  if (EEPROM.read(MAGIC_ADDR) == MAGIC_VALUE)
  {
    for (int i = 0; i < NUM_KEYS; i++)
    {
      int addr = PRESS_NAMES_ADDR + i * 16;
      for (int j = 0; j < 15; j++)
      {
        pressKeys[i][j] = EEPROM.read(addr + j);
        if (pressKeys[i][j] == '\0')
          break;
      }
      pressKeys[i][15] = '\0';
      addr = RELEASE_NAMES_ADDR + i * 16;
      for (int j = 0; j < 15; j++)
      {
        releaseKeys[i][j] = EEPROM.read(addr + j);
        if (releaseKeys[i][j] == '\0')
          break;
      }
      releaseKeys[i][15] = '\0';
      mode[i] = EEPROM.read(MODE_ADDR + i);
      if (mode[i] > 1)
        mode[i] = 0;
    }
    socdCount = EEPROM.read(SOCD_COUNT_ADDR);
    if (socdCount > MAX_SOCD_PAIRS)
      socdCount = MAX_SOCD_PAIRS;
    int addr = SOCD_DATA_ADDR;
    for (int p = 0; p < MAX_SOCD_PAIRS; p++)
    {
      int a = EEPROM.read(addr + p * 2) - 1;
      int b = EEPROM.read(addr + p * 2 + 1) - 1;
      if (p < socdCount && a >= 0 && a < NUM_KEYS && b >= 0 && b < NUM_KEYS && a != b &&
          isValidPressKey(a) && isValidPressKey(b) && mode[a] == 0 && mode[b] == 0)
      {
        socdPairs[p][0] = a;
        socdPairs[p][1] = b;
      }
      else
      {
        socdPairs[p][0] = -1;
        socdPairs[p][1] = -1;
      }
    }
    // clean invalid SOCD pairs
    for (int p = 0; p < socdCount; p++)
    {
      if (!isValidPressKey(socdPairs[p][0]) || !isValidPressKey(socdPairs[p][1]) ||
          mode[socdPairs[p][0]] != 0 || mode[socdPairs[p][1]] != 0)
      {
        for (int q = p; q < socdCount - 1; q++)
        {
          socdPairs[q][0] = socdPairs[q + 1][0];
          socdPairs[q][1] = socdPairs[q + 1][1];
        }
        socdCount--;
        socdPairs[socdCount][0] = -1;
        socdPairs[socdCount][1] = -1;
        p--;
      }
    }
  }
  else
  {
    // First boot or old EEPROM – write defaults
    for (int i = 0; i < NUM_KEYS; i++)
    {
      strcpy(pressKeys[i], defaultPressKeys[i]);
      strcpy(releaseKeys[i], defaultReleaseKeys[i]);
      mode[i] = defaultMode[i];
    }
    socdCount = 0;
    for (int p = 0; p < MAX_SOCD_PAIRS; p++)
    {
      socdPairs[p][0] = -1;
      socdPairs[p][1] = -1;
    }
    saveToEEPROM();
  }
}

void resetToDefaults()
{
  for (int i = 0; i < NUM_KEYS; i++)
  {
    strcpy(pressKeys[i], defaultPressKeys[i]);
    strcpy(releaseKeys[i], defaultReleaseKeys[i]);
    mode[i] = defaultMode[i];
  }
  socdCount = 0;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    socdPairs[p][0] = -1;
    socdPairs[p][1] = -1;
  }
  saveToEEPROM();
}

// ----- Update Keys (for Key mode and SOCD) -----
void updateKeys()
{
  bool isInAnyPair[NUM_KEYS] = {false};
  for (int p = 0; p < socdCount; p++)
  {
    int a = socdPairs[p][0];
    int b = socdPairs[p][1];
    if (a >= 0 && a < NUM_KEYS)
      isInAnyPair[a] = true;
    if (b >= 0 && b < NUM_KEYS)
      isInAnyPair[b] = true;
  }

  for (int i = 0; i < NUM_KEYS; i++)
  {
    if (mode[i] != 0 || isInAnyPair[i])
      continue;
    bool shouldBeActive = (currentState[i] == LOW);
    if (shouldBeActive && !activeState[i])
    {
      int key = parseKey(String(pressKeys[i]));
      Keyboard.press(key);
      activeState[i] = true;
    }
    else if (!shouldBeActive && activeState[i])
    {
      int key = parseKey(String(pressKeys[i]));
      Keyboard.release(key);
      activeState[i] = false;
    }
  }

  for (int p = 0; p < socdCount; p++)
  {
    int i1 = socdPairs[p][0];
    int i2 = socdPairs[p][1];
    if (i1 == -1 || i2 == -1)
      continue;
    if (mode[i1] != 0 || mode[i2] != 0)
    {
      for (int q = p; q < socdCount - 1; q++)
      {
        socdPairs[q][0] = socdPairs[q + 1][0];
        socdPairs[q][1] = socdPairs[q + 1][1];
      }
      socdCount--;
      socdPairs[socdCount][0] = -1;
      socdPairs[socdCount][1] = -1;
      p--;
      continue;
    }
    bool pressed1 = (currentState[i1] == LOW);
    bool pressed2 = (currentState[i2] == LOW);
    int activeIdx = -1;
    if (pressed1 && pressed2)
    {
      if (pressTime[i1] > pressTime[i2])
        activeIdx = i1;
      else if (pressTime[i2] > pressTime[i1])
        activeIdx = i2;
      else
      {
        if (activeState[i1])
          activeIdx = i1;
        else if (activeState[i2])
          activeIdx = i2;
      }
    }
    else if (pressed1)
      activeIdx = i1;
    else if (pressed2)
      activeIdx = i2;
    int pairIndices[2] = {i1, i2};
    for (int idx = 0; idx < 2; idx++)
    {
      int i = pairIndices[idx];
      bool shouldBeActive = (i == activeIdx);
      if (shouldBeActive && !activeState[i])
      {
        int key = parseKey(String(pressKeys[i]));
        Keyboard.press(key);
        activeState[i] = true;
      }
      else if (!shouldBeActive && activeState[i])
      {
        int key = parseKey(String(pressKeys[i]));
        Keyboard.release(key);
        activeState[i] = false;
      }
    }
  }
}

// ----- Setup -----
void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  for (int i = 0; i < NUM_KEYS; i++)
    pinMode(pins[i], INPUT_PULLUP);
  Keyboard.begin();
  Keyboard.releaseAll();
  Serial.begin(9600);
  loadFromEEPROM(); // This will initialise EEPROM if needed
}

// ----- Loop -----
void loop()
{
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 500)
  {
    lastBlink = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  for (int i = 0; i < NUM_KEYS; i++)
  {
    int reading = digitalRead(pins[i]);
    if (reading != lastState[i])
    {
      lastDebounceTime[i] = millis();
    }
    if ((millis() - lastDebounceTime[i]) > debounceDelay)
    {
      if (reading != currentState[i])
      {
        currentState[i] = reading;
        if (currentState[i] == LOW)
        {
          pressTime[i] = millis();
        }

        // Dual mode handling
        if (mode[i] == 1)
        {
          if (currentState[i] == LOW)
          {
            int pressKey = parseKey(String(pressKeys[i]));
            Keyboard.press(pressKey);
            activeState[i] = true;
          }
          else
          {
            if (activeState[i])
            {
              int pressKey = parseKey(String(pressKeys[i]));
              Keyboard.release(pressKey);
              activeState[i] = false;
            }
            if (isValidReleaseKey(i))
            {
              int relKey = parseKey(String(releaseKeys[i]));
              Keyboard.press(relKey);
              delay(10);
              Keyboard.release(relKey);
            }
          }
        }
      }
    }
    lastState[i] = reading;
  }

  updateKeys();

  // ----- Serial commands -----
  if (Serial.available())
  {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "RESET")
    {
      resetToDefaults();
      Keyboard.releaseAll();
      for (int i = 0; i < NUM_KEYS; i++)
        activeState[i] = false;
      Serial.println("OK RESET");
    }
    else if (cmd == "CLEAR")
    {
      Keyboard.releaseAll();
      for (int i = 0; i < NUM_KEYS; i++)
        activeState[i] = false;
      Serial.println("OK CLEARED");
    }
    else if (cmd == "PING")
    {
      Serial.println("PONG");
    }
    else if (cmd.startsWith("GETPRESS"))
    {
      if (cmd == "GETPRESSALL")
      {
        for (int i = 0; i < NUM_KEYS; i++)
        {
          Serial.println("PRESS" + String(i + 1) + ":" + String(pressKeys[i]));
        }
        Serial.println("END");
      }
      else
      {
        int idx = cmd.substring(8).toInt() - 1;
        if (idx >= 0 && idx < NUM_KEYS)
        {
          Serial.println("PRESS" + String(idx + 1) + ":" + String(pressKeys[idx]));
        }
      }
    }
    else if (cmd.startsWith("GETRELEASE"))
    {
      if (cmd == "GETRELEASEALL")
      {
        for (int i = 0; i < NUM_KEYS; i++)
        {
          Serial.println("RELEASE" + String(i + 1) + ":" + String(releaseKeys[i]));
        }
        Serial.println("END");
      }
      else
      {
        int idx = cmd.substring(10).toInt() - 1;
        if (idx >= 0 && idx < NUM_KEYS)
        {
          Serial.println("RELEASE" + String(idx + 1) + ":" + String(releaseKeys[idx]));
        }
      }
    }
    else if (cmd == "GETMODEALL")
    {
      for (int i = 0; i < NUM_KEYS; i++)
      {
        Serial.println("MODE" + String(i + 1) + ":" + String(mode[i]));
      }
      Serial.println("END");
    }
    else if (cmd.startsWith("SETPRESS "))
    {
      int space = cmd.indexOf(' ');
      if (space > 0)
      {
        int idx = cmd.substring(space + 1).toInt() - 1;
        int colon = cmd.indexOf(':', space + 1);
        if (colon > 0 && idx >= 0 && idx < NUM_KEYS)
        {
          String val = cmd.substring(colon + 1);
          val.trim();
          if (val.length() > 0 && val.length() < 16)
          {
            strcpy(pressKeys[idx], val.c_str());
            pressKeys[idx][15] = '\0';
            saveToEEPROM();
            Serial.println("OK");
          }
          else
          {
            Serial.println("ERROR: invalid key name");
          }
        }
        else
        {
          Serial.println("ERROR: invalid index");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SETPRESS <idx>:<key>");
      }
    }
    else if (cmd.startsWith("SETRELEASE "))
    {
      int space = cmd.indexOf(' ');
      if (space > 0)
      {
        int idx = cmd.substring(space + 1).toInt() - 1;
        int colon = cmd.indexOf(':', space + 1);
        if (colon > 0 && idx >= 0 && idx < NUM_KEYS)
        {
          String val = cmd.substring(colon + 1);
          val.trim();
          if (val.length() > 0 && val.length() < 16)
          {
            strcpy(releaseKeys[idx], val.c_str());
            releaseKeys[idx][15] = '\0';
            saveToEEPROM();
            Serial.println("OK");
          }
          else
          {
            Serial.println("ERROR: invalid key name");
          }
        }
        else
        {
          Serial.println("ERROR: invalid index");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SETRELEASE <idx>:<key>");
      }
    }
    else if (cmd.startsWith("SETMODE "))
    {
      int space = cmd.indexOf(' ');
      if (space > 0)
      {
        int idx = cmd.substring(space + 1).toInt() - 1;
        int colon = cmd.indexOf(':', space + 1);
        if (colon > 0 && idx >= 0 && idx < NUM_KEYS)
        {
          int val = cmd.substring(colon + 1).toInt();
          if (val == 0 || val == 1)
          {
            if (val == 1)
            {
              removeButtonFromSOCD(idx);
            }
            mode[idx] = val;
            if (activeState[idx])
            {
              int key = parseKey(String(pressKeys[idx]));
              Keyboard.release(key);
              activeState[idx] = false;
            }
            saveToEEPROM();
            Serial.println("OK");
          }
          else
          {
            Serial.println("ERROR: mode must be 0 or 1");
          }
        }
        else
        {
          Serial.println("ERROR: invalid index");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SETMODE <idx>:<0|1>");
      }
    }
    // Legacy SETKEY (sets press key and mode to 0)
    else if (cmd.startsWith("SETKEY"))
    {
      int colon = cmd.indexOf(':');
      if (colon > 0)
      {
        int idx = cmd.substring(6, colon).toInt() - 1;
        String val = cmd.substring(colon + 1);
        val.trim();
        if (idx >= 0 && idx < NUM_KEYS && val.length() > 0 && val.length() < 16)
        {
          strcpy(pressKeys[idx], val.c_str());
          pressKeys[idx][15] = '\0';
          if (mode[idx] == 1)
          {
            removeButtonFromSOCD(idx);
          }
          mode[idx] = 0;
          saveToEEPROM();
          if (activeState[idx])
          {
            int key = parseKey(String(pressKeys[idx]));
            Keyboard.release(key);
            activeState[idx] = false;
          }
          Serial.println("OK");
        }
      }
    }
    // Legacy GETALL – returns press keys only
    else if (cmd == "GETALL")
    {
      for (int i = 0; i < NUM_KEYS; i++)
      {
        Serial.println("KEY" + String(i + 1) + ":" + String(pressKeys[i]));
      }
      Serial.println("END");
    }
    // SOCD commands
    else if (cmd.startsWith("SOCD ADD "))
    {
      String rest = cmd.substring(9);
      rest.trim();
      int space = rest.indexOf(' ');
      if (space > 0)
      {
        int idx1 = rest.substring(0, space).toInt() - 1;
        int idx2 = rest.substring(space + 1).toInt() - 1;
        if (idx1 >= 0 && idx1 < NUM_KEYS && idx2 >= 0 && idx2 < NUM_KEYS && idx1 != idx2)
        {
          if (mode[idx1] != 0 || mode[idx2] != 0)
          {
            Serial.println("ERROR: both buttons must be in Key mode for SOCD");
          }
          else if (!isValidPressKey(idx1) || !isValidPressKey(idx2))
          {
            Serial.println("ERROR: both buttons must have valid press keys");
          }
          else
          {
            bool already = false;
            for (int p = 0; p < socdCount; p++)
            {
              if (socdPairs[p][0] == idx1 || socdPairs[p][0] == idx2 ||
                  socdPairs[p][1] == idx1 || socdPairs[p][1] == idx2)
              {
                already = true;
                break;
              }
            }
            if (already)
            {
              Serial.println("ERROR: one of these buttons is already in a SOCD pair");
            }
            else if (socdCount < MAX_SOCD_PAIRS)
            {
              socdPairs[socdCount][0] = idx1;
              socdPairs[socdCount][1] = idx2;
              socdCount++;
              saveToEEPROM();
              int relIdx[2] = {idx1, idx2};
              for (int r = 0; r < 2; r++)
              {
                int i = relIdx[r];
                if (activeState[i])
                {
                  Keyboard.release(parseKey(String(pressKeys[i])));
                  activeState[i] = false;
                }
              }
              updateKeys();
              Serial.println("OK SOCD ADD");
            }
            else
            {
              Serial.println("ERROR: maximum number of SOCD pairs reached");
            }
          }
        }
        else
        {
          Serial.println("ERROR: invalid indices");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SOCD ADD <idx1> <idx2>");
      }
    }
    else if (cmd.startsWith("SOCD REMOVE "))
    {
      int n = cmd.substring(12).toInt() - 1;
      if (n >= 0 && n < socdCount)
      {
        for (int p = n; p < socdCount - 1; p++)
        {
          socdPairs[p][0] = socdPairs[p + 1][0];
          socdPairs[p][1] = socdPairs[p + 1][1];
        }
        socdCount--;
        socdPairs[socdCount][0] = -1;
        socdPairs[socdCount][1] = -1;
        saveToEEPROM();
        for (int i = 0; i < NUM_KEYS; i++)
        {
          if (activeState[i])
          {
            Keyboard.release(parseKey(String(pressKeys[i])));
            activeState[i] = false;
          }
        }
        updateKeys();
        Serial.println("OK SOCD REMOVE");
      }
      else
      {
        Serial.println("ERROR: pair number out of range");
      }
    }
    else if (cmd == "SOCD CLEAR")
    {
      socdCount = 0;
      for (int p = 0; p < MAX_SOCD_PAIRS; p++)
      {
        socdPairs[p][0] = -1;
        socdPairs[p][1] = -1;
      }
      saveToEEPROM();
      for (int i = 0; i < NUM_KEYS; i++)
      {
        if (activeState[i])
        {
          Keyboard.release(parseKey(String(pressKeys[i])));
          activeState[i] = false;
        }
      }
      updateKeys();
      Serial.println("OK SOCD CLEAR");
    }
    else if (cmd == "GETSOCD")
    {
      if (socdCount == 0)
      {
        Serial.println("SOCD:OFF");
      }
      else
      {
        String response = "SOCD:";
        for (int p = 0; p < socdCount; p++)
        {
          if (p > 0)
            response += ";";
          response += String(socdPairs[p][0] + 1) + "," + String(socdPairs[p][1] + 1);
        }
        Serial.println(response);
      }
    }
    // Legacy SOCD SET/OFF
    else if (cmd == "SOCD OFF")
    {
      socdCount = 0;
      for (int p = 0; p < MAX_SOCD_PAIRS; p++)
      {
        socdPairs[p][0] = -1;
        socdPairs[p][1] = -1;
      }
      saveToEEPROM();
      for (int i = 0; i < NUM_KEYS; i++)
      {
        if (activeState[i])
        {
          Keyboard.release(parseKey(String(pressKeys[i])));
          activeState[i] = false;
        }
      }
      updateKeys();
      Serial.println("OK SOCD OFF");
    }
    else if (cmd.startsWith("SOCD SET "))
    {
      String rest = cmd.substring(9);
      rest.trim();
      int space = rest.indexOf(' ');
      if (space > 0)
      {
        int idx1 = rest.substring(0, space).toInt() - 1;
        int idx2 = rest.substring(space + 1).toInt() - 1;
        if (idx1 >= 0 && idx1 < NUM_KEYS && idx2 >= 0 && idx2 < NUM_KEYS && idx1 != idx2 &&
            mode[idx1] == 0 && mode[idx2] == 0 &&
            isValidPressKey(idx1) && isValidPressKey(idx2))
        {
          socdCount = 1;
          socdPairs[0][0] = idx1;
          socdPairs[0][1] = idx2;
          for (int p = 1; p < MAX_SOCD_PAIRS; p++)
          {
            socdPairs[p][0] = -1;
            socdPairs[p][1] = -1;
          }
          saveToEEPROM();
          int relIdx[2] = {idx1, idx2};
          for (int r = 0; r < 2; r++)
          {
            int i = relIdx[r];
            if (activeState[i])
            {
              Keyboard.release(parseKey(String(pressKeys[i])));
              activeState[i] = false;
            }
          }
          for (int i = 0; i < NUM_KEYS; i++)
          {
            if (i != idx1 && i != idx2 && activeState[i])
            {
              Keyboard.release(parseKey(String(pressKeys[i])));
              activeState[i] = false;
            }
          }
          updateKeys();
          Serial.println("OK SOCD SET");
        }
        else
        {
          Serial.println("ERROR: invalid indices or not Key mode / invalid press keys");
        }
      }
      else
      {
        Serial.println("ERROR syntax: SOCD SET <idx1> <idx2>");
      }
    }
  }
}