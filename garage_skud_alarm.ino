#include "GyverButton.h"
#include "TimerMs.h"
#include "OneWire.h"
#include "EEPROM.h"

#define PIN_HORN 12          // пин для подключения реле сигнала(в случае тревоги включено постоянно)
#define PIN_STROB 11         // пин для подключения реле стробоскопа(в случае тревоги мигает)
#define PIN_BTN_GUARD 10     // пин кнопки постановки снятия/охраны
#define PIN_BTN_DOOR 9       // пин концевика двери(NO)
#define PIN_MOTION_SENSOR 7  // пин датчика движения
#define PIN_LED_GUARD 8      // пин светодиода охраны
#define PIN_JAMPER_PROG 3    // пин джампера режима программирования
#define PIN_BEEPER 2         // пин пищалки
#define PIN_BTN_CLEAR 4      // пин кнопки сброса

#define DELAY_GUARD 10*1000         // Задержка установки на охрану (мс)
#define DELAY_SHORT 200             // Короткая задержка
#define DELAY_LONG 1000             // Длинная задержка
#define DELAY_ALARM 3*1000          // Задержка сработки тревоги
#define DELAY_BTN_CLEAR_HOLD 3*1000 // Время для сработки зажима кнопки сброса
#define BASE_ALARM_TIME_MS 10*1000  // Базовое работы тревоги

#define MAX_KEYS_COUNT 10      //Максимальное количестко ключей iButton в EEPROM
#define KEY_LENGTH 8           //Длина ключа

#define countTimerAlarmRestartes 6*10 //т.к не получается запустить таймер на длительное время, 
                                      //принято решение работать по схеме таймер на 10сек(BASE_ALARM_TIME_MS) перезапускается указанное количество раз
                                      //таким обзаром можно подобрать время работы. 6 раз = 1мин

int currentCountAlarmTimer = 0;

GButton motionSens(PIN_MOTION_SENSOR, LOW_PULL);          //Датчик движения
GButton doorSens(PIN_BTN_DOOR);                           //Концевик двери
GButton jamperProg(PIN_JAMPER_PROG);                      //Джампер режима программирования
GButton btnClear(PIN_BTN_CLEAR);                          //Кнопка сброса

OneWire iButton(PIN_BTN_GUARD);         //Считыватель iButton

TimerMs tmrSens(DELAY_LONG, 1, 0);
TimerMs tmrStrob(50, 1, 0);
TimerMs tmrStrobDelay(1200, 1, 0);
TimerMs tmrProgState(250, 1, 0);
TimerMs tmrAlarm(BASE_ALARM_TIME_MS, 0, 1);

boolean strobState = false;
boolean strobDelayState = false;
boolean strobProgram = false;

byte savedKeys[MAX_KEYS_COUNT][KEY_LENGTH];
byte readedKey[KEY_LENGTH];
byte nulls[KEY_LENGTH] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

enum { 
  WAIT,    //ожидание(снят с охраны)
  GUARD,   // На охране
  ALARM,   // Тревога
  PROGRAM // Программирование(добавление ключа)
  } states;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  pinMode(PIN_LED_GUARD, OUTPUT);
  pinMode(PIN_HORN, OUTPUT);
  pinMode(PIN_STROB, OUTPUT);
  pinMode(PIN_BEEPER, OUTPUT);
  pinMode(PIN_MOTION_SENSOR, INPUT);
  btnClear.setTimeout(DELAY_BTN_CLEAR_HOLD);
  readSavedKeys();
  states = WAIT;
  tmrAlarm.setTimerMode();
}

void loop() {
  btnTicks();
  checkProgrammingState();
  process();
}

//Вывод в консоль считанного ключа для отладки
void printKey(byte key[KEY_LENGTH]) {
  Serial.println();
  Serial.print("key:");
  Serial.println();
  for (int j=0; j < KEY_LENGTH; j++) {
    Serial.print(key[j], HEX);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print("________________");
  Serial.println();
}

//Сохранение ключа в eeprom
void putEEPROM(int adr, byte key[KEY_LENGTH]) {
  for (int i = 0; i < KEY_LENGTH; i++) {
     EEPROM.put(adr, key);
  }   
}

//чтение сохраненных ключей
void readSavedKeys() {
  for (int i = 0; i < MAX_KEYS_COUNT; i++){
    EEPROM.get(KEY_LENGTH * i, savedKeys[i]);
  }  
}

//Пищалка
void ton(){
  tone(PIN_BEEPER, 432, DELAY_SHORT); 
}

//Вывод в консоль сохраненных ключей, для отладки
void printKeys(){
  Serial.print("saved_keys:");
  Serial.println();
  for (int i=0; i < MAX_KEYS_COUNT; i++){
    for (int j=0; j < KEY_LENGTH; j++){
      Serial.print(savedKeys[i][j], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
  Serial.print("________________");
  Serial.println();
}

//Вывод в консоль считанного ключа для отладки
void printCurr() {
  Serial.print("readed_key:");
  Serial.println();
  for (int j=0; j < KEY_LENGTH; j++) {
    Serial.print(readedKey[j], HEX);
    Serial.print(" ");
  }
  Serial.println();
  Serial.print("________________");
 
}

//чтение ключа ibutton
void readKey() {
  if (readedKey[0] == NULL) {
      byte i;
      byte present = 0;
      byte data[12];
        
      if (!iButton.search(readedKey)) {
          iButton.reset_search();
          return;
      }

      if ( OneWire::crc8(readedKey, 7) != readedKey[7]) {
          ton();
          delay(DELAY_SHORT);
          ton();
          delay(DELAY_SHORT);
          ton();
          return;
      }
        
      if ( readedKey[0] != 0x01) {
          ton();
          delay(DELAY_SHORT);
          ton();
          delay(DELAY_SHORT);
          ton(); 
          return;
      }
      iButton.reset();
    
  }
}

//Сравнение двух массивов
boolean checkEqualsArrs(byte arrayFirst[KEY_LENGTH], byte arraySecond[KEY_LENGTH]) {
    for (int i = 0; i < KEY_LENGTH; i++){
      if (arrayFirst[i] != arraySecond[i]) return false;      
    }
    return true;
}

//Проверяет есть ли считанный ключ в памяти
boolean keyAlreadyInMemory(byte arrayFirst[KEY_LENGTH]) {
  for (int i=0; i < MAX_KEYS_COUNT; i++) {
    if (checkEqualsArrs(arrayFirst, savedKeys[i])){
      return true;
    }
  }
  return false;
}

//Сохранение ключа в EEPROM
void saveKey() {
  lightLed(true);
  int last = lastSavedKey();
  if (last < MAX_KEYS_COUNT) {
    if (!keyAlreadyInMemory(readedKey)) putEEPROM(KEY_LENGTH * last, readedKey);
    ton();
    readSavedKeys(); 
  } else {
    ton();
    delay(DELAY_SHORT);
    ton();
  }
  
  delay(DELAY_LONG*2);
  readedKey[0] = NULL;
  lightLed(false);
}

//Системные вызовы для обработки кнопок/датчиков
void btnTicks() {
  motionSens.tick();
  doorSens.tick();
  jamperProg.tick();
  btnClear.tick();
}

//Определяет позицию последнего сохраненного ключа
int lastSavedKey() {
  for (int i=0; i < MAX_KEYS_COUNT; i++) {
    if (savedKeys[i][0] == NULL) return i;
  }
  return MAX_KEYS_COUNT;
}

//Проверка запуска режима программирования
void checkProgrammingState() {
  if (states == WAIT) {
    if (jamperProg.isHold()) {
      lightLed(true);
      states = PROGRAM;
      ton();
    }
  } else if (states == PROGRAM) {
    if (!jamperProg.isHold()){
      states = WAIT;
      lightLed(false);
      ton();
    } else {
      strobLightProg();
      readKey(); 
      if (readedKey[0] != NULL){
        saveKey();
      }     
      if (btnClear.isHold()){
        clearAllKeys();
      }
    }
  }
}

//Очистка всех ключей из памяти
void clearAllKeys() {
   for (int i = 0; i < MAX_KEYS_COUNT; i++){
    putEEPROM(KEY_LENGTH * i, nulls);
  }
  memcpy(readedKey, nulls, sizeof(nulls));
  ton();
  delay(DELAY_LONG);
  readSavedKeys();
}

//Проверка постановки/снятия с охраны
void checkGuardOrWait() {
  readKey();
  if (readedKey[0] != NULL) {      
      if (keyAlreadyInMemory(readedKey)) {
        ton();    
        if (states == WAIT) {
          changeGuardState(true);
        } else {
          changeGuardState(false);
        }
      } else {
        ton();
        delay(DELAY_SHORT);
        ton();
        delay(DELAY_LONG);
        readedKey[0] = NULL;
      }
  }
}

//Рабочий процесс
void process() {
  checkGuardOrWait();
  if (states == WAIT) {
    lightLed(false); 
  } else if (states == GUARD) {
    lightLed(true);
    checkSensors();
  } else if (states == ALARM) {
    alarm();
  }
}

//Постановка/снятие с охраны 
void changeGuardState(boolean toGuard) {
  if (toGuard) {
    delay(DELAY_GUARD);
    digitalWrite(PIN_STROB, HIGH);
    delay(DELAY_SHORT);
    digitalWrite(PIN_STROB, LOW);
    states = GUARD;
  } else {
    stopAlarm(false);
    delay(DELAY_LONG);
    states = WAIT;
  }
  readedKey[0] = NULL;
}

//Запуск тревоги
void startAlarm() {
  if (states != ALARM) {
    states = ALARM;
    delay(DELAY_ALARM);
    tmrAlarm.start();
  }
  
}

//Остановка тревоги и снятие с охраны
void stopAlarm(boolean toGuard) {
  if (toGuard) {
    states = GUARD;
  } else {
    states = WAIT;
  }
  currentCountAlarmTimer = 0;
  digitalWrite(PIN_HORN, LOW);
  digitalWrite(PIN_STROB, LOW);
}

//Процесс тревоги
void alarm() {
  digitalWrite(PIN_HORN, HIGH);
  strob();
  if (tmrAlarm.tick()) {
    currentCountAlarmTimer ++;
    if (currentCountAlarmTimer >= countTimerAlarmRestartes) {
      stopAlarm(true);      
    } else {
      tmrAlarm.start();
    }
    
  }
}

//Запуск стробоскопа
void strob() {
  if (tmrStrobDelay.tick()){
    strobDelayState = !strobDelayState;
    digitalWrite(PIN_STROB, LOW);
  }
  if (tmrStrob.tick()) {
    strobState = !strobState;
    if (strobDelayState){
      if (strobState){
        digitalWrite(PIN_STROB, HIGH);
      } else {
        digitalWrite(PIN_STROB, LOW);
      }
    }
  }
}

//Проверка состояния датчиков
void checkSensors() {
  if (tmrSens.tick()) {
    // Опрос датчика движения
    if (motionSens.isHold()){
        startAlarm();
    }
    // Опрос концевика двери
    if (doorSens.isHold()){
        startAlarm();
    }
  }
}

//Включение/выключение светодиода
void lightLed(boolean light){
  if (light) {
    digitalWrite(PIN_LED_GUARD, HIGH);    
  } else {
    digitalWrite(PIN_LED_GUARD, LOW);    
  }
}

//Мигание светодиодом с режиме программирования
void strobLightProg() {
  if (tmrProgState.tick()){
    strobProgram = !strobProgram;
    if (strobProgram) {
      digitalWrite(PIN_LED_GUARD, HIGH);
    } else {
      digitalWrite(PIN_LED_GUARD, LOW);
    }
  } 
}
