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

//  описание вспомогательных Kлассов данных, предназначенных для получения информации о ТН
#include "Information.h"

// --------------------------------------------------------------------------------------------------------------- 
//  Класс системный журнал пишет в консоль и в память ------------------------------------------------------------
//  Место размещения (озу ли флеш) определяется дефайном #define I2C_EEPROM_64KB
// --------------------------------------------------------------------------------------------------------------- 
// Инициализация
void Journal::Init()
  {
    bufferTail = 0;
    bufferHead = 0;
    full=false;                   // Буфер не полный
    err=OK;
    #ifdef DEBUG
      Serial.begin(UART_SPEED);                   // Если надо инициализировать отладочный порт
    #endif
        
    #ifndef I2C_EEPROM_64KB     // журнал в памяти
     memset(_data,0,JOURNAL_LEN); 
     jprintf("\nSTART ----------------------\n"); 
     jprintf("Init RAM journal, size %d . . .\n",JOURNAL_LEN);  
     return;  
    #else                      // журнал во флеше

    #ifdef DEBUG
     Serial.println("\nInit I2C EEPROM journal . . .");   
    #endif  
     uint8_t eepStatus=0;
     uint16_t i;
     char *ptr;
     
     if ((eepStatus=eepromI2C.begin(I2C_SPEED)!=0))  // Инициализация памяти
     {
      #ifdef DEBUG
       Serial.println("$ERROR - open I2C journal, check chip i2C eeprom");   // ошибка открытия чипа
      #endif
      err=ERR_OPEN_I2C_JOURNAL; 
      return;
     }
     
     if (checkREADY()==false) // Проверка наличия журнал
     { 
      #ifdef DEBUG
        Serial.print("I2C journal not found\n"); 
       #endif  
      Format(); 
     }   
     #ifdef DEBUG  
       else Serial.print("I2C journal is ready for use\n");
       Serial.print("Scan I2C journal ");
     #endif 
     
     for (i=0;i<JOURNAL_LEN/W5200_MAX_LEN;i++)   // поиск журнала начала и конца, для ускорения читаем по W5200_MAX_LEN байт
       {
        #ifdef DEBUG  
        Serial.print(".");
        #endif
        if (readEEPROM_I2C(I2C_JOURNAL_START+i*W5200_MAX_LEN, (byte*)&bufI2C,W5200_MAX_LEN))   
           { err=ERR_READ_I2C_JOURNAL; 
             #ifdef DEBUG
             Serial.print(errorReadI2C); 
             #endif
            break; };
        if ((ptr=(char*)memchr(bufI2C,I2C_JOURNAL_HEAD,W5200_MAX_LEN))!=NULL)       { bufferHead=i*W5200_MAX_LEN+(ptr-bufI2C); }
        if ((ptr=(char*)memchr(bufI2C,I2C_JOURNAL_TAIL,W5200_MAX_LEN))!=NULL)       { bufferTail=i*W5200_MAX_LEN+(ptr-bufI2C); }
        if ((bufferTail!=0)&&(bufferHead!=0))  break;
       }
      if  (bufferTail<bufferHead) full=true;                   // Буфер полный
      jprintf("\nSTART ----------------------\n"); 
      jprintf("Found journal I2C EEPROM: total size %d bytes, head=0x%x, tail=0x%x \n",JOURNAL_LEN,bufferHead,bufferTail);  
    #endif //  #ifndef I2C_EEPROM_64KB     // журнал в памяти
  }

  
#ifdef I2C_EEPROM_64KB  // функции долько для I2C журнала
// Записать символ "начало" значение bufferHead должно быть установлено
void Journal::writeHEAD()
{  
    byte  b=I2C_JOURNAL_HEAD; 
    if (writeEEPROM_I2C(I2C_JOURNAL_START+bufferHead, (byte*)&b,sizeof(byte))) 
       { err=ERR_WRITE_I2C_JOURNAL; 
         #ifdef DEBUG
         Serial.println(errorWriteI2C);
         #endif
        }
}

// Записать символ "конец" значение bufferTail должно быть установлено
void Journal::writeTAIL()
{  
    byte  b=I2C_JOURNAL_TAIL; 
    if (writeEEPROM_I2C(I2C_JOURNAL_START+bufferTail, (byte*)&b,sizeof(byte))) 
       { err=ERR_WRITE_I2C_JOURNAL; 
         #ifdef DEBUG
         Serial.println(errorWriteI2C);
         #endif
        }
}
// Записать признак "форматирования" журнала - журналом можно пользоваться
void Journal::writeREADY()
{  
    uint16_t  w=I2C_JOURNAL_READY; 
    if (writeEEPROM_I2C(I2C_JOURNAL_START-2, (byte*)&w,sizeof(w))) 
       { err=ERR_WRITE_I2C_JOURNAL; 
         #ifdef DEBUG
         Serial.println(errorWriteI2C);
         #endif
        }
}
// Проверить наличие журнала
boolean Journal::checkREADY()
{  
    uint16_t  w=0x0; 
    if (readEEPROM_I2C(I2C_JOURNAL_START-2, (byte*)&w,sizeof(w))) 
       { err=ERR_READ_I2C_JOURNAL; 
         #ifdef DEBUG
         Serial.print(errorReadI2C);
         #endif
        }
    if (w!=I2C_JOURNAL_READY) return false; else return true;
}

// Форматирование журнала (инициализация I2C памяти уже проведена)
void Journal::Format()
{   
    uint16_t i;
    err=OK;
    memset(bufI2C,I2C_JOURNAL_FORMAT,W5200_MAX_LEN); 
    #ifdef DEBUG
     Serial.print("Formating journal I2C EEPROM ");
    #endif
    for (i=0;i<JOURNAL_LEN/W5200_MAX_LEN;i++) 
       { 
          #ifdef DEBUG  
          Serial.print("*");
          #endif 
          if (writeEEPROM_I2C(I2C_JOURNAL_START+i*W5200_MAX_LEN, (byte*)&bufI2C, W5200_MAX_LEN))   
              { err=ERR_WRITE_I2C_JOURNAL; 
                #ifdef DEBUG
                Serial.println(errorWriteI2C); 
                #endif
                break; }; 
          WDT_Restart(WDT);      
       }    
    full=false;                   // Буфер не полный
    bufferHead = 0;
    bufferTail = 1;
    writeHEAD();
    writeTAIL();
    if (err==OK)
    {
    writeREADY();                 // было форматирование
    jprintf("\nFormat I2C EEPROM journal (size %d bytes) - Ok\n",JOURNAL_LEN);
    }
}
#endif
    
// Печать только в консоль
void Journal::printf(const char *format, ...)             
  {
  #ifdef DEBUG
  va_list ap;
    if (strlen(format)>PRINTF_BUF-10) strcpy(pbuf,MessageLongString);   // Слишком длинная строка
    {
    va_start(ap, format);
    vsnprintf(pbuf, sizeof(pbuf), format, ap);
    va_end(ap);
    }
    Serial.print(pbuf);
  #endif 
  }
  
// Печать в консоль и журнал возвращает число записанных байт
uint16_t Journal::jprintf(const char *format, ...)
  {
      va_list ap;
      if (strlen(format)>PRINTF_BUF-10) strcpy(pbuf,MessageLongString);   // Слишком длинная строка
      else
      {
      va_start(ap, format);
      vsnprintf(pbuf, sizeof(pbuf), format, ap);
      va_end(ap);
      }
      #ifdef DEBUG
   //     Serial.print(full); Serial.print(">"); Serial.print(bufferHead);Serial.print(":"); Serial.print(bufferTail); Serial.print(" ");  
        Serial.print(pbuf);
      #endif 
   // добавить строку в журнал
   return _write(pbuf);
  }

//ПРОММТ вставляется в начале журнала а далее печать в консоль и журнал возвращает число записанных байт с типом промта
uint16_t Journal::jprintf(type_promt pr,const char *format, ...)
{
  uint16_t len_promt=0;
  switch (pr)
  {
  case  pP_NONE: len_promt=0;break;                                                           // ничего
  case  pP_TIME: len_promt=jprintf((char*)"%s ",NowTimeToStr()); break;                       // время
  case  pP_DATE: len_promt=jprintf((char*)"%s %s ",NowDateToStr(),NowTimeToStr()); break;     // дата и время
  case  pP_USER: len_promt=jprintf((char*)promtUser); break;                                  // константа определяемая пользователем
  default: len_promt=0;break;
  }
     va_list ap;
      if (strlen(format)>PRINTF_BUF-10) strcpy(pbuf,MessageLongString);   // Слишком длинная строка
      else
      {
      va_start(ap, format);
      vsnprintf(pbuf, sizeof(pbuf), format, ap);
      va_end(ap);
      }
      #ifdef DEBUG
       Serial.print(pbuf);
      #endif 
    return _write(pbuf)+len_promt;   // добавить строку в журнал
}   
  
// Печать ТОЛЬКО в журнал возвращает число записанных байт для использования в критических секциях кода
uint16_t Journal::jprintf_only(const char *format, ...)
{
  va_list ap;
    if (strlen(format)>PRINTF_BUF-10) strcpy(pbuf,MessageLongString);   // Слишком длинная строка
    {
    va_start(ap, format);
    vsnprintf(pbuf, sizeof(pbuf), format, ap);
    va_end(ap);
    }
   return _write(pbuf);   
}

// отдать журнал в сеть клиенту  Возвращает число записанных байт
uint16_t Journal::send_Data(uint8_t thread)
  {
    uint16_t i,num,len,sum=0;
   #ifdef I2C_EEPROM_64KB // чтение еепром
    num=bufferHead+1;                                                                          // Начинаем с начала журнала, num позиция в буфере пропуская символ начала
    for(i=0;i<(JOURNAL_LEN/W5200_MAX_LEN+1);i++)                                             // Передаем пакетами по W5200_MAX_LEN байт, может быть два неполных пакета!!
     {
      if((num>bufferTail))                                                                     // Текущая позиция больше хвоста (начало передачи)
        {
        if (JOURNAL_LEN-num>=W5200_MAX_LEN) len=W5200_MAX_LEN; else len=JOURNAL_LEN-num;   // Контроль достижения границы буфера
        }
      else                                                                                   // Текущая позиция меньше хвоста (конец передачи)
        {
          if (bufferTail-num>=W5200_MAX_LEN) len=W5200_MAX_LEN; else len=bufferTail-num;     // Контроль достижения хвоста журнала
        }
     if (readEEPROM_I2C(I2C_JOURNAL_START+num, (byte*)&bufI2C,len))                           // чтение из памяти
       { err=ERR_READ_I2C_JOURNAL; 
         #ifdef DEBUG
            Serial.print(errorReadI2C); 
         #endif
         return 0;
       };     
  //    Serial.println(strlen(bufI2C)); 
  //    Serial.println(bufI2C);  
      if(sendPacketRTOS(thread,(byte*)bufI2C,len,0)==0) return 0 ;                          // передать пакет, при ошибке выйти
      sum=sum+len;                                                                           // сколько байт передано
      if (sum>=available()) break;                                                           // Все передано уходим
      num=num+len;                                                                           // Указатель на переданные данные
      if (num>=JOURNAL_LEN) num=0;                                                           // переходим на начало
    }  // for
   #else
    num=bufferHead;                                                                          // Начинаем с начала журнала, num позиция в буфере
    for(i=0;i<(JOURNAL_LEN/W5200_MAX_LEN+1);i++)                                             // Передаем пакетами по W5200_MAX_LEN байт, может быть два неполных пакета!!
     {
      if((num>bufferTail))                                                                   // Текущая позиция больше хвоста (начало передачи)
        {
        if (JOURNAL_LEN-num>=W5200_MAX_LEN) len=W5200_MAX_LEN; else len=JOURNAL_LEN-num;     // Контроль достижения границы буфера
        }
      else                                                                                   // Текущая позиция меньше хвоста (конец передачи)
        {
          if (bufferTail-num>=W5200_MAX_LEN) len=W5200_MAX_LEN; else len=bufferTail-num;     // Контроль достижения хвоста журнала
        }
      if(sendPacketRTOS(thread,(byte*)_data+num,len,0)==0) return 0 ;                         // передать пакет, при ошибке выйти
      sum=sum+len;                                                                           // сколько байт передано
      if (sum>=available()) break;                                                           // Все передано уходим
      num=num+len;                                                                           // Указатель на переданные данные
      if (num>=JOURNAL_LEN) num=0;                                                           // переходим на начало
    }  // for
  #endif  
return sum;
  }

// Возвращает размер журнала
uint16_t Journal::available(void)
{ 
  #ifdef I2C_EEPROM_64KB
    if (full) return JOURNAL_LEN; else return bufferTail-1;
  #else   
     if (full) return JOURNAL_LEN; else return bufferTail;
  #endif
}    
                 
// чтобы print рабоtал для это класса
size_t Journal::write (uint8_t c)
  {
  Serial.print(char(c));  
  return 1;   // one byte output
  }  // end of myOutputtingClass::write
         
// Записать строку в журнал
uint16_t Journal::_write(char *dataPtr)
    {
      uint16_t numBytes;
      numBytes=strlen(dataPtr);
      if( dataPtr == 0 || numBytes == 0 )  return 0;  // Записывать нечего
  #ifdef I2C_EEPROM_64KB // запись в еепром
      if( numBytes >= JOURNAL_LEN-2 )  numBytes = JOURNAL_LEN-2;  // Ограничиваем размером журнала JOURNAL_LEN не забываем про два служебных символа
     // Запись в eeprom
      if(numBytes > (JOURNAL_LEN - bufferTail-1))       //  Запись в два приема если число записываемых бит больше чем место от конца очереди до конца буфера ( помним про символ начала)
      {
        int len =  JOURNAL_LEN - bufferTail-1;          // сколько можно записать в конец
        if (writeEEPROM_I2C(I2C_JOURNAL_START+bufferTail, (byte*)dataPtr,len)) 
           { err=ERR_WRITE_I2C_JOURNAL; 
             #ifdef DEBUG
             Serial.print(errorWriteI2C);
             #endif
           }
        if (writeEEPROM_I2C(I2C_JOURNAL_START, (byte*)dataPtr+len,numBytes-len)) 
           { err=ERR_WRITE_I2C_JOURNAL; 
             #ifdef DEBUG
             Serial.print(errorWriteI2C);
             #endif
           }          
            bufferTail = numBytes-len+1;                     // Хвост начинает рости с начала буфера
            writeTAIL();                                     // записать символ конца
            bufferHead=bufferTail +1;                        // Буфер полный по этому начало стоит сразу за концом (затирание данных)
            writeHEAD();                                     // записать символ начала
            full=true;                                       // буфер полный
      }
      else   // Запись в один прием Буфер не полный
      {     
             if (writeEEPROM_I2C(I2C_JOURNAL_START+bufferTail,(byte*)dataPtr,numBytes)) 
               { err=ERR_WRITE_I2C_JOURNAL; 
                 #ifdef DEBUG
                 Serial.print(errorWriteI2C);
                 #endif
               }  
             bufferTail = bufferTail + numBytes;               // Хвост вырос
             writeTAIL();                                      // записать символ конца
             if (full){ bufferHead=bufferTail+1;writeHEAD(); }  // голова изменяется только при полном буфере (затирание данных)
     //        else {bufferHead=0;writeHEAD();}
      }     
 #else   // Запись в память
     // Serial.print(">"); Serial.print(numBytes); Serial.println("<");
    
      if( numBytes >= JOURNAL_LEN )  numBytes = JOURNAL_LEN;  // Ограничиваем размером журнала
      // Запись в журнал
      if(numBytes > JOURNAL_LEN -  bufferTail) //  Запись в два приема если число записываемых бит больше чем место от конца очереди до конца буфера
      {
            int len =  JOURNAL_LEN - bufferTail;             // сколько можно записать в конец
            memcpy(_data+bufferTail,dataPtr,len);            // Пишем с конца очереди но до конца журнала
            memcpy(_data, dataPtr+len, numBytes-len);        // Пишем в конец буфера с начала
            bufferTail = numBytes-len;                       // Хвост начинает рости с начала буфера
            bufferHead=bufferTail +1;                        // Буфер полный по этому начало стоит сразу за концом (затирание данных)
            full=true;                                       // буфер полный
      }
      else   // Запись в один прием Буфер
      {
            memcpy(_data+bufferTail, dataPtr, numBytes);     // Пишем с конца очереди
            bufferTail = bufferTail + numBytes;              // Хвост вырос
            if (full) bufferHead=bufferTail+1;               // голова изменяется только при полном буфере (затирание данных)
            else bufferHead=0;
      }
  #endif  
     return numBytes;
    }

    
// ---------------------------------------------------------------------------------
//  Класс ГРАФИКИ    ------------------------------------------------------------
// ---------------------------------------------------------------------------------

 // Инициализация
void statChart::init(boolean pres)
{  
  err=OK; 
  present=pres;                                 // наличие статистики - зависит от конфигурации
  pos=0;                                        // текущая позиция для записи
  num=0;                                        // число накопленных точек
  flagFULL=false;                               // false в буфере менее CHART_POINT точек
  if (pres)                                     // отводим память если используем статистику
  { 
    data=(int16_t*)malloc(sizeof(int16_t)*CHART_POINT);
    if (data==NULL) {err=ERR_OUT_OF_MEMORY; set_Error(err,(char*)__FUNCTION__);return;}  // ОШИБКА если память не выделена
    for(int i=0;i<CHART_POINT;i++) data[i]=0;     // обнуление
  }  
}

// Очистить статистику
void statChart::clear()
{   
  pos=0;                                        // текущая позиция для записи
  num=0;                                        // число накопленных точек
  flagFULL=false;                               // false в буфере менее CHART_POINT точек
  for(int i=0;i<CHART_POINT;i++) data[i]=0;     // обнуление
}

 // добавить точку в массиве
void statChart::addPoint(int16_t y)
{
 if (!present) return; 
 data[pos]=y;
 if (pos<CHART_POINT-1) pos++; else { pos=0; flagFULL=true; }
 if (!flagFULL) num++ ;   // буфер пока не полный
}

// получить точку нумерация 0-самая новая CHART_POINT-1 - самая старая, (работает кольцевой буфер)
int16_t statChart::get_Point(uint16_t x)
{
  if (!present) return 0; 
//  if (num<CHART_POINT) return data[x];
//  else if ((pos+x)<CHART_POINT) return data[pos+x]; 
//       else return data[pos+x-CHART_POINT];
 if (!flagFULL) return data[x];
 else 
 {
    if ((pos+x)<CHART_POINT) return data[pos+x]; 
       else return data[pos+x-CHART_POINT];
 }
}

// БИНАРНЫЕ данные по маске: получить точку нумерация 0-самая старая CHART_POINT - самая новая, (работает кольцевой буфер)
boolean statChart::get_boolPoint(uint16_t x,uint16_t mask)  
{ 
 if (!present) return 0; 
 if (!flagFULL) return data[x]&mask?true:false;
 else 
 {
    if ((pos+x)<CHART_POINT) return data[pos+x]&mask?true:false; 
    else                     return data[pos+x-CHART_POINT]&mask?true:false;
 }
}

// получить строку в которой перечислены все точки в строковом виде через; при этом значения делятся на m
// строка не обнуляется перед записью
char *statChart::get_PointsStr(uint16_t m, char *b)
{ 
  char buf[10];
  if ((!present)||(num==0)) return (char*)";";
  for(int i=0;i<num;i++) 
  {
    // strcat(b,ftoa(buf,(float)data[i]/m, 2)); 
    strcat(b,ftoa(buf,(float)get_Point(i)/m, 2));
    strcat(b,(char*)";"); 
  }
  return b; 
}
// БИНАРНЫЕ данные: получить строку в которой перечислены все точки через ";"
//при этом на значения накладывается mask (0 или 1)
//char *get_boolPointsStr(uint16_t mask,  char *b)
//{
  
//}

// ---------------------------------------------------------------------------------
//  Класс Профиль ТН    ------------------------------------------------------------
// ---------------------------------------------------------------------------------
// Класс предназначен для работы с настройками ТН. в пямяти хранится текущий профиль, ав еепром можно хранить до 10 профилей, и загружать их по одному в пямять
// также есть функции по сохраннию и удалению из еепром
// номер текущего профиля хранится в структуре type_SaveON
// инициализация профиля
void Profile::initProfile()
{
  err=OK;
  magic=0xaa;
  crc16=0;
  strcpy(dataProfile.name,"unknow");
  strcpy(dataProfile.note,"default profile");
  dataProfile.flags=0x00;
  dataProfile.len=get_sizeProfile();
  dataProfile.id=0;
  
  // Состояние ТН структура SaveON
  SaveON.magic=0x55;                   // признак данных, должно быть  0x55
  SETBIT0(SaveON.flags,fHP_ON);        // насос выключен
  SETBIT0(SaveON.flags,fBoilerON);     // Бойлер выключен
  SaveON.startTime=0;                  // нет времени включения
  SaveON.mode=pOFF;                    // выключено
  
  // Охлаждение
  Cool.Rule=pHYSTERESIS,               // алгоритм гистерезис, интервальный режим
  Cool.Temp1=2011;                     // Целевая температура дома
  Cool.Temp2=2011;                     // Целевая температура Обратки
  SETBIT0(Cool.flags,fTarget);         // Что является целью ПИД - значения true (температура в доме), false (температура обратки).
  SETBIT0(Cool.flags,fWeather);        // флаг Погодозависмости
  Cool.dTemp=100;                      // Гистерезис целевой температуры
  Cool.dTempDay=100;                   // Гистерезис целевой температуры дневной тариф
  Cool.time=60;                        // Постоянная интегрирования времени в секундах ПИД ТН
  Cool.Kp=10;                          // Пропорциональная составляющая ПИД ТН
  Cool.Ki=1;                           // Интегральная составляющая ПИД ТН
  Cool.Kd=5;                           // Дифференциальная составляющая ПИД ТН
 // Защиты
  Cool.tempIn=1200;                    // Tемпература подачи (минимальная или минимальная)
  Cool.tempOut=3500;                   // Tемпература обратки (минимальная или минимальная)
  Cool.pause=5*60;                     // Минимальное время простоя компрессора минуты
  Cool.dt=1500;                        // Максимальная разность температур конденсатора.
  Cool.tempPID=2200;                   // Целевая температура ПИД
  Cool.kWeather=10;                    // Коэффициент погодозависимости в СОТЫХ градуса на градус
  
 // Cool.P1=0;
  
// Отопление
  Heat.Rule=pHYSTERESIS,               // алгоритм гистерезис, интервальный режим
  Heat.Temp1=2210;                     // Целевая температура дома
  Heat.Temp2=3610;                     // Целевая температура Обратки
  SETBIT0(Heat.flags,fTarget);         // Что является целью ПИД - значения true (температура в доме), false (температура обратки).
  SETBIT0(Heat.flags,fWeather);        // флаг Погодозависмости
  Heat.dTemp=100;                      // Гистерезис целевой температуры
  Heat.dTempDay=100;                   // Гистерезис целевой температуры дневной тариф
  Heat.time=60;                        // Постоянная интегрирования времени в секундах ПИД ТН
  Heat.Kp=10;                          // Пропорциональная составляющая ПИД ТН
  Heat.Ki=0;                           // Интегральная составляющая ПИД ТН
  Heat.Kd=5;                           // Дифференциальная составляющая ПИД ТН
 // Защиты
  Heat.tempIn=3800;                    // Tемпература подачи (минимальная или минимальная)
  Heat.tempOut=2000;                   // Tемпература обратки (минимальная или минимальная)
  Heat.pause=5*60;                     // Минимальное время простоя компрессора минуты
  Heat.dt=1500;                        // Максимальная разность температур конденсатора.
  Heat.tempPID=3000;                   // Целевая температура ПИД
  Heat.kWeather=10;                    // Коэффициент погодозависимости в СОТЫХ градуса на градус
  
 // Heat.P1=0;
 
 // Бойлер
  SETBIT0(Boiler.flags,fSchedule);      // !save! флаг Использование расписания выключено
  SETBIT0(Boiler.flags,fAddHeating);    // !save! флаг использование ТЭН для нагрева  выключено
  SETBIT0(Boiler.flags,fSalmonella);    // !save! флаг Сальмонела раз внеделю греть бойлер  выключено
  SETBIT0(Boiler.flags,fCirculation);   // !save! флагУправления циркуляционным насосом ГВС  выключено
  Boiler.TempTarget=5000;               // !save! Целевая температура бойлера
  Boiler.dTemp=500;                     // !save! гистерезис целевой температуры
  Boiler.tempIn=6500;                   // !save! Tемпература подачи максимальная
  Boiler.pause=5*60;                    // !save! Минимальное время простоя компрессора в секундах
  for (uint8_t i=0;i<7; i++) 
      Boiler.Schedule[i]=0;             // !save! Расписание бойлера
  Boiler.Circul_Work=60*3;              // Время  работы насоса ГВС секунды (fCirculation)
  Boiler.Circul_Pause=60*10;            // Пауза в работе насоса ГВС  секунды (fCirculation)
  SETBIT0(Boiler.flags,fResetHeat);     // флаг Сброса лишнего тепла в СО
  Boiler.Reset_Time=60*6;               // время сброса излишков тепла в секундах (fResetHeat)
  Boiler.time=20;                       // Постоянная интегрирования времени в секундах ПИД ГВС
  Boiler.Kp=3;                          // Пропорциональная составляющая ПИД ГВС
  Boiler.Ki=1;                          // Интегральная составляющая ПИД ГВС
  Boiler.Kd=0;                          // Дифференциальная составляющая ПИД ГВС
  Boiler.tempPID=3800;                  // Целевая температура ПИД ГВС
}

// Охлаждение Установить параметры ТН из числа (float)
boolean Profile::set_paramCoolHP(PARAM_HP p, float x)
{ 
 switch (p)
   {
  case  pRULE:    switch ((int)x)
                   {
                    case 0: Cool.Rule=pHYSTERESIS; return true; break;
                    case 1: Cool.Rule=pPID;        return true; break;
                    case 2: Cool.Rule=pHYBRID;     return true; break;
                    default:Cool.Rule=pHYSTERESIS; return true; break;  
                   }                                                                                                                    break;             // целевая температура в доме
  case  pTEMP1:   if ((x>=0.0)&&(x<=30.0))  {Cool.Temp1=x*100.0; return true;} else return false;                                       break;             // целевая температура обратки
  case  pTEMP2:   if ((x>=10.0)&&(x<=50.0))  {Cool.Temp2=x*100.0; return true;} else return false;                                      break;             // целевая температура
  case  pTARGET:  if (x==0.0) {SETBIT0(Cool.flags,fTarget); return true;} else if (x==1.0) {SETBIT1(Cool.flags,fTarget); return true;} else return false; break; // что является целью значения  0 (температура в доме), 1 (температура обратки).
  case  pDTEMP:   if ((x>=0.0)&&(x<=12.0))  {Cool.dTemp=x*100.0; return true;} else return false;                                       break;             // гистерезис целевой температуры
  case  pHP_TIME: if ((x>=10)&&(x<=600))     {Cool.time=x; return true;} else return false;                                             break;             // Постоянная интегрирования времени в секундах ПИД ТН !
  case  pHP_PRO:  if ((x>=0.0)&&(x<=100.0)) {Cool.Kp=x; return true;} else return false;                                                break;             // Пропорциональная составляющая ПИД ТН
  case  pHP_IN:   if ((x>=0.0)&&(x<=20.0))  {Cool.Ki=x; return true;} else return false;                                                break;             // Интегральная составляющая ПИД ТН
  case  pHP_DIF:  if ((x>=0.0)&&(x<=10.0))  {Cool.Kd=x; return true;} else return false;                                                break;             // Дифференциальная составляющая ПИД ТН
  case  pTEMP_IN: if ((x>=0.0)&&(x<=30.0))  {Cool.tempIn=x*100.0; return true;} else return false;                                      break;             // температура подачи (минимальная)
  case  pTEMP_OUT:if ((x>=0.0)&&(x<=35.0))  {Cool.tempOut=x*100.0; return true;} else return false;                                     break;             // температура обратки (максимальная)
  case  pPAUSE:   if ((x>=5)&&(x<=60))      {Cool.pause=x*60; return true;} else return false;                                          break;             // минимальное время простоя компрессора спереводом в минуты но хранится в секундах!!!!!
  case  pD_TEMP:  if ((x>=0.0)&&(x<=40.0))  {Cool.dt=x*100.0; return true;} else return false;                                          break;             // максимальная разность температур конденсатора.
  case  pTEMP_PID:if ((x>=0.0)&&(x<=30.0))  {Cool.tempPID=x*100.0; return true;} else return false;                                     break;             // Целевая темпеартура ПИД
  case  pWEATHER: if (x==0.0) {SETBIT0(Cool.flags,fWeather); return true;} else if (x==1.0) {SETBIT1(Cool.flags,fWeather); return true;} else return false; break;     // Использование погодозависимости
  case  pK_WEATHER:if ((x>=0.0)&&(x<=1.0)) {Cool.kWeather=(int)(x*1000.0); return true;} else return false;                             break;             // Коэффициент погодозависимости

 // case  pP1:      Cool.P1=x;                                                                                                          break;             // резервный параметр 1
  default:        return  false;                                                                                                        break;   
   }
return false; 
}

//Охлаждение Получить параметр в виде строки  второй параметр - наличие частотника
char* Profile::get_paramCoolHP(PARAM_HP p, boolean fc)
{
char static temp[16];

 switch (p)
   {
  case  pRULE:     if (fc)   // Есть частотник
                   switch (Cool.Rule)       // алгоритм работы
                   {
                    case 0: return (char*)"HYSTERESIS:1;PID:0;HYBRID:0;"; break;
                    case 1: return (char*)"HYSTERESIS:0;PID:1;HYBRID:0;"; break;
                    case 2: return (char*)"HYSTERESIS:0;PID:0;HYBRID:1;"; break; 
                    default:Cool.Rule=pHYSTERESIS; return (char*)"HYSTERESIS:1;PID:0;HYBRID:0;"; break;// исправить
                   }                                                         
                  else {Cool.Rule=pHYSTERESIS;return (char*)"HYSTERESIS:1;";}break;             // частотника нет единсвенный алгоритм гистрезис
  case  pTEMP1:   return ftoa(temp,(float)Cool.Temp1/100.0,1);               break;             // целевая температура в доме
  case  pTEMP2:   return ftoa(temp,(float)Cool.Temp2/100.0,1);               break;             // целевая температура обратки
  case  pTARGET:  if (!(GETBIT(Cool.flags,fTarget))) return (char*)"Дом:1;Обратка:0;";
                                 else return (char*)"Дом:0;Обратка:1;";      break;             // что является целью значения  0 (температура в доме), 1 (температура обратки).
  case  pDTEMP:   return ftoa(temp,(float)Cool.dTemp/100.0,1);               break;             // гистерезис целевой температуры
  case  pHP_TIME: return  int2str(Cool.time);                                break;             // Постоянная интегрирования времени в секундах ПИД ТН
  case  pHP_PRO:  return  int2str(Cool.Kp);                                  break;             // Пропорциональная составляющая ПИД ТН
  case  pHP_IN:   return  int2str(Cool.Ki);                                  break;             // Интегральная составляющая ПИД ТН
  case  pHP_DIF:  return  int2str(Cool.Kd);                                  break;             // Дифференциальная составляющая ПИД ТН
  case  pTEMP_IN: return ftoa(temp,(float)Cool.tempIn/100.0,1);              break;             // температура подачи (минимальная)
  case  pTEMP_OUT:return ftoa(temp,(float)Cool.tempOut/100.0,1);             break;             // температура обратки (максимальная)
  case  pPAUSE:   return  int2str(Cool.pause/60);                            break;             // минимальное время простоя компрессора спереводом в минуты но хранится в секундах!!!!!
  case  pD_TEMP:  return ftoa(temp,(float)Cool.dt/100.0,1);                  break;             // максимальная разность температур конденсатора.
  case  pTEMP_PID:return ftoa(temp,(float)Cool.tempPID/100.0,1);             break;             // Целевая темпеартура ПИД
  case  pWEATHER: if (GETBIT(Cool.flags,fWeather)) return (char*)cOne;else return (char*)cZero; // Использование погодозависимости
  case  pK_WEATHER:return ftoa(temp,(float)Cool.kWeather/1000.0,2);          break;             // Коэффициент погодозависимости
//  case  pP1:      return int2str(Cool.P1);                                   break;             // резервный параметр 1
  
  case  pEND:     return (char*)"end";                                       break;             // Обязательно должен быть последним, добавляем ПЕРЕД!!!
  default:        return  (char*)cInvalid;                                  break;   
   }
 return  (char*)cInvalid;   
}

// Отопление Установить параметры ТН из числа (float)
boolean Profile::set_paramHeatHP(PARAM_HP p, float x)
{ 

 switch (p)
   {
  case  pRULE:    switch ((int)x)
                   {
                    case 0: Heat.Rule=pHYSTERESIS; return true; break;
                    case 1: Heat.Rule=pPID;        return true; break;
                    case 2: Heat.Rule=pHYBRID;     return true; break;
                    default:Heat.Rule=pHYSTERESIS; return true; break;    
                   }                                                                                                                  break;             // целевая температура в доме
  case  pTEMP1:   if ((x>=0.0)&&(x<=30.0))  {Heat.Temp1=x*100.0; return true;} else return false;                                     break;             // целевая температура обратки
  case  pTEMP2:   if ((x>=10.0)&&(x<=50.0))  {Heat.Temp2=x*100.0; return true;} else return false;                                    break;             // целевая температура
  case  pTARGET:  if (x==0) { SETBIT0(Heat.flags,fTarget); return true;} else if (x==1) { SETBIT1(Heat.flags,fTarget); return true;} else return false;break; // что является целью значения  0 (температура в доме), 1 (температура обратки).
  case  pDTEMP:   if ((x>=0.0)&&(x<=12.0))  {Heat.dTemp=x*100.0; return true;} else return false;                                     break;             // гистерезис целевой температуры
  case  pHP_TIME: if ((x>=10)&&(x<=600))     {Heat.time=x; return true;} else return false;                                           break;             // Постоянная интегрирования времени в секундах ПИД ТН
  case  pHP_PRO:  if ((x>=0.0)&&(x<=100.0)) {Heat.Kp=x; return true;} else return false;                                              break;             // Пропорциональная составляющая ПИД ТН
  case  pHP_IN:   if ((x>=0.0)&&(x<=20.0))  {Heat.Ki=x; return true;} else return false;                                              break;             // Интегральная составляющая ПИД ТН
  case  pHP_DIF:  if ((x>=0.0)&&(x<=10.0))  {Heat.Kd=x; return true;} else return false;                                              break;             // Дифференциальная составляющая ПИД ТН
  case  pTEMP_IN: if ((x>=10.0)&&(x<=55.0))  {Heat.tempIn=x*100.0; return true;} else return false;                                   break;             // температура подачи (максимальная) отстанов компрессора
  case  pTEMP_OUT:if ((x>=10.0)&&(x<=50.0))  {Heat.tempOut=x*100.0; return true;} else return false;                                  break;             // температура обратки (минимальная) включение компрессора
  case  pPAUSE:   if ((x>=5)&&(x<=60))      {Heat.pause=x*60; return true;} else return false;                                        break;             // минимальное время простоя компрессора спереводом в минуты но хранится в секундах!!!!!
  case  pD_TEMP:  if ((x>2.0)&&(x<=40.0))   {Heat.dt=x*100.0; return true;} else return false;                                         break;             // максимальная разность температур конденсатора.
  case  pTEMP_PID:if ((x>=0.0)&&(x<=50.0))  {Heat.tempPID=x*100.0; return true;} else return false;                                   break;             // Целевая темпеартура ПИД
  case  pWEATHER: if (x==0.0) {SETBIT0(Heat.flags,fWeather); return true;} else if (x==1.0) {SETBIT1(Heat.flags,fWeather); return true;} else return false; break;   // Использование погодозависимости
  case  pK_WEATHER:if ((x>=0.0)&&(x<=1.0)) {Heat.kWeather=(int)(x*1000.0); return true;} else return false;                           break;             // Коэффициент погодозависимости
//  case  pP1:      Heat.P1=x;                                                                                                        break;             // резервный параметр 1

  default:        return  false;                                                                                                      break;   
   }
return false; 
}

// Отопление Получить параметр в виде строки  второй параметр - наличие частотника
char* Profile::get_paramHeatHP(PARAM_HP p, boolean fc)
{
char static temp[16];

 switch (p)
   {
 case  pRULE:     if (fc)   // Есть частотник
                  switch (Heat.Rule)      // алгоритм работы
                   {
                    case 0: return (char*)"HYSTERESIS:1;PID:0;HYBRID:0;"; break;
                    case 1: return (char*)"HYSTERESIS:0;PID:1;HYBRID:0;"; break;
                    case 2: return (char*)"HYSTERESIS:0;PID:0;HYBRID:1;"; break; 
                    default:Heat.Rule=pHYSTERESIS; return (char*)"HYSTERESIS:1;PID:0;HYBRID:0;"; break;// исправить
                   }  
                  else {Heat.Rule=pHYSTERESIS; return (char*)"HYSTERESIS:1;";}break;            // частотника нет единсвенный алгоритм гистрезис
  case  pTEMP1:   return ftoa(temp,(float)Heat.Temp1/100.0,1);               break;             // целевая температура в доме
  case  pTEMP2:   return ftoa(temp,(float)Heat.Temp2/100.0,1);               break;             // целевая температура обратки
  case  pTARGET:  if (!(GETBIT(Heat.flags,fTarget))) { return (char*)"Дом:1;Обратка:0;";}
                              else { return (char*)"Дом:0;Обратка:1;"; }     break;             // что является целью значения  0 (температура в доме), 1 (температура обратки).
  case  pDTEMP:   return ftoa(temp,(float)Heat.dTemp/100.0,1);               break;             // гистерезис целевой температуры
  case  pHP_TIME: return  int2str(Heat.time);                                break;             // Постоянная интегрирования времени в секундах ПИД ТН
  case  pHP_PRO:  return  int2str(Heat.Kp);                                  break;             // Пропорциональная составляющая ПИД ТН
  case  pHP_IN:   return  int2str(Heat.Ki);                                  break;             // Интегральная составляющая ПИД ТН
  case  pHP_DIF:  return  int2str(Heat.Kd);                                  break;             // Дифференциальная составляющая ПИД ТН
  case  pTEMP_IN: return ftoa(temp,(float)Heat.tempIn/100.0,1);              break;             // температура подачи (минимальная)
  case  pTEMP_OUT:return ftoa(temp,(float)Heat.tempOut/100.0,1);             break;             // температура обратки (максимальная)
  case  pPAUSE:   return  int2str(Heat.pause/60);                            break;             // минимальное время простоя компрессора спереводом в минуты но хранится в секундах!!!!!
  case  pD_TEMP:  return ftoa(temp,(float)Heat.dt/100.0,1);                  break;             // максимальная разность температур конденсатора.
  case  pTEMP_PID:return ftoa(temp,(float)Heat.tempPID/100.0,1);             break;             // Целевая темпеартура ПИД
  case  pWEATHER: if (GETBIT(Heat.flags,fWeather)) return (char*)cOne;else return (char*)cZero;    // Использование погодозависимости
  case  pK_WEATHER:return ftoa(temp,(float)Heat.kWeather/1000.0,2);          break;             // Коэффициент погодозависимости
//  case  pP1:      return int2str(.Heat.P1);                                break;             // резервный параметр 1

  case  pEND:     return (char*)"end";                                       break;             // Обязательно должен быть последним, добавляем ПЕРЕД!!!
  default:        return (char*)cInvalid;                                   break;   
   }
 return  (char*)cInvalid;   
}

// Настройка бойлера --------------------------------------------------
//Установить параметр из строки
boolean Profile::set_boiler(BOILER_HP p, char *c)
{ 
 float x;
 x=my_atof(c);
// Serial.print(x);Serial.print(" ");Serial.println(c);
 if ((x==ATOF_ERROR)&&(p!=pSCHEDULER)) return false;   // Ошибка преобразования короме расписания - это не число
 
 switch (p)
   {
    case pBOILER_ON:   if (strcmp(c,cZero)==0)      { SETBIT0(SaveON.flags,fBoilerON); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(SaveON.flags,fBoilerON);  return true;}
                       else return false;  
                       break; 
    case pSCHEDULER_ON:if (strcmp(c,cZero)==0)      { SETBIT0(Boiler.flags,fSchedule); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Boiler.flags,fSchedule);  return true;}
                       else return false;  
                       break; 
    case pADD_HEATING: if (strcmp(c,cZero)==0)      { SETBIT0(Boiler.flags,fAddHeating); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Boiler.flags,fAddHeating);  return true;}
                       else return false;  
                       break; 
    case pSALLMONELA:  if (strcmp(c,cZero)==0)      { SETBIT0(Boiler.flags,fSalmonella); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Boiler.flags,fSalmonella);  return true;}
                       else return false;  
                       break; 
    case pCIRCULATION: if (strcmp(c,cZero)==0)      { SETBIT0(Boiler.flags,fCirculation); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Boiler.flags,fCirculation);  return true;}
                       else return false;  
                       break; 
    case pTEMP_TARGET: if ((x>=5)&&(x<=60))       {Boiler.TempTarget=x*100.0; return true;} else return false;       // Целевай температура бойлера
                       break;             
    case pDTARGET:     if ((x>=1)&&(x<=20))       {Boiler.dTemp=x*100.0; return true;} else return false;            // гистерезис целевой температуры
                       break;      
    case pTEMP_MAX:    if ((x>=20)&&(x<=70))      {Boiler.tempIn=x*100.0; return true;} else return false;           // Tемпература подачи максимальная
                       break; 
    case pPAUSE1:      if ((x>=3)&&(x<=20))       {Boiler.pause=x*60; return true;} else return false;               // Минимальное время простоя компрессора в секундах
                       break;                                                         

    case pSCHEDULER:  return set_Schedule(c,Boiler.Schedule); break;                                                  // разбор строки расписания

    case pCIRCUL_WORK: if ((x>=0)&&(x<=60))   {Boiler.Circul_Work=60*x; return true;} else return false;             // Время  работы насоса ГВС секунды (fCirculation)
                       break; 
                        
    case pCIRCUL_PAUSE: if ((x>=0)&&(x<=60))   {Boiler.Circul_Pause=60*x; return true;} else return false;            // Пауза в работе насоса ГВС  секунды (fCirculation)
                       break;  
    case pRESET_HEAT: if (strcmp(c,cZero)==0)       { SETBIT0(Boiler.flags,fResetHeat); return true;}                   // флаг Сброса лишнего тепла в СО
                       else if (strcmp(c,cOne)==0) { SETBIT1(Boiler.flags,fResetHeat);  return true;}
                       else return false;  
                       break; 
    case pRESET_TIME: if ((x>=3)&&(x<=20))   {Boiler.Reset_Time=60*x; return true;} else return false;                // время сброса излишков тепла в секундах (fResetHeat)
                       break;      
    case pBOIL_TIME:   if ((x>=5)&&(x<=300))     {Boiler.time=x; return true;} else return false;  break;             // Постоянная интегрирования времени в секундах ПИД ГВС
    case pBOIL_PRO:    if ((x>=0.0)&&(x<=100.0)) {Boiler.Kp=x; return true;} else return false;  break;               // Пропорциональная составляющая ПИД ГВС
    case pBOIL_IN:     if ((x>=0.0)&&(x<=20.0))  {Boiler.Ki=x; return true;} else return false;  break;               // Интегральная составляющая ПИД ГВС
    case pBOIL_DIF:    if ((x>=0.0)&&(x<=10.0))  {Boiler.Kd=x; return true;} else return false;   break;              // Дифференциальная составляющая ПИД ГВС
    case pBOIL_TEMP:   if ((x>=30.0)&&(x<=60))   {Boiler.tempPID=x*100.0; return true;} else return false;            // Целевая темпеартура ПИД ГВС
                                                                                                
    default:       return false;                               break;   
   }
  return false;
}

//Получить параметр из строки
char* Profile::get_boiler(BOILER_HP p)
{
  char static temp[16];   
  switch (p)
   {
    case pBOILER_ON:       if (GETBIT(SaveON.flags,fBoilerON))   return  (char*)cOne; else return  (char*)cZero; break;
    case pSCHEDULER_ON:    if (GETBIT(Boiler.flags,fSchedule))   return  (char*)cOne; else return  (char*)cZero; break;
    case pADD_HEATING:     if (GETBIT(Boiler.flags,fAddHeating)) return  (char*)cOne; else return  (char*)cZero; break;
    case pSALLMONELA:      if (GETBIT(Boiler.flags,fSalmonella)) return  (char*)cOne; else return  (char*)cZero; break;
    case pCIRCULATION:     if (GETBIT(Boiler.flags,fCirculation))return  (char*)cOne; else return  (char*)cZero; break;
    case pTEMP_TARGET:     return ftoa(temp,(float)Boiler.TempTarget/100.0,1);      break;             
    case pDTARGET:         return ftoa(temp,(float)Boiler.dTemp/100.0,1);           break;      
    case pTEMP_MAX:        return ftoa(temp,(float)Boiler.tempIn/100.0,1);          break; 
    case pPAUSE1:          return int2str(Boiler.pause/60);                         break;                                                         
    case pSCHEDULER:       return get_Schedule(Boiler.Schedule);                    break;  
    case pCIRCUL_WORK:     return int2str(Boiler.Circul_Work/60);                   break;                            // Время  работы насоса ГВС секунды (fCirculation)
    case pCIRCUL_PAUSE:    return int2str(Boiler.Circul_Pause/60);                  break;                            // Пауза в работе насоса ГВС  секунды (fCirculation)
    case pRESET_HEAT:      if (GETBIT(Boiler.flags,fResetHeat))   return  (char*)cOne; else return  (char*)cZero; break;       // флаг Сброса лишнего тепла в СО
    case pRESET_TIME:      return int2str(Boiler.Reset_Time/60);                    break;                            // время сброса излишков тепла в секундах (fResetHeat)
    case pBOIL_TIME:       return  int2str(Boiler.time);                            break;                            // Постоянная интегрирования времени в секундах ПИД ГВС
    case pBOIL_PRO:        return  int2str(Boiler.Kp);                              break;                            // Пропорциональная составляющая ПИД ГВС
    case pBOIL_IN:         return  int2str(Boiler.Ki);                              break;                            // Интегральная составляющая ПИД ГВС
    case pBOIL_DIF:        return  int2str(Boiler.Kd);                              break;                            // Дифференциальная составляющая ПИД ГВС
    case pBOIL_TEMP:       return ftoa(temp,(float)Boiler.tempPID/100.0,1);         break;                            // Целевая темпеартура ПИД ГВС
    default:               return false;                                            break;   
   }
 return (char*)cInvalid;
}

// Порядок записи профиля
/*
 * sizeof(magic) + \
   sizeof(crc16) + \
   // данные контрольная сумма считается с этого места
   sizeof(dataProfile) + \
   sizeof(SaveON) + \
   sizeof(Cool)  + \
   sizeof(Heat)  + \
   sizeof(Boiler)+ \
 */
// static uint16_t crc= 0xFFFF;  // рабочее значение
 uint16_t  Profile::get_crc16_mem()  // Расчитать контрольную сумму
 {
 uint16_t i;
  crc= 0xFFFF;
  for(i=0;i<sizeof(dataProfile);i++) crc=_crc16(crc,*((byte*)&dataProfile+i));           // CRC16 структуры  dataProfile
  for(i=0;i<sizeof(SaveON);i++) crc=_crc16(crc,*((byte*)&SaveON+i));                     // CRC16 структуры  SaveON
  for(i=0;i<sizeof(Cool);i++) crc=_crc16(crc,*((byte*)&Cool+i));                         // CRC16 структуры  Cool
  for(i=0;i<sizeof(Heat);i++) crc=_crc16(crc,*((byte*)&Heat+i));                         // CRC16 структуры  Heat
  for(i=0;i<sizeof(Boiler);i++) crc=_crc16(crc,*((byte*)&Boiler+i));                     // CRC16 структуры  Boiler
  return crc;   
 }

// Проверить контрольную сумму ПРОФИЛЯ в EEPROM для данных на выходе ошибка, длина определяется из заголовка
int8_t Profile::check_crc16_eeprom(int8_t num)
{
  uint16_t i, crc16tmp;
  byte x;
  crc= 0xFFFF;
  int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;     // вычислить адрес начала данных
  
  if (readEEPROM_I2C(adr, (byte*)&x, sizeof(x))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(x);              // прочитать заголовок
  if (x==!0xaa)   return OK;                                                                                                                              // профиль пустой, или его вообще нет, контрольная сумма не актуальна
//  if (x!=0xaa)  { set_Error(ERR_HEADER_PROFILE,(char*)nameHeatPump); return ERR_HEADER_PROFILE;}                                                       // Заголовок не верен, данных нет
  if (readEEPROM_I2C(adr, (byte*)&crc16tmp, sizeof(crc16tmp))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(crc16tmp);        // прочитать crc16
  
  for (i=0;i<dataProfile.len-1-2;i++) {if (readEEPROM_I2C(adr+i, (byte*)&x, sizeof(x))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  crc=_crc16(crc,x);} // расчет - записанной в еепром сумму за вычетом заголовка
  if (crc==crc16tmp) return OK; 
  else            return err=ERR_CRC16_PROFILE;
}
// Проверить контрольную сумму в буфере для данных на выходе ошибка, длина определяется из заголовка
int8_t  Profile::check_crc16_buf(int32_t adr, byte* buf)   
{
  uint16_t i, readCRC, crc=0xFFFF;
  byte x;
  memcpy((byte*)&x,buf+adr,sizeof(x)); adr=adr+sizeof(x);                                              // заголовок
  if (x==!0xaa)   return OK;                                                                           // профиль пустой, или его вообще нет, контрольная сумма не актуальна
 // if (x!=0xaa)   { set_Error(ERR_HEADER_PROFILE,(char*)nameHeatPump); return ERR_HEADER_PROFILE;}    // Заголовок не верен, данных нет
  memcpy((byte*)&readCRC,buf+adr,sizeof(readCRC)); adr=adr+sizeof(readCRC);                            // прочитать crc16
  // Расчет контрольной суммы
  for (i=0;i<dataProfile.len-1-2;i++) crc=_crc16(crc,buf[adr+i]);                                      // расчет -2 за вычетом CRC16 из длины и заголовка один байт
  if (crc==readCRC) return OK; 
  else              return err=ERR_CRC16_EEPROM;
}
 
// Записать профайл в еепром под номерм num
// Возвращает число записанных байт или ошибку
int16_t  Profile::save(int8_t num)
{
  magic=0xaa;                                   // Обновить заголовок
  dataProfile.len=get_sizeProfile();            // вычислить адрес начала данных
  dataProfile.saveTime=rtcSAM3X8.unixtime();   // запомнить время сохранения профиля
  
  int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;
  
  if (writeEEPROM_I2C(adr, (byte*)&magic, sizeof(magic))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(magic);       // записать заголовок
  int32_t adrCRC16=adr;                                                                                                                                              // Запомнить адрес куда писать контрольную сумму
  adr=adr+sizeof(crc16);                                                                                                                                             // пропуск записи контрольной суммы
  if (writeEEPROM_I2C(adr, (byte*)&dataProfile, sizeof(dataProfile))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(dataProfile);// записать данные
  if (writeEEPROM_I2C(adr, (byte*)&SaveON, sizeof(SaveON))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(SaveON);    // записать состояние ТН
  if (writeEEPROM_I2C(adr, (byte*)&Cool, sizeof(Cool))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(Cool);          // записать настройки охлаждения
  if (writeEEPROM_I2C(adr, (byte*)&Heat, sizeof(Heat))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(Heat);          // записать настройи отопления
  if (writeEEPROM_I2C(adr, (byte*)&Boiler, sizeof(Boiler))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  adr=adr+sizeof(Boiler);    // записать настройки ГВС
  // запись контрольной суммы
  crc16=get_crc16_mem();
  if (writeEEPROM_I2C(adrCRC16, (byte*)&crc16, sizeof(crc16))) {set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return err=ERR_SAVE_PROFILE;} 

  if ((err=check_crc16_eeprom(num))!=OK) { journal.jprintf(" Verification error, profile not write eeprom\n"); return (int16_t) err;}                            // ВЕРИФИКАЦИЯ Контрольные суммы не совпали
  journal.jprintf(" Save profile #%d to eeprom OK, write: %d bytes crc16: 0x%x\n",num,dataProfile.len,crc16);                                                        // дошли до конца значит ошибок нет
  update_list(num);                                                                                                                                                  // обновить список
  return dataProfile.len;
}

// загрузить профайл num из еепром память
int32_t Profile::load(int8_t num)
{
  byte x;
  int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;     // вычислить адрес начала данных
   
  if (readEEPROM_I2C(adr, (byte*)&x, sizeof(magic))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return err=ERR_LOAD_PROFILE;}  adr=adr+sizeof(magic);         // прочитать заголовок
 
  if (x==PROFILE_EMPTY) {journal.jprintf(" Profile #%d is empty\n",num); return OK;}                                                                                  // профиль пустой, загружать нечего, выходим
  if (x==!0xaa)  {journal.jprintf(" Profile #%d is bad format\n",num); return OK; }                                                                                   // профиль битый, читать нечего выходим

  #ifdef LOAD_VERIFICATION
    if ((err=check_crc16_eeprom(num))!=OK) { journal.jprintf(" Error load profile #%d from eeprom, CRC16 is wrong!\n",num); return err;}                           // проверка контрольной суммы перед чтением
  #endif
  
  if (readEEPROM_I2C(adr, (byte*)&crc16, sizeof(crc16))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(crc16);                   // прочитать crc16
  if (readEEPROM_I2C(adr, (byte*)&dataProfile, sizeof(dataProfile))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(dataProfile); // прочитать данные
  
  #ifdef LOAD_VERIFICATION
  if (dataProfile.len!=get_sizeProfile())  { set_Error(ERR_BAD_LEN_PROFILE,(char*)nameHeatPump); return err=ERR_BAD_LEN_PROFILE;}                                    // длины не совпали
  #endif
  
  // читаем основные данные
  if (readEEPROM_I2C(adr, (byte*)&SaveON, sizeof(SaveON))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(SaveON);     // прочитать состояние ТН
  if (readEEPROM_I2C(adr, (byte*)&Cool, sizeof(Cool))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(Cool);           // прочитать настройки охлаждения
  if (readEEPROM_I2C(adr, (byte*)&Heat, sizeof(Heat))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(Heat);           // прочитать настройки отопления
  if (readEEPROM_I2C(adr, (byte*)&Boiler, sizeof(Boiler))) { set_Error(ERR_LOAD_PROFILE,(char*)nameHeatPump); return ERR_LOAD_PROFILE;}  adr=adr+sizeof(Boiler);     // прочитать настройки ГВС
 
// ВСЕ ОК
   #ifdef LOAD_VERIFICATION
  // проверка контрольной суммы
  if(crc16!=get_crc16_mem()) { set_Error(ERR_CRC16_PROFILE,(char*)nameHeatPump); return err=ERR_CRC16_PROFILE;}                                                           // прочитать crc16
  if (dataProfile.len!=adr-(I2C_PROFILE_EEPROM+dataProfile.len*num))  {err=ERR_BAD_LEN_EEPROM;set_Error(ERR_BAD_LEN_EEPROM,(char*)nameHeatPump); return err;} // Проверка длины
    journal.jprintf(" Load profile #%d from eeprom OK, read: %d bytes crc16: 0x%x\n",num,adr-(I2C_PROFILE_EEPROM+dataProfile.len*num),crc16);
  #else
    journal.jprintf(" Load profile #%d from eeprom OK, read: %d bytes VERIFICATION OFF!\n",num,adr-(I2C_PROFILE_EEPROM+dataProfile.len*num));
  #endif
  update_list(num);     
  return adr;
 }

// Считать настройки из буфера на входе адрес с какого, на выходе код ошибки (меньше нуля)
int8_t Profile::loadFromBuf(int32_t adr,byte *buf)  
{
  uint16_t i;
  byte x;
  uint32_t aStart=adr;
   
  // Прочитать заголовок
  memcpy((byte*)&x,buf+adr,sizeof(magic)); adr=adr+sizeof(magic);                                                     // заголовок
  if (x==PROFILE_EMPTY) {journal.jprintf(" Profile of memory is empty\n"); return OK;}                                  // профиль пустой, загружать нечего, выходим
  if (x==!0xaa)  {journal.jprintf(" Profile of memory is bad format\n"); return OK; }                                   // профиль битый, читать нечего выходим

  // проверка контрольной суммы
  #ifdef LOAD_VERIFICATION 
  if ((err=check_crc16_buf(aStart,buf)!=OK)) {journal.jprintf(" Error load profile from file, crc16 is wrong!\n"); return err;}
  #endif 
  
  memcpy((byte*)&i,buf+adr,sizeof(i)); adr=adr+sizeof(i);                                                             // прочитать crc16
  
  // Прочитать переменные ТН
   memcpy((byte*)&dataProfile,buf+adr,sizeof(dataProfile)); adr=adr+sizeof(dataProfile);                              // прочитать структуры  dataProfile
   memcpy((byte*)&SaveON,buf+adr,sizeof(SaveON)); adr=adr+sizeof(SaveON);                                             // прочитать SaveON
   memcpy((byte*)&Cool,buf+adr,sizeof(Cool)); adr=adr+sizeof(Cool);                                                   // прочитать параметры охлаждения
   memcpy((byte*)&Heat,buf+adr,sizeof(Heat)); adr=adr+sizeof(Heat);                                                   // прочитать параметры отопления
   memcpy((byte*)&Boiler,buf+adr,sizeof(Boiler)); adr=adr+sizeof(Boiler);                                             // прочитать параметры бойлера
 
   #ifdef LOAD_VERIFICATION
    if (dataProfile.len!=adr-aStart)  {err=ERR_BAD_LEN_EEPROM;set_Error(ERR_BAD_LEN_EEPROM,(char*)nameHeatPump); return err;}    // Проверка длины
    journal.jprintf(" Load profile from file OK, read: %d bytes crc16: 0x%x\n",adr-aStart,i);                                                                    // ВСЕ ОК
  #else
    journal.jprintf(" Load setting from file OK, read: %d bytes VERIFICATION OFF!\n",adr-aStart);
  #endif
  return OK;       
}

 // Профиль Установить параметры ТН из числа (float)
boolean Profile::set_paramProfile(TYPE_PARAM_PROFILE  p, char *c)
{
 uint8_t x;
 switch (p)
   {
    case pNAME_PROFILE:
    					urldecode(dataProfile.name,c,sizeof(dataProfile.name));
                        return true;
    case ENABLE_PROFILE:
    					if (strcmp(c,cZero)==0) { SETBIT0(dataProfile.flags,fEnabled); return true;}
                        else if (strcmp(c,cOne)==0) { SETBIT1(dataProfile.flags,fEnabled);  return true;}
                        break;
    case ID_PROFILE:
						x = atoi(c);
						if(x >= I2C_PROFIL_NUM) break; // не верный номер профиля
						dataProfile.id = x;
						return true;
    case NOTE_PROFILE:  urldecode(dataProfile.note,c,sizeof(dataProfile.note));
                        return true;
    case DATE_PROFILE:  // параметры только чтение
    case CRC16_PROFILE: 
    case NUM_PROFILE:	return true;
    default:			break;
   }
  return false;
}
 // профиль Получить параметры
char*   Profile::get_paramProfile(TYPE_PARAM_PROFILE p)
{
switch (p)
   {
    case pNAME_PROFILE:  return dataProfile.name;                              break;    
    case ENABLE_PROFILE: if (GETBIT(dataProfile.flags,fEnabled)) return  (char*)cOne;
                         else      return  (char*)cZero;                         break;
    case ID_PROFILE:     return int2str(dataProfile.id);                       break; 
    case NOTE_PROFILE:   return  dataProfile.note;                             break;    
    case DATE_PROFILE:   return DecodeTimeDate(dataProfile.saveTime);          break;// параметры только чтение
    case CRC16_PROFILE:  return uint16ToHex(crc16);                            break;  
    case NUM_PROFILE:    return int2str(I2C_PROFIL_NUM);                       break;
//    case SEL_PROFILE:	 {
//    	for(uint8_t i = 0; i < I2C_PROFIL_NUM; i++) {
//    		int32_t adr=I2C_PROFILE_EEPROM + dataProfile.len * i;
//    	}
//    	break;
//    }
    default:             return  (char*)cInvalid;                             break;   
   }
  return  (char*)cInvalid;              
}

// Временные данные для профиля
static type_dataProfile temp_prof;

// ДОБАВЛЯЕТ к строке с - описание профиля num
char *Profile::get_info(char *c,int8_t num)  
{
  byte xx;
  uint16_t crc16temp;
  int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;                                            // вычислить адрес начала профиля
  if (readEEPROM_I2C(adr, (byte*)&xx, sizeof(magic))) {strcat(c,"Error read profile"); return c; }     // прочитать заголовок
  
  if (xx==PROFILE_EMPTY)  {strcat(c,"Empty profile"); return c;}                                 // Данных нет
  if (xx!=0xaa)  {strcat(c,"Bad format profile"); return c;}                                     // Заголовок не верен, данных нет
    
  adr=adr+sizeof(magic); 
  if (readEEPROM_I2C(adr, (byte*)&crc16temp,sizeof(crc16))) {strcat(c,"Error read profile");return c;} // прочитать crc16
  adr=adr+sizeof(crc16);                                                                         // вычислить адрес начала данных

    if (readEEPROM_I2C(adr, (byte*)&temp_prof, sizeof(temp_prof))) {strcat(c,"Error read profile");return c;}    // прочитать данные
    // прочли формируем строку  сописанием
    if (GETBIT(temp_prof.flags,fEnabled)) strcat(c,"+ ");else strcat(c,"- ");                         // отметка об использовании в списке
    strcat(c,temp_prof.name);
    strcat(c,":  ");
    strcat(c,temp_prof.note);
    strcat(c," [");
    strcat(c,DecodeTimeDate(temp_prof.saveTime));
    strcat(c," ");
    strcat(c,uint16ToHex(crc16temp));  
    strcat(c,"]");
  //  Serial.print("num=");Serial.print(num); Serial.print(" : "); Serial.println(c);
    return c;
}

// стереть профайл num из еепром  (ставится признак пусто, данные не стираются)
int32_t Profile::erase(int8_t num)
{
   int32_t adr=I2C_PROFILE_EEPROM+dataProfile.len*num;                                             // вычислить адрес начала профиля
   byte xx=PROFILE_EMPTY;                                                                          // признак стертого профайла
   if (writeEEPROM_I2C(adr, (byte*)&xx, sizeof(xx))) { set_Error(ERR_SAVE_PROFILE,(char*)nameHeatPump); return ERR_SAVE_PROFILE;}  // записать заголовок
   return dataProfile.len;                                                                         // вернуть длину профиля
}

// ДОБАВЛЯЕТ к строке с - список возможных конфигураций num - текущий профиль,
char *Profile::get_list(char *c/*,int8_t num*/)
{
  return strcat(c,list);
}

// Устанавливает текущий профиль из номера списка, новый профиль;
int8_t Profile::set_list( int8_t num)
 {
  uint8_t xx, i,j=0;
  int32_t adr;
  
  for (i=0;i<I2C_PROFIL_NUM;i++)                                                                // перебор по всем профилям
  {
    adr=I2C_PROFILE_EEPROM+ get_sizeProfile()*i;                                                // вычислить адрес начала профиля
    if (readEEPROM_I2C(adr, (byte*)&xx, sizeof(magic))) { continue; }                                 // прочитать заголовок
    if (xx!=0xaa)  {  continue; }                                                               // Заголовок не верен, данных нет, пропускаем чтение профиля это не ошибка
    Serial.print("xx==0xaa ");Serial.println(i);
    adr=adr+sizeof(magic)+sizeof(crc16);                                                        // вычислить адрес начала данных
    if (readEEPROM_I2C(adr, (byte*)&temp_prof, sizeof(temp_prof))) { continue; }                          // прочитать данные
    if ((GETBIT(temp_prof.flags,fEnabled))&&(temp_prof.id==i))                                            // Если разрешено использовать профиль  в  списке, и считанный номер совпадает с текущим (это должно быть всегда)
     {
      if (num==j) {load(i);  break; }                                                           // надо проверить не выбран ли он , и если совпало загрузить профиль и выход
      else j++;                                                                                 // увеличить счетчик - двигаемся по списку
     } 
   }
//update_list(num); // <-- вызывается в load()!                                                 // обновить список
return dataProfile.id;                                                                          // вернуть текущий профиль
}

// обновить список имен профилей, зопоминается в строке list
// Возможно что будет отсутвовать выбранный элемент - это нормально
// такое будет догда когда текущйий профиль не отмечен что учасвует в списке
int8_t Profile::update_list(int8_t num)
{
  byte xx;
  uint8_t i;
  int32_t adr;
  strcpy(list,"");                                                                              // стереть список
  char *p = list;
  for (i=0;i<I2C_PROFIL_NUM;i++)                                                                // перебор по всем профилям
  {
    adr=I2C_PROFILE_EEPROM+ get_sizeProfile()*i;                                                // вычислить адрес начала профиля
    if (readEEPROM_I2C(adr, (byte*)&xx, sizeof(xx))) { continue; }                              // прочитать заголовок
    if (xx!=0xaa)   continue;                                                                   // Заголовок не верен, данных нет, пропускаем чтение профиля это не ошибка
    adr=adr+sizeof(magic)+sizeof(crc16);                                                        // вычислить адрес начала данных
    if (readEEPROM_I2C(adr, (byte*)&temp_prof, sizeof(temp_prof))) { continue; }                          // прочитать данные
    if ((GETBIT(temp_prof.flags,fEnabled))||(i==num))                                                // Если разрешено использовать или ТЕКУЩИЙ профиль
   // if (GETBIT(temp.flags,fEnabled)))                                                         // Если разрешено использовать
     { 
    	p = list + strlen(list);
    	itoa(i, p, 10);
    	strcat(p, ". ");
    	strcat(p, temp_prof.name);
    	if (i==num) strcat(p,":1;"); else strcat(p,":0;");
     }                    
  }
 return OK;
}

// Прочитать из EEPROM структуру: режим работы ТН (SaveON), возврат OK - успешно
int8_t  Profile::load_from_EEPROM_SaveON(type_SaveON *_SaveOn)
{
	return readEEPROM_I2C(I2C_PROFILE_EEPROM + dataProfile.len * dataProfile.id + sizeof(magic) + sizeof(crc16) + dataProfile.len, (byte*)_SaveOn, sizeof(SaveON)) ? ERR_LOAD_PROFILE : OK;
}

// ---------------------------------------------------------------------------------
//  Класс Статистика ТН ------------------------------------------------------------
//  ------------- только для 64 кбайтного чипа памяти ------------------------------
// ---------------------------------------------------------------------------------
#ifdef I2C_EEPROM_64KB  
// Очистка статистики текущего дня
void Statistics::clear_OneDay()
{
dateUpdate=rtcSAM3X8.unixtime();     // Время поледнего обновления статистики
Hour=0;                              // 1 Сколько часов в точке
tin=0;                               // 4 средняя температура в доме
tout=0;                              // 4 средняя температура на улице
tbol=0;                              // 4 средняя температура в бойлере
eCO=0;                               // 4 Выработаная энергия
eEn=0;                               // 4 Затраченная энергия 220
moto=0;                              // 4 Моточасы компрессора
  
OneDay.flags=0x00;                   // Флаги точки
OneDay.Hour=0;;                      // Сколько часов в точке
OneDay.date=dateUpdate;              // Дата точки
OneDay.tin=0;                        // средняя температура в доме
OneDay.tout=0;                       // средняя температура на улице
OneDay.tbol=0;                       // средняя температура в бойлере
OneDay.eCO=0;                        // Выработаная энергия
OneDay.eEn=0;                        // Затраченная энергия 220
OneDay.moto=0;                       // Моточасы компрессора
OneDay.P1=0;                         // резерв 1
OneDay.P2=0;                         // резерв 2
}
// Инициализация статистки
void Statistics::Init()
{
  clear_OneDay();
  pos=0,
  num=0;                            // Позиция  для записи и число точек
  full=false;
  journal.jprintf("Init I2C EEPROM statistics . . . \n");  
   if (checkREADY()==false) // Проверка наличия статистики
   { 
    journal.jprintf("Statistic not found from I2C EEPROM\n"); 
    Format(); 
   }   
   journal.jprintf("Statistic found: ");
   // Вывод найденой статистики
    pos=readDBYTE(I2C_STAT_EEPROM+2);
    num=readDBYTE(I2C_STAT_EEPROM+4);
    if (error==OK)
    {
    if ((pos==0)&&(num==0)) journal.jprintf("is empty, total %d points\n",STAT_POINT);
    else journal.jprintf("position=%d number=%d\n",pos,num);
    }
    else journal.jprintf("Error %d format ststistics from i2c eeprom\n",error); 
}

// Форматирование статистики (инициализация I2C памяти уже проведена)
void Statistics::Format()
{   
    uint16_t i;
    error=OK;
    memset(bufI2C,I2C_STAT_FORMAT,sizeof(bufI2C)); 
    journal.jprintf("Formating I2C EEPROM for statictic:\n");
    for (i=0;i<STAT_LEN/sizeof(bufI2C);i++) 
       { 
          journal.jprintf("*");
          if (writeEEPROM_I2C(I2C_STAT_EEPROM+i*sizeof(bufI2C), (byte*)&bufI2C, sizeof(bufI2C)))   
              { error=ERR_WRITE_I2C_STAT; 
                journal.jprintf(errorWriteI2C);   // Сообщение об ошибке
                break; 
              }; 
          WDT_Restart(WDT);      
       }    
    full=false;                   // Буфер не полный
    pos = 0;
    num = 0;
    writePOS();
    writeNUM();
    if (error==OK)   // было удачное форматирование
    {
    writeREADY();  
    journal.jprintf("\nFormat I2C EEPROM statistics (size %d bytes, %d points) - Ok\n",STAT_LEN,STAT_POINT);
    }
}

// Записать номер для записи нового дня
void Statistics::writePOS()
{
if (writeEEPROM_I2C(I2C_STAT_EEPROM+2, (byte*)&pos,sizeof(pos))) 
   { error=ERR_WRITE_I2C_STAT;
     journal.jprintf(errorWriteI2C);   // Сообщение об ошибке
    }  
}
// Записать число хранящихся дней
void Statistics::writeNUM()
{
if (writeEEPROM_I2C(I2C_STAT_EEPROM+4, (byte*)&num,sizeof(num))) 
   { error=ERR_WRITE_I2C_STAT;
     journal.jprintf(errorWriteI2C);   // Сообщение об ошибке
    }  
}
// Записать признак "форматирования" журнала - журналом можно пользоваться
void Statistics::writeREADY()
{
uint16_t  w=I2C_STAT_READY; 
if (writeEEPROM_I2C(I2C_STAT_EEPROM, (byte*)&w,sizeof(w))) 
   { error=ERR_WRITE_I2C_STAT;
     journal.jprintf(errorWriteI2C);   // Сообщение об ошибке
    }  
}

// Проверить наличие статистики
boolean Statistics::checkREADY()
{  
    uint16_t  w=0x0; 
    if (readEEPROM_I2C(I2C_STAT_EEPROM, (byte*)&w,sizeof(w))) 
       { error=ERR_READ_I2C_STAT; 
         journal.jprintf(errorReadI2C);   // Сообщение об ошибке
        }
    if (w!=I2C_STAT_READY) return false; else return true;
}
// прочитать uint16_t по адресу adr
uint16_t Statistics::readDBYTE(uint16_t adr)
{
  uint16_t  ret; 
  if (readEEPROM_I2C(adr, (byte*)&ret,sizeof(ret))) 
     { error=ERR_READ_I2C_STAT; 
       journal.jprintf(errorReadI2C);   // Сообщение об ошибке
      } 
  return ret;                         
}
// Возвращает размер статистики в днях (записях)
uint16_t Statistics::available()
{ 
   if (full) return STAT_LEN; else return num;
} 
// ЗАКРЫТИЕ ДНЯ Записать один день в еепром, обратно код ошибки
uint8_t Statistics::writeOneDay(uint32_t energyCO,uint32_t energy220,uint32_t motoH)
{
OneDay.date=dateUpdate;                                            // Дата точки
OneDay.flags=0x00;                                                 // Флаги точки
if (Hour==24) SETBIT1(OneDay.flags,fStatFull);                     // День полный
OneDay.Hour=Hour;                                                  // Сколько часов в точке
if (Hour>0)                                                        // если есть что закрывать
{
OneDay.tin=tin/Hour;                                               // средняя температура в доме
OneDay.tout=tout/Hour;                                             // средняя температура на улице
OneDay.tbol=tbol/Hour;                                             // средняя температура в бойлере
}

if (energyCO>eCO)  OneDay.eCO=energyCO-eCO;  else OneDay.eCO=0;    // Выработаная энергия за день
if (energy220>eEn) OneDay.eEn=energy220-eEn; else OneDay.eEn=0;    // Затраченная энергия 220
if (motoH>moto)    OneDay.moto=motoH-moto;   else OneDay.moto=0;   // Моточасы компрессора
OneDay.P1=0;                   // резерв 1
OneDay.P2=0;                   // резерв 2

// Записать данные
if (writeEEPROM_I2C(I2C_STAT_START+pos*sizeof(type_OneDay), (byte*)&OneDay,sizeof(OneDay))) 
   { error=ERR_WRITE_I2C_STAT;
     journal.jprintf(errorWriteI2C);   // Сообщение об ошибке
    }  
if (error==OK) // Если удачно индекс и число точек поправить
  {  if (!full) {num++;writeNUM();}                            // ЧИСЛО ТОЧЕК буфер пока не полный, отсчет с 0 идет не забыть
     journal.jprintf(pP_TIME," Save statictic day pos=%d num=%d\n", pos,num);   // вывод именно здесть - правильные значения
     if (pos<STAT_POINT-1) pos++; else { pos=0; full=true; }   // ПОЗИЦИЯ ДЛЯ СЛЕДУЮЩЕЙ ЗАПИСИ (Следующей!!!!!)
     writePOS(); 
  } 
clear_OneDay();                                                // очитска данных дня
// Запомнить новые начальные значения для дня
eCO=energyCO;         // Выработаная энергия
eEn=energy220;        // Затраченная энергия 220
moto=motoH;           // Моточасы компрессора
return error;                                    
}
// Сгенерировать тестовые данные в памяти i2c на входе число точек
void Statistics::generate_TestData(uint16_t number)
{
uint32_t  last_date=rtcSAM3X8.unixtime();
static int8_t a;
uint16_t i;

if (number>STAT_POINT) number=STAT_POINT; 
journal.jprintf("Generate statictic %d test point \n",number); 
pos=0;  // все с начала
num=0;   
for(i=0;i<number;i++)  // герация и запись одной точки в цикле
{
 //   clear_OneDay();                      // очитска данных дня
    // генерация данных
    dateUpdate=last_date-(STAT_POINT-i)*(60*60*24);  
    a=random(10,24);
    Hour=a;                      // добавить часов
    tin=a*random(1700,2500);     // добавить температура в доме
    tout=a*random(-2500,0);      // добавить температура на улице
    tbol=a*random(3500,5000);    // добавить температура в бойлере
    writeOneDay(a*random(4,15),a*random(1,4),random(6,12));  // запись данных
    // При генерации новые начальные значения дня равны 0
    eCO=0;         // Выработаная энергия
    eEn=0;         // Затраченная энергия 220
    moto=0;        // Моточасы компрессора
 //   journal.jprintf("*"); 
    taskYIELD();   
}

 Init();  // Проинализировать статистику заново
}
// получить список доступой статистики кладется в str
// cat true - список добавляется в конец, false - строка обнуляется и список добавляется
char * Statistics::get_listStat(char* str, boolean cat)
{
if (!cat) strcpy(str,"");     // Обнулить строку если есть соответсвующий флаг
 strcat(str,"none:1;");
 strcat(str,"Tin:0;");        // средняя темпеартура дома
 strcat(str,"Tout:0;");       // средняя темпеартура улицы
 strcat(str,"Tboiler:0;");    // средняя темпеартура бойлера
 strcat(str,"Hour:0;");       // число накопленных часов должно быть 24
 strcat(str,"Hmoto:0;");      // моточасы за сутки
 #ifdef FLOWCON
 strcat(str,"EnergyCO:0;");   // выработанная энергия
 #endif
 #ifdef USE_ELECTROMETER_SDM
 strcat(str,"Energy220:0;");  // потраченная энергия
 strcat(str,"-COP-:0;");        // КОП
 #endif
 #ifdef FLOWCON
 strcat(str,"PowerCO:0;");    // средння мощность СО
 #endif
 #ifdef USE_ELECTROMETER_SDM
 strcat(str,"Power220:0;");   // средняя потребляемая мощность
 #endif
 return str;      
}
// получить данные статистики по одному типу данных в виде строки
// cat=true - не обнулять входную строку а добавить в конец
char *Statistics::get_Stat(TYPE_STAT t,char* str, boolean cat)
{
char buf[10];
uint16_t index;           // индекс текущей точки
if (!cat) strcpy(str,""); // Обнулить строку если есть соответсвующий флаг

for(int i=0;i<num;i++) // цикл по всем точкам
  {
   
   if (!full) index=i; // вычисление текущей точки
   else { if ((pos+i)<STAT_POINT) index=pos+i; else    index=pos+i-STAT_POINT;  }  
    // чтение данных одной точки
   taskYIELD();
   if (readEEPROM_I2C(I2C_STAT_START+index*sizeof(type_OneDay), (byte*)&ReadDay,sizeof(type_OneDay))) 
   { error=ERR_READ_I2C_STAT;
     journal.jprintf(errorReadI2C);   // Сообщение об ошибке
    } 
  if (error==OK) // Если удачно
    { 
    strcat(str,StatDate(ReadDay.date,true)); strcat(str,(char*)":");    // готовим дату кратко
      switch (t) // в зависимости от того чо нужно
      {
         case pNONE_STAT:      strcat(str,""); return str;  break;
         case pTIN_STAT:       strcat(str,ftoa(buf,(float)(ReadDay.tin/100.0),2)); break;          // средняя темпеартура дома
         case pTOUT_STAT:      strcat(str,ftoa(buf,(float)(ReadDay.tout/100.0),2)); break;         // средняя темпеартура улицы
         case pTBOILER_STAT:   strcat(str,ftoa(buf,(float)(ReadDay.tbol/100.0),2)); break;         // средняя температура бойлера
         case pHOUR_STAT:      strcat(str,int2str(ReadDay.Hour)); break;                           // число накопленных часов должно быть 24
         case pHMOTO_STAT:     strcat(str,int2str(ReadDay.moto)); break;                           // моточасы за сутки
         case pENERGYCO_STAT:  strcat(str,int2str(ReadDay.eCO));  break;                           // выработанная энергия
         case pENERGY220_STAT: strcat(str,int2str(ReadDay.eEn)); break;                            // потраченная энергия
         case pCOP_STAT:       strcat(str,ftoa(buf,(float)(ReadDay.eCO/ReadDay.eEn),2)); break;    // КОП
         case pPOWERCO_STAT:   strcat(str,ftoa(buf,(float)(ReadDay.eCO/ReadDay.Hour),2)); break;   // средння мощность СО
         case pPOWER220_STAT:  strcat(str,ftoa(buf,(float)(ReadDay.eEn/ReadDay.Hour),2)); break;   // средняя потребляемая мощность
        default:  strcat(str,(char*)cZero); break; 
      }  
    strcat(str,(char*)";"); 
     } // if
  }  // for
    return str; 
}
// получить данные статистики по одному дню в виде строки
// cat=true - не обнулять входную строку а добавить в конец
char *Statistics::get_OneDay(char* str,uint16_t ii,boolean cat)
{
  uint16_t index;           // индекс текущей точки
  char buf[10];
  if (!cat) strcpy(str,""); // Обнулить строку если есть соответсвующий флаг

  if (!full) index=ii;        // вычисление текущей точки
  else { if ((pos+ii)<STAT_POINT) index=pos+ii; else    index=pos+ii-STAT_POINT;  }  

   _delay(1); // или будут ошибки чтения из памяти
   if (readEEPROM_I2C(I2C_STAT_START+index*sizeof(type_OneDay), (byte*)&ReadDay,sizeof(type_OneDay))) // читаем
   { error=ERR_READ_I2C_STAT;journal.jprintf(errorReadI2C);}  // Сообщение об ошибке
 
  if (error==OK) // Если удачно
    {   
    strcat(str,int2str(ii));                              strcat(str,(char*)";");      // номер
    strcat(str,StatDate(ReadDay.date,false));             strcat(str,(char*)";");      // готовим дату
    strcat(str,ftoa(buf,(float)(ReadDay.tin/100.0),2));   strcat(str,(char*)";");      // средняя темпеартура дома
    strcat(str,ftoa(buf,(float)(ReadDay.tout/100.0),2));  strcat(str,(char*)";");      // средняя темпеартура улицы
    strcat(str,ftoa(buf,(float)(ReadDay.tbol/100.0),2));  strcat(str,(char*)";");      // средняя температура бойлера
    strcat(str,int2str(ReadDay.Hour));                    strcat(str,(char*)";");      // число накопленных часов должно быть 24
    strcat(str,int2str(ReadDay.moto));                    strcat(str,(char*)";");      // моточасы за сутки
    #ifdef FLOWCON
    strcat(str,int2str(ReadDay.eCO));                     strcat(str,(char*)";");      // выработанная энергия
    #endif
    #ifdef USE_ELECTROMETER_SDM
    strcat(str,int2str(ReadDay.eEn));                     strcat(str,(char*)";");      // потраченная энергия
    strcat(str,ftoa(buf,(float)(ReadDay.eCO/ReadDay.eEn),2));strcat(str,(char*)";");   // КОП
    #endif
    #ifdef FLOWCON
    strcat(str,ftoa(buf,(float)(ReadDay.eCO/ReadDay.Hour),2));strcat(str,(char*)";");  // средння мощность СО
    #endif
    #ifdef USE_ELECTROMETER_SDM 
    strcat(str,ftoa(buf,(float)(ReadDay.eEn/ReadDay.Hour),2));strcat(str,(char*)";");  // средняя потребляемая мощность
    #endif
    }     
return str;          
}
// получить информацию о накопленной стаитистики в виде строки
// cat=true - не обнулять входную строку а добавить в конец
char *Statistics::get_Info(char* str, boolean cat)
{
uint16_t index;           // индекс текущей точки
if (!cat) strcpy(str,""); // Обнулить строку если есть соответсвующий флаг

strcat(str,(char*)"Максимальный объем накапливаемой статистики (дни)|"); strcat(str,int2str(STAT_POINT));  strcat(str,(char*)";");
strcat(str,(char*)"Объем накопленной статистики (дни)|");  strcat(str,int2str(num));  strcat(str,(char*)";");

if (!full) index=0;        // начальная дата
else { if ((pos+0)<STAT_POINT) index=pos+0; else    index=pos+0-STAT_POINT;  }  
if (readEEPROM_I2C(I2C_STAT_START+index*sizeof(type_OneDay), (byte*)&ReadDay,sizeof(type_OneDay))) // читаем
{ error=ERR_READ_I2C_STAT;journal.jprintf(errorReadI2C);}  // Сообщение об ошибке
if (error==OK) { strcat(str,(char*)"Начальная дата статистики|"); strcat(str,StatDate(ReadDay.date,false));  strcat(str,(char*)";");} // Если удачно
 
if (!full) index=num-1;        // конечная дата
else { if ((pos+num-1)<STAT_POINT) index=pos+num-1; else    index=pos+num-1-STAT_POINT;  }  
if (readEEPROM_I2C(I2C_STAT_START+index*sizeof(type_OneDay), (byte*)&ReadDay,sizeof(type_OneDay))) // читаем
{ error=ERR_READ_I2C_STAT;journal.jprintf(errorReadI2C);}  // Сообщение об ошибке
if (error==OK) {strcat(str,(char*)"Конечная дата статистики|"); strcat(str,StatDate(ReadDay.date,false));  strcat(str,(char*)";");} // Если удачно
    
strcat(str,(char*)"Позиция для записи|"); strcat(str,int2str(pos));  strcat(str,(char*)";");

return str;                  
}

#endif // I2C_EEPROM_64KB
// ---------------------------------------------------------------------------------
//  MQTT клиент ТН -----------------------------------------------------------------
// ---------------------------------------------------------------------------------
// инициализация MQTT
void clientMQTT::initMQTT()
{
  
 IPAddress zeroIP(0,0,0,0);   
 mqttSettintg.flags=0x00;                                 // Бинарные флага настроек
 SETBIT0(mqttSettintg.flags,fMqttUse);                    // флаг использования MQTT
 SETBIT1(mqttSettintg.flags,fTSUse);                      // флаг использования ThingSpeak
 SETBIT0(mqttSettintg.flags,fMqttBig);                    // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
 SETBIT0(mqttSettintg.flags,fMqttSDM120);                 // флаг отправки данных электросчетчика на MQTT
 SETBIT0(mqttSettintg.flags,fMqttFC);                     // флаг отправки данных инвертора на MQTT
 SETBIT0(mqttSettintg.flags,fMqttCOP);                    // флаг отправки данных COP на MQTT
 SETBIT0(mqttSettintg.flags,fNarodMonUse);                // флаг отправки данных на народный мониторинг
 SETBIT0(mqttSettintg.flags,fNarodMonBig);                // флаг отправки данных на народный мониторинг расширенную версию
 mqttSettintg.ttime=DEFAULT_TIME_MQTT;                    // период отправки на сервер в сек. 10...60000
 strcpy(mqttSettintg.mqtt_server,DEFAULT_ADR_MQTT);       // Адрес сервера
 mqttSettintg.mqtt_serverIP=zeroIP;                       // IP Адрес сервера
  
 mqttSettintg.mqtt_port=DEFAULT_PORT_MQTT;                // Адрес порта сервера
 strcpy(mqttSettintg.mqtt_login,"admin");                 // логин сервера
 strcpy(mqttSettintg.mqtt_password,"admin");              // пароль сервера
 strcpy(mqttSettintg.mqtt_id,"HeatPump");                 // Идентификатор клиента на MQTT сервере
 // NARMON
 strcpy(mqttSettintg.narodMon_server,DEFAULT_ADR_NARMON); // Адрес сервера народного мониторинга
 mqttSettintg.narodMon_serverIP=zeroIP;                   // IP Адрес сервера народного мониторинга
 mqttSettintg.narodMon_port=DEFAULT_PORT_MQTT;            // Адрес порта сервера народного мониторинга
 strcpy(mqttSettintg.narodMon_login,"login");             // логин сервера народного мониторинга
 strcpy(mqttSettintg.narodMon_password,"1234");           // пароль сервера народного мониторинга
 strcpy(mqttSettintg.narodMon_id,"HeatPump");             // Идентификатор клиента на MQTT сервере
  
 dnsUpadateMQTT=false;                                     // Флаг необходимости обновления через dns IP адреса для MQTT
 dnsUpadateNARMON=false;                                   // Флаг необходимости обновления через dns IP адреса для NARMON
 w5200_MQTT.setSock(W5200_SOCK_SYS);                       // Установить сокет с которым рабоатем
// w5200_MQTT.setCallback(callback);  
}

// Установить параметр Уведомления из строки
boolean clientMQTT::set_paramMQTT(TYPE_PARAM_MQTT p, char *c)
{
  float x;
 //  Serial.print(p);Serial.print(" : c=");Serial.print(c);Serial.println(">>>");
 switch (p)
   {
    case pUSE_TS:      if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fTSUse); return true;}          // флаг использования ThingSpeak
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fTSUse);  return true;}
                       else return false;  
                       break;  
   case pUSE_MQTT:     if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fMqttUse); return true;}          // флаг использования MQTT
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fMqttUse);  return true;}
                       else return false;  
                       break; 
   case pBIG_MQTT:     if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fMqttBig); return true;}          // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fMqttBig);  return true;}
                       else return false;  
                       break; 
   case pSDM_MQTT:     if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fMqttSDM120); return true;}       //  флаг отправки данных электросчетчика на MQTT
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fMqttSDM120);  return true;}
                       else return false;  
                       break; 
   case pFC_MQTT:     if (strcmp(c,cZero)==0)       { SETBIT0(mqttSettintg.flags,fMqttFC); return true;}           //  флаг отправки данных инвертора на MQTT
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fMqttFC);  return true;}
                       else return false;  
                       break;
   case pCOP_MQTT:     if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fMqttCOP); return true;}          // флаг отправки данных COP на MQTT
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fMqttCOP);  return true;}
                       else return false;  
                       break;
               
   case  pTIME_MQTT:   x=my_atof(c);                                                                             // ПРИХОДЯТ МИНУТЫ храним СЕКУНДЫ период отправки на сервер в сек. 10...60000
                       if (x==ATOF_ERROR) return   false;
                       else if((x<1)||(x>=1000)) return   false;    
                       else mqttSettintg.ttime=(int)x*60; return true;
                       break; 
   case  pADR_MQTT:    if(strlen(c)==0) return false;                                                            // Адрес сервера  пустая строка
                       if(strlen(c)>sizeof(mqttSettintg.mqtt_server)-1) return false;                            // слишком длиная строка
                       else // ок сохраняем
                        {  
                        strcpy(mqttSettintg.mqtt_server,c); 
                        dnsUpadateMQTT=true;
                        return true;  
                        }                        
                       break; 
   case pIP_MQTT:      return true; break;                                                                       // IP Адрес сервера,  Только на чтение. описание первого параметра для отправки смс
   case pPORT_MQTT:    x=my_atof(c);                                                                             // Порт сервера
                       if (x==ATOF_ERROR) return   false;
                       else if((x<=1)||(x>=65535-1)) return   false;   
                       else mqttSettintg.mqtt_port=(int)x; return true;
                       break;  
   case pLOGIN_MQTT:   if(strlen(c)==0) return false;                                                            // логин сервера
                       if(strlen(c)>sizeof(mqttSettintg.mqtt_login)-1) return false;                     
                       else { strcpy(mqttSettintg.mqtt_login,c); return true;  }                         
                       break;
   case pPASSWORD_MQTT:if(strlen(c)==0) return false;                                                            // пароль сервера
                       if(strlen(c)>sizeof(mqttSettintg.mqtt_password)-1) return false;                     
                       else { strcpy(mqttSettintg.mqtt_password,c); return true;  }                         
                       break;
   case pID_MQTT:      if(strlen(c)==0) return false;                                                            // дентификатор клиента на MQTT сервере
                       if(strlen(c)>sizeof(mqttSettintg.mqtt_id)-1) return false;                     
                       else { strcpy(mqttSettintg.mqtt_id,c); return true;  }                         
                       break;
                       // --------------------- NARMON -------------------------
  case  pUSE_NARMON:   if (strcmp(c,cZero)==0)      { SETBIT0(mqttSettintg.flags,fNarodMonUse); return true;}     // флаг отправки данных на народный мониторинг
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fNarodMonUse);  return true;}
                       else return false;  
                       break;
   case  pBIG_NARMON:  if (strcmp(c,cZero)==0)       { SETBIT0(mqttSettintg.flags,fNarodMonBig); return true;}    // флаг отправки данных на народный мониторинг расширенная версия
                       else if (strcmp(c,cOne)==0) { SETBIT1(mqttSettintg.flags,fNarodMonBig);  return true;}
                       else return false;  
                       break;    
  case  pADR_NARMON:   if(strlen(c)==0) return false;                                                             // Адрес сервера  пустая строка
                       if(strlen(c)>sizeof(mqttSettintg.narodMon_server)-1) return false;                         // слишком длиная строка
                       else // ок сохраняем
                        {  
                        strcpy(mqttSettintg.narodMon_server,c); 
                        dnsUpadateNARMON=true;
                        return true;  
                        }                        
                       break; 
   case pIP_NARMON:    return true; break;                                                                       // IP Адрес сервера,  Только на чтение. описание первого параметра для отправки смс
   case pPORT_NARMON:  x=my_atof(c);                                                                             // Порт сервера
                       if (x==ATOF_ERROR) return   false;
                       else if((x<=1)||(x>=65535-1)) return   false;   
                       else mqttSettintg.narodMon_port=(int)x; return true;
                       break;  
   case pLOGIN_NARMON: if(strlen(c)==0) return false;                                                            // логин сервера
                       if(strlen(c)>sizeof(mqttSettintg.narodMon_login)-1) return false;                     
                       else { strcpy(mqttSettintg.narodMon_login,c); return true;  }                         
                       break;
   case pPASSWORD_NARMON:if(strlen(c)==0) return false;                                                          // пароль сервера
                       if(strlen(c)>sizeof(mqttSettintg.narodMon_password)-1) return false;                     
                       else { strcpy(mqttSettintg.narodMon_password,c); return true;  }                         
                       break;
   case pID_NARMON:    if(strlen(c)==0) return false;                                                            // дентификатор клиента на MQTT сервере
                       if(strlen(c)>sizeof(mqttSettintg.narodMon_id)-1) return false;                     
                       else { strcpy(mqttSettintg.narodMon_id,c); return true;  }                         
                       break;
 default:              return false;                               break;   
   }
   
  return false;                        
}

// Получить параметр MQTT из строки
char* clientMQTT::get_paramMQTT(TYPE_PARAM_MQTT p)
{
  
 switch (p)
   {
    case pUSE_TS:          if (GETBIT(mqttSettintg.flags,fTSUse))      return  (char*)cOne; else return  (char*)cZero;   break;     // флаг использования ThingSpeak
    case pUSE_MQTT:        if (GETBIT(mqttSettintg.flags,fMqttUse))    return  (char*)cOne; else return  (char*)cZero;   break;     // флаг использования MQTT
    case pBIG_MQTT:        if (GETBIT(mqttSettintg.flags,fMqttBig))    return  (char*)cOne; else return  (char*)cZero;   break;     // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
    case pSDM_MQTT:        if (GETBIT(mqttSettintg.flags,fMqttSDM120)) return  (char*)cOne; else return  (char*)cZero;   break;     // флаг отправки данных электросчетчика на MQTT
    case pFC_MQTT:         if (GETBIT(mqttSettintg.flags,fMqttFC))     return  (char*)cOne; else return  (char*)cZero;   break;     // флаг отправки данных инвертора на MQTT
    case pCOP_MQTT:        if (GETBIT(mqttSettintg.flags,fMqttCOP))    return  (char*)cOne; else return  (char*)cZero;   break;     // флаг отправки данных COP на MQTT
    case pTIME_MQTT:       return int2str(mqttSettintg.ttime/60);                                                     break;     // ПРИХОДЯТ МИНУТЫ храним СЕКУНДЫ период отправки на сервер в сек. 10...60000
    case pADR_MQTT:        return mqttSettintg.mqtt_server;                                                           break;     // Адрес сервера
    case pIP_MQTT:         return IPAddress2String(mqttSettintg.mqtt_serverIP);                                       break;     // IP Адрес сервера,  Только на чтение. описание первого параметра для отправки смс
    case pPORT_MQTT:       return int2str(mqttSettintg.mqtt_port);                                                    break;     // Порт сервера
    case pLOGIN_MQTT:      return mqttSettintg.mqtt_login;                                                            break;     // логин сервера
    case pPASSWORD_MQTT:   return mqttSettintg.mqtt_password;                                                         break;     // пароль сервера
    case pID_MQTT:         return mqttSettintg.mqtt_id;                                                               break;     // дентификатор клиента на MQTT сервере
    // ----------------------NARMON -------------------------
    case pUSE_NARMON:      if (GETBIT(mqttSettintg.flags,fNarodMonUse)) return  (char*)cOne; else return  (char*)cZero;  break;     // флаг отправки данных на народный мониторинг
    case pBIG_NARMON:      if (GETBIT(mqttSettintg.flags,fNarodMonBig)) return (char*)cOne; else return  (char*)cZero;   break;     // флаг отправки данных на народный мониторинг  расширенная версия
    case pADR_NARMON:      return mqttSettintg.narodMon_server;                                                       break;     // Адрес сервера народного мониторинга
    case pIP_NARMON:       return IPAddress2String(mqttSettintg.narodMon_serverIP);                                   break;     // IP Адрес сервера народного мониторинга,
    case pPORT_NARMON:     return int2str(mqttSettintg.narodMon_port);                                                break;     // Порт сервера народного мониторинга
    case pLOGIN_NARMON:    return mqttSettintg.narodMon_login;                                                        break;     // логин сервера народного мониторинга
    case pPASSWORD_NARMON: return mqttSettintg.narodMon_password;                                                     break;     // пароль сервера  народного мониторинга
    case pID_NARMON:       return mqttSettintg.narodMon_id;                                                           break;     // дентификатор клиента на MQTT сервере народного мониторинга
    default:               return (char*)cError;                                                                      break;   
   }
  return (char*)cError;                        
}


// Обновление IP адресов MQTT через dns
// возвращает true обновление не было false - прошло обновление или ошибка
boolean clientMQTT::dnsUpdate()
{ boolean ret=true;
  if  (dnsUpadateMQTT) //надо обновлятся
  {
     dnsUpadateMQTT=false;   // сбросить флаг
     if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE)  {return false;}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
     ret=check_address(mqttSettintg.mqtt_server,mqttSettintg.mqtt_serverIP);                                  // Получить адрес IP через DNS
     SemaphoreGive(xWebThreadSemaphore);
     _delay(20);
     ret=false;
  }
  if  (dnsUpadateNARMON) //надо обновлятся
  {
     dnsUpadateNARMON=false;   // сбросить флаг
     if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE)  {return false;}  // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
     ret=check_address(mqttSettintg.narodMon_server,mqttSettintg.narodMon_serverIP);                          // Получить адрес IP через DNS
     SemaphoreGive(xWebThreadSemaphore);
     _delay(20);
     ret=false;
   }
  return ret; 
}
// Обновление IP адресов серверов через dns при СТАРТЕ!!!  вачдог сбрасывается т.к. может сеть не рабоать
boolean clientMQTT::dnsUpdateStart()
{    boolean ret=false;
     IPAddress zeroIP(0,0,0,0);  
     dnsUpadateMQTT=false;
     WDT_Restart(WDT);                                                               // Сбросить вачдог
     if(mqttSettintg.mqtt_serverIP==zeroIP) ret=check_address(mqttSettintg.mqtt_server,mqttSettintg.mqtt_serverIP);     // если адрес нулевой Получить адрес IP через DNS
     dnsUpadateNARMON=false;
     WDT_Restart(WDT);                                                              // Сбросить вачдог
     if(mqttSettintg.narodMon_serverIP==zeroIP) ret=check_address(mqttSettintg.narodMon_server,mqttSettintg.narodMon_serverIP);              //  если адрес нулевой Получить адрес IP через DNS
     return ret;                                               
} 

// Записать настройки в eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::save(int32_t adr)    
{
 if (writeEEPROM_I2C(adr, (byte*)&mqttSettintg, sizeof(mqttSettintg))) { set_Error(ERR_SAVE_EEPROM,(char*)nameHeatPump); return ERR_SAVE_EEPROM;}  adr=adr+sizeof(mqttSettintg);           // записать параметры MQTT
 return adr;   
}
// Считать настройки из eeprom i2c на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::load(int32_t adr)
{
  if (readEEPROM_I2C(adr, (byte*)&mqttSettintg, sizeof(mqttSettintg))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(mqttSettintg);           // прочитать параметры MQTT
  return adr;                                       
}
// Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
int32_t clientMQTT::loadFromBuf(int32_t adr, byte *buf)
{
 memcpy((byte*)&mqttSettintg,buf+adr,sizeof(mqttSettintg)); adr=adr+sizeof(mqttSettintg); 
 return adr;                         
}
// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
uint16_t clientMQTT::get_crc16(uint16_t crc)
{
  uint16_t i;
  for(i=0;i<sizeof(mqttSettintg);i++) crc=_crc16(crc,*((byte*)&mqttSettintg+i));   // CRC16 настройки MQTT
  return crc;                                    
}

// Еще одна ошибка отправки на MQTT параметр тип сервера true - народный мониторинг false MQTT
uint16_t clientMQTT::updateErrMQTT(boolean NM)                
{
 if (NM)
     {
     numErrNARMON++;
     if (numErrNARMON>=MQTT_NUM_ERR_OFF)
         {
         SETBIT0(mqttSettintg.flags,fNarodMonUse);
         journal.jprintf(pP_TIME,(char*)BlockService,HP.clMQTT.get_paramMQTT(pADR_NARMON));   
         numErrNARMON=0;// сбросить счетчик
         }
      return numErrNARMON;
     }
 else 
     {
     numErrMQTT++;
     if (numErrMQTT>=MQTT_NUM_ERR_OFF)
         {
         SETBIT0(mqttSettintg.flags,fMqttUse);
         journal.jprintf(pP_TIME,(char*)BlockService,HP.clMQTT.get_paramMQTT(pADR_MQTT));    
         numErrMQTT=0;// сбросить счетчик
         }
      return numErrMQTT;
     }
return 0;
}

/*
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
} */

// Внутренная функция послать один топик, возврат удачно или нет послан топик, при не удаче запись в журнал
// t - название топика
// p - значение топика
// NM - true народный мониторинг, false обычный сервер
// debug - выводить отладочные сообщения
// link_close  = true - по завершению закрывать связь  false по завершению не закрывать связь (отсылка нескольких топиков)
boolean clientMQTT::sendTopic(char * t,char *p,boolean NM, boolean debug, boolean link_close)
{
  TYPE_STATE_MQTT state; 
  IPAddress tempIP;
  uint8_t i;
  if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBuzy);return false;} // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим
 if (NM) state=stateNARMON; else state=stateMQTT;
 
 if (!linkStatusWiznet(false)) {journal.jprintf((char*)"sendTopic:no link.\n");  SemaphoreGive(xWebThreadSemaphore); return false;}// Нет связи выходим

 //debug=true;    // выводить отладку
 if (!w5200_MQTT.connected())   // если нет соединения то возможно требуется реанимация
  {
     switch (state) // Реанимация варианты
     {
          case pMQTT_OK:  break;  // Предыдущая отправка была удачна
          case pMQTT_SOCK:  // Попытка реанимировать сокет
                          HP.num_resMQTT++;   
                          if(closeSockSys(true))  journal.jprintf((char*)ResSock);  
                          break;         
          case pMQTT_DNS: // Предыдущая отправка была НЕ удачна
                          HP.num_resMQTT++;  
                          if(closeSockSys(true))   // НЕУДАЧА Пытаемся реанимировать сокет
                          { 
                           journal.jprintf((char*)ResDHCP);
                          _delay(50);
                          // Пытаемся еще раз узнать адрес через ДНС, возмлжно он поменялось, если поменялся то меняем не сохраняем настройки
                          if (NM) {check_address(HP.clMQTT.get_paramMQTT(pADR_NARMON),tempIP);if (tempIP!=HP.clMQTT.get_narodMon_serverIP()){ HP.clMQTT.set_narodMon_serverIP(tempIP);journal.jprintf((char*)ChangeIP,HP.clMQTT.get_paramMQTT(pADR_NARMON));}}
                          else    {check_address(HP.clMQTT.get_paramMQTT(pADR_MQTT),tempIP);  if (tempIP!=HP.clMQTT.get_mqtt_serverIP())    { HP.clMQTT.set_mqtt_serverIP(tempIP);    journal.jprintf((char*)ChangeIP,HP.clMQTT.get_paramMQTT(pADR_MQTT));}}
                           _delay(50);
                          }
                          break;
          case pMQTT_RES: // Предудущая отправка - был сброс ДНС адреса теперь НАДО ЧИП СБРОСИТЬ
                         HP.num_resMQTT++; 
                         journal.jprintf((char*)ResChip);  
                         initW5200(false);                                  // Инициализация сети без вывода инфы в консоль
                         for (i=0;i<W5200_THREARD;i++) SETBIT1(Socket[i].flags,fABORT_SOCK);  // Признак инициализации сокета, надо прерывать передачу в сервере
                          _delay(50);
                         break;
     }
 } // if (!connect(NM)) 
// Попытка соедиения - и определение что делаем дальше
if (debug){journal.jprintf((char*)">"); ShowSockRegisters(W5200_SOCK_SYS);}// выводим состояние регистров ЕСЛИ ОТЛАДКА
         if (connect(NM))                                        // Попытка соедиениея
          {
           w5200_MQTT.publish(t,p);                              // Посылка данных топика
           if (link_close) w5200_MQTT.disconnect();              // отсоединение если надо
           if (debug) ShowSockRegisters(W5200_SOCK_SYS);         // выводим состояние регистров ЕСЛИ ОТЛАДКА
         
 //          if(NM)    // Чтение информации
 //          {
 //            w5200_MQTT.subscribe("pav2000/HeatPump/");  //  pav2000/HeatPump/
 //            w5200_MQTT.loop(); 
 //          }
           SemaphoreGive(xWebThreadSemaphore);
           resetErrMQTT(NM);                                     // Удачно - сбрасываем счетчик ошибок
           updateState(NM,pMQTT_OK);   
           return true;      
          }
          else                                             // соединениe не установлено
          { 
          ShowSockRegisters(W5200_SOCK_SYS);    // выводим состояние регистров ВСЕГДА
          closeSockSys(true);
          SemaphoreGive(xWebThreadSemaphore);
          journal.jprintf(" %s\n",get_stateMQTT()); 
          switch(state)  // устанавливаем метод реанимации
               {
               case pMQTT_OK:   updateState(NM,pMQTT_SOCK); break;        
               case pMQTT_SOCK: updateState(NM,pMQTT_DNS);  break;   
               case pMQTT_DNS:  updateState(NM,pMQTT_RES);  break;   
               case pMQTT_RES:  updateState(NM,pMQTT_OK);   break;        
               }
           updateErrMQTT(NM);                             // Добавляем ошибку
           return false; 
          }   
         SemaphoreGive(xWebThreadSemaphore);
 return false; 
}   
 // Попытаться соеденится с сервером МЮТЕКС уже захвачен!!
boolean clientMQTT::connect(boolean NM)
{
if (w5200_MQTT.connected()) return true;  // соеденино выходим
w5200_MQTT.setSock(W5200_SOCK_SYS);       // Установить сокет с которым рабоатем
if (NM) // В зависимости от того с кем надо соединяться
         {
         w5200_MQTT.setServer(mqttSettintg.narodMon_serverIP,mqttSettintg.narodMon_port);                       // установить параметры Народного мониторинга
         w5200_MQTT.connect(HP.get_network(pMAC), mqttSettintg.narodMon_login,mqttSettintg.narodMon_password);  // Соедиенение с народным мониторингом
         #ifdef MQTT_REPEAT            // разрешен повтор соединения
         if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(HP.get_network(pMAC), mqttSettintg.narodMon_login,mqttSettintg.narodMon_password); } // вторая попытка
         #endif
         }
         else 
         {
          if(HP.clMQTT.get_TSUse()) 
               {
                w5200_MQTT.setServer(mqttSettintg.mqtt_serverIP,mqttSettintg.mqtt_port);
                w5200_MQTT.connect(mqttSettintg.mqtt_id);   //  установить параметры сервера (используется обращение через ДНС) Соедиенение с ThingSpeak
                #ifdef MQTT_REPEAT            // разрешен повтор соединения
                if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(mqttSettintg.mqtt_id); }  // вторая попытка
                #endif
               }       
               else  
               {
                w5200_MQTT.setServer(mqttSettintg.mqtt_serverIP,mqttSettintg.mqtt_port);
                w5200_MQTT.connect(mqttSettintg.mqtt_id,mqttSettintg.mqtt_login,mqttSettintg.mqtt_password); //  установить параметры сервера Соедиенение с сервером MQTT
                #ifdef MQTT_REPEAT            // разрешен повтор соединения
                if (!w5200_MQTT.connected()) { _delay(20); ShowSockRegisters(W5200_SOCK_SYS); w5200_MQTT.connect(mqttSettintg.mqtt_id,mqttSettintg.mqtt_login,mqttSettintg.mqtt_password);}   // вторая попытка
                #endif
               }
         }
return  w5200_MQTT.connected();                  
}

// Попытаться сбросить системный сокет МЮТЕКС уже захвачен!!
const char* NoClosed={"Problem: W5200_SOCK_SYS sock is not closed, SR=%s\n"};
boolean clientMQTT::closeSockSys(boolean debug)
{
uint8_t x, sr;
sr = W5100.readSnSR(W5200_SOCK_SYS);                                                   // Прочитать статус сокета
if(sr==SnSR::CLOSED) return true;                                                      // Сокет уже закрыт выходим
if ((sr==SnSR::ESTABLISHED)||(sr==SnSR::CLOSE_WAIT)) { W5100.execCmdSn(W5200_SOCK_SYS, Sock_DISCON);_delay(20);}// если надо разорать соедиение

W5100.execCmdSn(W5200_SOCK_SYS, Sock_CLOSE);       // закрыть сокет
W5100.writeSnIR(W5200_SOCK_SYS, 0xFF);             // сбросить прерывания
_delay(20);
if((x=W5100.readSnSR(W5200_SOCK_SYS))!=SnSR::CLOSED) { if(debug) journal.jprintf((char*)NoClosed,byteToHex(x)); return false;}   // проверка на закрытие сокета
return true; 
}

