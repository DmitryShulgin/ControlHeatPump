/*
 * Copyright (c) 2016-2018 by Pavel Panfilov <firstlast2007@gmail.com> skype pav2000pav
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
// -------- Работа со временем ---------------------------
#include "Constant.h"                       // Вся конфигурация и константы проекта Должен быть первым !!!!
#include <rtc_clock.h>                      // Hабота со встроенными часами
#include "stdlib.h"


// Объявление функций 
// NTP ----------------------------
#define NTP_PACKET_SIZE 48                  // временная отметка NTP находится в первых 48 байтах сообщения
byte packetBuffer[NTP_PACKET_SIZE+1];       // буфер, в котором будут храниться входящие и исходящие пакеты 

// Установка веремени системы (сначала читаем из i2c часов)
// Возвращает код ошибки
int8_t set_time(void)
{
   unsigned long ttime;
   //rtcI2C.begin(); // I2C уже инициализирована.// Запустить i2c часы 
   rtcSAM3X8.init();                             // Запуск внутренних часов
   journal.jprintf(" Init internal RTC sam3x8e\n"); 
   ttime=TimeToUnixTime(getTime_RtcI2C());   // Прочитать время из часов i2c
   rtcSAM3X8.set_clock(ttime,0);                // Установить внутренние часы по i2c   
   _delay(200);         
   journal.jprintf(" Set time internal RTC form i2c RTC DS3231: %s ",NowDateToStr());journal.jprintf("%s\n",NowTimeToStr());  // Одним оператором есть косяк
  
   if (HP.get_updateNTP()) set_time_NTP() ;      // Обновить время по NTP
   HP.set_uptime(ttime);                         // Запомнить время старта контроллера
   return OK;
}

// Функция обновления времени по ntp вызывается из задачи или кнопкой. true - если время обновлено
// Запрос времени от NTP сервера, возвращает время как long
boolean set_time_NTP(void)
{
   unsigned long secsSince1900=0;
   unsigned long  epoch=0;
   int i;
   boolean flag=false;
   IPAddress ip(0,0,0,0);
   journal.jprintf(pP_TIME," Update time from NTP server: %s\n",HP.get_serverNTP()); 
   //1. Установить адрес  не забываем работаетм через один сокет, опреации строго последовательные,иначе настройки сбиваются
   WDT_Restart(WDT);                                        // Сбросить вачдог  при ошибке долго ждем
 
  // Если запущен шедулер то захватываем семафор
     if (SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE)  {return false;}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
   // DNS запрос для определения адреса
   if (!check_address(HP.get_serverNTP(),ip)) { SemaphoreGive(xWebThreadSemaphore); return false;}  // DNS - ошибка выходим
 
   // 2. Посылка пакета 
   if(!Udp.begin(NTP_LOCAL_PORT,W5200_SOCK_SYS)) 
   { 
   journal.jprintf(" UDP fail\n"); 
   SemaphoreGive(xWebThreadSemaphore);
   return false; 
   }
   for(i=0;i<NTP_REPEAT;i++)                                       // Делам 5 попыток получить время
   {  
      WDT_Restart(WDT);                                            // Сбросить вачдог  
      journal.jprintf(" Send packet NTP, wait . . .\n"); 
      if(!sendNTPpacket(ip)) { SemaphoreGive(xWebThreadSemaphore);return false;}   // Отсылаем время-серверу NTP-пакет по IP что DNS не задействовать
       _delay(NTP_REPEAT_TIME);                                                    // Ждем, чтобы увидеть, доступен ли ответ: 
       if (Udp.parsePacket() ) 
       {  
           // Пакет получен, значит считываем данные оттуда:
           Udp.read(packetBuffer,NTP_PACKET_SIZE); // считываем содержимое пакета в буфер
           // Временная отметка начинается с 40 байта полученного пакета и его длина составляет четыре байта или два слова.
           // Для начала извлекаем два этих слова:
           unsigned long highWord = packetBuffer[40]<<8 | packetBuffer[41];
           unsigned long lowWord =  packetBuffer[42]<<8 | packetBuffer[43];
           // Совмещаем четыре байта (два слова) в длинное целое. Это и будет NTP-временем (секунды начиная с 1 января 1990 года):
           secsSince1900 = highWord << 16 | lowWord;
           const unsigned long seventyYears = 2208988800UL; // Время Unix стартует с 1 января 1970 года. В секундах это 2208988800:
           epoch = secsSince1900-seventyYears;              // Вычитаем 70 лет 
           flag=true;                                       // Время получили
           break;                                           // ответ получен выходим
        } 
   } // for
   SemaphoreGive(xWebThreadSemaphore);
   if (flag)   // Обновление времени если оно получено
     {
         rtcSAM3X8.set_clock(epoch,TIME_ZONE);                    // обновить внутренние часы
         // обновились, можно и часы i2c обновить   
         setTime_RtcI2C(rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(),rtcSAM3X8.get_seconds());
         setDate_RtcI2C(rtcSAM3X8.get_days(), rtcSAM3X8.get_months(),rtcSAM3X8.get_years());
         journal.jprintf(" Set time from NTP server: %s ",NowDateToStr());journal.jprintf("%s\n",NowTimeToStr());  // Одним оператором есть косяк
     }
     else 
     {
       journal.jprintf(" ERROR update time from NTP server! %s ",NowDateToStr());
       journal.jprintf("%s\n",NowTimeToStr());// Одним оператором есть косяк
     }
      
return flag;  
}  

 // send an NTP request to the time server at the given address
 // true если нет ошибок
boolean sendNTPpacket(IPAddress &ip)
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum, or type of clock
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
if(Udp.beginPacket(ip, 123, W5200_SOCK_SYS) == 1) 
  { 
    Udp.write(packetBuffer,NTP_PACKET_SIZE);
    if(Udp.endPacket() != 1) { journal.jprintf("Send packet NTP error\n"); return false;}
  }
  else 
  {
    journal.jprintf("Socket error\n");
    return false;
  }
return true;
}


//  Получить текущее время (с секундами!) в виде строки
char* NowTimeToStr()
{
  uint32_t x;
  static char _tmp[12];  // Длина xx:xx:xx - 10+1 символов
  x=rtcSAM3X8.get_hours();
  if (x<10) strcpy(_tmp,cZero); else strcpy(_tmp,"");  strcat(_tmp,int2str(x)); strcat(_tmp,":");

  x=rtcSAM3X8.get_minutes();
  if (x<10) strcat(_tmp,cZero);   strcat(_tmp,int2str(x)); strcat(_tmp,":");

   x=rtcSAM3X8.get_seconds();
  if (x<10) strcat(_tmp,cZero);   strcat(_tmp,int2str(x)); 
  
   return _tmp;
}
//  Получить текущее время (без секунд!) в виде строки
char* NowTimeToStr1()
{
  uint32_t x;
  static char _tmp[8];   // Длина xx:xx - 5+1 символов
  x=rtcSAM3X8.get_hours();
  if (x<10) strcpy(_tmp,cZero); else strcpy(_tmp,"");  strcat(_tmp,int2str(x)); strcat(_tmp,":");

  x=rtcSAM3X8.get_minutes();
  if (x<10) strcat(_tmp,cZero);   strcat(_tmp,int2str(x)); 
 
  return _tmp;
}

//  Получить текущую дату в виде строки
char* NowDateToStr()
{
  static char _tmp[16];  // Длина xx/xx/xxxx - 10+1 символов
  strcpy(_tmp,int2str(rtcSAM3X8.get_days())); strcat(_tmp,"/");strcat(_tmp,int2str(rtcSAM3X8.get_months()));strcat(_tmp,"/");strcat(_tmp,int2str(rtcSAM3X8.get_years())); 
  return _tmp;
}

// (Длительность инервала в строку) Время в формате день day 12:34 используется для рассчета uptime
char* TimeIntervalToStr(uint32_t idt)
{
    uint32_t Day;
    uint8_t  Hour;
    uint8_t  Min;
    static  char _tmp[16]; // Длина xxxxd xxh xxm - 13+1 символов
  //  uint8_t  Sec;
  /* decode the interval into days, hours, minutes, seconds */
 // Sec = idt % 60;
  idt /= 60;
  Min = idt % 60;
  idt /= 60;
  Hour = idt % 24;
  idt /= 24;
  Day = idt;
  strcpy(_tmp,"");  // очистить строку
  if  (Day>0)  { strcat(_tmp,int2str(Day)); strcat(_tmp,"d ");}  // если есть уже дни
//  if  (Hour>0) { if (Hour<10) strcat(_tmp,cZero); strcat(_tmp,int2str(Hour));strcat(_tmp," hour ");}
//                 if (Min<10) strcat(_tmp,cZero);  strcat(_tmp,int2str(Min)); strcat(_tmp," min");
  if  (Hour>0) { if (Hour<10) strcat(_tmp,cZero); strcat(_tmp,int2str(Hour));strcat(_tmp,"h ");}
                 if (Min<10) strcat(_tmp,cZero);  strcat(_tmp,int2str(Min)); strcat(_tmp,"m ");
  return _tmp;       
}

// вывод Времени в формате 12:34:34 
char* TimeToStr(uint32_t idt)
{
static char _tmp[12];  // Длина xx:xx:xx - 10+1 символов
strcpy(_tmp,"");  // очистить строку

// Время
strcat(_tmp,int2str((idt % 86400L)/3600));       // показываем час (86400  - это количество секунд в сутках)
strcat(_tmp,":");
if (((idt % 3600) / 60) < 10) strcat(_tmp,cZero);  // у первых 10 минут каждого часа впереди должна стоять цифра «0»:
strcat(_tmp,int2str((idt % 3600)/60));           // показываем минуту (3600 – это количество секунд в минуту)  
strcat(_tmp,":");
if ((idt % 60) < 10)  strcat(_tmp,cZero);          // у первых 10 секунд каждой минуты впереди должна стоять цифра «0»:
strcat(_tmp,int2str(idt % 60));                  // показываем секунду

strcat(_tmp," ");

return _tmp;       
}

// вывод Времи и даты  в формате 12:34 11/11/2016
//http://stackoverflow.com/questions/21593692/convert-unix-timestamp-to-date-without-system-libs
char*  DecodeTimeDate(uint32_t idt)
{
  uint32_t seconds, minutes, hours, days, year, month;
  uint32_t dayOfWeek;
  static char _tmp[32];
  int x;

  seconds=idt;

  /* calculate minutes */
  minutes  = seconds / 60;
  seconds -= minutes * 60;
  /* calculate hours */
  hours    = minutes / 60;
  minutes -= hours   * 60;
  /* calculate days */
  days     = hours   / 24;
  hours   -= days    * 24;

  /* Unix time starts in 1970 on a Thursday */
  year      = 1970;
  dayOfWeek = 4;

  while(1)
  {
    bool     leapYear   = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (days >= daysInYear)
    {
      dayOfWeek += leapYear ? 2 : 1;
      days      -= daysInYear;
      if (dayOfWeek >= 7)
        dayOfWeek -= 7;
      ++year;
    }
    else
    {
      dayOfWeek  += days;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(month = 0; month < 12; ++month)
      {
        uint8_t dim = daysInMonth[month];
        /* add a day to feburary if this is a leap year */
        if (month == 1 && leapYear)
          ++dim;
        if (days >= dim)
          days -= dim;
        else
          break;
      }
      break;
    }
  }
// формирование строки
  x=hours;
  if (x<10) strcpy(_tmp,cZero); else strcpy(_tmp,"");  strcat(_tmp,int2str(x)); strcat(_tmp,":");
  x=minutes;
  if (x<10) strcat(_tmp,cZero);   strcat(_tmp,int2str(x)); strcat(_tmp,":");
   x=seconds;
  if (x<10) strcat(_tmp,cZero);   strcat(_tmp,int2str(x)); 
  strcat(_tmp," "); 
  strcat(_tmp,int2str(days + 1)); strcat(_tmp,"/");strcat(_tmp,int2str(month+1));strcat(_tmp,"/");strcat(_tmp,int2str(year)); 
  return _tmp;   
}

// вывод даты  в формате 010218 для графиков статистики (всегда 6 байт+1)
// forma тип вывода true - краткий (ДДММГГ) false - полный (Д/М/ГГГГ)
//http://stackoverflow.com/questions/21593692/convert-unix-timestamp-to-date-without-system-libs
char*  StatDate(uint32_t idt,boolean forma)
{
  uint32_t seconds, minutes, hours, days, year, month;
  uint32_t dayOfWeek;
  static char _tmp[16];
  static char _int3str[12];
  static int x;
  seconds=idt;

  /* calculate minutes */
  minutes  = seconds / 60;
  seconds -= minutes * 60;
  /* calculate hours */
  hours    = minutes / 60;
  minutes -= hours   * 60;
  /* calculate days */
  days     = hours   / 24;
  hours   -= days    * 24;

  /* Unix time starts in 1970 on a Thursday */
  year      = 1970;
  dayOfWeek = 4;
  while(1)
  {
    bool     leapYear   = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    uint16_t daysInYear = leapYear ? 366 : 365;
    if (days >= daysInYear)
    {
      dayOfWeek += leapYear ? 2 : 1;
      days      -= daysInYear;
      if (dayOfWeek >= 7)
        dayOfWeek -= 7;
      ++year;
    }
    else
    {
      dayOfWeek  += days;
      dayOfWeek  %= 7;
      /* calculate the month and day */
      static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for(month = 0; month < 12; ++month)
      {
        uint8_t dim = daysInMonth[month];
        /* add a day to feburary if this is a leap year */
        if (month == 1 && leapYear)
          ++dim;
        if (days >= dim)
          days -= dim;
        else
          break;
      }
      break;
    }
  }

  if(forma)  // Тип вывода 
   { 
    strcpy(_tmp,"");
    x=days+1;  if (x<10) strcat(_tmp,cZero);itoa(x,_int3str,10);strcat(_tmp,_int3str);   
    x=month+1; if (x<10) strcat(_tmp,cZero);itoa(x,_int3str,10);strcat(_tmp,_int3str);
    x=year;    if (x>2000) itoa(x-2000,_int3str,10); else itoa(x-1900,_int3str,10); strcat(_tmp,_int3str); // формирование строки формата ДДММГГ (год последние две цифры)
    }
 else
    {itoa(days + 1,_int3str,10);strcpy(_tmp,_int3str); strcat(_tmp,"/");itoa(month+1,_int3str,10); strcat(_tmp,_int3str);strcat(_tmp,"/");itoa(year,_int3str,10); strcat(_tmp,_int3str);}
 return _tmp;   
}

// Перевод формата времени Time в формат Unix (секунды с 1970 года)
#define SEC_1970_TO_2000      946684800
static  const uint8_t dim[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
unsigned long TimeToUnixTime(tmElements_t t) //[V]*
 {
    uint16_t  dc;
    dc = t.Day;
    for (uint8_t i = 0; i<(t.Month-1); i++) dc += dim[i];
    if ((t.Month > 2) && (((t.Year-2000) % 4) == 0))  ++dc;
    dc = dc + (365 * (t.Year-2000)) + (((t.Year-2000) + 3) / 4) - 1;
    return ((((((dc * 24L) + t.Hour) * 60) + t.Minute) * 60) + t.Second) + SEC_1970_TO_2000;
 } 
