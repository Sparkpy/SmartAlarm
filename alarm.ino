#include <dht.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>

#define BTN1_PIN 2
#define BTN2_PIN 3
#define RGBR_PIN 8
#define RGBG_PIN 9
#define RGBB_PIN 10
#define BUZZ_PIN 23
#define DHT_PIN 12
#define PHR_PIN A14

#define idealMinTemp 15
#define idealMaxTemp 20
#define idealMinHumid 35
#define idealMaxHumid 60

dht DHT;

LiquidCrystal_I2C lcd(0x27, 16, 4);

ThreeWire myWire(5, 4, 6);  // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

long lastDebounce;
long lastRTCCheck;
long lastDHTCheck;

int lightValue;
int lastLightValue = 0;
long lastLightTime;

const short WINDOW_SIZE = 10; // Adjust the window size as needed
int readings[WINDOW_SIZE]; // Array to store the recent readings
int currentIndex = 0; // Index to keep track of the current position in the array
int totalLight = 0; // Variable to store the sum of the recent readings

int lastMinute = 0;
volatile bool updateScreen = false;
volatile bool inMenu = false;
volatile bool enableBacklight = true;

int dhtResult;
int temperature = 0;
int humidity = 0;
int sleepScore = 2;

// -- Settings-- 
// Menu States
enum MenuState {
  HOME,
  SENSOR_READOUTS,
  SET_ALARM,
  NUM_MENU_STATES
};

enum SensorState {
  SENSOR_MAIN,
  SLEEP_DATA,
};

// Buzzer State
volatile bool interruptBuzz = false;
volatile bool ringAlarm = false;

// Alarm Settings
volatile bool sleep_mode = false; // Whether or not the backlight will turn off after 30 seconds
short alarm_mode = 0; // 0 = Auto, 1 = Manual, 2 = Off
short sleep_hours = 8;
short alarm_auto_time[] = {6, 0}; // Hours, Minutes
short alarm_manual_time[] = {6, 0};
enum AlarmState {
  MODE,
  MANUAL_HOURS,
  MANUAL_MINUTES,
  MIN_SLEEP_HOURS,
  NUM_ALARM_STATES
};

volatile MenuState currentState = HOME;
volatile AlarmState alarmState = MODE;
volatile SensorState sensorState = SENSOR_MAIN;

RtcDateTime lastAlarm;
RtcDateTime now = Rtc.GetDateTime();

byte alarmChar[] = {
  0x00,
  0x0E,
  0x15,
  0x15,
  0x13,
  0x0E,
  0x00,
  0x00
};

byte humidChar[] = {
  0x00,
  0x04,
  0x0E,
  0x1F,
  0x1F,
  0x1F,
  0x0E,
  0x00
};

byte tempChar[] = {
  0x04,
  0x0A,
  0x0A,
  0x0A,
  0x0A,
  0x11,
  0x1F,
  0x0E
};

byte lightChar[] = {
  0x00,
  0x15,
  0x0E,
  0x1F,
  0x0E,
  0x15,
  0x00,
  0x00
};

byte moonChar[] = {
  0x00,
  0x0E,
  0x1C,
  0x18,
  0x18,
  0x1D,
  0x0E,
  0x00
};


void setup() {
  Serial.begin(9600);
  pinMode(BTN1_PIN, INPUT_PULLUP);  // Копче 1
  attachInterrupt(digitalPinToInterrupt(BTN1_PIN), interactPressed, RISING); // Прекин 1
  pinMode(BTN2_PIN, INPUT_PULLUP);  // Копче 2
  attachInterrupt(digitalPinToInterrupt(BTN2_PIN), nextPressed, RISING); // Прекин 2
  pinMode(RGBR_PIN, OUTPUT);  // Црвена Диода
  pinMode(RGBG_PIN, OUTPUT);  // Зелена Диода
  pinMode(RGBB_PIN, OUTPUT);  // Сина Диода
  pinMode(DHT_PIN, INPUT); // DHT сензор
  pinMode(PHR_PIN, INPUT); // Фотоотпорник
  pinMode(BUZZ_PIN, OUTPUT); // Звучник

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  if (now < compiled) {
    Rtc.SetDateTime(compiled);
  }

  initializeReadings();

  lcd.init(); // Initialize LCD
  lcd.createChar(0, alarmChar); // Create custom characters
  lcd.createChar(1, tempChar);
  lcd.createChar(2, humidChar);
  lcd.createChar(3, lightChar);
  lcd.createChar(4, moonChar);
  lcd.backlight();   // Turn on backlight
  lcd.setCursor(0, 0);
  lcd.print("Smart Alarm");
  lcd.setCursor(0, 1);
  lcd.print("Daniel B. OEMUC");
  delay(2000);  // Display the startup message for 2 seconds
  lcd.clear();
  updateScreen = true;

  dhtResult = DHT.read11(DHT_PIN);
  lastDHTCheck = millis();

  temperature = int(DHT.temperature);
  humidity = int(DHT.humidity);
}

void loop() {

  serialTimeCheck(); // Check if user is trying to set the time over Serial connection

  lightValue = smoothPhotoresistor(); // Read Light Value

  lastLightTime = (lightValue > 15) ? millis() : lastLightTime;

  if (sleep_mode && ((millis() - lastDebounce) > 30000)) {
    lcd.noBacklight();
  }
  else if (!sleep_mode && ((millis() - lastLightTime) > 60000)) {
    lcd.noBacklight();
  }
  else {
    lcd.backlight();
    // enableBacklight = false;
  }
  // Ако сензорот правилно отчитал, зачувај ги податоците 
  if ((millis() - lastDHTCheck) > 5000) { 
    lastDHTCheck = millis();
    dhtResult = DHT.read11(DHT_PIN);
    if (dhtResult == DHTLIB_OK) {
      temperature = int(DHT.temperature);
      humidity = int(DHT.humidity);
    }
  }


  if ((millis() - lastRTCCheck) > 1000) { // RTC Check
    lastRTCCheck = millis();
    now = Rtc.GetDateTime();
  }

  switch (currentState) {
    case HOME:
      displayHome();
      break;
    case SENSOR_READOUTS:
      switch(sensorState) {
        case SENSOR_MAIN:
          displaySensorReadouts();
          break;
        case SLEEP_DATA:
          displaySleepData();
          break;
      }
      break;
    case SET_ALARM:
      displaySetAlarm();
      break;
    default:
      break;
  }
  // Serial.println("currenntState: " + String((currentState == 1) ? "SENSOR_READOUTS, " : "OTHER, ") + "InMenu: " + String((inMenu) ? "True, " :  "False, ") + "sensorState: " + String((sensorState == 1) ? "SLEEP_DATA" : "SLEEP_MAIN"));
  if (sensorState == SLEEP_DATA) {
    switch (sleepScore) {
      case 0:
        digitalWrite(RGBR_PIN, HIGH);
        digitalWrite(RGBG_PIN, LOW);
        digitalWrite(RGBB_PIN, LOW);
        break;
      case 1:
        digitalWrite(RGBR_PIN, HIGH);
        digitalWrite(RGBG_PIN, HIGH);
        digitalWrite(RGBB_PIN, LOW);
        break;
      case 2:
        digitalWrite(RGBR_PIN, LOW);
        digitalWrite(RGBG_PIN, HIGH);
        digitalWrite(RGBB_PIN, LOW);
        break;
      default:
        digitalWrite(RGBR_PIN, LOW);
        digitalWrite(RGBG_PIN, LOW);
        digitalWrite(RGBB_PIN, LOW);
        break;
    }
  } else {
    digitalWrite(RGBR_PIN, LOW);
    digitalWrite(RGBG_PIN, LOW);
    digitalWrite(RGBB_PIN, LOW);
  }
  
  if (alarm_mode == 0) {
    if ((timesMatch(now, alarm_auto_time) && (now.Hour() != lastAlarm.Hour()) && (now.Minute() != lastAlarm.Minute()) && (now.Day() != lastAlarm.Day()))) {
      ringAlarm = true;
    }
  }
  else if (alarm_mode == 1) {
    if ((timesMatch(now, alarm_manual_time) && (now.Hour() != lastAlarm.Hour()) && (now.Minute() != lastAlarm.Minute()) && (now.Day() != lastAlarm.Day()))) {
      ringAlarm = true;
    }
  }

  if (ringAlarm) {
    buzz(100, 1000, 10);
    buzz(100, 500, 30);
    buzz(100, 250, 10000);
    ringAlarm = false;
    interruptBuzz = false;
    lastAlarm = now;
  }

  // All sensor data should be read here, not in each function separately, functions should only be displaying text and navigating through menus
}

// FUNCTIONS //

String createTimeString(int h, int m){
  return (h < 10 ? "0" : "") + String(h) + ":" + (m < 10 ? "0" : "") + String(m);
}

void displayHome() {
  if (updateScreen) {
    lcd.clear();
    lcd.print(createTimeString(now.Hour(), now.Minute())); // Print Time
    lcd.setCursor(10, 0);
    if (alarm_mode < 2) { lcd.print(char(0)); } // If there is an active alarm, show custom symbol
    if (alarm_mode == 0) {
      lcd.print(createTimeString(alarm_auto_time[0], alarm_auto_time[1]));
    } else if (alarm_mode == 1) {
      lcd.print(createTimeString(alarm_manual_time[0], alarm_manual_time[1]));
    } else {
      lcd.setCursor(11,0);
      lcd.print("-Off-");
    }
    lcd.setCursor(0, 1);
    lcd.print(String(temperature) + (char) 223 + "C");  // Print Temperature
    lcd.setCursor(11, 1);
    lcd.print("Alarm");
    if (sleep_mode) {
      lcd.setCursor(7, 0);
      lcd.print(char(4));
    }
    updateScreen = false;
  }
  if (((millis() - lastDHTCheck) > 5000) || (lastMinute != now.Minute())) { 
    updateScreen = true;
    lastMinute = now.Minute();
  }
}

void displaySensorReadouts() {
  if (updateScreen) {
    lcd.clear();
    lcd.print((char) 1 + String(temperature) + (char) 223 + "C "); // Print Temperature
    lcd.print((char) 3 + String(lightValue)); // Print Light
    lcd.setCursor(0, 1);
    lcd.print((char) 2 + String(humidity) + "%"); // Print Relative Humidity
    lastLightValue = lightValue;
    updateScreen = false;
  }
  if (((millis() - lastDHTCheck) > 5000) || (lightValue > lastLightValue+10) || (lightValue < lastLightValue-10)) {
    updateScreen = true;
  }
}

void displaySetAlarm() {
  if ((updateScreen) && (!inMenu)) {
    lcd.clear();
    lcd.print("Alarm:");
    if (alarm_mode == 0) {
      lcd.setCursor(12, 0);
      lcd.print("Auto");
    } else if (alarm_mode == 1) {
      lcd.setCursor(10, 0);
      lcd.print("Manual");
      lcd.setCursor(0, 1);
      lcd.print("Time:");
      lcd.setCursor(11, 1);
      lcd.print(createTimeString(alarm_manual_time[0], alarm_manual_time[1]));
    } else {
      lcd.setCursor(13, 0);
      lcd.print("Off");
    }
    updateScreen = false;
  }
  if ((updateScreen) && (inMenu)) {
    lcd.clear();
    switch(alarmState) {
      case MODE:
        lcd.print("Alarm:");
        if (alarm_mode == 0) {
          lcd.setCursor(12, 0);
          lcd.print("Auto");
          lcd.setCursor(12, 1);
          lcd.print("^");
        } else if (alarm_mode == 1) {
          lcd.setCursor(10, 0);
          lcd.print("Manual");
          lcd.setCursor(10, 1);
          lcd.print("^");
        } else {
          lcd.setCursor(13, 0);
          lcd.print("Off");
          lcd.setCursor(13, 1);
          lcd.print("^");
        }
        updateScreen = false;
        break;
      case MANUAL_HOURS:
        lcd.print("M.Alarm: ");
        lcd.setCursor(11, 0);
        lcd.print(createTimeString(alarm_manual_time[0], alarm_manual_time[1]));
        lcd.setCursor(0, 1);
        lcd.print("Hours");
        lcd.setCursor(11, 1);
        lcd.print("^");
        updateScreen = false;
        break;
      case MANUAL_MINUTES:
        lcd.print("M.Alarm: ");
        lcd.setCursor(11, 0);
        lcd.print(createTimeString(alarm_manual_time[0], alarm_manual_time[1]));
        lcd.setCursor(0, 1);
        lcd.print("Minutes");
        lcd.setCursor(14, 1);
        lcd.print("^");
        updateScreen = false;
        break;
      case MIN_SLEEP_HOURS:
        lcd.print("Min Sleep");
        lcd.setCursor(14, 0);
        lcd.print(String(sleep_hours) + "h");
        lcd.setCursor(0, 1);
        lcd.print("Time (h)");
        lcd.setCursor(14, 1);
        lcd.print("^");
        updateScreen = false;
        break;
      default:
          // Handle invalid menu option
          break;
    }
  }
  if ((millis() - lastRTCCheck) > 1000) {
    updateScreen = true;
  }
}

void displaySleepData() {
  if (updateScreen) {
    lcd.clear();
    sleepScore = 2;
    if (temperature < idealMinTemp) {
      lcd.print("Temps: LOW");
      sleepScore -= 1;
    } else if (temperature > idealMaxTemp) {
      lcd.print("Temps: HIGH");
      sleepScore -= 1;
    } else {
      lcd.print("Temps: GOOD");
    }

    lcd.setCursor(0, 1);
    if (humidity < idealMinHumid) {
      lcd.print("Humid: LOW");
      sleepScore -= 1;
    } else if (humidity > idealMaxHumid) {
      lcd.print("Humid: HIGH");
      sleepScore -= 1;
    } else {
      lcd.print("Humid: GOOD");
    }

    updateScreen = false;
  }
  if ((millis() - lastDHTCheck) > 5000) {
    updateScreen = true;
  }
}

void interactPressed() {
  if ((millis() - lastDebounce) > 200) {
    if (ringAlarm) { interruptBuzz = true; }
    handleSelectAction();
    lastDebounce = millis();
    // enableBacklight = true;
    lastLightTime = millis();
    updateScreen = true;
  }
}

void nextPressed() {
  if ((millis() - lastDebounce) > 200) {
    if (!inMenu) {
      currentState = static_cast<MenuState>((currentState + 1) % NUM_MENU_STATES);
    }
    if (inMenu && (currentState == SET_ALARM)) {
      alarmState = static_cast<AlarmState>((alarmState + 1) % NUM_ALARM_STATES);
      if (alarmState == MODE) {
        inMenu = false;
      }
    }
    if (currentState == SENSOR_READOUTS && sensorState == SLEEP_DATA) {
      sensorState = SENSOR_MAIN;
      inMenu = false;
    }

    lastDebounce = millis();
    // enableBacklight = true;
    lastLightTime = millis();
    updateScreen = true;
  }
}

void handleSelectAction() {
  updateScreen = true;
  switch (currentState) {
    case HOME:
      if (alarm_mode == 0) {
        calculateAlarmTime();
      }
      if (!interruptBuzz) { sleep_mode = !sleep_mode;}
      break;
    case SENSOR_READOUTS:
      inMenu = !inMenu;
      sensorState = (sensorState == 1) ? SENSOR_MAIN : SLEEP_DATA;
      break;
    case SET_ALARM:
      switch(alarmState) {
        case MODE:
          if (!inMenu) {
            inMenu = true;
          } else {
            if (alarm_mode < 2) {
            alarm_mode += 1;
          } else { alarm_mode = 0; }
          }
          break;
        case MANUAL_HOURS:
          if (alarm_manual_time[0] < 23) {
            alarm_manual_time[0] += 1;
          } else { alarm_manual_time[0] = 0; }
          break;
        case MANUAL_MINUTES:
          if (alarm_manual_time[1] < 55) {
            alarm_manual_time[1] += 5;
          } else { alarm_manual_time[1] = 0; }
          break;
        case MIN_SLEEP_HOURS:
          if (sleep_hours < 9) {
            sleep_hours += 1;
          } else { sleep_hours = 1; }
          break;
        default:
          // Handle invalid menu option
          break;
      }
      break;
    default:
      break;
  }
}

void calculateAlarmTime() {
  // Просечниот човек заспива во 15 минути
  int totalMinutes = 15;
  
  while (totalMinutes < sleep_hours * 60) {
    totalMinutes += 90;
  }

  int sleepMinutes = totalMinutes % 60;
  int sleepHours = totalMinutes / 60;
  
  int alarmMinute = sleepMinutes + now.Minute();
  int alarmHour = sleepHours + now.Hour() + alarmMinute / 60;

  // Одржување на рамките на времето
  alarmMinute %= 60;
  alarmHour %= 24;

  alarm_auto_time[0] = alarmHour;
  alarm_auto_time[1] = alarmMinute;
}

bool timesMatch(RtcDateTime currentTime, short alarmTime[]) {
  return ((currentTime.Hour() == alarmTime[0]) && (currentTime.Minute() == alarmTime[1]));
}

void buzz(int duration, int beepDelay, int times) {
  for (int i = 0; i < times; i++) {
    if (interruptBuzz) { break; }
    digitalWrite(BUZZ_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZ_PIN, LOW);
    delay(beepDelay);
  }
}

void initializeReadings() {
  for (int i = 0; i < WINDOW_SIZE; i++) {
    readings[i] = analogRead(PHR_PIN);
    totalLight += readings[i];
  }
}

int updateRollingAverage(int newValue) {
  totalLight -= readings[currentIndex];
  readings[currentIndex] = newValue;
  totalLight += newValue;
  currentIndex = (currentIndex + 1) % WINDOW_SIZE;
  return totalLight / WINDOW_SIZE;
}

// Function to get the smoothed photoresistor value
int smoothPhotoresistor() {
  int newValue = analogRead(PHR_PIN);
  return updateRollingAverage(newValue);
}

void serialTimeCheck() {
  if (Serial.available() > 0) {
    unsigned long unixTime = Serial.parseInt();
    if (unixTime != 0) {
      RtcDateTime rtcTime = unixTimeToRtcDateTime(unixTime + 7200);
      Rtc.SetDateTime(rtcTime);
    }
  }
}

RtcDateTime unixTimeToRtcDateTime(unsigned long unixTime) {
  // Unix unixTime starts from January 1, 1970 (the epoch)
  const unsigned long secondsPerMinute = 60;
  const unsigned long secondsPerHour = 3600;
  const unsigned long secondsPerDay = 86400;
  const unsigned long daysPerYear = 365;

  int years, months, days, hours, minutes, seconds;
  
  years = unixTime / (secondsPerDay * daysPerYear);
  unsigned long remainingSeconds = unixTime % (secondsPerDay * daysPerYear);

  int daysInMonth;
  int year = 1970 + years;
  bool isLeapYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));

  // Adjust for leap year
  if (isLeapYear && remainingSeconds >= (secondsPerDay * 366)) {
    remainingSeconds -= (secondsPerDay * 366);
    ++years;
    year++;
  }

  // Calculate months and days
  for (months = 0; months < 12; ++months) {
    daysInMonth = 31; // Jan, Mar, May, Jul, Aug, Oct, Dec have 31 days
    if (months == 3 || months == 5 || months == 8 || months == 10) {
      daysInMonth = 30;
    } else if (months == 1) { // February
      daysInMonth = (isLeapYear) ? 29 : 28;
    }

    if (remainingSeconds < (daysInMonth * secondsPerDay)) {
      days = remainingSeconds / secondsPerDay;
      remainingSeconds %= secondsPerDay;
      break;
    }
    remainingSeconds -= (daysInMonth * secondsPerDay);
  }

  // Calculate hours, minutes, and seconds
  hours = remainingSeconds / secondsPerHour;
  remainingSeconds %= secondsPerHour;
  minutes = remainingSeconds / secondsPerMinute;
  seconds = remainingSeconds % secondsPerMinute;

  // Create and return the RtcDateTime object
  return RtcDateTime(years, months, days, hours, minutes, seconds);
}
