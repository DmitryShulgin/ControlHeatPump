 /*
 * Copyright (c) 2016-2018 by Pavel Panfilov <firstlast2007@gmail.com> skype pav2000pav,
 * vad711, vad7@yahoo.com
 *
 * "Народный контроллер" для тепловых насосов.
 * Данное програмноое обеспечение предназначено для управления
 * различными типами тепловых насосов для отопления и ГВС.
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 */
// Цифровые датчики температуры -------------------------------------------------------------

#ifndef DigitalTemp_h
#define DigitalTemp_h

#ifndef OVERRIDE_TNUMBERS   // Если определено, берется из Config.h

#define TOUT        0       // Температура улицы
#define TIN         1       // Температура в доме
#define TEVAIN      2       // Температура на входе испарителя (по фреону)
#define TEVAOUT     3       // Температура на выходе испарителя (по фреону)
#define TCONIN      4       // Температура на входе конденсатора (по фреону)
#define TCONOUT     5       // Температура на выходе конденсатора (по фреону)
#define TBOILER     6       // Температура в бойлере ГВС
#define TACCUM      7       // Температура на выходе теплоаккмулятора
#define TRTOOUT     8       // Температура на выходе RTO (по фреону)
#define TCOMP       9       // Температура нагнетания компрессора
#define TEVAING     10      // Температура на входе испарителя (по гликолю)
#define TEVAOUTG    11      // Температура на выходе испарителя (по гликолю)
#define TCONING     12      // Температура на входе конденсатора (по гликолю)
#define TCONOUTG    13      // Температура на выходе конденсатора (по гликолю)

#endif

#define STARTTEMP   -27321  // Значение инициализации датчика температуры, по нему определяется первая итерация (сотые градуса)

enum TEMP_SETUP_FLAGS {
	fDS2482_second = 0	// датчик на втором DS2482 (адрес I2C_ADR_DS2482two)
};

// Удаленные датчики температуры -------------------------------------------------------------
#ifdef SENSOR_IP
//  Работа с отдельными флагами sensorIP
#define fUse        0               // флаг разрешить использование датчика
#define fRule       1               // флаг Правило использования датчика 0 - использование удаленного датчика 1- усреднение с основным
class sensorIP
  {
  public:
    void initIP();                             // Инициализация датчика
    boolean set_DataIP(int16_t a,int16_t b,int16_t c,int16_t d,uint32_t e,IPAddress f);  // Запомнить данные (обновление данных)
    char* get_sensorIP(TYPE_SENSOR_IP  p);                     // Получить параметр в виде строки
    __attribute__((always_inline)) inline int16_t get_Temp(){return Temp;}                           // Получение последний температуры датчика  если данные не достоверны или датчик не рабоает возврат -10000 (-100 градусов)
    __attribute__((always_inline)) inline uint32_t get_update(){return rtcSAM3X8.unixtime()-stime;}  // Получение времени прошедшего с последнего пакета
    __attribute__((always_inline)) inline boolean get_fUse(){return GETBIT(flags,fUse);}             // Получить  флаг разрешить использование датчика
    __attribute__((always_inline)) inline boolean get_fRule(){return GETBIT(flags,fRule);}           // Получить  флаг Правило использования датчика 0 - использование удаленного датчика 1- усреднение с основным
    __attribute__((always_inline)) inline int8_t get_link(){return link;}                            // Получение номера датчика (из массива датиков температуры),c которым связан удаленный датчик
    uint8_t get_num(){return num;}                             // Получение номера датчика, по нему осуществляется идентификация датачика о 0 до MAX_SENSOR_IP-1
    int8_t get_RSSI(){return RSSI;}                            // Получение Уровень сигнала датчика (-дБ)
    uint16_t get_VCC(){return VCC;}                            // Получение Уровень напряжениея питания датчика (мВ)
    uint32_t get_count(){return count;}                        // Получение Кольцевой счетчик пакетов с момента включения контрола
    uint32_t get_stime(){return stime;}                        // Получение Время получения последнего пакета
    IPAddress get_ip(){return ip;}                             // Адрес датчика
    void set_fUse(boolean b)  {if (b) SETBIT1(flags,fUse); else SETBIT0(flags,fUse); };   // Установить флаг использования
    void set_fRule(boolean b) {if (b) SETBIT1(flags,fRule); else SETBIT0(flags,fRule);}   // Установить флаг учреднения
    void set_link(int8_t b) {link=b;}                          // Установить флаг усреднения
    int32_t save(int32_t adr);                                 // Записать настройки в eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    int32_t load(int32_t adr);                                 // Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    int32_t loadFromBuf(int32_t adr,byte *buf);                // Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    uint16_t get_crc16(uint16_t crc);                          // Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая

  private:
    int16_t Temp;                                              // Последние показания датчика
    int8_t num;                                                // Номер датчика (-1 датчик отсутсвует), по нему осуществляется идентификация датачика о 0 до MAX_SENSOR_IP-1
    int16_t RSSI;                                              // Уровень сигнала датчика (-дБ)
    uint16_t VCC;                                              // Уровень напряжениея питания датчика (мВ)
    uint32_t count;                                            // Кольцевой счетчик пакетов с момента включения контрола
    uint32_t stime;                                            // Время получения последнего пакета
    uint8_t flags;                                             // флаги
    int8_t link;                                               // привязка датчика (номер в массиве температурных датчиков, -1 не привязан)
    IPAddress ip;                                              // Адрес датчика
  };
#endif


// класс датчик DS18B20 Температура хранится в сотых градуса в целых значениях
class sensorTemp
  {
  public:
 //   sensorTemp();                                     // Конструктор
    void initTemp(int sensor);                          // Инициализация на входе номер датчика
    int8_t  PrepareTemp();                              // запуск преобразования
    int8_t  Read();                                     // чтение данных, возвращает код ошибки, делает все преобразования
    int16_t Test();                                     // полный цикл получения данных возвращает значение темературы, только тестирование!! никакие переменные класса не трогает!!
    int16_t get_minTemp(){return minTemp;}              // Минимальная темература датчика - нижняя граница диапазона, при выходе из него ошибка
    int16_t get_maxTemp(){return maxTemp;}              // Максимальная темература датчика - верхняя граница диапазона, при выходе из него ошибка
    int16_t get_errTemp(){ return errTemp;}             // Значение систематической ошибки датчика
    int8_t  set_errTemp(int16_t t);                     // Установить значение систематической ошибки датчика (поправка)
    int16_t get_lastTemp(){return lastTemp;}            // Последнее считанное значение датчика - НЕ обработанное (без коррекции ошибки и усреднения)
    int16_t get_Temp();                                 // Получить значение температуры датчика с учетом удаленных датчиков!!! - это то что используется
    int16_t get_rawTemp();                              // Получить значение температуры ПРОВОДНОГО датчика
    int16_t get_testTemp(){return testTemp;}            // Получить значение температуры датчика - в режиме теста
    int8_t  set_testTemp(int16_t t);                    // Установить значение температуры датчика - в режиме теста
    TEST_MODE get_testMode(){return  testMode;}         // Получить текущий режим работы
    void  set_testMode(TEST_MODE t){testMode=t;}        // Установить значение текущий режим работы
    
    void set_address(byte *addr, byte bus_type);        // Привязать адрес и тип шины
    byte* get_address(){return address;}                // Получить адрес датчика
    __attribute__((always_inline)) inline boolean get_present(){return GETBIT(flags,fPresent);} // Наличие датчика в текущей конфигурации
    int8_t get_lastErr(){return err;}                   // Получить последнюю ошибку
    uint32_t get_sumErrorRead(){return sumErrorRead;}   // Получить число ошибок чтения датчика с момента сброса НК
    char* get_note(){return note;}                      // Получить оисание датчика
    char* get_name(){return name;}                      // Получить имя датчика
    int32_t save(int32_t adr);                          // Записать настройки в eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    int32_t load(int32_t adr);                          // Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    int32_t loadFromBuf(int32_t adr, byte *buf);         // Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
    uint16_t get_crc16(uint16_t crc);                   // Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая

    statChart Chart;                                     // Статистика по датчику
    
    #ifdef SENSOR_IP                                    // Удаленные устройства  #ifdef SENSOR_IP
     sensorIP *devIP;                                   // Ссылка на привязаный датчик (класс) если NULL привявязки нет
    #endif
    
  private:
   int16_t minTemp;                                     // минимальная разрешенная температура
   int16_t maxTemp;                                     // максимальная разрешенная температура
   int16_t lastTemp;                                    // последняя считанная температура с датчика
   int16_t Temp;                                        // температура датчика (обработанная)
   byte address[8];                                     // текущий адресс датчика (при охлаждении не совпадает с основным) датчик всегда читается по этому адресу
   int16_t errTemp;                                     // статическая ошибка датчика
   int16_t testTemp;                                    // температура датчика в режиме тестирования
   TEST_MODE testMode;                                  // Значение режима тестирования
   int8_t  err;                                         // ошибка датчика (работа) при ошибке останов ТН
   uint8_t numErrorRead;                                // Счечик ошибок чтения датчика подряд если оно больше NUM_READ_TEMP_ERR генерация ошибки датчика
   uint32_t sumErrorRead;                               // Cуммарный счечик ошибок чтения датчика - число ошибок датчика с момента перегрузки
   
   byte flags;                                          // флаги  датчика
   uint8_t setup_flags;								    // флаги настройки (T_SETUP_FLAGS)
   char *note;                                          // Описание датчика
   char *name;                                          // Имя датчика
   // Кольцевой буфер для усреднения
   int16_t t[T_NUMSAMLES];                              // буфер для усреднения показаний температуры
   int32_t sum;                                         // Накопленная сумма
   uint8_t last;                                        // указатель на последнее (самое старое) значение в буфере диапазон от 0 до T_NUMSAMLES-1
   uint8_t nGap;                                        // Счечик "разорванных" данных  - требуется для фильтрации помехи

   void set_onewire_bus_type();							// Устанавливает по флагам тип шины
   deviceOneWire *busOneWire;                           // указатель на используемую шину
   };

#endif
