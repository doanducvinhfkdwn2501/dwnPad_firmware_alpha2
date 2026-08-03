#include <Keyboard.h>
#include <EEPROM.h>

// --------------------------------------------------------------
// Uncomment to force reset to default keys & clear SOCD on next boot
// --------------------------------------------------------------
// #define FORCE_RESET_ON_BOOT // comment this when u dont want to force reset anymore

// Pin layout
const int pins[] = {7, 4, 6, 2, 5, 3};
const int NUM_KEYS = 6;

// EEPROM layout
const int MAGIC_ADDR = 0;
const int KEY_NAMES_ADDR = 1;
const int SOCD_COUNT_ADDR = KEY_NAMES_ADDR + NUM_KEYS * 16; // 1 byte: number of pairs
const int SOCD_DATA_ADDR = SOCD_COUNT_ADDR + 1;             // 2 bytes per pair
const byte MAGIC_VALUE = 0xBB;

// Max pairs = 2
const int MAX_SOCD_PAIRS = 2;

// Default keys
const char defaultKeys[NUM_KEYS][16] = {"q", "w", "e", "a", "s", "d"};
char keyNames[NUM_KEYS][16];

// SOCD storage
int socdPairs[MAX_SOCD_PAIRS][2]; // each pair: [idx1, idx2]
int socdCount = 0;

// Debounce
unsigned long lastDebounceTime[NUM_KEYS] = {0};
int lastState[NUM_KEYS] = {HIGH};
int currentState[NUM_KEYS] = {HIGH};
unsigned long pressTime[NUM_KEYS] = {0};
bool activeState[NUM_KEYS] = {false};
const unsigned long debounceDelay = 5;

// ----- parseKey (unchanged) -----
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
  }
  return ' ';
}

// ----- EEPROM -----
void saveToEEPROM()
{
  for (int i = 0; i < NUM_KEYS; i++)
  {
    int addr = KEY_NAMES_ADDR + i * 16;
    for (int j = 0; j < 16; j++)
    {
      EEPROM.write(addr + j, keyNames[i][j]);
      if (keyNames[i][j] == '\0')
        break;
    }
  }
  EEPROM.write(SOCD_COUNT_ADDR, socdCount);
  int addr = SOCD_DATA_ADDR;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    EEPROM.write(addr + p * 2, socdPairs[p][0] + 1); // 1‑based, 0 = none
    EEPROM.write(addr + p * 2 + 1, socdPairs[p][1] + 1);
  }
  EEPROM.write(MAGIC_ADDR, MAGIC_VALUE);
}

void loadFromEEPROM()
{
#ifdef FORCE_RESET_ON_BOOT
  for (int i = 0; i < NUM_KEYS; i++)
    strcpy(keyNames[i], defaultKeys[i]);
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
      int addr = KEY_NAMES_ADDR + i * 16;
      for (int j = 0; j < 15; j++)
      {
        keyNames[i][j] = EEPROM.read(addr + j);
        if (keyNames[i][j] == '\0')
          break;
      }
      keyNames[i][15] = '\0';
    }
    socdCount = EEPROM.read(SOCD_COUNT_ADDR);
    if (socdCount > MAX_SOCD_PAIRS)
      socdCount = MAX_SOCD_PAIRS;
    int addr = SOCD_DATA_ADDR;
    for (int p = 0; p < MAX_SOCD_PAIRS; p++)
    {
      int a = EEPROM.read(addr + p * 2) - 1;
      int b = EEPROM.read(addr + p * 2 + 1) - 1;
      if (p < socdCount && a >= 0 && a < NUM_KEYS && b >= 0 && b < NUM_KEYS && a != b)
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
  }
  else
  {
    for (int i = 0; i < NUM_KEYS; i++)
      strcpy(keyNames[i], defaultKeys[i]);
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
    strcpy(keyNames[i], defaultKeys[i]);
  socdCount = 0;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    socdPairs[p][0] = -1;
    socdPairs[p][1] = -1;
  }
  saveToEEPROM();
}

// ----- SOCD update -----
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

  // Non‑paired keys: follow physical state
  for (int i = 0; i < NUM_KEYS; i++)
  {
    if (isInAnyPair[i])
      continue;
    bool shouldBeActive = (currentState[i] == LOW);
    if (shouldBeActive && !activeState[i])
    {
      Keyboard.press(parseKey(String(keyNames[i])));
      activeState[i] = true;
    }
    else if (!shouldBeActive && activeState[i])
    {
      Keyboard.release(parseKey(String(keyNames[i])));
      activeState[i] = false;
    }
  }

  // Each SOCD pair independently
  for (int p = 0; p < socdCount; p++)
  {
    int i1 = socdPairs[p][0];
    int i2 = socdPairs[p][1];
    if (i1 == -1 || i2 == -1)
      continue;

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
    {
      activeIdx = i1;
    }
    else if (pressed2)
    {
      activeIdx = i2;
    }

    int pairIndices[2] = {i1, i2};
    for (int idx = 0; idx < 2; idx++)
    {
      int i = pairIndices[idx];
      bool shouldBeActive = (i == activeIdx);
      if (shouldBeActive && !activeState[i])
      {
        Keyboard.press(parseKey(String(keyNames[i])));
        activeState[i] = true;
      }
      else if (!shouldBeActive && activeState[i])
      {
        Keyboard.release(parseKey(String(keyNames[i])));
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
  loadFromEEPROM();
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
      }
    }
    lastState[i] = reading;
  }

  updateKeys();

  // Serial commands
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
    else if (cmd.startsWith("GETKEY"))
    {
      int idx = cmd.substring(6).toInt() - 1;
      if (idx >= 0 && idx < NUM_KEYS)
      {
        Serial.println("KEY" + String(idx + 1) + ":" + String(keyNames[idx]));
      }
    }
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
          strncpy(keyNames[idx], val.c_str(), 15);
          keyNames[idx][15] = '\0';
          saveToEEPROM();
          Keyboard.releaseAll();
          for (int i = 0; i < NUM_KEYS; i++)
            activeState[i] = false;
          updateKeys();
          Serial.println("OK");
        }
      }
    }
    else if (cmd == "GETALL")
    {
      for (int i = 0; i < NUM_KEYS; i++)
      {
        Serial.println("KEY" + String(i + 1) + ":" + String(keyNames[i]));
      }
      Serial.println("END");
    }
    // ---------- SOCD commands ----------
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
            // Release any active states for these two (fix: use array)
            int relIdx[2] = {idx1, idx2};
            for (int r = 0; r < 2; r++)
            {
              int i = relIdx[r];
              if (activeState[i])
              {
                Keyboard.release(parseKey(String(keyNames[i])));
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
            Keyboard.release(parseKey(String(keyNames[i])));
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
          Keyboard.release(parseKey(String(keyNames[i])));
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
    // Legacy: SOCD SET / OFF (treat as single pair)
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
          Keyboard.release(parseKey(String(keyNames[i])));
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
        if (idx1 >= 0 && idx1 < NUM_KEYS && idx2 >= 0 && idx2 < NUM_KEYS && idx1 != idx2)
        {
          // Replace all with this single pair
          socdCount = 1;
          socdPairs[0][0] = idx1;
          socdPairs[0][1] = idx2;
          for (int p = 1; p < MAX_SOCD_PAIRS; p++)
          {
            socdPairs[p][0] = -1;
            socdPairs[p][1] = -1;
          }
          saveToEEPROM();
          // Release any active states for these two (fix: use array)
          int relIdx[2] = {idx1, idx2};
          for (int r = 0; r < 2; r++)
          {
            int i = relIdx[r];
            if (activeState[i])
            {
              Keyboard.release(parseKey(String(keyNames[i])));
              activeState[i] = false;
            }
          }
          // Also release all other active keys
          for (int i = 0; i < NUM_KEYS; i++)
          {
            if (i != idx1 && i != idx2 && activeState[i])
            {
              Keyboard.release(parseKey(String(keyNames[i])));
              activeState[i] = false;
            }
          }
          updateKeys();
          Serial.println("OK SOCD SET");
        }
        else
        {
          Serial.println("ERROR invalid indices");
        }
      }
      else
      {
        Serial.println("ERROR syntax: SOCD SET <idx1> <idx2>");
      }
    }
  }
}