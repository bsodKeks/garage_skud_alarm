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
#define PIN_JAMPER_PROG 2    // пин джампера режима программирования
#define PIN_BEEPER 3         // пин пищалки
#define PIN_BTN_CLEAR 4      // пин кнопки сброса

#define DELAY_GUARD 10*1000   // Задержка установки на охрану (мс)
#define DELAY_SHORT 200
#define DELAY_LONG 1000
#define DELAY_ALARM 3*1000
#define DELAY_BTN_CLEAR_HOLD 3*1000

#define MAX_KEYS_COUNT 10

GButton motionSens(PIN_MOTION_SENSOR, LOW_PULL);          //Датчик движения
GButton doorSens(PIN_BTN_DOOR);                           //Концевик двери
GButton jamperProg(PIN_JAMPER_PROG);                      //Джампер режима программирования
GButton btnClear(PIN_BTN_CLEAR);                          //Кнопка сброса

OneWire iButton(PIN_BTN_GUARD);         //Считыватель iButton

TimerMs tmrSens(DELAY_LONG, 1, 0);
TimerMs tmrStrob(50, 1, 0);
TimerMs tmrStrobDelay(1200, 1, 0);
TimerMs tmrProgState(250, 1, 0);
TimerMs tmrReadkey(DELAY_LONG, 1, 0);

boolean strobState = false;
boolean strobDelayState = false;
boolean strobProgram = false;
boolean mayRead = false;

byte dataArray[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72};
byte savedKeys[10][8];
byte readedKey[8];
byte nulls[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};

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
}

void loop() {
  btnTicks();
  checkProgrammingState();
  process();
}

//Пищалка
void ton(){
  tone(PIN_BEEPER, 432, DELAY_SHORT); 
}

//чтение сохраненных ключей
void readSavedKeys(){
  for (int i = 0; i < MAX_KEYS_COUNT; i++){
    EEPROM.get(dataArray[i], savedKeys[i]);
  }  
}

//чтение ключа ibutton
void readKey(){
  if (tmrReadkey.tick()) {
    mayRead = !mayRead;
  }
  if (readedKey[0] == NULL) {
      if (mayRead) {
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
}

//Сравнение двух массивов
boolean checkEqualsArrs(byte arrayFirst[8], byte arraySecond[8]){
    for (int i = 0; i < 8; i++){
      if (arrayFirst[i] != arraySecond[i]) return false;      
    }
    return true;
}

//Проверяет есть ли считанный ключ в памяти
boolean keyAlreadyInMemory(byte arrayFirst[8]) {
  for (int i=0; i < MAX_KEYS_COUNT; i++) {
    return checkEqualsArrs(arrayFirst, savedKeys[i]);
  }
  return false;
}

//Сохранение ключа в EEPROM
void saveKey(){
  lightLed(true);
  int last = lastSavedKey();
  if (last < MAX_KEYS_COUNT) {
    if (!keyAlreadyInMemory(readedKey)) EEPROM.put(dataArray[last], readedKey);
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
void btnTicks(){
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
  return 10;
}

//Проверка запуска режима программирования
void checkProgrammingState(){
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
void clearAllKeys(){
   for (int i = 0; i < MAX_KEYS_COUNT; i++){
    EEPROM.put(dataArray[i], nulls);
  }
  memcpy(readedKey, nulls, sizeof(nulls));
  ton();
  delay(DELAY_LONG);
  readSavedKeys();
}

//Проверка постановки/снятия с охраны
void checkGuardOrWait(){
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
    stopAlarm();
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
  }
  
}

//Остановка тревоги и снятие с охраны
void stopAlarm() {
  states = WAIT;
  digitalWrite(PIN_HORN, LOW);
  digitalWrite(PIN_STROB, LOW);
}

//Процесс тревоги
void alarm() {
  digitalWrite(PIN_HORN, HIGH);
  strob();
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
