#include <Keyboard.h>
#include <EEPROM.h>

// --------------------------------------------------------------
// Uncomment to force reset on next boot (then comment out again)
// --------------------------------------------------------------
// #define FORCE_RESET_ON_BOOT

// Pin layout
const int pins[] = {7, 4, 6, 2, 5, 3};
const int NUM_KEYS = 6;

// ---------- New EEPROM layout ----------
const int MAGIC_ADDR = 0;
const int SENTENCE_ADDR = 1;                               // 6 * 48 = 288 bytes
const int ENTER_FLAG_ADDR = SENTENCE_ADDR + NUM_KEYS * 48; // 6 bytes
const int SOCD_COUNT_ADDR = ENTER_FLAG_ADDR + NUM_KEYS;
const int SOCD_DATA_ADDR = SOCD_COUNT_ADDR + 1;
const byte MAGIC_VALUE = 0xCC; // new magic

// Sentence limits
const int MAX_SENTENCE_LEN = 47; // 47 chars + null terminator
const int MAX_SOCD_PAIRS = 2;

// Default sentences (single characters for backward compatibility)
const char defaultSentences[NUM_KEYS][MAX_SENTENCE_LEN + 1] = {"q", "w", "e", "a", "s", "d"};
bool defaultEnterAfter[NUM_KEYS] = {false, false, false, false, false, false};

// Runtime data
char sentences[NUM_KEYS][MAX_SENTENCE_LEN + 1];
bool enterAfter[NUM_KEYS];

// SOCD storage
int socdPairs[MAX_SOCD_PAIRS][2];
int socdCount = 0;

// Debounce & state
unsigned long lastDebounceTime[NUM_KEYS] = {0};
int lastState[NUM_KEYS] = {HIGH};
int currentState[NUM_KEYS] = {HIGH};
bool wasPressed[NUM_KEYS] = {false}; // for falling edge detection
unsigned long pressTime[NUM_KEYS] = {0};
bool activeState[NUM_KEYS] = {false}; // only used for single‑char keys
const unsigned long debounceDelay = 5;

// ---------- parseKey (moved to the top) ----------
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

// ----- Helper: check if a button has a single‑character sentence -----
bool isSingleChar(int idx)
{
  return (strlen(sentences[idx]) == 1);
}

// ----- Helper: remove a button from all SOCD pairs -----
void removeButtonFromSOCD(int idx)
{
  for (int p = socdCount - 1; p >= 0; p--)
  {
    if (socdPairs[p][0] == idx || socdPairs[p][1] == idx)
    {
      // shift remaining pairs down
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
  // Ensure activeState for this button is cleared
  if (activeState[idx])
  {
    int key = parseKey(String(sentences[idx]));
    Keyboard.release(key);
    activeState[idx] = false;
  }
}

// ----- EEPROM -----
void saveToEEPROM()
{
  for (int i = 0; i < NUM_KEYS; i++)
  {
    int addr = SENTENCE_ADDR + i * MAX_SENTENCE_LEN;
    for (int j = 0; j < MAX_SENTENCE_LEN; j++)
    {
      EEPROM.write(addr + j, sentences[i][j]);
      if (sentences[i][j] == '\0')
        break;
    }
    EEPROM.write(ENTER_FLAG_ADDR + i, enterAfter[i] ? 1 : 0);
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
    strncpy(sentences[i], defaultSentences[i], MAX_SENTENCE_LEN);
    sentences[i][MAX_SENTENCE_LEN] = '\0';
    enterAfter[i] = defaultEnterAfter[i];
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
      int addr = SENTENCE_ADDR + i * MAX_SENTENCE_LEN;
      for (int j = 0; j < MAX_SENTENCE_LEN; j++)
      {
        sentences[i][j] = EEPROM.read(addr + j);
        if (sentences[i][j] == '\0')
          break;
      }
      sentences[i][MAX_SENTENCE_LEN] = '\0';
      enterAfter[i] = EEPROM.read(ENTER_FLAG_ADDR + i) == 1;
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
          isSingleChar(a) && isSingleChar(b))
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
    // Ensure SOCD only contains single‑char buttons
    for (int p = 0; p < socdCount; p++)
    {
      if (!isSingleChar(socdPairs[p][0]) || !isSingleChar(socdPairs[p][1]))
      {
        // remove this pair
        for (int q = p; q < socdCount - 1; q++)
        {
          socdPairs[q][0] = socdPairs[q + 1][0];
          socdPairs[q][1] = socdPairs[q + 1][1];
        }
        socdCount--;
        socdPairs[socdCount][0] = -1;
        socdPairs[socdCount][1] = -1;
        p--; // recheck same index
      }
    }
  }
  else
  {
    // Defaults
    for (int i = 0; i < NUM_KEYS; i++)
    {
      strncpy(sentences[i], defaultSentences[i], MAX_SENTENCE_LEN);
      sentences[i][MAX_SENTENCE_LEN] = '\0';
      enterAfter[i] = defaultEnterAfter[i];
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
    strncpy(sentences[i], defaultSentences[i], MAX_SENTENCE_LEN);
    sentences[i][MAX_SENTENCE_LEN] = '\0';
    enterAfter[i] = defaultEnterAfter[i];
  }
  socdCount = 0;
  for (int p = 0; p < MAX_SOCD_PAIRS; p++)
  {
    socdPairs[p][0] = -1;
    socdPairs[p][1] = -1;
  }
  saveToEEPROM();
}

// ----- Send sentence (typed once) -----
void sendSentence(int idx)
{
  String s = String(sentences[idx]);
  if (s.length() == 0)
    return;
  // Type each character
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s.charAt(i);
    int key = parseKey(String(c)); // parseKey handles single characters
    Keyboard.press(key);
    delay(10);
    Keyboard.release(key);
    delay(5);
  }
  // If Enter after, press Enter
  if (enterAfter[idx])
  {
    Keyboard.press(KEY_RETURN);
    delay(10);
    Keyboard.release(KEY_RETURN);
  }
}

// ----- SOCD update (only for single‑char buttons) -----
void updateKeys()
{
  // Determine which buttons are in any SOCD pair
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

  // For single‑char buttons NOT in SOCD, handle as normal key (press/hold)
  for (int i = 0; i < NUM_KEYS; i++)
  {
    if (!isSingleChar(i) || isInAnyPair[i])
      continue;
    bool shouldBeActive = (currentState[i] == LOW);
    if (shouldBeActive && !activeState[i])
    {
      int key = parseKey(String(sentences[i]));
      Keyboard.press(key);
      activeState[i] = true;
    }
    else if (!shouldBeActive && activeState[i])
    {
      int key = parseKey(String(sentences[i]));
      Keyboard.release(key);
      activeState[i] = false;
    }
  }

  // Handle SOCD pairs (only for single‑char buttons)
  for (int p = 0; p < socdCount; p++)
  {
    int i1 = socdPairs[p][0];
    int i2 = socdPairs[p][1];
    if (i1 == -1 || i2 == -1)
      continue;
    // Ensure they are still single‑char
    if (!isSingleChar(i1) || !isSingleChar(i2))
    {
      // remove this pair (should not happen, but safety)
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
        int key = parseKey(String(sentences[i]));
        Keyboard.press(key);
        activeState[i] = true;
      }
      else if (!shouldBeActive && activeState[i])
      {
        int key = parseKey(String(sentences[i]));
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

  // Read switches with debounce and detect falling edge
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
          pressTime[i] = millis(); // for SOCD
          // If this button has a multi‑character sentence, send it on falling edge
          if (!isSingleChar(i))
          {
            sendSentence(i);
          }
        }
      }
    }
    lastState[i] = reading;
  }

  // Update all keys (for single‑char buttons, including SOCD)
  updateKeys();

  // ----- Serial commands (extended) -----
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
    else if (cmd.startsWith("GETSENT "))
    {
      int idx = cmd.substring(7).toInt() - 1;
      if (idx >= 0 && idx < NUM_KEYS)
      {
        Serial.print("SENT" + String(idx + 1) + ":");
        Serial.print(sentences[idx]);
        Serial.print("|");
        Serial.println(enterAfter[idx] ? "1" : "0");
      }
    }
    else if (cmd == "GETSENTALL")
    {
      for (int i = 0; i < NUM_KEYS; i++)
      {
        Serial.print("SENT" + String(i + 1) + ":");
        Serial.print(sentences[i]);
        Serial.print("|");
        Serial.println(enterAfter[i] ? "1" : "0");
      }
      Serial.println("END");
    }
    else if (cmd.startsWith("SETSENT "))
    {
      // Format: SETSENT <idx>:<sentence>
      int space = cmd.indexOf(' ');
      if (space > 0)
      {
        int idx = cmd.substring(space + 1).toInt() - 1;
        int colon = cmd.indexOf(':', space + 1);
        if (colon > 0 && idx >= 0 && idx < NUM_KEYS)
        {
          String sent = cmd.substring(colon + 1);
          sent.trim();
          if (sent.length() <= MAX_SENTENCE_LEN)
          {
            // If new sentence is multi‑char, remove this button from any SOCD pair
            if (sent.length() > 1)
            {
              removeButtonFromSOCD(idx);
            }
            strncpy(sentences[idx], sent.c_str(), MAX_SENTENCE_LEN);
            sentences[idx][MAX_SENTENCE_LEN] = '\0';
            saveToEEPROM();
            // Clear active state for this button if it was held
            if (activeState[idx])
            {
              int key = parseKey(String(sentences[idx]));
              Keyboard.release(key);
              activeState[idx] = false;
            }
            Serial.println("OK");
          }
          else
          {
            Serial.println("ERROR: sentence too long (max 47 chars)");
          }
        }
        else
        {
          Serial.println("ERROR: invalid index");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SETSENT <idx>:<sentence>");
      }
    }
    else if (cmd.startsWith("SETENTER "))
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
            enterAfter[idx] = (val == 1);
            saveToEEPROM();
            Serial.println("OK");
          }
          else
          {
            Serial.println("ERROR: value must be 0 or 1");
          }
        }
        else
        {
          Serial.println("ERROR: invalid index");
        }
      }
      else
      {
        Serial.println("ERROR: syntax: SETENTER <idx>:<0|1>");
      }
    }
    // Legacy GETKEY (for compatibility, returns first char of sentence)
    else if (cmd.startsWith("GETKEY"))
    {
      int idx = cmd.substring(6).toInt() - 1;
      if (idx >= 0 && idx < NUM_KEYS)
      {
        String firstChar = String(sentences[idx][0]);
        Serial.println("KEY" + String(idx + 1) + ":" + firstChar);
      }
    }
    // Legacy SETKEY (sets sentence to single character, disables Enter)
    else if (cmd.startsWith("SETKEY"))
    {
      int colon = cmd.indexOf(':');
      if (colon > 0)
      {
        int idx = cmd.substring(6, colon).toInt() - 1;
        String val = cmd.substring(colon + 1);
        val.trim();
        if (idx >= 0 && idx < NUM_KEYS && val.length() > 0 && val.length() <= MAX_SENTENCE_LEN)
        {
          strncpy(sentences[idx], val.c_str(), MAX_SENTENCE_LEN);
          sentences[idx][MAX_SENTENCE_LEN] = '\0';
          enterAfter[idx] = false; // disable Enter when using legacy SETKEY
          // If multi‑char, remove from SOCD
          if (val.length() > 1)
            removeButtonFromSOCD(idx);
          saveToEEPROM();
          if (activeState[idx])
          {
            int key = parseKey(String(sentences[idx]));
            Keyboard.release(key);
            activeState[idx] = false;
          }
          Serial.println("OK");
        }
      }
    }
    // Legacy GETALL – returns first character of each sentence (for display in old UI)
    else if (cmd == "GETALL")
    {
      for (int i = 0; i < NUM_KEYS; i++)
      {
        String firstChar = String(sentences[i][0]);
        Serial.println("KEY" + String(i + 1) + ":" + firstChar);
      }
      Serial.println("END");
    }
    // ---------- SOCD commands (unchanged) ----------
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
          // Check both are single‑char
          if (!isSingleChar(idx1) || !isSingleChar(idx2))
          {
            Serial.println("ERROR: both buttons must have single‑character sentences");
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
              // Release active states for these two
              int relIdx[2] = {idx1, idx2};
              for (int r = 0; r < 2; r++)
              {
                int i = relIdx[r];
                if (activeState[i])
                {
                  Keyboard.release(parseKey(String(sentences[i])));
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
            Keyboard.release(parseKey(String(sentences[i])));
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
          Keyboard.release(parseKey(String(sentences[i])));
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
    // Legacy SOCD SET / OFF (treat as single pair)
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
          Keyboard.release(parseKey(String(sentences[i])));
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
            isSingleChar(idx1) && isSingleChar(idx2))
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
          // Release active states for these two
          int relIdx[2] = {idx1, idx2};
          for (int r = 0; r < 2; r++)
          {
            int i = relIdx[r];
            if (activeState[i])
            {
              Keyboard.release(parseKey(String(sentences[i])));
              activeState[i] = false;
            }
          }
          // Also release all other active keys
          for (int i = 0; i < NUM_KEYS; i++)
          {
            if (i != idx1 && i != idx2 && activeState[i])
            {
              Keyboard.release(parseKey(String(sentences[i])));
              activeState[i] = false;
            }
          }
          updateKeys();
          Serial.println("OK SOCD SET");
        }
        else
        {
          Serial.println("ERROR: invalid indices or not single‑char");
        }
      }
      else
      {
        Serial.println("ERROR syntax: SOCD SET <idx1> <idx2>");
      }
    }
  }
}