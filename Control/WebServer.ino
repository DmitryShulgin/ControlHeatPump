/* Copyright (c) 2016-2018 by Pavel Panfilov <firstlast2007@gmail.com> skype pav2000pav
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
// ------------------------------------------------------------------------------------------------------
// Простой веб серевер с обработкой запросов
// ------------------------------------------------------------------------------------------------------
#include "HeatPump.h"
#include "utility/socket.h" 
#include "FreeRTOS_ARM.h"     
// HTTP типы запросов
#define HTTP_invalid   0
#define HTTP_GET       1      // Обычный запрос GET
#define HTTP_REQEST    2      // Наш запрос "GET /&"
#define HTTP_POST      3      // POST передача файла
#define HTTP_POST_     4      // POST - подготовка
#define UNAUTHORIZED   5      // Авторизация не пройдена
#define BAD_LOGIN_PASS 6      // Не верный логин или пароль


extern "C" void TaskGetRunTimeStats(void);
extern void  get_txtSettings(uint8_t thread);
extern void  get_fileState(uint8_t thread);
extern void  get_fileSettings(uint8_t thread);
extern void  get_txtJournal(uint8_t thread);
extern void  get_datTest(uint8_t thread);
extern uint16_t get_csvChart(uint8_t thread);
extern int16_t  get_indexNoSD(uint8_t thread);
extern void  noCsvChart_SD(uint8_t thread);


// Названия режимов теста
const char *noteTestMode[] =   {"NORMAL","SAFE_TEST","TEST","HARD_TEST"};
// Названия типы фреонов
const char *noteFreon[]    =   {"R22","R410A","R600","R134A","R407C","R12","R290","R404A","R717"};
// Названия правило работы ЭРВ
const char *noteRuleEEV[]   =  {"TEVAOUT-TEVAIN","TRTOOUT-TEVAIN","TEVAOUT-T[PEVA]","TRTOOUT-T[PEVA]","Table[EVA CON]","Manual"};
// Описание правила работы ЭРВ
const char *noteRemarkEEV[] = {"Перегрев равен температуре на выходе испарителя - температура на входе испарителя. Есть возможность введения поправки (добавляется).",
                               "Перегрев равен температуре на выходе РТО - температура на входе испарителя. Есть возможность введения поправки (добавляется).",
                               "Перегрев равен температура на выходе испарителя - температура пересчитанной из давления на выходе испарителя. Есть выбор фреона и поправка (добавляется).",
                               "Перегрев равен температура на выходе РТО - температура пересчитанной из давления на выходе испарителя. Есть выбор фреона и поправка (добавляется).",
                               "Перегрев не вычисляется. ЭРВ открывается по значению из таблицы, которая увязывает температуры испарителя и конденсатора с шагами открытия ЭРВ.",
                               "Перегрев не вычисляется. Ручной режим, ЭРВ открывается на заданное число шагов."};
// Описание режима теста
static const char *noteRemarkTest[] = {"Тестирование отключено. Основной режим работы",
                                       "Значения датчиков берутся из соответвующих полей ""Тест"", работа исполнительны устройств эмулируется. Безопасно.",
                                       "Значения датчиков берутся из соответвующих полей ""Тест"", исполнительные устройства работают за исключениме компрессора (FC и RCOMP). Почти безопасно.",
                                       "Значения датчиков берутся из соответвующих полей ""Тест"", все исполнительные устройства работают. Внимание! Может быть поврежден компрессор!."};
                               
                               
const char* file_types[] = {"text/html", "image/x-icon", "text/css", "application/javascript", "image/jpeg", "image/png", "image/gif", "text/plain", "text/ajax"};

const char*  pageUnauthorized     = {"HTTP/1.0 401 Unauthorized\r\nWWW-Authenticate: Basic real_m=Admin Zone\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n"};
const char* HEADER_FILE_NOT_FOUND = {"HTTP/1.1 404 Not Found\r\n\r\n<html>\r\n<head><title>404 NOT FOUND</title><meta charset=\"utf-8\" /></head>\r\n<body><h1>404 NOT FOUND</h1></body>\r\n</html>\r\n\r\n"};
//const char* HEADER_FILE_WEB       = {"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: keep-alive\r\n\r\n"}; // КЕШ НЕ ИСПОЛЬЗУЕМ
const char* HEADER_FILE_WEB       = {"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nCache-Control: max-age=3600, must-revalidate\r\n\r\n"}; // КЕШ ИСПОЛЬЗУЕМ
const char* HEADER_FILE_CSS       = {"HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nConnection: keep-alive\r\nCache-Control: max-age=3600, must-revalidate\r\n\r\n"}; // КЕШ ИСПОЛЬЗУЕМ

const char* HEADER_ANSWER         = {"HTTP/1.1 200 OK\r\nContent-Type: text/ajax\r\nAccess-Control-Allow-Origin: *\r\n\r\n"};  // начало ответа на запрос
const char* NO_SUPPORT            = {"no support"};                                                                            // сокращение места
const char* NO_STAT               = {"Statistics are not supported in the firmware"};

void web_server(uint8_t thread)
{
 int32_t len;
 int8_t sock;

if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE)  { return;}          // Захват семафора потока или ОЖИДАНИЕ W5200_TIME_WAIT, если семафор не получен то выходим

Socket[thread].sock=-1;                      // Сокет свободный

SPI_switchW5200();                    // Это лишнее но для надежности пусть будет
    for (sock = 0; sock < W5200_SOCK_SYS; sock++)  // Цикл по сокетам веб сервера!!!! служебный не трогаем!!
       {

        #if    W5200_THREARD < 2 
         if (Socket[0].sock==sock)  continue;   // исключение повторного захвата сокетов
        #elif  W5200_THREARD < 3
          if((Socket[0].sock==sock)||(Socket[1].sock==sock))  continue;   // исключение повторного захвата сокетов
        #elif  W5200_THREARD < 4
          if((Socket[0].sock==sock)||(Socket[1].sock==sock)||(Socket[2].sock==sock))  continue;   // исключение повторного захвата сокетов
        #else
          if((Socket[0].sock==sock)||(Socket[1].sock==sock)||(Socket[2].sock==sock)||(Socket[3].sock==sock))  continue;   // исключение повторного захвата сокетов
        #endif

        // Настройка  переменных потока для работы
        Socket[thread].http_req_type = HTTP_invalid;        // нет полезной инфы
        SETBIT0(Socket[thread].flags,fABORT_SOCK);          // Сокет сброса нет
        SETBIT0(Socket[thread].flags,fUser);                // Признак идентификации как обычный пользователь (нужно подменить файл меню)
        Socket[thread].client =  server1.available_(sock);  // надо обработать
        Socket[thread].sock=sock;                           // запомнить сокет с которым рабоатет поток

if (Socket[thread].client) // запрос http заканчивается пустой строкой
{  
        while (Socket[thread].client.connected())
        {   
          // Ставить вот сюда
            if (Socket[thread].client.available()) 
             {
             len=Socket[thread].client.get_ReceivedSizeRX();                            // получить длину входного пакета
             if(len>W5200_MAX_LEN-1) len=W5200_MAX_LEN-1;                               // Ограничить размером в максимальный размер пакета w5200
             Socket[thread].client.read(Socket[thread].inBuf,len);                      // прочитать буфер
             Socket[thread].inBuf[len]=0;                                // обрезать строку
                     // Ищем в запросе полезную информацию (имя файла или запрос ajax)
                     #ifdef LOG
                        journal.jprintf("$INPUT: %s\n",(char*)Socket[thread].inBuf);
                     #endif
                      // пройти авторизацию и разобрать заголовок -  получить имя файла, тип, тип запроса, и признак меню пользователя
                      Socket[thread].http_req_type = GetRequestedHttpResource(thread);  
                    #ifdef LOG
                        journal.jprintf("\r\n$QUERY: %s\r\n",Socket[thread].inPtr);
                     #endif
                //       Serial.print(">>Thread=");Serial.print(thread); Serial.print(" Sock=");Serial.print(sock);Serial.print(" IP=");Serial.print(IPAddress2String(temp));Serial.print(" MAC=");Serial.println(MAC2String(mac));
                      switch (Socket[thread].http_req_type)  // По типу запроса
                          {
                          case HTTP_invalid:
                               {
                               journal.jprintf("Error GET reqest\n");
                               sendConstRTOS(thread,"HTTP/1.1 Error GET reqest\r\n\r\n");
                               break;
                               }
                          case HTTP_GET:     // чтение файла
                               {
                               // Для обычного пользователя подменить файл меню, для сокращения функционала
                               if ((GETBIT(Socket[thread].flags,fUser))&&(strcmp(Socket[thread].inPtr,"menu.js")==0)) strcpy(Socket[thread].inPtr,"menu-user.js");
                               readFileSD(Socket[thread].inPtr,thread); 
                               break;
                               }
                          case HTTP_REQEST:  // Запрос AJAX
                               {
                               strcpy(Socket[thread].outBuf,HEADER_ANSWER);   // Начало ответа
                               parserGET(Socket[thread].inPtr,Socket[thread].outBuf,sock);    // выполнение запроса
                               #ifdef LOG
                                journal.jprintf("$RETURN: %s\n",Socket[thread].outBuf);
                               #endif
                               if (sendBufferRTOS(thread,(byte*)(Socket[thread].outBuf),strlen(Socket[thread].outBuf))==0) journal.jprintf("$Error send buf:  %s\n",(char*)Socket[thread].inBuf);
                               break;
                               }
                          case HTTP_POST:    // загрузка настроек
                               {
                               strcpy(Socket[thread].outBuf,HEADER_ANSWER);   // Начало ответа
                               if(parserPOST(thread))    strcat(Socket[thread].outBuf,"Настройки из выбранного файла восстановлены, CRC16 OK\r\n\r\n");
                               else                      strcat(Socket[thread].outBuf,"Ошибка восстановления настроек из файла (см. журнал)\r\n\r\n");
                               if (sendBufferRTOS(thread,(byte*)(Socket[thread].outBuf),strlen(Socket[thread].outBuf))==0) journal.jprintf("$Error send buf:  %s\n",(char*)Socket[thread].inBuf);
                               break;
                               }
                          case HTTP_POST_: // предвариательный запрос post
                               {
                                sendConstRTOS(thread,"HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: HEAD, OPTIONS, GET, POST\r\nAccess-Control-Allow-Headers: Overwrite, Content-Type, Cache-Control\r\n\r\n");  
                                break;
                               }
                         case UNAUTHORIZED:
                               {
                               journal.jprintf("$UNAUTHORIZED\n");
                               sendConstRTOS(thread,pageUnauthorized);
                               break;
                               }
                          case BAD_LOGIN_PASS:
                               {
                               journal.jprintf("$Wrong login or password\n");
                               sendConstRTOS(thread,pageUnauthorized);
                               break;
                               }

                          default: journal.jprintf("$Unknow  %s\n",(char*)Socket[thread].inBuf);   break;        
                         }
                   
                   Socket[thread].inBuf[0]=0;    break;   // Подготовить к следующей итерации
             } // end if (client.available())
        } // end while (client.connected())
       taskYIELD();
       Socket[thread].client.stop();   // close the connection
       Socket[thread].sock=-1;
     } // end if (client)
#ifdef FAST_LIB  // Переделка
  }  // for (int sock = 0; sock < W5200_SOCK_SYS; sock++)
#endif
    SemaphoreGive(xWebThreadSemaphore);              // Семафор отдать
}

//  Чтение файла с SD или его генерация
void readFileSD(char *filename,uint8_t thread)
{   
  volatile int n,i;
  SdFile  webFile; 

  //  journal.jprintf("$Thread: %d socket: %d read file: %s\n",thread,Socket[thread].sock,filename); 

  	  	 // В начале обрабатываем генерируемые файлы (для выгрузки из контроллера)
         if (strcmp(filename,"state.txt")==0)     {get_txtState(thread,true); return;}  
         if (strcmp(filename,"settings.txt")==0)  {get_txtSettings(thread);   return;}      
         if (strcmp(filename,"settings.bin")==0)  {get_binSettings(thread);   return;}   
         if (strcmp(filename,"chart.csv")==0)     {get_csvChart(thread);      return;}   
         if ((strcmp(filename,FILE_CHART)==0)&&(!card.exists(FILE_CHART))) {noCsvChart_SD(thread); return;}   // Если файла статистики нет то сгенерить файл с объяснением
         if (strcmp(filename,"journal.txt")==0)   {get_txtJournal(thread);    return;}  
         if (strcmp(filename,"test.dat")==0)      {get_datTest(thread);       return;}  
         #ifdef I2C_EEPROM_64KB    
         if (strcmp(filename,"statistic.csv")==0) {get_csvStatistic(thread);  return;}  
         #endif
         if (!HP.get_fSD())                       { get_indexNoSD(thread);    return;}                  // СД карта не работает - упрощенный интерфейс
         
          // Чтение с карты  файлов
          SPI_switchSD();
          if (!card.exists(filename))  // проверка на сущестование файла
              {  
               SPI_switchW5200(); 
               sendConstRTOS(thread,HEADER_FILE_NOT_FOUND);
               journal.jprintf((char*)"$WARNING - Can't find %s file!\n",filename); 
               return;    
              } // файл не найден
              
          for(i=0;i<SD_REPEAT;i++)   // Делаем SD_REPEAT попыток открытия файла
          {
              if (!webFile.open(filename, O_READ))    // Карта не читатаеся
              {
                if (i>=SD_REPEAT-1)                   // Исчерпано число попыток
                 {
                  SPI_switchW5200();  
                  sendConstRTOS(thread,HEADER_FILE_NOT_FOUND);
                  journal.jprintf("$ERROR - opening %s for read failed!\n",filename); 
                  HP.message.setMessage(pMESSAGE_SD,(char*)"Ошибка открытия файла с SD карты",0);    // сформировать уведомление об ошибке чтения
                  HP.set_fSD(false);                                                                 // Отказ карты, работаем без нее
                  return;
                  }//if
              }
              else  break;  // Прочиталось
			  _delay(50);	
              journal.jprintf("Error opening file %s repeat open . . .\n",filename);
 
          }  // for     

          SPI_switchW5200();         // переключение на сеть

                // Файл открыт читаем данные и кидаем в сеть
                 #ifdef LOG  
                   journal.jprintf("$Thread: %d socket: %d read file: %s\n",thread,Socket[thread].sock,filename); 
                 #endif
              //   if (strstr(filename,".css")>0) sendConstRTOS(thread,HEADER_FILE_CSS);
                 if (strstr(filename,".css")!=NULL) sendConstRTOS(thread,HEADER_FILE_CSS); // разные заголовки
                 else                               sendConstRTOS(thread,HEADER_FILE_WEB);
                 SPI_switchSD();
               while ((n=webFile.read(Socket[thread].outBuf,sizeof(Socket[thread].outBuf))) > 0) 
                 {
                  SPI_switchW5200();
                  if (sendBufferRTOS(thread,(byte*)(Socket[thread].outBuf),n)==0) break;
                  SPI_switchSD();    
                } // while
               SPI_switchSD(); 
               webFile.close(); 
               SPI_switchW5200();         
 }

// ========================== P A R S E R  G E T =================================
// Разбор и обработка строк запросов buf (начало &) входная строка strReturn выходная
// ТОЛЬКО запросы!
// возвращает число обработанных одиночных запросов
int parserGET(char *buf, char *strReturn, int8_t sock)
{ 
   
  char *str,*x,*y, *z;
  int count=0;
  int param=-1,p=-1;
  char temp[10];   
  float pm=0;
  int8_t i;
  // переменные для удаленных датчиков
   #ifdef SENSOR_IP                           // Получение данных удаленного датчика
       char *ptr;
       int16_t a,b,c,d;
       int32_t e; 
   #endif
  
 // strcpy(strReturn,"&");   // начало запроса
   strcat(strReturn,"&");   // начало запроса
  strstr(buf,"&&")[1]=0;   // Обрезать строку после комбинации &&
  while ((str = strtok_r(buf, "&", &buf)) != NULL) // разбор отдельных комманд
   {
   count++;
   if ((strpbrk(str,"="))==0) // Повторить тело запроса и добавить "=" ДЛЯ НЕ set_ запросов
     {
      strcat(strReturn,str); strcat(strReturn,"="); 
     } 
     if (strlen(strReturn)>sizeof(Socket[0].outBuf)-200)  // Контроль длины выходной строки - если слишком длинная обрезаем и выдаем ошибку 200 байт на заголовок
     {  
         strcat(strReturn,"E07"); 
         strcat(strReturn,"&") ;
         journal.jprintf("$ERROR - strReturn long, request circumcised . . . \n"); 
         journal.jprintf("%s\n",strReturn);    
          break;   // выход из обработки запроса
     }
    // 1. Функции без параметра
   if (strcmp(str,"TEST")==0)   // Команда TEST
       {
       strcat(strReturn,int2str(random(-50,50)));
       strcat(strReturn,"&") ;
       continue;
       }

//   if (strcmp(str,"TASK_STAT")==0)  // Функция получение статистики по задачам
//       {
//       #ifdef STAT_FREE_RTOS   // определена в utility/FreeRTOSConfig.h 
//         strcat(strReturn,cStrEnd);
//         vTaskGetRunTimeStats(strReturn+strlen(strReturn));
//       #else
//         strcat(strReturn,NO_SUPPORT);
//       #endif
//       strcat(strReturn,"&");  continue;
//       }
  if (strcmp(str,"TASK_LIST")==0)  // Функция получение списка задач и статистики
       {
         #ifdef STAT_FREE_RTOS   // определена в utility/FreeRTOSConfig.h 
         //strcat(strReturn,cStrEnd);
         vTaskList(strReturn+strlen(strReturn));
         #else
         strcat(strReturn,NO_SUPPORT);
       #endif
         strcat(strReturn,"&");  continue;
       } 
   if (strcmp(str,"RESET_STAT")==0)   // Команда очистки статистики (в зависимости от типа)
       {
      
       #ifndef I2C_EEPROM_64KB     // Статистика в памяти
           strcat(strReturn,"Статистика не поддерживается в конфигурации . . .&");
           journal.jprintf("No support statistics (low i2C eeprom) . . .\n"); 
       #else                      // Статистика в ЕЕПРОМ
           if (HP.get_modWork()==pOFF)
             {
              strcat(strReturn,"Форматирование I2C EEPROM статистики, ожидайте 10 сек . . .&");
              HP.sendCommand(pSFORMAT);        // Послать команду форматирование статитсики
             }
             else strcat(strReturn,"The heat pump must be switched OFF&");  
       #endif
       continue;
       }         
        
     if (strcmp(str,"RESET_JOURNAL")==0)   // Команда очистки журнала (в зависимости от типа)
       {
      
       #ifndef I2C_EEPROM_64KB     // журнал в памяти
           strcat(strReturn,"Сброс системного журнала в RAM . . .&");
           journal.Clear();       // Послать команду на очистку журнала в памяти
           journal.jprintf("Reset system RAM journal . . .\n"); 
       #else                      // Журнал в ЕЕПРОМ
            strcat(strReturn,"Форматирование I2C EEPROM журнала, ожидайте 15 сек . . .&");
            HP.sendCommand(pJFORMAT);        // Послать команду форматирование журнала
       #endif
       continue;
       }        
     if (strcmp(str,"RESET_JOURNAL")==0)   // Команда очистки журнала (в зависимости от типа)
       {
      
       #ifndef I2C_EEPROM_64KB     // журнал в памяти
           strcat(strReturn,"Сброс системного журнала в RAM . . .&");
           journal.Clear();       // Послать команду на очистку журнала в памяти
           journal.jprintf("Reset system RAM journal . . .\n"); 
       #else                      // Журнал в ЕЕПРОМ
            strcat(strReturn,"Форматирование I2C EEPROM журнала, ожидайте 15 сек . . .&");
            HP.sendCommand(pJFORMAT);        // Послать команду форматирование журнала
       #endif
       continue;
       }  
    if (strcmp(str,"RESET")==0)   // Команда сброса контроллера
       {
       strcat(strReturn,"Сброс контроллера, подождите 10 секунд . . .");
       strcat(strReturn,"&");
       HP.sendCommand(pRESET);        // Послать команду на сброс
       continue;
       }
    if (strcmp(str,"RESET_COUNT")==0) // Команда RESET_COUNT
       {
       journal.jprintf("$RESET counter moto hour . . .\n"); 
       strcat(strReturn,"Сброс счетчика моточасов за сезон");
       strcat(strReturn,"&") ;
       HP.resetCount(false);
       continue;
       }
   if (strcmp(str,"RESET_ALL_COUNT")==0) // Команда RESET_COUNT
       {
       journal.jprintf("$RESET All counter moto hour . . .\n"); 
       strcat(strReturn,"Сброс ВСЕХ счетчика моточасов");
       strcat(strReturn,"&") ;
       HP.resetCount(true);  // Полный сброс
       continue;
       }    
   if (strcmp(str,"RESET_SETTINGS")==0) // Команда сброса настроек HP
   {
	   if(HP.get_State() == pOFF_HP) {
	       journal.jprintf("$RESET All HP settings . . .\n");
	       HP.headerEEPROM.magic = 0;
	   	   writeEEPROM_I2C(I2C_SETTING_EEPROM, (byte*)&HP.headerEEPROM.magic, sizeof(HP.headerEEPROM.magic));
	       HP.sendCommand(pRESET);        // Послать команду на сброс
	       //HP.resetSettingHP(); // не работает!!
	       strcat(strReturn, "OK&");
	       continue;
	   }
   }
   if (strcmp(str,"get_status")==0) // Команда get_status Получить состояние ТН - основные параметры ТН
       {
        strcat(strReturn,HP.get_datetime(pTIME));                        strcat(strReturn,"|");
        strcat(strReturn,HP.get_datetime(pDATE));                        strcat(strReturn,"|");
        strcat(strReturn,VERSION);                                       strcat(strReturn,"|");
        strcat(strReturn,int2str(freeRam()+HP.startRAM));                strcat(strReturn,"b|");
        strcat(strReturn,int2str(100-HP.CPU_IDLE));                      strcat(strReturn,"%|");
        strcat(strReturn,TimeIntervalToStr(HP.get_uptime()));            strcat(strReturn,"|");
        #ifdef EEV_DEF
        strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Overheat()/100,2));strcat(strReturn,"°C|");
        #else
        strcat(strReturn,"-°C|");
        #endif
        if (HP.dFC.get_present()) {strcat(strReturn,HP.dFC.get_paramFC(pFC));strcat(strReturn,"Гц|");} // В зависимости от наличия инвертора
        else                      {strcat(strReturn," - ");strcat(strReturn,"Гц|");}
        if (HP.get_errcode()==OK)  strcat(strReturn,HP.StateToStr());                   // Ошибок нет
        else {strcat(strReturn,"Error "); strcat(strReturn,int2str(HP.get_errcode()));} // есть ошибки
        strcat(strReturn,"|");   strcat(strReturn,"&") ;    continue;
       }
       
   if (strcmp(str,"get_version")==0) // Команда get_version
       {
       strcat(strReturn,VERSION);
       strcat(strReturn,"&") ;
       continue;
       }
   if (strcmp(str,"get_uptime")==0) // Команда get_uptime
       {
       strcat(strReturn,TimeIntervalToStr(HP.get_uptime()));
       strcat(strReturn,"&") ;
       continue;
       }
    if (strcmp(str,"get_startDT")==0) // Команда get_startDT
       {
       strcat(strReturn,DecodeTimeDate(HP.get_startDT()));
       strcat(strReturn,"&") ;
       continue;
       }
   if (strcmp(str,"get_resetCause")==0) // Команда  get_resetCause
       {
       strcat(strReturn,ResetCause());
       strcat(strReturn,"&") ;
       continue;
       }
    if (strcmp(str,"get_config")==0)  // Функция get_config
       {
       strcat(strReturn,CONFIG_NAME);
       strcat(strReturn,"&") ;
       continue;
       }
    if (strcmp(str,"get_configNote")==0)  // Функция get_configNote
       {
       strcat(strReturn,CONFIG_NOTE);
       strcat(strReturn,"&") ;
       continue;
       }
    if (strcmp(str,"get_freeRam")==0)  // Функция freeRam
       {
       strcat(strReturn,int2str(freeRam()+HP.startRAM));
       strcat(strReturn," b&") ;
       continue;
       }   
    if (strcmp(str,"get_loadingCPU")==0)  // Функция freeRam
       {
        strcat(strReturn,int2str(100-HP.CPU_IDLE));
        strcat(strReturn,"%&") ;
       continue;
       }        
     if (strcmp(str,"get_socketInfo")==0)  // Функция  get_socketInfo
       {
       socketInfo(strReturn);    // Информация  о сокетах
       strcat(strReturn,"&") ;
       continue;
       }
     if (strcmp(str,"get_socketRes")==0)  // Функция  get_socketRes
       {
       strcat(strReturn,int2str(HP.socketRes()));
       strcat(strReturn,"&") ;
       continue;
       }  
     if (strcmp(str,"get_listChart")==0)  // Функция get_listChart - получить список доступных графиков
       {
       HP.get_listChart(strReturn, true);  // строка добавляется
       strcat(strReturn,"&") ;
       continue;
       }
     if (strcmp(str,"get_listStat")==0)  // Функция get_listChart - получить список доступных статистик
       {
       #ifdef I2C_EEPROM_64KB 
       HP.Stat.get_listStat(strReturn, true);  // строка добавляется
       #else
       strcat(strReturn,"absent:1;") ;
       #endif
       strcat(strReturn,"&") ;
       continue;
       }   
    if (strncmp(str,"get_listProfile", 15)==0)  // Функция get_listProfile - получить список доступных профилей
       {
       HP.Prof.get_list(strReturn /*,HP.Prof.get_idProfile()*/);  // текущий профиль
       strcat(strReturn,"&") ;
       continue;
       }
       if (strcmp(str,"update_NTP")==0)  // Функция update_NTP обновление времени по NTP
       {
      // set_time_NTP();                                                 // Обновить время
       HP.timeNTP=0;                                    // Время обновления по NTP в тиках (0-сразу обновляемся)
       strcat(strReturn,"Update time from NTP");
       strcat(strReturn,"&");
       continue;
       }
       if ((strcmp(str,"set_updateNet")==0)||(strcmp(str,"RESET_NET")==0))  // Функция Сброс w5200 и применение сетевых настроек, подождите 5 сек . . .
       {
       journal.jprintf("Update network setting . . .\r\n");
       HP.sendCommand(pNETWORK);        // Послать команду применение сетевых настроек
       strcat(strReturn,"Сброс Wiznet w5XXX и применение сетевых настроек, подождите 5 сек . . .");
       strcat(strReturn,"&") ;
       continue;
       }         
    if (strcmp(str,"get_WORK")==0)  // Функция get_WORK  ТН включен если он работает или идет его пуск
       {
       if ((HP.get_State()==pWORK_HP)||(HP.get_State()==pSTARTING_HP))  strcat(strReturn,"ON"); else  strcat(strReturn,"OFF"); strcat(strReturn,"&") ;    continue;
       }   
    if (strcmp(str,"get_MODE")==0)  // Функция get_MODE в каком состояниии находится сейчас насос
       {
       strcat(strReturn,HP.StateToStr());
       strcat(strReturn,"&") ;    continue;
       }    
 
    if (strcmp(str,"get_modeHP")==0)           // Функция get_modeHP - получить режим отопления ТН
        {
            switch ((MODE_HP)HP.get_mode())   // режим работы отопления
                   {
                    case pOFF:   strcat(strReturn,(char*)"Выключено:1;Отопление:0;Охлаждение:0;"); break;
                    case pHEAT:  strcat(strReturn,(char*)"Выключено:0;Отопление:1;Охлаждение:0;"); break;
                    case pCOOL:  strcat(strReturn,(char*)"Выключено:0;Отопление:0;Охлаждение:1;"); break;
                    default: HP.set_mode(pOFF);  strcat(strReturn,(char*)"Выключено:1;Отопление:0;Охлаждение:0;"); break;   // Исправить по умолчанию
                   }  
          strcat(strReturn,"&") ; continue;
        } // strcmp(str,"get_modeHP")==0)   
    if (strcmp(str,"get_testMode")==0)  // Функция get_testMode
       {
       for(i=0;i<=HARD_TEST;i++) // Формирование списка
           { strcat(strReturn,noteTestMode[i]); strcat(strReturn,":"); if(i==HP.get_testMode()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,";");  }          
       strcat(strReturn,"&") ;    continue;
       }  
    if (strcmp(str,"get_remarkTest")==0)  // Функция remarkTest
       {
       switch (HP.get_testMode())
         {
          case NORMAL:    strcat(strReturn,noteRemarkTest[0]);     break; //  Режим работа не тст, все включаем
          case SAFE_TEST: strcat(strReturn,noteRemarkTest[1]);     break; //  Ничего не включаем
          case TEST:      strcat(strReturn,noteRemarkTest[2]);     break; //  Включаем все кроме компрессора
          case HARD_TEST: strcat(strReturn,noteRemarkTest[3]);     break; //  Все включаем и компрессор тоже
         }     
       strcat(strReturn,"&") ;    continue;
       }  
     
        
    if (strncmp(str, "set_SAVE", 8) == 0)  // Функция set_SAVE -
		{
			if(strncmp(str+8, "_SCHDLR", 7) == 0) {
				strcat(strReturn, int2str(HP.Schdlr.save())); // сохранение расписаний
			} else {
				strcat(strReturn, int2str(HP.save())); // сохранение настроек ВСЕХ!
				HP.save_motoHour();
			}
			strcat(strReturn,"&");
			continue;
		}
    if (strncmp(str, "set_LOAD", 8) == 0)  // Функция set_LOAD -
		{
			if(strncmp(str+8, "_SCHDLR", 7) == 0) {
				strcat(strReturn, int2str(HP.Schdlr.load())); // сохранение расписаний
			} else {
			}
			strcat(strReturn,"&");
			continue;
		}
    if (strcmp(str,"set_ON")==0)  // Функция set_ON
       {
       HP.sendCommand(pSTART);        // Послать команду на пуск ТН
       if (HP.get_State()==pWORK_HP)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); strcat(strReturn,"&") ;    continue;
       }   
    if (strcmp(str,"set_OFF")==0)  // Функция set_OFF
       {
       HP.sendCommand(pSTOP);        // Послать команду на останов ТН
       if (HP.get_State()==pWORK_HP)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); strcat(strReturn,"&") ;    continue;
       }   
    if (strcmp(str,"get_errcode")==0)  // Функция get_errcode
       {
       strcat(strReturn,int2str(HP.get_errcode())); strcat(strReturn,"&") ;    continue;
       }   
    if (strcmp(str,"get_error")==0)  // Функция get_error
       {
       strcat(strReturn,HP.get_lastErr()); strcat(strReturn,"&") ;    continue;
       } 
    if (strcmp(str,"get_tempSAM3x")==0)  // Функция get_tempSAM3x  - получение температуры чипа sam3x
       {
       strcat(strReturn,ftoa(temp,temp_DUE(),2)); strcat(strReturn,"&"); continue;
       }   
    if (strcmp(str,"get_tempDS3231")==0)  // Функция get_tempDS3231  - получение температуры DS3231
       {
       strcat(strReturn,ftoa(temp,getTemp_RtcI2C(),2)); strcat(strReturn,"&"); continue;
       }       
    if (strcmp(str,"get_VCC")==0)  // Функция get_VCC  - получение напряжение питания контроллера
       {
       #ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
           strcat(strReturn,ftoa(temp,(float)HP.AdcVcc/K_VCC_POWER,2)); 
        #else 
           strcat(strReturn,NO_SUPPORT); 
        #endif
       strcat(strReturn,"&"); continue;
       }       
    if (strcmp(str,"get_OneWirePin")==0)  // Функция get_OneWirePin
       {
       #ifdef ONEWIRE_DS2482  
        strcat(strReturn,"I2C"); 
		#ifdef ONEWIRE_DS2482_SECOND
        strcat(strReturn,"(2)"); 
		#endif
       #else
        strcat(strReturn,"D"); strcat(strReturn,int2str((int)(PIN_ONE_WIRE_BUS))); 
       #endif
       strcat(strReturn,"&") ;    continue;
       }       
    if (strcmp(str,"scan_OneWire")==0)  // Функция scan_OneWire  - сканирование датчикиков
       {
    	HP.scan_OneWire(strReturn); strcat(strReturn,"&"); continue;
       }
     if (strstr(str,"get_numberIP"))  // Удаленные датчики - получить число датчиков
       { 
        #ifdef SENSOR_IP                           
         strcat(strReturn,int2str(IPNUMBER));strcat(strReturn,"&"); continue;
        #else
         strcat(strReturn,"0&");continue;
        #endif 
       }   
      if (strcmp(str,"set_testStat")==0)  // сгенерить тестовые данные статистики ОЧИСТКА СТАРЫХ ДАННЫХ!!!!!
       {
       #ifdef I2C_EEPROM_64KB  // рабоатет на выключенном ТН
       if (HP.get_modWork()==pOFF)
       {
         HP.Stat.generate_TestData(STAT_POINT); // Сгенерировать статистику STAT_POINT точек только тестирование
         strcat(strReturn,"Generation of test data - OK&");
       }
       else strcat(strReturn,"The heat pump must be switched OFF&");  
       #else
       strcat(strReturn,NO_STAT);
       #endif
       continue;
       }   
     if (strcmp(str,"get_infoStat")==0)  // Получить информацию о статистике
       {
       #ifdef I2C_EEPROM_64KB    
       HP.Stat.get_Info(strReturn,true);
       #else
       strcat(strReturn,NO_STAT) ;
       #endif
       strcat(strReturn,"&") ;
       continue;
       }   

    if (strstr(str,"get_infoESP"))  // Удаленные датчики - запрос состояния контрола
       { 
       // TIN, TOUT, TBOILER, ВЕРСИЯ, ПАМЯТЬ, ЗАГРУЗКА, АПТАЙМ, ПЕРЕГРЕВ, ОБОРОТЫ, СОСТОЯНИЕ.
        strcat(strReturn,ftoa(temp,(float)HP.sTemp[TIN].get_Temp()/100.0,1));     strcat(strReturn,";");
        strcat(strReturn,ftoa(temp,(float)HP.sTemp[TOUT].get_Temp()/100.0,1));    strcat(strReturn,";");
        strcat(strReturn,ftoa(temp,(float)HP.sTemp[TBOILER].get_Temp()/100.0,1)); strcat(strReturn,";");
        strcat(strReturn,VERSION);                                                strcat(strReturn,";");        
       strcat(strReturn,int2str(freeRam()+HP.startRAM));                          strcat(strReturn,";");
        strcat(strReturn,int2str(100-HP.CPU_IDLE));                               strcat(strReturn,";");
        strcat(strReturn,TimeIntervalToStr(HP.get_uptime()));                     strcat(strReturn,";");
        #ifdef EEV_DEF 
        strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Overheat()/100,2));         strcat(strReturn,";");
        #else
        strcat(strReturn,"-;");
        #endif
        if (HP.dFC.get_present()) {strcat(strReturn,HP.dFC.get_paramFC(pFC));     strcat(strReturn,";");} // В зависимости от наличия инвертора
        else                      {strcat(strReturn," - ");                       strcat(strReturn,";");}
      //  strcat(strReturn,HP.StateToStrEN());                                      strcat(strReturn,";");
        if (HP.get_errcode()==OK)  strcat(strReturn,HP.StateToStrEN());                   // Ошибок нет
        else {strcat(strReturn,"Error "); strcat(strReturn,int2str(HP.get_errcode()));} // есть ошибки
        strcat(strReturn,";");   strcat(strReturn,"&") ;    continue;
       }   
    
     if (strcmp(str,"get_infoFC")==0)  // get_infoFC
       {
        
       if (!HP.dFC.get_present()) { strcat(strReturn,"|Данные не доступны (нет инвертора)|;&"); continue;}          // Инвертора нет в конфигурации
       if(HP.dFC.get_blockFC())  { strcat(strReturn,"|Данные не доступны (нет связи по Modbus, инвертор заблокирован)|;&"); continue;}  // Инвертор заблокирован
       // Все нормально опрашиваем инвертор
       #ifndef FC_ANALOG_CONTROL     
         HP.dFC.get_infoFC(strReturn);
       #else
         strcat("Данные не доступны, работа через анлоговый вход|;","&") ;
       #endif  
       strcat(strReturn,"&") ;    continue;
       }       
       
    if (strcmp(str,"CONST")==0)   // Команда CONST  Информация очень большая по этому разбито на 2 запроса CONST CONST1
        {
       strcat(strReturn,"VERSION|Версия прошивки|");strcat(strReturn,VERSION);strcat(strReturn,";");
       strcat(strReturn,"__DATE__ __TIME__|Дата и время сборки прошивки|");strcat(strReturn,__DATE__);strcat(strReturn," ");strcat(strReturn,__TIME__) ;strcat(strReturn,";");
       strcat(strReturn,"VER_SAVE|Версия формата данных, при чтении eeprom, если версии не совпадают, отказ от чтения|");strcat(strReturn,int2str(VER_SAVE));strcat(strReturn,";");
       strcat(strReturn,"CONFIG_NAME|Имя конфигурации|");strcat(strReturn,CONFIG_NAME);strcat(strReturn,";");
       strcat(strReturn,"CONFIG_NOTE|");strcat(strReturn,CONFIG_NOTE);strcat(strReturn,"|-");strcat(strReturn,";");  
       strcat(strReturn,"UART_SPEED|Скорость отладочного порта (бод)|");strcat(strReturn,int2str(UART_SPEED));strcat(strReturn,";");
       strcat(strReturn,"DEMO|Режим демонстрации (эмуляция датчиков)|");
           #ifdef DEMO
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
        strcat(strReturn,"DEBUG|Вывод в порт отладочных сообщений|");
           #ifdef DEBUG
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
        strcat(strReturn,"STAT_FREE_RTOS|Накопление статистики Free RTOS (отладка)|");
           #ifdef STAT_FREE_RTOS
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
        strcat(strReturn,"LOG|Вывод в порт лога web сервера|");
           #ifdef LOG
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
        #ifdef I2C_EEPROM_64KB   
       strcat(strReturn,"STAT_POINT|Максимальное число дней накопления статистики|");strcat(strReturn,int2str(STAT_POINT));strcat(strReturn,";");
       #endif
       strcat(strReturn,"CHART_POINT|Максимальное число точек графиков|");strcat(strReturn,int2str(CHART_POINT));strcat(strReturn,";");
       strcat(strReturn,"I2C_EEPROM_64KB|Место хранения системного журнала|");
            #ifdef I2C_EEPROM_64KB
             strcat(strReturn,"I2C flash memory;");
            #else
             strcat(strReturn,"RAM memory;");
            #endif 
        strcat(strReturn,"JOURNAL_LEN|Размер кольцевого буфера системного журнала (байт)|");strcat(strReturn,int2str(JOURNAL_LEN));strcat(strReturn,";");
                   
       // Карта
       strcat(strReturn,"SD_FAT_VERSION|Версия библиотеки SdFat|");strcat(strReturn,int2str(SD_FAT_VERSION));strcat(strReturn,";");
       strcat(strReturn,"SD_SPI_CONFIGURATION|Режим SPI SD карты, меняется в файле SdFatConfig.h (0-DMA, 1-standard, 2-software, 3-custom)|");strcat(strReturn,int2str(SD_SPI_CONFIGURATION));strcat(strReturn,";");
       strcat(strReturn,"SD_REPEAT|Число попыток чтения карты и открытия файлов, при неудаче переход на работу без карты|");strcat(strReturn,int2str(SD_REPEAT));strcat(strReturn,";");
       strcat(strReturn,"SD_SPI_SPEED|Частота SPI SD карты, пересчитывается через делитель базовой частоты CPU 84 МГц (МГц)|");strcat(strReturn,int2str(84/SD_SPI_SPEED));strcat(strReturn,";");

       // W5200
       strcat(strReturn,"W5200_THREARD|Число потоков для сетевого чипа (web сервера) "); strcat(strReturn,nameWiznet);strcat(strReturn,"|");strcat(strReturn,int2str(W5200_THREARD));strcat(strReturn,";");
       strcat(strReturn,"W5200_TIME_WAIT|Время ожидания захвата мютекса, для управления потоками (мсек)|");strcat(strReturn,int2str( W5200_TIME_WAIT));strcat(strReturn,";");
       strcat(strReturn,"W5200_STACK_SIZE|Размер стека для одного потока чипа "); strcat(strReturn,nameWiznet);strcat(strReturn," (х4 байта)|");strcat(strReturn,int2str(W5200_STACK_SIZE));strcat(strReturn,";");
       strcat(strReturn,"W5200_NUM_PING|Число попыток пинга до определения потери связи |");strcat(strReturn,int2str(W5200_NUM_PING));strcat(strReturn,";");
       strcat(strReturn,"W5200_MAX_LEN|Размер аппаратного буфера  сетевого чипа "); strcat(strReturn,nameWiznet);strcat(strReturn," (байт)|");strcat(strReturn,int2str(W5200_MAX_LEN));strcat(strReturn,";");
       strcat(strReturn,"W5200_SPI_SPEED|Частота SPI чипа "); strcat(strReturn,nameWiznet);strcat(strReturn,", пересчитывается через делитель базовой частоты CPU 84 МГц (МГц)|");strcat(strReturn,int2str(84/W5200_SPI_SPEED));strcat(strReturn,";");
       strcat(strReturn,"INDEX_FILE|Файл загружаемый по умолчанию|");strcat(strReturn,INDEX_FILE);strcat(strReturn,";");
       // Частотник
       if (HP.dFC.get_present())
       {
       strcat(strReturn,"DEVICEFC|Поддержка инвертора для компрессора|");strcat(strReturn,HP.dFC.get_name());strcat(strReturn,";");
       strcat(strReturn,"FC_MODBUS_ADR|Адрес инвертора на шине Modbus RTU|");strcat(strReturn,byteToHex(FC_MODBUS_ADR));strcat(strReturn,";");
       strcat(strReturn,"MODBUS_PORT_NUM|Используемый порт для обмена по Modbus RTU|Serial");
              if(&MODBUS_PORT_NUM==&Serial1) strcat(strReturn,cOne);
         else if(&MODBUS_PORT_NUM==&Serial2) strcat(strReturn,"2");
         else if(&MODBUS_PORT_NUM==&Serial3) strcat(strReturn,"3");   
         else strcat(strReturn,"??");
       strcat(strReturn,";");  
       strcat(strReturn,"MODBUS_PORT_SPEED|Скорость обмена (бод)|");strcat(strReturn,int2str(MODBUS_PORT_SPEED));strcat(strReturn,";");
       strcat(strReturn,"MODBUS_PORT_CONFIG|Конфигурация порта|");strcat(strReturn,"SERIAL_8N1");strcat(strReturn,";");
       strcat(strReturn,"MODBUS_TIME_WAIT|Максимальное время ожидания освобождения порта (мсек)|");strcat(strReturn,int2str(MODBUS_TIME_WAIT));strcat(strReturn,";");

       strcat(strReturn,"FC_MIN_FREQ|Минимальная частота компрессора (Гц)|");strcat(strReturn,ftoa(temp,(float)FC_MIN_FREQ/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"FC_MAX_FREQ|Максимальная частота компрессора (Гц)|");strcat(strReturn,ftoa(temp,(float)FC_MAX_FREQ/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"FC_START_FREQ|Стартовая частота компрессора (Гц)|");strcat(strReturn,ftoa(temp,(float)FC_START_FREQ/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"FC_PID_FREQ_STEP|Максимальный шаг (на увеличение) изменения частоты при ПИД регулировании (Гц)|");strcat(strReturn,ftoa(temp,(float)FC_PID_FREQ_STEP/100.0,2));strcat(strReturn,";");
         
       strcat(strReturn,"FC_MAX_POWER|Максимальная мощность инвертора (кВт)|");strcat(strReturn,ftoa(temp,(float)FC_MAX_POWER/10.0,1));strcat(strReturn,";");
       strcat(strReturn,"FC_TIME_READ|Период опроса инвертора по Modbus (мсек)|");strcat(strReturn,int2str(FC_TIME_READ));strcat(strReturn,";");
       strcat(strReturn,"FC_ACCEL_TIME|Время разгона компрессора (сек)|");strcat(strReturn,ftoa(temp,(float)FC_ACCEL_TIME/100.0,1));strcat(strReturn,";");
       strcat(strReturn,"FC_STEP_FREQ|Шаг уменьшения частоты инвертора при необходимости ее уменьшения (Гц)|");strcat(strReturn,ftoa(temp,(float)FC_STEP_FREQ/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"FC_UPTIME|Период обновления алгоритма ПИД регулятора (мсек)|");strcat(strReturn,int2str(FC_UPTIME));strcat(strReturn,";");
       }
       else strcat(strReturn,"DEVICEFC|Поддержка инвертора для компрессора|Нет;");
      // NEXTION
       strcat(strReturn,"NEXTION|Использование дисплея Nextion 4.3|");
           #ifdef NEXTION
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
       strcat(strReturn,"NEXTION_DEBUG|Вывод в порт отладочных сообщений от дисплея|");
           #ifdef NEXTION_DEBUG
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
       strcat(strReturn,"NEXTION_PORT|Порт куда присоединен дисплей Nextion|Serial");
              if(&NEXTION_PORT==&Serial1) strcat(strReturn,cOne);
         else if(&NEXTION_PORT==&Serial2) strcat(strReturn,"2");
         else if(&NEXTION_PORT==&Serial3) strcat(strReturn,"3");   
         else strcat(strReturn,"??");
        strcat(strReturn,";"); 
       strcat(strReturn,"NEXTION_UPDATE|Время обновления информации на дисплее Nextion (мсек)|");strcat(strReturn,int2str(NEXTION_UPDATE));strcat(strReturn,";");
       strcat(strReturn,"NEXTION_READ|Время опроса дисплея Nextion (мсек)|");strcat(strReturn,int2str(NEXTION_READ));strcat(strReturn,";");
       strcat(strReturn,"TIME_ZONE|Часовой пояс|");strcat(strReturn,int2str(TIME_ZONE));strcat(strReturn,";");
       // Free RTOS
       strcat(strReturn,"FREE_RTOS_ARM_VERSION|Версия библиотеки Free RTOS due|");strcat(strReturn,int2str(FREE_RTOS_ARM_VERSION));strcat(strReturn,";");
       strcat(strReturn,"configCPU_CLOCK_HZ|Частота CPU (мГц)|");strcat(strReturn,int2str(configCPU_CLOCK_HZ/1000000));strcat(strReturn,";");
       strcat(strReturn,"configTICK_RATE_HZ|Квант времени системы Free RTOS (мкс)|");strcat(strReturn,int2str(configTICK_RATE_HZ));strcat(strReturn,";");
       strcat(strReturn,"WDT_TIME|Период Watchdog таймера, 0 - запрет таймера (сек)|");strcat(strReturn,int2str(WDT_TIME));strcat(strReturn,";");
    
       // Удаленные датчики
       strcat(strReturn,"SENSOR_IP|Использование удаленных датчиков|");
       #ifdef SENSOR_IP 
        strcat(strReturn,"ON;");
        strcat(strReturn,"IPNUMBER|Максимальное число удаленных датчиков, нумерация начинается с 1|");strcat(strReturn,int2str(IPNUMBER));strcat(strReturn,";");
        strcat(strReturn,"UPDATE_IP|Максимальное время между посылками данных с удаленного датчика (сек)|");strcat(strReturn,int2str(UPDATE_IP));strcat(strReturn,";");
       #else
        strcat(strReturn,"OFF;");
       #endif 
       
        strcat(strReturn,"K_VCC_POWER|Коэффициент пересчета для канала контроля напряжения питания (отсчеты/В)|");
        #ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
          strcat(strReturn,ftoa(temp,(float)K_VCC_POWER,2));strcat(strReturn,";");
        #else 
           strcat(strReturn,NO_SUPPORT); strcat(strReturn,";");
        #endif
        
       strcat(strReturn,"&") ;    continue;
       } // end CONST
       
    if (strcmp(str,"CONST1")==0)   // Команда CONST1 Информация очень большая по этому разбито на 2 запроса CONST CONST1
       {
       
       // i2c
       strcat(strReturn,"I2C_SPEED|Частота работы шины I2C (кГц)|"); strcat(strReturn,int2str(I2C_SPEED/1000)); strcat(strReturn,";");
       strcat(strReturn,"I2C_COUNT_EEPROM|Адрес внутри чипа eeprom с которого пишется счетчики ТН|"); strcat(strReturn,uint16ToHex(I2C_COUNT_EEPROM)); strcat(strReturn,";");
       strcat(strReturn,"I2C_SETTING_EEPROM|Адрес внутри чипа eeprom с которого пишутся настройки ТН|"); strcat(strReturn,uint16ToHex(I2C_SETTING_EEPROM)); strcat(strReturn,";");
       strcat(strReturn,"I2C_PROFILE_EEPROM|Адрес внутри чипа eeprom с которого пишется профили ТН|"); strcat(strReturn,uint16ToHex(I2C_PROFILE_EEPROM)); strcat(strReturn,";");
       strcat(strReturn,"TIME_READ_SENSOR|Период опроса датчиков + DELAY_DS1820 (мсек)|");strcat(strReturn,int2str(TIME_READ_SENSOR+cDELAY_DS1820));strcat(strReturn,";");
       strcat(strReturn,"TIME_CONTROL|Период управления тепловым насосом (мсек)|");strcat(strReturn,int2str(TIME_CONTROL));strcat(strReturn,";");
       strcat(strReturn,"TIME_EEV|Период управления ЭРВ (мсек)|");strcat(strReturn,int2str(TIME_EEV));strcat(strReturn,";");
       strcat(strReturn,"TIME_WEB_SERVER|Период опроса web сервера "); strcat(strReturn,nameWiznet);strcat(strReturn," (мсек)|");strcat(strReturn,int2str(TIME_WEB_SERVER));strcat(strReturn,";");
       strcat(strReturn,"TIME_COMMAND|Период разбора команд управления ТН (мсек)|");strcat(strReturn,int2str(TIME_COMMAND));strcat(strReturn,";");
       strcat(strReturn,"TIME_I2C_UPDATE |Период синхронизации внутренних часов с I2C часами (сек)|");strcat(strReturn,int2str(TIME_I2C_UPDATE ));strcat(strReturn,";");
       strcat(strReturn,"DELAY_ON_PUMP|Задержка включения компрессора после включения насосов (сек)|");strcat(strReturn,int2str(DELAY_ON_PUMP));strcat(strReturn,";");
       strcat(strReturn,"DELAY_OFF_PUMP|Задержка выключения насосов после выключения компрессора (сек)|");strcat(strReturn,int2str(DELAY_OFF_PUMP));strcat(strReturn,";");
       strcat(strReturn,"DELAY_REPEAD_START|Задержка перед повторным включением ТН при ошибке (сек)|");strcat(strReturn,int2str(DELAY_REPEAD_START));strcat(strReturn,";");
       // Датчики
       strcat(strReturn,"P_NUMSAMLES|Число значений для усреднения показаний давления|");strcat(strReturn,int2str(P_NUMSAMLES));strcat(strReturn,";");
       strcat(strReturn,"PRESS_FREQ|Частота опроса датчика давления (Гц)|");strcat(strReturn,int2str(PRESS_FREQ));strcat(strReturn,";");
       strcat(strReturn,"FILTER_SIZE|Длина фильтра датчика давления (отсчеты)|");strcat(strReturn,int2str(FILTER_SIZE));strcat(strReturn,";");
       strcat(strReturn,"T_NUMSAMLES|Число значений для усреднения показаний температуры|");strcat(strReturn,int2str(T_NUMSAMLES));strcat(strReturn,";");
       strcat(strReturn,"GAP_TEMP_VAL|Допустимая разница показаний между двумя считываниями|");strcat(strReturn,ftoa(temp,(float)GAP_TEMP_VAL/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"MAX_TEMP_ERR|Максимальная систематическая ошибка датчика температуры|");strcat(strReturn,ftoa(temp,(float)MAX_TEMP_ERR/100.0,2));strcat(strReturn,";");
       // ЭРВ
       #ifdef EEV_DEF
       strcat(strReturn,"EEV_STEPS|Максимальное число шагов ЭРВ|");strcat(strReturn,int2str(EEV_STEPS));strcat(strReturn,";");
       strcat(strReturn,"EEV_MIN_STEPS|Минимальное число шагов открытия ЭРВ|");strcat(strReturn,int2str(EEV_MIN_STEPS));strcat(strReturn,";");
       strcat(strReturn,"EEV_PSTART|Позиция открытия ЭРВ при пуске компрессора (пусковая позиция) - легкий пуск компрессора|");strcat(strReturn,int2str(EEV_PSTART));strcat(strReturn,";");
       strcat(strReturn,"EEV_START|Позиция открытия ЭРВ в начале работы ПИД ЭРВ (стартовая позиция)|");strcat(strReturn,int2str(EEV_START));strcat(strReturn,";");
       strcat(strReturn,"EEV_SPEED|Скорость шагового двигателя ЭРВ (шаги в сек)|");strcat(strReturn,int2str(EEV_SPEED));strcat(strReturn,";");
       strcat(strReturn,"EEV_QUEUE|Длина очереди команд шагового двигателя ЭРВ|");strcat(strReturn,int2str(EEV_QUEUE));strcat(strReturn,";");
       strcat(strReturn,"EEV_HOLD_MOTOR|Удержание шагового двигателя ЭРВ в простое|"); if (EEV_HOLD_MOTOR) strcat(strReturn,"ON;"); else strcat(strReturn,"OFF;");
       strcat(strReturn,"EEV_INVERT|Инвертирование направления движения ЭРВ (по выходам)|");
           #ifdef EEV_INVERT
             strcat(strReturn,"ON;");
           #else
             strcat(strReturn,"OFF;");
           #endif 
       strcat(strReturn,"EEV_PHASE|Управляющая последовательность ЭРВ (фазы движения)|");
           #if EEV_PHASE==PHASE_8s
             strcat(strReturn,"8 фаз, полушаг;");
           #elif EEV_PHASE==PHASE_8
             strcat(strReturn,"8 фаз, шаг;");
           #elif EEV_PHASE==PHASE_4
             strcat(strReturn,"4 фазы, шаг;");
           #else
             strcat(strReturn,"Ошибочная;");
           #endif
    //   strcat(strReturn,"EVI_TEMP_CON|Температура кондесатора для включения соленойда EVI|");strcat(strReturn,ftoa(temp,(float)EVI_TEMP_CON/100.0,2));strcat(strReturn,";");
       strcat(strReturn,"DELAY_START_POS|Время после старта компрессора когда EEV уходит c пусковой на стартовую позицию - облегчение пуска (сек)|");strcat(strReturn,int2str(DELAY_START_POS));strcat(strReturn,";");
       strcat(strReturn,"DELAY_ON_PID_EEV|Задержка включения ПИД ЭРВ после включения компрессора (сек)|");strcat(strReturn,int2str(DELAY_ON_PID_EEV));strcat(strReturn,";");
       strcat(strReturn,"DELAY_ON3_EEV|Задержка между открытием ЭРВ и включением компрессора, для выравнивания давлений (сек)|");strcat(strReturn,int2str(DELAY_ON3_EEV));strcat(strReturn,";");
        #endif   // EEV
       #ifdef MQTT
//       strcat(strReturn,"MQTT_REPEAT|Число попыток соединениея с MQTT сервером за одну итерацию|");strcat(strReturn,int2str(MQTT_REPEAT));strcat(strReturn,";");
       strcat(strReturn,"MQTT_NUM_ERR_OFF|Число ошибок отправки подряд при котором отключается сервис MQTT (флаг сбрасывается)|");strcat(strReturn,int2str(MQTT_NUM_ERR_OFF));strcat(strReturn,";");
       
       #endif 
       strcat(strReturn,"&") ;
       continue;
       } // end CONST1
       
      if (strcmp(str,"get_sysInfo")==0)  // Функция вывода системной информации для разработчика
       {
        strcat(strReturn,"Входное напряжение питания контроллера (В): |");
        #ifdef VCC_CONTROL  // если разрешено чтение напряжение питания
          strcat(strReturn,ftoa(temp,(float)HP.AdcVcc/K_VCC_POWER,2));strcat(strReturn,";");
        #else 
            strcat(strReturn,NO_SUPPORT); strcat(strReturn,";");
        #endif
         
        strcat(strReturn,"Режим safeNetwork (адрес:192.168.0.177 шлюз:192.168.0.1, не спрашиваеть пароль на вход) [активно 1]|");strcat(strReturn,int2str(HP.safeNetwork));strcat(strReturn,";");
        strcat(strReturn,"Уникальный ID чипа SAM3X8E|");getIDchip(strReturn);strcat(strReturn,";");
        strcat(strReturn,"Значение регистра VERSIONR сетевого чипа WizNet (51-w5100, 3-w5200, 4-w5500)|");strcat(strReturn,int2str(W5200VERSIONR()));strcat(strReturn,";");
      
        strcat(strReturn,"Контроль за работой драйвера ЭРВ |");
        #ifdef DRV_EEV_L9333          // Контроль за работой драйвера ЭРВ
           if (digitalReadDirect(PIN_STEP_DIAG))  strcat(strReturn,"Error L9333;"); else strcat(strReturn,"Normal;");
        #else
          strcat(strReturn,NO_SUPPORT); strcat(strReturn,";");
        #endif
         strcat(strReturn,"Состояние FreeRTOS task+err_code [1:configASSERT fails, 2:malloc fails, 3:stack overflow, 4:hard fault, 5:bus fault, 6:usage fault, 7:crash data]|");
         strcat(strReturn,uint16ToHex(lastErrorFreeRtosCode));strcat(strReturn,";");
          
           strcat(strReturn,"Регистры контроллера питания (SUPC) SAM3X8E [SUPC_SMMR SUPC_MR SUPC_SR]|");  // Регистры состояния контроллера питания
           strcat(strReturn,uint32ToHex(SUPC->SUPC_SMMR));strcat(strReturn," ");
           strcat(strReturn,uint32ToHex(SUPC->SUPC_MR));strcat(strReturn," ");
     //      strcat(strReturn,uint32ToHex(SUPC->SUPC_SR));strcat(strReturn,"/");
      //     strcat(strReturn,uint32ToHex(startSupcStatusReg));strcat(strReturn,";");
           startSupcStatusReg |= SUPC->SUPC_SR;                                  // Копим изменения
           strcat(strReturn,uint32ToHex(startSupcStatusReg));
           if ((startSupcStatusReg|SUPC_SR_SMS)==SUPC_SR_SMS_PRESENT)  strcat(strReturn," bad VDDIN!");
           strcat(strReturn,";");
         /*      
        for (uint8_t i=0;i<ANUMBER;i++)    // по всем датчикам
            {
                if (!HP.sADC[i].get_present()) continue;    // датчик отсутсвует в конфигурации пропускаем
                strcat(strReturn,"Счетчик числа ошибок чтения аналогового датчика AD");
                strcat(strReturn,int2str(HP.sADC[i].get_pinA()));
                strcat(strReturn,"|");
                strcat(strReturn,int2str(HP.sADC[i].get_errADC()));strcat(strReturn,";");
            }
         */
        strcat(strReturn,"Счетчик текущего числа повторных попыток пуска ТН|");
            if(HP.get_State()==pWORK_HP) { strcat(strReturn,int2str(HP.num_repeat));strcat(strReturn,";");} else strcat(strReturn,"0;");
        strcat(strReturn,"Счетчик \"Потеря связи с "); strcat(strReturn,nameWiznet);strcat(strReturn,"\", повторная инициализация  <sup>1</sup>|");strcat(strReturn,int2str(HP.num_resW5200));strcat(strReturn,";");
        strcat(strReturn,"Счетчик числа сбросов мютекса захвата шины SPI|");strcat(strReturn,int2str(HP.num_resMutexSPI));strcat(strReturn,";");
        strcat(strReturn,"Счетчик числа сбросов мютекса захвата шины I2C|");strcat(strReturn,int2str(HP.num_resMutexI2C));strcat(strReturn,";");
        #ifdef MQTT
        strcat(strReturn,"Счетчик числа повторных соединений MQTT клиента|");strcat(strReturn,int2str(HP.num_resMQTT));strcat(strReturn,";");
        #endif
        strcat(strReturn,"Счетчик потерянных пакетов ping|");strcat(strReturn,int2str(HP.num_resPing));strcat(strReturn,";");
        #ifdef USE_ELECTROMETER_SDM  
        strcat(strReturn,"Счетчик числа ошибок чтения счетчика SDM120 (RS485)|");strcat(strReturn,int2str(HP.dSDM.get_numErr()));strcat(strReturn,";");
        #endif
        if (HP.dFC.get_present()) strcat(strReturn,"Счетчик числа ошибок чтения частотного преобразователя (RS485)|");strcat(strReturn,int2str(HP.dFC.get_numErr()));strcat(strReturn,";");
       
        strcat(strReturn,"Счетчик числа ошибок чтения датчиков температуры (ds18b20)|");strcat(strReturn,int2str(HP.get_errorReadDS18B20()));strcat(strReturn,";");

        strcat(strReturn,"Время последнего включения ТН|");strcat(strReturn,DecodeTimeDate(HP.get_startTime()));strcat(strReturn,";");
        strcat(strReturn,"Время сохранения текущих настроек ТН|");strcat(strReturn,DecodeTimeDate(HP.get_saveTime()));strcat(strReturn,";");
        
        // Вывод строки статуса
        strcat(strReturn,"Строка статуса ТН| modWork:");strcat(strReturn,int2str((int)HP.get_modWork()));strcat(strReturn,"[");strcat(strReturn,codeRet[HP.get_ret()]);strcat(strReturn,"]");
        if(!(HP.dFC.get_present())) { strcat(strReturn," RCOMP:");if (HP.dRelay[RCOMP].get_Relay()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); }
        #ifdef RPUMPI
        strcat(strReturn," RPUMPI:");                                         if (HP.dRelay[RPUMPI].get_Relay()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); 
        #endif
        strcat(strReturn," RPUMPO:");                                         if (HP.dRelay[RPUMPO].get_Relay()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
        #ifdef  RTRV
        if (HP.dRelay[RTRV].get_present())    { strcat(strReturn," RTRV:");   if (HP.dRelay[RTRV].get_Relay()==true)    strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
        #endif
        #ifdef R3WAY
        if (HP.dRelay[R3WAY].get_present())   { strcat(strReturn," R3WAY:");  if (HP.dRelay[R3WAY].get_Relay()==true)   strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
        #endif
        #ifdef RBOILER
        if (HP.dRelay[RBOILER].get_present()) { strcat(strReturn," RBOILER:");if (HP.dRelay[RBOILER].get_Relay()==true) strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
        #endif
        #ifdef RHEAT
        if (HP.dRelay[RHEAT].get_present())   { strcat(strReturn," RHEAT:");  if (HP.dRelay[RHEAT].get_Relay()==true)   strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
        #endif
        #ifdef REVI
        if (HP.dRelay[REVI].get_present()) { strcat(strReturn," REVI:");      if (HP.dRelay[REVI].get_Relay()==true)    strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
        #endif
        if(HP.dFC.get_present())  {strcat(strReturn," freqFC:"); strcat(strReturn,ftoa(temp,(float)HP.dFC.get_freqFC()/100.0,2)); }
        if(HP.dFC.get_present())  {strcat(strReturn," Power:"); strcat(strReturn,ftoa(temp,(float)HP.dFC.get_power()/10.0,2));  } 
        strcat(strReturn,";");  
   
           
        strcat(strReturn,"Время сброса счетчиков с момента запуска ТН|");strcat(strReturn,DecodeTimeDate(HP.get_motoHourD1()));strcat(strReturn,";");
        strcat(strReturn,"Часы работы ТН с момента запуска (час)|");strcat(strReturn,ftoa(temp,(float)HP.get_motoHourH1()/60.0,1));strcat(strReturn,";");
        strcat(strReturn,"Часы работы компрессора ТН с момента запуска (час)|");strcat(strReturn,ftoa(temp,(float)HP.get_motoHourC1()/60.0,1));strcat(strReturn,";");
        #ifdef USE_ELECTROMETER_SDM  
          strcat(strReturn,"Потребленная энергия ТН с момента запуска (кВт/ч)|");strcat(strReturn,ftoa(temp, HP.dSDM.get_Energy()-HP.get_motoHourE1(),2));strcat(strReturn,";");
        #endif
        if(HP.ChartPowerCO.get_present())  strcat(strReturn,"Выработанная энергия ТН с момента запуска (кВт/ч)|");strcat(strReturn,ftoa(temp, HP.get_motoHourP1()/1000.0,2));strcat(strReturn,";"); // Если есть оборудование
  
        strcat(strReturn,"Время сброса сезонных счетчиков ТН|");strcat(strReturn,DecodeTimeDate(HP.get_motoHourD2()));strcat(strReturn,";");
        strcat(strReturn,"Часы работы ТН за сезон (час)|");strcat(strReturn,ftoa(temp,(float)HP.get_motoHourH2()/60.0,1));strcat(strReturn,";");
        strcat(strReturn,"Часы работы компрессора ТН за сезон (час)|");strcat(strReturn,ftoa(temp,(float)HP.get_motoHourC2()/60.0,1));strcat(strReturn,";");
        #ifdef USE_ELECTROMETER_SDM  
          strcat(strReturn,"Потребленная энергия ТН за сезон (кВт/ч)|");strcat(strReturn,ftoa(temp, HP.dSDM.get_Energy()-HP.get_motoHourE2(),2));strcat(strReturn,";");
        #endif
        if(HP.ChartPowerCO.get_present())  strcat(strReturn,"Выработанная энергия ТН за сезон (кВт/ч)|");strcat(strReturn,ftoa(temp, HP.get_motoHourP2()/1000.0,2));strcat(strReturn,";"); // Если есть оборудование
  
        strcat(strReturn,"&") ;    continue;
       } // sisInfo
       
       if (strcmp(str,"test_Mail")==0)  // Функция test_mail
       {
       if (HP.message.setTestMail()) { strcat(strReturn,"Send test mail to "); strcat(strReturn,HP.message.get_messageSetting(pSMTP_RCPTTO)); }
       else { strcat(strReturn,"Error send test mail.");}
       strcat(strReturn,"&") ;   
        continue;  
       }   // test_Mail  
       if (strcmp(str,"test_SMS")==0)  // Функция test_mail
       {
       if (HP.message.setTestSMS()) { strcat(strReturn,"Send SMS to ");  strcat(strReturn,HP.message.get_messageSetting(pSMS_PHONE));}
       else { strcat(strReturn,"Error send test sms.");}
       strcat(strReturn,"&") ;           
       continue;  
       }   // test_Mail    
       // ЭРВ запросы , те которые без параметра ------------------------------
       if (strcmp(str,"get_pinEEV")==0)           // Функция get_pinEEV - строка с перечислением ного куда шаговик прицеплен
                  {   
                  #ifdef EEV_DEF 
                  strcat(strReturn,"D");  strcat(strReturn,int2str(PIN_EEV1_D24));
                  strcat(strReturn," D"); strcat(strReturn,int2str(PIN_EEV2_D25));
                  strcat(strReturn," D"); strcat(strReturn,int2str(PIN_EEV3_D26));
                  strcat(strReturn," D"); strcat(strReturn,int2str(PIN_EEV4_D27));
                  #else
                  strcat(strReturn,"- - - -");  
                  #endif   
                  strcat(strReturn,"&") ;    continue;
                  } 
         if (strcmp(str,"set_zeroEEV")==0)  // Функция set_zeroEEV
         {  
            #ifdef EEV_DEF 
            strcat(strReturn,int2str(HP.dEEV.set_zero())); 
            #else
            strcat(strReturn,"-");  
            #endif   
           strcat(strReturn,"&") ; continue;
         }          
        if (strcmp(str,"get_overheatEEV")==0)  // Функция get_overheatEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Overheat()/100,2)); 
          #else
          strcat(strReturn,"-");  
          #endif   
          strcat(strReturn,"&") ;    continue;
         }  
        if (strcmp(str,"get_EEV")==0)  // Функция get_EEV
         {
           #ifdef EEV_DEF 
           if (HP.dEEV.stepperEEV.isBuzy())  strcat(strReturn,"<<");  // признак движения
           strcat(strReturn,int2str(HP.dEEV.get_EEV())); if (HP.dEEV.stepperEEV.isBuzy())  strcat(strReturn,">>");  // признак движения
           #else
           strcat(strReturn,"-");  
           #endif   
           strcat(strReturn,"&") ;    continue;
         }   
        if (strcmp(str,"get_minEEV")==0)  // Функция get_minEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,int2str(HP.dEEV.get_minEEV())); 
          #else
          strcat(strReturn,"-");  
          #endif   
        strcat(strReturn,"&") ;    continue;
         }                
        if (strcmp(str,"get_maxEEV")==0)  // Функция get_maxEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,int2str(HP.dEEV.get_maxEEV())); 
          #else
          strcat(strReturn,"-");  
          #endif   
          strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"get_nameEEV")==0)  // Функция get_nameEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,HP.dEEV.get_name()); 
          #else
          strcat(strReturn,"none");  
          #endif   
         strcat(strReturn,"&") ;    continue;
         }            
         if (strcmp(str,"get_noteEEV")==0)  // Функция get_noteEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,HP.dEEV.get_note()); 
          #else
          strcat(strReturn,"EEV absent config");  
          #endif   
         strcat(strReturn,"&") ;    continue;
         }   
        if (strcmp(str,"get_errcodeEEV")==0)  // Функция get_errcodeEEV
         {
          #ifdef EEV_DEF 
          strcat(strReturn,int2str(HP.dEEV.get_lastErr())); 
          #else
          strcat(strReturn,cZero);  
          #endif   
         strcat(strReturn,"&") ;    continue;
         }   
        if (strcmp(str,"get_presentEEV")==0)  // Функция get_presentEEV
         {
         #ifdef EEV_DEF  
         if (HP.dEEV.get_present()) strcat(strReturn,cOne); else strcat(strReturn,cZero); 
         #else
          strcat(strReturn,cZero);  
         #endif   
         strcat(strReturn,"&"); continue;
         }      
        if (strcmp(str,"get_freonEEV")==0)  // Функция get_freonEEV
         {
         #ifdef EEV_DEF 
         for(i=0;i<=R717;i++) // Формирование списка
            { strcat(strReturn,noteFreon[i]); strcat(strReturn,":"); if(i==HP.dEEV.get_typeFreon()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,";");  }
         #else
          strcat(strReturn,"none:1;");  
         #endif   
           strcat(strReturn,"&");    continue;
         }     
       if (strcmp(str,"get_ruleEEV")==0)  // Функция get_ruleEEV
         {
          #ifdef EEV_DEF
          for(i=TEVAOUT_TEVAIN;i<=MANUAL;i++) // Формирование списка вариантов алгоритма расчета перегрева
            { strcat(strReturn,noteRuleEEV[i]); strcat(strReturn,":"); 
              if(i==HP.dEEV.get_ruleEEV())  strcat(strReturn,cOne); else strcat(strReturn,cZero);strcat(strReturn,":");
              switch (i)  // определение доступности элемента
              {
                case  TEVAOUT_TEVAIN: if ((HP.sTemp[TEVAOUT].get_present())&&(HP.sTemp[TEVAIN].get_present())) strcat(strReturn,cOne); else strcat(strReturn,cZero); break;
                case  TRTOOUT_TEVAIN: if ((HP.sTemp[TRTOOUT].get_present())&&(HP.sTemp[TEVAIN].get_present())) strcat(strReturn,cOne); else strcat(strReturn,cZero); break;
                case  TEVAOUT_PEVA:   if ((HP.sTemp[TEVAOUT].get_present())&&(HP.sADC[PEVA].get_present())) strcat(strReturn,cOne); else strcat(strReturn,cZero);  break;
                case  TRTOOUT_PEVA:   if ((HP.sTemp[TRTOOUT].get_present())&&(HP.sADC[PEVA].get_present())) strcat(strReturn,cOne); else strcat(strReturn,cZero);  break;
                case  TABLE:           
                case  MANUAL:         strcat(strReturn,cOne); break;
                default:              strcat(strReturn,cZero); break;
              }
          strcat(strReturn,";");  } 
          #else
          strcat(strReturn,"none:1;");  
          #endif      
          strcat(strReturn,"&") ;    continue;
         }     
      if (strcmp(str,"get_remarkEEV")==0)  // Функция get_remarkEEV
         { 
          #ifdef EEV_DEF
           switch (HP.dEEV.get_ruleEEV())
           {
             case TEVAOUT_TEVAIN:   strcat(strReturn,noteRemarkEEV[TEVAOUT_TEVAIN]);        break;
             case TRTOOUT_TEVAIN:   strcat(strReturn,noteRemarkEEV[TRTOOUT_TEVAIN]);        break;
             case TEVAOUT_PEVA:     if(!HP.sADC[PEVA].get_present()) {strcat(strReturn,"Отсутвует датчик давления! Выберите другое правило управления ЭРВ. ТН будет остановлен по ошибке."); break;}
                                    else {strcat(strReturn,noteRemarkEEV[TEVAOUT_PEVA]);    break;}
             case TRTOOUT_PEVA:     strcat(strReturn,noteRemarkEEV[ TRTOOUT_PEVA]);         break;                       
             case TABLE:            strcat(strReturn,noteRemarkEEV[TABLE]);                 break;
             case MANUAL:           strcat(strReturn,noteRemarkEEV[MANUAL]);                break;
             default:               strcat(strReturn,"Unknown");                            break;      
            }
           #else
           strcat(strReturn,"EEV absent in config");    
           #endif 
          strcat(strReturn,"&") ;    continue;
         }    
        // FC запросы, те которые без параметра ------------------------------
        if (strcmp(str,"get_nameFC")==0)  // Функция get_nameFC
         {
          strcat(strReturn,HP.dFC.get_name()); strcat(strReturn,"&") ;    continue;
         }            
         if (strcmp(str,"get_noteFC")==0)  // Функция get_noteFC
         {
          strcat(strReturn,HP.dFC.get_note()); strcat(strReturn,"&") ;    continue;
         }   
        if (strcmp(str,"get_presentFC")==0)  // Функция get_presentEEV
         {
         if (HP.dFC.get_present()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,"&"); continue;
         }      
        if (strcmp(str,"get_pinFC")==0)  // Функция get_presentEEV
         {
         strcat(strReturn,"D"); strcat(strReturn,int2str(HP.dFC.get_pinA())); strcat(strReturn,"&"); continue;
         }         
        if (strcmp(str,"get_FC")==0)  // Функция get_FC выдает
         {
     //    strcat(strReturn,ftoa(temp,(float)HP.dFC.get_targetFreq()/100.0,2)); strcat(strReturn,"&") ;    continue;
          strcat(strReturn,int2str(HP.dFC.get_targetFreq()/100)); strcat(strReturn,"&") ;    continue;
         }  
        if (strcmp(str,"get_dacFC")==0)  // Функция get_dacFC
         {
          strcat(strReturn,int2str(HP.dFC.get_DAC())); strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"reset_errorFC")==0)  // Функция get_dacFC
         {
          if (!HP.dFC.get_present()) {strcat(strReturn,"Инвертор отсутствует&"); ;continue;}
          #ifndef FC_ANALOG_CONTROL    // НЕ АНАЛОГОВОЕ УПРАВЛЕНИЕ
              HP.dFC.reset_errorFC();
              strcat(strReturn,"Ошибки инвертора сброшены . . .");
          #endif
          strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"reset_FC")==0)     // Функция get_dacFC
         {
          if (!HP.dFC.get_present()) {strcat(strReturn,"Инвертор отсутствует&"); ;continue;}
           HP.dFC.reset_FC();                             // подать команду на сброс
           strcat(strReturn,"Cброс преобразователя частоты . . ."); strcat(strReturn,"&") ;    continue;
         } 
         #ifdef USE_ELECTROMETER_SDM
        // SDM запросы которые без параметра
        if (strcmp(str,"settingSDM")==0)     // Функция settingSDM  Запрограммировать параметры связи счетчика
         {
          if (!HP.dSDM.get_present()) {strcat(strReturn,"Счетчик отсутвует &"); ;continue;}
          HP.dSDM.progConnect();
          strcat(strReturn,"Счетчик запрограммирован, необходимо сбросить счетчик!!"); strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"uplinkSDM")==0)     // Функция settingSDM  Попытаться возобновить связь со счетчиком при ее потери
         {
          if (!HP.dSDM.get_present()) {strcat(strReturn,"Счетчик отсутвует &"); ;continue;}
          HP.dSDM.uplinkSDM();
          strcat(strReturn,"Проверка связи со счетчиком"); strcat(strReturn,"&") ;    continue;
         } 

         
          #endif
        // -------------- СПИСКИ ДАТЧИКОВ и ИСПОЛНИТЕЛЬНЫХ УСТРОЙСТВ  -----------------------------------------------------
        // Список аналоговых датчиков выводятся только присутсвующие датчики список вида name:0;
        if (strcmp(str,"get_listPress")==0)     // Функция get_listPress
         {
          for(i=0;i<ANUMBER;i++) if (HP.sADC[i].get_present()){strcat(strReturn,HP.sADC[i].get_name());strcat(strReturn,";");}
          strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"get_listTemp")==0)     // Функция get_listTemp
         {
          for(i=0;i<TNUMBER;i++) if (HP.sTemp[i].get_present()){strcat(strReturn,HP.sTemp[i].get_name());strcat(strReturn,";");}
          strcat(strReturn,"&") ;    continue;
         } 
        if (strcmp(str,"get_listInput")==0)     // Функция get_listInput
         {
          for(i=0;i<INUMBER;i++) if (HP.sInput[i].get_present()){strcat(strReturn,HP.sInput[i].get_name());strcat(strReturn,";");}
          strcat(strReturn,"&") ;    continue;
         }         
        if (strcmp(str,"get_listRelay")==0)     // Функция get_listRelay
         {
          for(i=0;i<RNUMBER;i++) if (HP.dRelay[i].get_present()){strcat(strReturn,HP.dRelay[i].get_name());strcat(strReturn,";");}
          strcat(strReturn,"&") ;    continue;
         }
        if (strcmp(str,"get_listFlow")==0)     // Функция get_lisFlow
         {
          for(i=0;i<FNUMBER;i++) if (HP.sFrequency[i].get_present()){strcat(strReturn,HP.sFrequency[i].get_name());strcat(strReturn,";");}
          strcat(strReturn,"&") ;    continue;
         }
        // -----------------------------------------------------------------------------------------------------        
       // 2. Функции с параметром ------------------------------------------------------------------------------
   if (((x=strpbrk(str,"("))!=0)&&((y=strpbrk(str,")"))!=0))  // Функция с одним параметром - найдена открывающиеся и закрывающиеся скобка
       {
       // Выделяем параметр функции на выходе число - номер параметра
       // применяется кодирование 0-19 - температуры 20-29 - сухой контакт 30-39 -аналоговые датчики
       y[0]=0;                                  // Стираем скобку ")"  строка х содержит параметр
       param=-1;                                // по умолчанию параметр не валидный

      // -----------------------------------------------------------------------------------------------------        
      // 2.1  Удаленный датчик
      // -----------------------------------------------------------------------------------------------------        
     #ifdef SENSOR_IP                           // Получение данных удаленного датчика
     // Получение данных с удаленного датчика
      if (strstr(str,"set_sensorIP"))           // Удаленные датчики - получить значения
      {
       // разбор строки формат "номер:температура:уровень_сигнала:питание:счетчик"
  //     Serial.println(x+1); 
       ptr=strtok(x+1,":");
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}
       if ((a=atoi(ptr))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков

       ptr=strtok(NULL,":");
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}
       b=100*my_atof(ptr);
       if ((b<-4000)||(b>12000))      {strcat(strReturn,"E24&");continue;}  // проверка диапазона температуры -40...+120
         
       ptr=strtok(NULL,":");
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}
       if ((c=10*my_atof(ptr))==0)   {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((c<-1000)||(c>0))         {strcat(strReturn,"E24&");continue;}  // проверка диапазона уровня сигнала -100...-1 дБ
     
       ptr=strtok(NULL,":");
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}
       if ((d=atoi(ptr))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((d<1000)||(d>5000))       {strcat(strReturn,"E24&");continue;}  // проверка диапазона питания 1000...4000 мВ
       
       ptr=strtok(NULL,":");
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}
       e=atoi(ptr);
       if (e<0)                      {strcat(strReturn,"E24&");continue;}  // счетичк больше 0
         
       
       // Дошли до сюда - ошибок нет, можно использовать данные
       byte adr[4];
       W5100.readSnDIPR(sock, adr);
       HP.sIP[a-1].set_DataIP(a,b,c,d,e,BytesToIPAddress(adr));
       strcat(strReturn,"OK&"); continue;
      }
 
      if (strstr(str,"get_sensorParamIP"))    // Удаленные датчики - Получить отдельное значение конкретного параметра
      {
       ptr=strtok(x+1,":");     // Нужно
       if (ptr==NULL)                {strcat(strReturn,"E21&");continue;}  // нет параметра
       if ((a=atoi(ptr))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования не число
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
       // Получили номер запрашиваемого датчика, теперь определяем параметр
         ptr=strtok(NULL,":");
            if (strstr(ptr,"SENSOR_TEMP"))     strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_TEMP));
       else if (strstr(ptr,"SENSOR_NUMBER"))   strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_NUMBER));
       else if (strstr(ptr,"RSSI"))            strcat(strReturn,HP.sIP[a-1].get_sensorIP(pRSSI));
       else if (strstr(ptr,"VCC"))             strcat(strReturn,HP.sIP[a-1].get_sensorIP(pVCC));
       else if (strstr(ptr,"SENSOR_USE"))      strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_USE));
       else if (strstr(ptr,"SENSOR_RULE"))     strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_RULE));
       else if (strstr(ptr,"SENSOR_IP"))       strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_IP));
       else if (strstr(ptr,"SENSOR_COUNT"))    strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_COUNT));
       else if (strstr(ptr,"STIME"))           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSTIME));
       else if (strstr(ptr,"SENSOR"))          strcat(strReturn,"----");
       else strcat(strReturn,"E26");
       strcat(strReturn,"&") ; continue;
      }
      
     if (strstr(str,"get_sensorIP"))    // Удаленные датчики - Получить параметры (ВСЕ) удаленного датчика в виде строки
      {
       ptr=x+1; 
       if ((a=atoi(ptr))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
       // Формируем строку
       strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_NUMBER)); strcat(strReturn,":");
       
       if (HP.sIP[a-1].get_update()>UPDATE_IP)  strcat(strReturn,"-:") ;                       // Время просрочено, удаленный датчик не используем
       else { strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_TEMP));strcat(strReturn,":"); }

       if (HP.sIP[a-1].get_count()>0)     // Если были пакеты то выводим данные по ним
       {
           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSTIME));strcat(strReturn,":");
           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_IP));strcat(strReturn,":");
           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pRSSI));strcat(strReturn,":");
           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pVCC)); strcat(strReturn,":");
           strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_COUNT)); strcat(strReturn,":");  
       }
       else strcat(strReturn,"-:-:-:-:-:");  // После включения еще ни разу данные не поступали поэтому прочерки
        
       strcat(strReturn,"&") ; continue; 
      }  
      
      if (strstr(str,"get_sensorListIP"))    // Удаленные датчики - список привязки удаленного датчика
      {
       ptr=x+1; 
       if ((a=atoi(ptr))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
       
       strcat(strReturn,"Reset:"); if (HP.sIP[a-1].get_link()==-1) strcat(strReturn,"1;"); else strcat(strReturn,"0;");
       for(i=0;i<TNUMBER;i++)        // формирование списка
           {
               if (HP.sTemp[i].get_present())  // только представленные датчики
               {
               strcat(strReturn,HP.sTemp[i].get_name()); strcat(strReturn,":");
               if (HP.sIP[a-1].get_link()==i) strcat(strReturn,"1;"); else strcat(strReturn,"0;");
               }
           }
       strcat(strReturn,"&") ; continue; 
      }   
      

      if (strstr(str,"get_sensorUseIP"))    // Удаленные датчики - ПОЛУЧИТЬ использование удаленного датчика
      {
       if ((a=atoi(x+1))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
       strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_USE));strcat(strReturn,"&") ;continue;  
      }

 
      if (strstr(str,"get_sensorRuleIP"))    // Удаленные датчики - ПОЛУЧИТЬ использование усреднение
      {
       if ((a=atoi(x+1))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
       if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
       strcat(strReturn,HP.sIP[a-1].get_sensorIP(pSENSOR_RULE));strcat(strReturn,"&") ;continue;  
      }
   
      #else
       if (strstr(str,"set_sensorIP"))   {strcat(strReturn,"E25&");continue;}        // Удаленные датчики  НЕ ПОДДЕРЖИВАЕТСЯ
     #endif  

       // ----------------------------------------------------------------------------------------------------------
       // 2.2 Функции с одним параметром
       // ----------------------------------------------------------------------------------------------------------

      if (strstr(str,"set_modeHP"))           // Функция set_modeHP - установить режим отопления ТН
         {
        if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
        else
           {
             switch ((MODE_HP)pm)  // режим работы отопления
                   {
                    case pOFF:   HP.set_mode(pOFF);  strcat(strReturn,(char*)"Выключено:1;Отопление:0;Охлаждение:0;"); break;
                    case pHEAT:  HP.set_mode(pHEAT); strcat(strReturn,(char*)"Выключено:0;Отопление:1;Охлаждение:0;"); break;
                    case pCOOL:  HP.set_mode(pCOOL); strcat(strReturn,(char*)"Выключено:0;Отопление:0;Охлаждение:1;"); break;
                    default: HP.set_mode(pOFF);  strcat(strReturn,(char*)"Выключено:1;Отопление:0;Охлаждение:0;"); break;   // Исправить по умолчанию
                   }  
           }
   //        Serial.print(pm); Serial.print("   "); Serial.println(HP.get_mode());
           strcat(strReturn,"&") ; continue;
          } // strcmp(str,"set_modeHP")==0)      
      // ------------------------------------------------------------------------  
      if (strstr(str,"set_testMode"))  // Функция set_testMode  - Установить режим работы бойлера
       {
       if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
       else
           {
            HP.set_testMode((TEST_MODE)pm);             // Установить режим работы тестирования
           for(i=0;i<=HARD_TEST;i++)                    // Формирование списка
              { strcat(strReturn,noteTestMode[i]); strcat(strReturn,":"); if(i==HP.get_testMode()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,";");  }                
           } // else  
          strcat(strReturn,"&") ;    continue;
        } //  if (strcmp(str,"set_testMode")==0)  

       // ------------------------------------------------------------------------
       if (strstr(str,"set_freonEEV"))  // Функция set_freonEEV
         {
       #ifdef EEV_DEF   
         if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
          else
            {
            HP.dEEV.set_typeFreon((TYPEFREON)pm);
            for(i=0;i<=R717;i++) // Формирование списка
               { strcat(strReturn,noteFreon[i]); strcat(strReturn,":"); if(i==HP.dEEV.get_typeFreon()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,";");  }                
              } // else  
           strcat(strReturn,"&") ;    continue; 
           #else
            strcat(strReturn,"none:1;&") ;    continue;
           #endif 
           }  //  if (strcmp(str,"set_freonEEV")==0) 
        // -----------------------------------------------------------------------------    
        if (strstr(str,"set_ruleEEV"))  // Функция set_ruleEEV
         {
        #ifdef EEV_DEF  
         if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
          else
            {
            HP.dEEV.set_ruleEEV((RULE_EEV)pm);
            for(i=0;i<=MANUAL;i++) // Формирование списка
                 { strcat(strReturn,noteRuleEEV[i]); strcat(strReturn,":"); if(i==HP.dEEV.get_ruleEEV()) strcat(strReturn,cOne); else strcat(strReturn,cZero); strcat(strReturn,";");  }                    
              } // else  
             strcat(strReturn,"&") ;    continue; 
            #else
            strcat(strReturn,"none:1;&") ;    continue;
            #endif 
           }  //  if (strcmp(str,"set_ruleEEV")==0)    
         // -----------------------------------------------------------------------------  
        if (strstr(str,"set_EEV"))  // Функция set_EEV
             {
             #ifdef EEV_DEF 
             if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                if(HP.dEEV.set_EEV((int)pm)==0) 
                strcat(strReturn,int2str(pm));  // если значение правильное его возвращаем сразу
                else strcat(strReturn,"E12"); 
     //           Serial.print("set_EEV ");    Serial.print(pm); Serial.print(" set "); Serial.println(int2str(HP.dEEV.get_EEV())); 
                strcat(strReturn,"&") ;    continue; 
                } 
             #else
                strcat(strReturn,"-&") ;    continue;
             #endif 
               }  //  if (strcmp(str,"set_set_EEV")==0)    
         // -----------------------------------------------------------------------------  
        if (strstr(str,"set_targetFreq"))  // Функция set_EEV
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E09");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
              //  if(HP.dFC.set_targetFreq(pm*100)==0) strcat(strReturn,ftoa(temp,(float)HP.dFC.get_targetFreq()/100.0,2)); else strcat(strReturn,"E12");  strcat(strReturn,"&") ;    continue; 
                 if(HP.dFC.set_targetFreq(pm*100,true,FC_MIN_FREQ_USER ,FC_MAX_FREQ_USER)==0) strcat(strReturn,int2str(HP.dFC.get_targetFreq()/100)); else strcat(strReturn,"E12");  strcat(strReturn,"&") ;    continue;   // ручное управление границы максимальны
                }
               }  //  if (strcmp(str,"set_set_targetFreq")==0)    
         // -----------------------------------------------------------------------------  
         // ПРОФИЛИ функции с одним параметром
         // ----------------------------------------------------------------------------- 
          if (strstr(str,"saveProfile"))  // Функция saveProfile сохранение текущего профиля
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E29");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                  if ((pm>=0)&&(pm<I2C_PROFIL_NUM)) strcat(strReturn,int2str(HP.Prof.save((int8_t)pm))); else strcat(strReturn,"E29");  strcat(strReturn,"&") ;    continue; 
                }
               }  //if (strstr(str,"saveProfile"))  
           // -----------------------------------------------------------------------------     
            if (strstr(str,"loadProfile"))  // Функция loadProfile загрузка профиля в текущий
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E29");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                  if ((pm>=0)&&(pm<I2C_PROFIL_NUM)) strcat(strReturn,int2str(HP.Prof.load((int8_t)pm))); else strcat(strReturn,"E29");  strcat(strReturn,"&") ;    continue; 
                }
               }  //if (strstr(str,"loadProfile"))  
            // -----------------------------------------------------------------------------     
            if (strstr(str,"infoProfile"))  // Функция infoProfile получить информацию о профиле
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E29");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                  if ((pm>=0)&&(pm<I2C_PROFIL_NUM)) HP.Prof.get_info(strReturn,(int8_t)pm); else strcat(strReturn,"E29");  strcat(strReturn,"&") ;    continue; 
                }
               }  //if (strstr(str,"infoProfile"))   
           // -----------------------------------------------------------------------------     
            if (strstr(str,"eraseProfile"))  // Функция eraseProfile стереть профиль
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E29");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                  if (pm==HP.Prof.get_idProfile())  {strcat(strReturn,"E30");}  // попытка стереть текущий профиль
                  else if((pm>=0)&&(pm<I2C_PROFIL_NUM)) { strcat(strReturn,int2str(HP.Prof.erase((int8_t)pm))); HP.Prof.update_list(HP.Prof.get_idProfile()); }
                  else strcat(strReturn,"E29"); 
                  strcat(strReturn,"&") ;    continue; 
                }
               }  //if (strstr(str,"eraseProfile"))     
          // -----------------------------------------------------------------------------     
            if (strstr(str,"set_listProfile"))  // Функция set_listProfil загрузить профиль из списка и сразу СОХРАНИТЬ !!!!!!
             {
            if ((pm=my_atof(x+1))==ATOF_ERROR)  strcat(strReturn,"E29");      // Ошибка преобразования   - завершить запрос с ошибкой
              else
                {
                  if ((pm>=0)&&(pm<I2C_PROFIL_NUM)) { HP.Prof.set_list((int8_t)pm); HP.save(); HP.Prof.get_list(strReturn/*,HP.Prof.get_idProfile()*/);} 
                  else strcat(strReturn,"E29");  
                  strcat(strReturn,"&") ;    continue; 
                }
               }  //if (strstr(str,"set_listProfile"))     

       //  2.3 Функции с параметрами
      // проверяем наличие функции set_  конструкция типа (TIN=23)
        char strbuf[MAX_LEN_PM+1]; // буфер для хранения строкового параметра, описание профиля до 120 байт - может быть 40 русских букв прикодировании URL это по 3 байта на букву
        if ((z=strpbrk(str,"="))!=0)  // нашли знак "=" запрос на установку параметра
        { 
          // Serial.print(strlen(z+1));Serial.print(" >");Serial.print(z+1);Serial.println("<") ;          
           strncpy(strbuf,z+1,MAX_LEN_PM); // Сохранение для запроса у которого параметр не число а строка - адрес
       //    pm = atof(z+1);               // Добавляет 9 кб кода !!!!!!!!!!!!!!!!
           pm = my_atof(z+1);              // мой код 150 байт и отработка ошибки
          // формируем начало ответа - повторение запроса без параметра установки  ДЛЯ set_ запросов
          z[0]=0; // Обрезать строку - откинуть значение параметра - мы его получили pm
          strcat(strReturn,str); 
          strcat(strReturn,")=");
    //      if (pm==ATOF_ERROR)        // Ошибка преобразования   - завершить запрос с ошибкой
   //       { strcat(strReturn,"E04");strcat(strReturn,"&");  continue;  }
         } //if "="

		 #ifdef USE_SCHEDULER // vad711
			// ошибки: E33 - не верный номер расписания, E34 - не хватает места для календаря
			if(strstr(str,"SCHDLR")) { // Класс Scheduler
				x++;
				if(strncmp(str, "set", 3) == 0) { // set_SCHDLR(x=n)
					if((i = HP.Schdlr.web_set_param(x, z+1))) {
						strcat(strReturn, i == 1 ? "E33&" : "E34&");
						continue;
					}
				} else if(strncmp(str, "get", 3) == 0) { // get_SCHDLR(x)
				} else goto x_FunctionNotFound;
				HP.Schdlr.web_get_param(x, strReturn);
				strcat(strReturn,"&");
				continue;
			}
		 #endif

       // --- УДАЛЕННЫЕ ДАТЧИКИ ----------  кусок кода для удаленного датчика - установка параметров ответ - повторение запроса уже сделали
         #ifdef SENSOR_IP                           // Получение данных удаленного датчика
              
              if (strstr(str,"set_sensorListIP"))    // Удаленные датчики - привязка датчика
              {
               // первое число (имя удаленного датчика)
               if ((x+1)==NULL)              {strcat(strReturn,"E21&");continue;}
               if ((a=atoi(x+1))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
               if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
               
               // Второе число (новое значение параметра)
               if (strbuf==NULL)             {strcat(strReturn,"E21&");continue;}  
               b=atoi(strbuf);
               if ((b<0)||(b>TNUMBER))       {strcat(strReturn,"E24&");continue;}  //  проверка диапазона номеров датчиков учитываем reset!!
               
               c=-1; 
               HP.sIP[a-1].set_link(-1);     // Сбросить старую привязку (привязки нет)
               if(b>0)                       // если привязка есть
               {
               b--;                          // убрать пункт ресет
               for(i=0;i<TNUMBER;i++)        // формирование списка
                   {
                    if (HP.sTemp[i].get_present())  c++;             // считаем индекс с учетом только представленных датчиков
                    if (c==b) { HP.sIP[a-1].set_link(i); break;}     // а запоминаем фактический индекс
         //           if (strstr(ptr,HP.sTemp[i].get_name())) { HP.sIP[a-1].set_link(i); break;}     // а запоминаем фактический индекс
                   }
               }
                HP.updateLinkIP();                 // Обновить ВСЕ привязки удаленных датчиков
                 
                // Формируем список для ответа
                 strcat(strReturn,"Reset:"); if (HP.sIP[a-1].get_link()==-1) strcat(strReturn,"1;"); else strcat(strReturn,"0;");
                 for(i=0;i<TNUMBER;i++)        // формирование списка
                   {
                       if (HP.sTemp[i].get_present())  // только представленные датчики
                       {
                       strcat(strReturn,HP.sTemp[i].get_name()); strcat(strReturn,":");
                       if (HP.sIP[a-1].get_link()==i) strcat(strReturn,"1;"); else strcat(strReturn,"0;");
                       }
                   }
                 strcat(strReturn,"&") ; continue;     
                
                } 
      
             if (strstr(str,"set_sensorUseIP"))    // Удаленные датчики - УСТАНОВИТЬ использование удаленного датчика
              {
               // первое число (имя удаленного датчика)
               if ((x+1)==NULL)              {strcat(strReturn,"E21&");continue;}
               if ((a=atoi(x+1))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
               if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
               // Второе число (зновое значение параметра)
               if (strbuf==NULL)             {strcat(strReturn,"E21&");continue;}  
               b=atoi(strbuf);
               if ((b<0)||(b>1))             {strcat(strReturn,"E24&");continue;}  //  проверка диапазона
 
               if (b==1) HP.sIP[a-1].set_fUse(true); else HP.sIP[a-1].set_fUse(false);
               
               HP.updateLinkIP();                 // Обновить ВСЕ привязки удаленных датчиков
               
               strcat(strReturn,int2str(HP.sIP[a-1].get_fUse()));strcat(strReturn,"&") ;continue;  
              }

             if (strstr(str,"set_sensorRuleIP"))    // Удаленные датчики - УСТАНОВИТЬ использование усреднение
              {
               // первое число (имя удаленного датчика)
               if ((x+1)==NULL)              {strcat(strReturn,"E21&");continue;}
               if ((a=atoi(x+1))==0)         {strcat(strReturn,"E22&");continue;}  // если возвращен 0 то ошибка преобразования
               if ((a<1)||(a>IPNUMBER))      {strcat(strReturn,"E23&");continue;}  // проверка диапазона номеров датчиков
               
               if (strbuf==NULL)             {strcat(strReturn,"E21&");continue;}  
               b=atoi(strbuf);
               if ((b<0)||(b>1))             {strcat(strReturn,"E24&");continue;}  //  проверка диапазона
          
               if (b==1) HP.sIP[a-1].set_fRule(true); else HP.sIP[a-1].set_fRule(false);
               strcat(strReturn,int2str(HP.sIP[a-1].get_fRule()));strcat(strReturn,"&") ;continue;  
              }              
         #endif
       param=-1;
       // Температуры 0-19 смещение 0
            if (strcmp(x+1,"TOUT")==0)           { param=TOUT; }  // Температура улицы
       else if (strcmp(x+1,"TIN")==0)            { param=TIN; }  // Температура в доме
       else if (strcmp(x+1,"TEVAIN")==0)         { param=TEVAIN; }  // Температура на входе испарителя (направление по фреону)
       else if (strcmp(x+1,"TEVAOUT")==0)        { param=TEVAOUT; }  // Температура на выходе испарителя (направление по фреону)
       else if (strcmp(x+1,"TCONIN")==0)         { param=TCONIN; }  // Температура на входе конденсатора (направление по фреону)
       else if (strcmp(x+1,"TCONOUT")==0)        { param=TCONOUT; }  // Температура на выходе конденсатора (направление по фреону)
       else if (strcmp(x+1,"TBOILER")==0)        { param=TBOILER; }  // Температура в бойлере ГВС
       else if (strcmp(x+1,"TACCUM")==0)         { param=TACCUM; }  // Температура на выходе теплоаккмулятора
       else if (strcmp(x+1,"TRTOOUT")==0)        { param=TRTOOUT; } // Температура на выходе RTO (по фреону)
       else if (strcmp(x+1,"TCOMP")==0)          { param=TCOMP; } // Температура нагнетания компрессора
       else if (strcmp(x+1,"TEVAING")==0)        { param=TEVAING;} // Температура на входе испарителя (по гликолю)
       else if (strcmp(x+1,"TEVAOUTG")==0)       { param=TEVAOUTG;} // Температура на выходе испарителя (по гликолю)
       else if (strcmp(x+1,"TCONING")==0)        { param=TCONING;} // Температура на входе конденсатора (по гликолю)
       else if (strcmp(x+1,"TCONOUTG")==0)       { param=TCONOUTG;} // Температура на выходе конденсатора (по гликолю)
       // Сухой контакт 20-25 смещение 20
       /*
       else if (strcmp(x+1,"SEVA")==0)           { param=20;}  // Датчик протока по испарителю
       else if (strcmp(x+1,"SLOWP")==0)          { param=21;}  // Датчик низкого давления
       else if (strcmp(x+1,"SHIGHP")==0)         { param=22;}  // Датчик высокого давления
       else if (strcmp(x+1,"SFROZEN")==0)        { param=23;}  // Датчик заморозки SFROZEN
       */
        else   // Поиск среди имен контактных датчиков смещение 20 (максимум 6)
       {
         for(i=0;i<INUMBER;i++) if(strcmp(x+1,HP.sInput[i].get_name())==0) {param=20+i; break;} 
       }
   //    if (param==-1)  // имя не найдено, дальше разбираем строку
       
       // Частотные датчики смещение 26 (максимум 4)
        if (param==-1) for(i=0;i<FNUMBER;i++) if(strcmp(x+1,HP.sFrequency[i].get_name())==0) {param=26+i; break;} 
       
       if (param==-1)  // имя не найдено, дальше разбираем строку
  //       {   
         // Аналоговые датчики 30-35 смещение 30  количество до 6 штук
              if (strcmp(x+1,"PEVA")==0)           { param=30;}  //  Датчик давления испарителя
         else if (strcmp(x+1,"PCON")==0)           { param=31;}  //  Датчик давления кондесатора
         // Реле  36-49 смещение 36  количество до 14 штук
  //       }
       /*
       else if (strcmp(x+1,"RCOMP")==0)          { param=36;}  // Реле включения компрессора (через пускатель)
       else if (strcmp(x+1,"RPUMPI")==0)         { param=37;}  // Реле включения насоса входного контура  (геоконтур)
       else if (strcmp(x+1,"RPUMPO")==0)         { param=38;}  // Реле включения насоса выхордного контура  (отопление и ГВС)
       else if (strcmp(x+1,"RBOILER")==0)        { param=39;}  // Включение ТЭНа бойлера
       else if (strcmp(x+1,"RTRV")==0)           { param=40;}  // 4-ходовой клапан
       else if (strcmp(x+1,"RFAN1")==0)          { param=41;}  // Реле включения вентилятора испарителя №1
       else if (strcmp(x+1,"RFAN2")==0)          { param=42;}  // Реле включения вентилятора испарителя №2
       else if (strcmp(x+1,"REVI")==0)           { param=43;}  // Соленойд для EVI. (испаритель ниже +3гр и конденсатор выше +40гр)
       else if (strcmp(x+1,"R3WAY")==0)          { param=44;}  // Трех ходовой клапан. Переключение системы СО — ГВС (что сейчас греть)
       else if (strcmp(x+1,"RHEAT")==0)          { param=45;}  // Включение ТЭНа СО (электрокотел), может использоваться как догрев, резерв и т.д.
       else if (strcmp(x+1,"RPUMPB")==0)         { param=46;}  // Реле насоса циркуляции бойлера (ГВС)
       */
       else   // Поиск среди имен среди исполнительных устройств
   //     {
           for(i=0;i<RNUMBER;i++) if (strcmp(x+1,HP.dRelay[i].get_name())==0) {param=36+i; break;} 
  //      }
       if (param==-1)  // имя не найдено, дальше разбираем строку
  //      {
               // ЭРВ 50 только одно устройство поддерживается смещение 50, зато есть имена переменных
                    if (strcmp(x+1,"T_OWERHEAT")==0)     { param=50;}  // ЭРВ целевой перегрев
               else if (strcmp(x+1,"TIME_IN")==0)        { param=51;}  // Постоянная интегрирования времени в секундах
               else if (strcmp(x+1,"K_PRO")==0)          { param=52;}  // ЭРВ Пропорциональная составляющая
               else if (strcmp(x+1,"K_IN")==0)           { param=53;}  // ЭРВ Интегральная составляющая
               else if (strcmp(x+1,"K_DIF")==0)          { param=54;}  // ЭРВ дифференциальная составляющая.
               else if (strcmp(x+1,"M_STEP")==0)         { param=55;}  // ЭРВ Число шагов открытия ЭРВ для правила работы ЭРВ «Manual»
               else if (strcmp(x+1,"CORRECTION")==0)     { param=56;}  // ЭРВ поправка в градусах для правила работы ЭРВ «TEVAOUT-TEVAIN».
        
                // Параметры и опции ТН  смещение 60 занимат 40 позиций не 10!!!!!!!!!!!!!!!!!!!! используется в двух разных функциях, единый список
               else if (strcmp(x+1,"RULE")==0)           { param=60;}  // 0  Алгоритм отопления
               else if (strcmp(x+1,"TEMP1")==0)          { param=61;}  // 1  целевая температура в доме
               else if (strcmp(x+1,"TEMP2")==0)          { param=62;}  // 2  целевая температура обратки
               else if (strcmp(x+1,"TARGET")==0)         { param=63;}  // 3  что является целью ПИД - значения  0 (температура в доме), 1 (температура обратки).
               else if (strcmp(x+1,"DTEMP")==0)          { param=64;}  // 4  гистерезис целевой температуры
               else if (strcmp(x+1,"HP_TIME")==0)        { param=65;}  // 5  Постоянная интегрирования времени в секундах ПИД ТН
               else if (strcmp(x+1,"HP_PRO")==0)         { param=66;}  // 6  Пропорциональная составляющая ПИД ТН
               else if (strcmp(x+1,"HP_IN")==0)          { param=67;}  // 7  Интегральная составляющая ПИД ТН
               else if (strcmp(x+1,"HP_DIF")==0)         { param=68;}  // 8  Дифференциальная составляющая ПИД ТН
               else if (strcmp(x+1,"TEMP_IN")==0)        { param=69;}  // 9  температура подачи
               else if (strcmp(x+1,"TEMP_OUT")==0)       { param=70;}  // 10 температура обратки
               else if (strcmp(x+1,"PAUSE")==0)          { param=71;}  // 11 минимальное время простоя компрессора
               else if (strcmp(x+1,"D_TEMP")==0)         { param=72;}  // 12 максимальная разность температур конденсатора
               else if (strcmp(x+1,"TEMP_PID")==0)       { param=73;}  // 13 Целевая темпеартура ПИД
               else if (strcmp(x+1,"WEATHER")==0)        { param=74;}  // 14 Использование погодозависимости
               else if (strcmp(x+1,"K_WEATHER")==0)      { param=75;}  // 15 Коэффициент погодозависимости
 
               // опции ТН
               else if (strcmp(x+1,"ADD_HEAT")==0)       { param=80;}  // 0  использование дополнительного нагревателя (значения 1 и 0)
               else if (strcmp(x+1,"TEMP_RHEAT")==0)     { param=81;}  // 1  температура управления RHEAT (градусы)
               else if (strcmp(x+1,"PUMP_WORK")==0)      { param=82;}  // 2  работа насоса конденсатора при выключенном компрессоре секунды
               else if (strcmp(x+1,"PUMP_PAUSE")==0)     { param=83;}  // 3  пауза между работой насоса конденсатора при выключенном компрессоре (секунды)
               else if (strcmp(x+1,"ATTEMPT")==0)        { param=84;}  // 4  число попыток пуска ТН
               else if (strcmp(x+1,"TIME_CHART")==0)     { param=85;}  // 5  период обновления статистики
               else if (strcmp(x+1,"BEEP")==0)           { param=86;}  // 6  использование звука
               else if (strcmp(x+1,"NEXTION")==0)        { param=87;}  // 7  использование дисплея nextion
               else if (strcmp(x+1,"EEV_CLOSE")==0)      { param=88;}  // 8  Закрытие ЭРВ при выключении компрессора
               else if (strcmp(x+1,"EEV_LIGHT_START")==0){ param=89;}  // 9  Облегчение старта компрессора
               else if (strcmp(x+1,"EEV_START")==0)      { param=90;}  // 10 Всегда начинать работу ЭРВ со стратовой позици
               else if (strcmp(x+1,"SD_CARD")==0)        { param=91;}  // 11 Запись статистики на карту памяти
               else if (strcmp(x+1,"SAVE_ON")==0)        { param=92;}  // 12 Запись состояния ТН для воссановления его при перезагрузке
               else if (strcmp(x+1,"NEXT_SLEEP")==0)     { param=93;}  // 13 Время засыпания секунды NEXTION
               else if (strcmp(x+1,"NEXT_DIM")==0)       { param=94;}  // 14 Якрость % NEXTION
               else if (strcmp(x+1,"ADD_BOILER")==0)     { param=95;}  // 15 флаг использования тена для догрева ГВС
               else if (strcmp(x+1,"TEMP_RBOILER")==0)   { param=96;}  // 16 температура включчения догрева бойлера
              
               // Параметры для графиков смещение 100 занимает 40 позиций  используется в одной функции get_Chart
               else if (strcmp(x+1,"_NONE")==0)          { param=100;} // ничего не показываем
               else if (strcmp(x+1,"_TOUT")==0)          { param=101;} // Температура улицы
               else if (strcmp(x+1,"_TIN")==0)           { param=102;} // Температура в доме
               else if (strcmp(x+1,"_TEVAIN")==0)        { param=103;} // Температура на входе испарителя (по фреону)
               else if (strcmp(x+1,"_TEVAOUT")==0)       { param=104;} // Температура на выходе испарителя (по фреону)
               else if (strcmp(x+1,"_TCONIN")==0)        { param=105;} // Температура на входе конденсатора (по фреону)
               else if (strcmp(x+1,"_TCONOUT")==0)       { param=106;} // Температура на выходе конденсатора (по фреону)
               else if (strcmp(x+1,"_TBOILER")==0)       { param=107;} // Температура в бойлере ГВС
               else if (strcmp(x+1,"_TACCUM")==0)        { param=108;} // Температура на выходе теплоаккмулятора
               else if (strcmp(x+1,"_TRTOOUT")==0)       { param=109;} // Температура на выходе RTO (по фреону)
               else if (strcmp(x+1,"_TCOMP")==0)         { param=110;} // Температура нагнетания компрессора
               else if (strcmp(x+1,"_TEVAING")==0)       { param=111;} // Температура на входе испарителя (по гликолю)
               else if (strcmp(x+1,"_TEVAOUTG")==0)      { param=112;} // Температура на выходе испарителя (по гликолю)
               else if (strcmp(x+1,"_TCONING")==0)       { param=113;} // Температура на входе конденсатора (по гликолю)
               else if (strcmp(x+1,"_TCONOUTG")==0)      { param=114;} // Температура на выходе конденсатора (по гликолю)
               else if (strcmp(x+1,"_PEVA")==0)          { param=115;} // Давление испарителя
               else if (strcmp(x+1,"_PCON")==0)          { param=116;} // Давление конденсатора
               else if (strcmp(x+1,"_FLOWCON")==0)       { param=117;} // Датчик потока по кондесатору
               else if (strcmp(x+1,"_FLOWEVA")==0)       { param=118;} // Датчик потока по испарителю
               else if (strcmp(x+1,"_FLOWPCON")==0)      { param=119;} // Датчик протока по предконденсатору
               else if (strcmp(x+1,"_posEEV")==0)        { param=120;} // позиция ЭРВ
               else if (strcmp(x+1,"_freqFC")==0)        { param=121;} // частота частотника
               else if (strcmp(x+1,"_powerFC")==0)       { param=122;} // мощность частотника
               else if (strcmp(x+1,"_currentFC")==0)     { param=123;} // Ток компрессора
               else if (strcmp(x+1,"_RCOMP")==0)         { param=124;} // включение компрессора
               else if (strcmp(x+1,"_OVERHEAT")==0)      { param=125;} // перегрев
               else if (strcmp(x+1,"_dCO")==0)           { param=126;} // дельта СО
               else if (strcmp(x+1,"_dGEO")==0)          { param=127;} // дельта геоконтура
               else if (strcmp(x+1,"_T[PEVA]")==0)       { param=128;} // температура расчитанная из давления испариения
               else if (strcmp(x+1,"_T[PCON]")==0)       { param=129;} // температура расчитанная из давления конденсации
               else if (strcmp(x+1,"_PowerCO")==0)       { param=130;} // выходная мощность теплового насоса
               else if (strcmp(x+1,"_PowerGEO")==0)      { param=131;} // Мощность геоконтура
               else if (strcmp(x+1,"_COP")==0)           { param=132;} // Коэффициент преобразования холодилной машины
               else if (strcmp(x+1,"_VOLTAGE")==0)       { param=133;} // Статистика по напряжению
               else if (strcmp(x+1,"_CURRENT")==0)       { param=134;} // Статистика по току
               else if (strcmp(x+1,"_acPOWER")==0)       { param=135;} // Статистика по активная мощность
               else if (strcmp(x+1,"_rePOWER")==0)       { param=136;} // Статистика по Реактивная мощность
               else if (strcmp(x+1,"_fullPOWER")==0)     { param=137;} // Статистика по Полная мощность
               else if (strcmp(x+1,"_kPOWER")==0)        { param=138;} // Статистика по Коэффициент мощности
               else if (strcmp(x+1,"_fullCOP")==0)       { param=139;} // Полный COP
         
               // Параметры для статистики смещение 140 занимает 15 позиций  используется в одной функции get_Stat
               else if (strcmp(x+1,"none")==0)           { param=140;} // ничего не показываем
               else if (strcmp(x+1,"Tin")==0)            { param=141;} // средняя температура дома
               else if (strcmp(x+1,"Tout")==0)           { param=142;} // средняя температура улицы
               else if (strcmp(x+1,"Tboiler")==0)        { param=143;} // средняя температура бойлера
               else if (strcmp(x+1,"Hour")==0)           { param=144;} // число накопленных часов должно быть 24
               else if (strcmp(x+1,"Hmoto")==0)          { param=145;} // моточасы за сутки
               else if (strcmp(x+1,"EnergyCO")==0)       { param=146;} // выработанная энергия
               else if (strcmp(x+1,"Energy220")==0)      { param=147;} // потраченная энергия
               else if (strcmp(x+1,"-COP-")==0)          { param=148;} // Средний КОП за день
               else if (strcmp(x+1,"PowerCO")==0)        { param=149;} // средняя мощность СО
               else if (strcmp(x+1,"Power220")==0)       { param=150;} // средняя потребляемая мощность
                      
               // Частотник 155 только одно устройство поддерживается смещение 155 занимает 15 позиций
               else if (strcmp(x+1,"ON_OFF")==0)         { param=155;} // Флаг включения выключения (управление частотником)
               else if (strcmp(x+1,"MIN_FC")==0)         { param=156;} // Только чтение минимальная частота работы
               else if (strcmp(x+1,"MAX_FC")==0)         { param=157;} // Только чтение максимальная частота работы
               else if (strcmp(x+1,"START_FC")==0)       { param=158;} // Только чтение стартовая частота работы
               else if (strcmp(x+1,"MAX_POWER")==0)      { param=159;} // Только чтение максимальная мощность
               else if (strcmp(x+1,"STATE")==0)          { param=160;} // Только чтение Состояние ПЧ
               else if (strcmp(x+1,"FC")==0)             { param=161;} // Целевая частота ПЧ
               else if (strcmp(x+1,"POWER")==0)          { param=162;} // Текущая мощность
               else if (strcmp(x+1,"AUTO_FC")==0)        { param=163;} // Флаг автоматической подбора частоты
               else if (strcmp(x+1,"ANALOG")==0)         { param=164;} // Флаг аналогового управления
               else if (strcmp(x+1,"LEVEL0")==0)         { param=165;} // Уровень частоты 0 в отсчетах ЦАП
               else if (strcmp(x+1,"LEVEL100")==0)       { param=166;} // Уровень частоты 100% в  отсчетах ЦАП
               else if (strcmp(x+1,"LEVELOFF")==0)       { param=167;} // Уровень частоты в % при отключении
               else if (strcmp(x+1,"STOP_FC")==0)        { param=168;} // флаг глобальная ошибка инвертора - работа инвертора запрещена блокировку можно сбросить установив в 0
               else if (strcmp(x+1,"ERROR_FC")==0)       { param=169;} // Получить ошибку частотника
               
               //  Запросы где второй параметр строка не ЧИСЛО а СТРОКА -----------------------------------
               // Сетевые настройки  смещение 170  занимает 20 позиций
               else if (strcmp(x+1,"ADDRESS")==0)        { param=170;}  // Адрес IP
               else if (strcmp(x+1,"DNS")==0)            { param=171;}  // Адрес DNS
               else if (strcmp(x+1,"GATEWAY")==0)        { param=172;}  // Адрес GATEWAY
               else if (strcmp(x+1,"SUBNET")==0)         { param=173;}  // Адрес SUBNET
               else if (strcmp(x+1,"DHCP")==0)           { param=174;}  // Адрес DHCP
               else if (strcmp(x+1,"MAC")==0)            { param=175;}  // Адрес MAC
               else if (strcmp(x+1,"RES_SOCKET")==0)     { param=176;}  // Время сброса «зависших» сокетов
               else if (strcmp(x+1,"RES_W5200")==0)      { param=177;}  // Время сброса сетевого чипа w5200
               else if (strcmp(x+1,"PASSWORD")==0)       { param=178;}  // Использование паролей
               else if (strcmp(x+1,"PASSUSER")==0)       { param=179;}  // Пароль пользователя
               else if (strcmp(x+1,"PASSADMIN")==0)      { param=180;}  // Пароль администратора
               else if (strcmp(x+1,"SIZE_PACKET")==0)    { param=181;}  // Длина пакета
               else if (strcmp(x+1,"INIT_W5200")==0)     { param=182;}  // Ежеминутный контроль SPI для сетевого чипа
               else if (strcmp(x+1,"PORT")==0)           { param=183;}  // Порт веб сервера
               else if (strcmp(x+1,"NO_ACK")==0)         { param=184;}  // Флаг Не ожидать ответа ACK
               else if (strcmp(x+1,"DELAY_ACK")==0)      { param=185;}  // Задержка мсек перед отправкой пакета
               else if (strcmp(x+1,"PING_ADR")==0)       { param=186;}  // Адрес для пинга
               else if (strcmp(x+1,"PING_TIME")==0)      { param=187;}  // Время пинга в секундах
               else if (strcmp(x+1,"NO_PING")==0)        { param=188;}  // Запрет пинга контроллера
         
                // Настройки дата время  смещение 190
               else if (strcmp(x+1,"TIME")==0)           { param=190;}  // время
               else if (strcmp(x+1,"DATE")==0)           { param=191;}  // дата
               else if (strcmp(x+1,"NTP")==0)            { param=192;}  // NTP сервер
               else if (strcmp(x+1,"UPDATE")==0)         { param=193;}  // обновление по NTP
               else if (strcmp(x+1,"TIMEZONE")==0)       { param=194;}  // часовой пояс
               else if (strcmp(x+1,"UPDATE_I2C")==0)     { param=195;}  // обновление внутренних часов по i2c раз час
        
               // Настройка бойлера смещение 200 занимает 20
               else if (strcmp(x+1,"BOILER_ON")==0)      { param=200;}  // флаг Включения бойлера
               else if (strcmp(x+1,"SCHEDULER_ON")==0)   { param=201;}  // флаг Использование расписания
               else if (strcmp(x+1,"ADD_HEATING")==0)    { param=202;}  // флаг использование ТЭН для нагрева
               else if (strcmp(x+1,"SALLMONELA")==0)     { param=203;}  // флаг Сальмонела раз в неделю греть бойлер
               else if (strcmp(x+1,"CIRCULATION")==0)    { param=204;}  // флаг Управления циркуляционным насосом ГВС
               else if (strcmp(x+1,"TEMP_TARGET")==0)    { param=205;}  // Целевая температура бойлера
               else if (strcmp(x+1,"DTARGET")==0)        { param=206;}  // гистерезис целевой температуры ночью
               else if (strcmp(x+1,"TEMP_MAX")==0)       { param=207;}  // Температура подачи максимальная
               else if (strcmp(x+1,"PAUSE1")==0)         { param=208;}  // Минимальное время простоя компрессора в минутах
               else if (strcmp(x+1,"SCHEDULER")==0)      { param=209;}  // Расписание SCHEDULER
               else if (strcmp(x+1,"CIRCUL_WORK")==0)    { param=210;}  // Время  работы насоса ГВС
               else if (strcmp(x+1,"CIRCUL_PAUSE")==0)   { param=211;}  // Пауза в работе насоса ГВС
               else if (strcmp(x+1,"RESET_HEAT")==0)     { param=212;}  // флаг Сброса лишнего тепла в СО
               else if (strcmp(x+1,"RESET_TIME")==0)     { param=213;}  // время сброса излишков тепла в секундах (fResetHeat)
               else if (strcmp(x+1,"BOIL_TIME")==0)      { param=214;}  // Постоянная интегрирования времени в секундах ПИД ТН
               else if (strcmp(x+1,"BOIL_PRO")==0)       { param=215;}  // Пропорциональная составляющая ПИД ГВС
               else if (strcmp(x+1,"BOIL_IN")==0)        { param=216;}  // Интегральная составляющая ПИД ГВС
               else if (strcmp(x+1,"BOIL_DIF")==0)       { param=217;}  // Дифференциальная составляющая ПИД ГВС
               else if (strcmp(x+1,"BOIL_TEMP")==0)      { param=218;}  // Целевая темпеартура ПИД ГВС
               else if (strcmp(x+1,"DTARGET_DAY")==0)    { param=219;}  // гистерезис целевой температуры днем
               // Настройка Уведомлений смещение 220 занимает 30
               else if (strcmp(x+1,"MAIL")==0)           { param=220;}  // флаг уведомления скидывать на почту
               else if (strcmp(x+1,"MAIL_AUTH")==0)      { param=221;}  // флаг необходимости авторизации на почтовом сервере
               else if (strcmp(x+1,"MAIL_INFO")==0)      { param=222;}  // флаг необходимости добавления в письмо информации о состоянии ТН
               else if (strcmp(x+1,"SMS")==0)            { param=223;}  // флаг уведомления скидывать на СМС (пока не реализовано)
               else if (strcmp(x+1,"MESS_RESET")==0)     { param=224;}  // флаг уведомления Сброс
               else if (strcmp(x+1,"MESS_ERROR")==0)     { param=225;}  // флаг уведомления Ошибка
               else if (strcmp(x+1,"MESS_LIFE")==0)      { param=226;}  // флаг уведомления Сигнал жизни
               else if (strcmp(x+1,"MESS_TEMP")==0)      { param=227;}  // флаг уведомления Достижение граничной температуры
               else if (strcmp(x+1,"MESS_SD")==0)        { param=228;}  // флаг уведомления "Проблемы с sd картой"
               else if (strcmp(x+1,"MESS_WARNING")==0)   { param=229;}  // флаг уведомления "Прочие уведомления"
               else if (strcmp(x+1,"SMTP_SERVER")==0)    { param=230;}  // Адрес сервера SMTP_IP
               else if (strcmp(x+1,"SMTP_IP")==0)        { param=231;}  // IP Адрес сервера
               else if (strcmp(x+1,"SMTP_PORT")==0)      { param=232;}  // Адрес порта сервера
               else if (strcmp(x+1,"SMTP_LOGIN")==0)     { param=233;}  // логин сервера если включена авторизация
               else if (strcmp(x+1,"SMTP_PASS")==0)      { param=234;}  // пароль сервера если включена авторизация
               else if (strcmp(x+1,"SMTP_MAILTO")==0)    { param=235;}  // адрес отправителя
               else if (strcmp(x+1,"SMTP_RCPTTO")==0)    { param=236;}  // адрес получателя
               else if (strcmp(x+1,"SMS_SERVICE")==0)    { param=237;}  // сервис для отправки смс
               else if (strcmp(x+1,"SMS_IP")==0)         { param=238;}  // IP адрес сервера для отправки смс
               else if (strcmp(x+1,"SMS_PHONE")==0)      { param=239;}  // телефон куда отправляется смс
               else if (strcmp(x+1,"SMS_P1")==0)         { param=240;}  // первый параметр
               else if (strcmp(x+1,"SMS_P2")==0)         { param=241;}  // второй параметр
               else if (strcmp(x+1,"SMS_NAMEP1")==0)     { param=242;}  // описание первого параметра
               else if (strcmp(x+1,"SMS_NAMEP2")==0)     { param=243;}  // описание второго параметра
               else if (strcmp(x+1,"MESS_TIN")==0)       { param=244;}  // Критическая температура в доме (если меньше то генерится уведомление)
               else if (strcmp(x+1,"MESS_TBOILER")==0)   { param=245;}  // Критическая температура бойлера (если меньше то генерится уведомление)
               else if (strcmp(x+1,"MESS_TCOMP")==0)     { param=246;}  // Критическая температура компрессора (если больше то генериться уведомление)
               else if (strcmp(x+1,"MAIL_RET")==0)       { param=247;}  // ответ на тестовое сообщение
               else if (strcmp(x+1,"SMS_RET")==0)        { param=248;}  // ответ на тестовое SMS
        
               // Настройки Профиля  смещение 250
               else if (strcmp(x+1,"NAME_PROFILE")==0)   { param=250;}  // Имя профиля до 10 русских букв
               else if (strcmp(x+1,"ENABLE_PROFILE")==0) { param=251;}  // разрешение использовать в списке
               else if (strcmp(x+1,"ID_PROFILE")==0)     { param=252;}  // номер профиля
               else if (strcmp(x+1,"NOTE_PROFILE")==0)   { param=253;}  // описание профиля
               else if (strcmp(x+1,"DATE_PROFILE")==0)   { param=254;}  // дата профиля
               else if (strcmp(x+1,"CRC16_PROFILE")==0)  { param=255;}  // контрольная сумма профиля
               else if (strcmp(x+1,"NUM_PROFILE")==0)    { param=256;}  // максимальное число профилей
               //else if (strcmp(x+1,"SEL_PROFILE")==0)    { param=257;}  // список профилей
               
               // Настройки Счетчика смещение 260
               else if (strcmp(x+1,"NAME_SDM")==0)       { param=260;}  // Имя счетчика
               else if (strcmp(x+1,"NOTE_SDM")==0)       { param=261;}  // Описание счетчика
               else if (strcmp(x+1,"MAX_VOLTAGE_SDM")==0){ param=262;}  // Контроль напряжения max
               else if (strcmp(x+1,"MIN_VOLTAGE_SDM")==0){ param=263;}  // Контроль напряжения min
               else if (strcmp(x+1,"MAX_POWER_SDM")==0)  { param=264;}  // Контроль мощности max
               else if (strcmp(x+1,"VOLTAGE_SDM")==0)    { param=265;}  // Напряжение
               else if (strcmp(x+1,"CURRENT_SDM")==0)    { param=266;}  // Ток
               else if (strcmp(x+1,"REPOWER_SDM")==0)    { param=267;}  // Реактивная мощность
               else if (strcmp(x+1,"ACPOWER_SDM")==0)    { param=268;}  // Активная мощность
               else if (strcmp(x+1,"POWER_SDM")==0)      { param=269;}  // Полная мощность
               else if (strcmp(x+1,"POW_FACTOR_SDM")==0) { param=270;}  // Коэффициент мощности
               else if (strcmp(x+1,"PHASE_SDM")==0)      { param=271;}  // Угол фазы (градусы)
               else if (strcmp(x+1,"IACENERGY_SDM")==0)  { param=272;}  // Потребленная активная энергия
               else if (strcmp(x+1,"EACENERGY_SDM")==0)  { param=273;}  // Переданная активная энергия
               else if (strcmp(x+1,"IREENERGY_SDM")==0)  { param=274;}  // Потребленная реактивная энергия
               else if (strcmp(x+1,"REENERGY_SDM")==0)   { param=275;}  // Переданная реактивная энергия
               else if (strcmp(x+1,"ACENERGY_SDM")==0)   { param=276;}  // Суммараная активная энергия
               else if (strcmp(x+1,"REENERGY_SDM")==0)   { param=277;}  // Суммараная реактивная энергия
               else if (strcmp(x+1,"ENERGY_SDM")==0)     { param=278;}  // Суммараная  энергия
               else if (strcmp(x+1,"LINK_SDM")==0)       { param=279;}  // Cостояние связи со счетчиком
        
              // Настройки MQTT клиента смещение 290 общее число 30 шт
               else if (strcmp(x+1,"USE_THINGSPEAK")==0) { param=290;}  // флаг использования  ThingSpeak
               else if (strcmp(x+1,"USE_MQTT")==0)       { param=291;}  // флаг использования MQTT
               else if (strcmp(x+1,"BIG_MQTT")==0)       { param=292;}  // флаг отправки ДОПОЛНИТЕЛЬНЫХ данных на MQTT
               else if (strcmp(x+1,"SDM_MQTT")==0)       { param=293;}  // флаг отправки данных электросчетчика на MQTT
               else if (strcmp(x+1,"FC_MQTT")==0)        { param=294;}  // флаг отправки данных инвертора на MQTT
               else if (strcmp(x+1,"COP_MQTT")==0)       { param=295;}  // флаг отправки данных COP на MQTT
               else if (strcmp(x+1,"TIME_MQTT")==0)      { param=296;}  // период отправки на сервер в сек. 10...60000
               else if (strcmp(x+1,"ADR_MQTT")==0)       { param=297;}  // Адрес сервера
               else if (strcmp(x+1,"IP_MQTT")==0)        { param=298;}  // IP Адрес сервера
               else if (strcmp(x+1,"PORT_MQTT")==0)      { param=299;}  // Адрес порта сервера
               else if (strcmp(x+1,"LOGIN_MQTT")==0)     { param=300;}  // логин сервера
               else if (strcmp(x+1,"PASSWORD_MQTT")==0)  { param=301;}  // пароль сервера
               else if (strcmp(x+1,"ID_MQTT")==0)        { param=302;}  // Идентификатор клиента на MQTT сервере
               else if (strcmp(x+1,"USE_NARMON")==0)     { param=303;}  // флаг отправки данных на народный мониторинг
               else if (strcmp(x+1,"BIG_NARMON")==0)     { param=304;}  // флаг отправки данных на народный мониторинг ,большую версию
               else if (strcmp(x+1,"ADR_NARMON")==0)     { param=305;}  // Адрес сервера народный мониторинг
               else if (strcmp(x+1,"IP_NARMON")==0)      { param=306;}  // IP Адрес сервера народный мониторинг
               else if (strcmp(x+1,"PORT_NARMON")==0)    { param=307;}  // Адрес порта сервера народный мониторинг
               else if (strcmp(x+1,"LOGIN_NARMON")==0)   { param=308;}  // логин сервера народный мониторинг
               else if (strcmp(x+1,"PASSWORD_NARMON")==0){ param=309;}  // пароль сервера народный мониторинг
               else if (strcmp(x+1,"ID_NARMON")==0)      { param=310;}  // Идентификатор клиента на народный мониторинг
  //      }
        if ((pm==ATOF_ERROR)&&((param<170)||(param>320)))        // Ошибка преобразования для чисел но не для строк (смещение 170)! - завершить запрос с ошибкой
          { strcat(strReturn,"E04");strcat(strReturn,"&");  continue;  }
       
       if (param==-1)  { strcat(strReturn,"E02");strcat(strReturn,"&");  continue; }  // Не верный параметр
       x[0]=0;                                                                        // Обрезаем строку до скобки (
       // Все готово к разбору имен функций c параметром
       // 1. Датчики температуры смещение param 0
       if (strstr(str,"Temp"))          // Проверка для запросов содержащих Temp
        {
        if(param>=20)  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
         else  // параметр верный
           {    p=param-0;                                // вычисление параметра без смещения
              if (strcmp(str,"get_Temp")==0)              // Функция get_Temp
                { if (HP.sTemp[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_Temp()/100.0,1)); 
                  else strcat(strReturn,"-");             // Датчика нет ставим прочерк
                  strcat(strReturn,"&"); continue; }
               if (strcmp(str,"get_rawTemp")==0)           // Функция get_RawTemp
                { if (HP.sTemp[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_rawTemp()/100.0,1)); 
                  else strcat(strReturn,"-");             // Датчика нет ставим прочерк
                  strcat(strReturn,"&"); continue; }    
               if (strcmp(str,"get_fullTemp")==0)         // Функция get_FulTemp
                { if (HP.sTemp[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                  { 
                    #ifdef SENSOR_IP
                    strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_rawTemp()/100.0,1));   // Значение проводного датчика вывод
                    if((HP.sTemp[p].devIP!=NULL)&& (HP.sTemp[p].devIP->get_fUse())&&(HP.sTemp[p].devIP->get_link()>-1)) // Удаленный датчик привязан к данному проводному датчику надо использовать
                       {
                        strcat(strReturn," [");
                        strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_Temp()/100.0,1));
                        strcat(strReturn,"]");
                       }
                    #else
                     strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_Temp()/100.0,1)); 
                    #endif    
                  }  
                  else strcat(strReturn,"-");             // Датчика нет ставим прочерк
                  strcat(strReturn,"&"); continue; }    
                
              if (strcmp(str,"get_minTemp")==0)           // Функция get_minTemp
                { if (HP.sTemp[p].get_present()) // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_minTemp()/100.0,1)); 
                  else strcat(strReturn,"-");              // Датчика нет ставим прочерк
                  strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_maxTemp")==0)           // Функция get_maxTemp
                { if (HP.sTemp[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_maxTemp()/100.0,1)); 
                  else strcat(strReturn,"-");             // Датчика нет ставим прочерк
                  strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_errTemp")==0)           // Функция get_errTemp
                { strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_errTemp()/100.0,1)); strcat(strReturn,"&"); continue; }
                               
              if (strcmp(str,"get_addressTemp")==0)           // Функция get_addressTemp
                { strcat(strReturn,addressToHex(HP.sTemp[p].get_address())); strcat(strReturn,"&"); continue; }  
                        
              if (strcmp(str,"get_testTemp")==0)           // Функция get_testTemp
                { strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_testTemp()/100.0,1)); strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_errcodeTemp")==0)           // Функция get_errcodeTemp
                 { strcat(strReturn,int2str(HP.sTemp[p].get_lastErr())); strcat(strReturn,"&"); continue; }
                 
              if (strcmp(str,"get_presentTemp")==0)           // Функция get_presentTemp
                  {
                  if (HP.sTemp[p].get_present()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  }
              if (strcmp(str,"get_noteTemp")==0)           // Функция get_noteTemp
                 { strcat(strReturn,HP.sTemp[p].get_note()); strcat(strReturn,"&"); continue; }    

             /*    
             if (strcmp(str,"get_targetTemp")==0)           // Функция get_targetTemp резрешены не все датчики при этом.
                 {
                  if (p==1) {strcat(strReturn,ftoa(temp,HP.get_TempTargetIn()/100.0,1));  }
                   else if (p==5)  {strcat(strReturn,ftoa(temp,HP.get_TempTargetCO()/100.0,1)); }
                     else if (p==6)  {strcat(strReturn,ftoa(temp,HP.get_TempTargetBoil()/100.0,1)); }
                       else  strcat(strReturn,"E06");                 // использование имя устанавливаемого параметра «здесь» запрещено
                         strcat(strReturn,"&");  continue;
                 }
               */    
              // ---- SET ----------------- Для температурных датчиков - запросы на УСТАНОВКУ парметров
              if (strcmp(str,"set_testTemp")==0)           // Функция set_testTemp
                 { if (HP.sTemp[p].set_testTemp(pm*100)==OK)    // Установить значение в сотых градуса
                   { strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_testTemp()/100.0,1)); strcat(strReturn,"&");  continue;  } 
                    else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}       // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                 }
               if (strcmp(str,"set_errTemp")==0)           // Функция set_errTemp
                  { if (HP.sTemp[p].set_errTemp(pm*100)==OK)    // Установить значение в сотых градуса
                    { strcat(strReturn,ftoa(temp,(float)HP.sTemp[p].get_errTemp()/100.0,1)); strcat(strReturn,"&"); continue; }   
                    else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}      // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }

                /*  
              if (strcmp(str,"set_targetTemp")==0)           // Функция set_targetTemp резрешены не все датчики при этом.
                 {
                  if (p==1) {HP.set_TempTargetIn(pm*100);  }
                   else if (p==5)  {HP.set_TempTargetCO(pm*100); }
                     else if (p==6)  {HP.set_TempTargetBoil(pm*100); }
                       else  strcat(strReturn,"E06");                 // использование имя устанавливаемого параметра «здесь» запрещено
                         strcat(strReturn,"&");  continue;
                 }
                 */
               if (strcmp(str,"set_addressTemp")==0)        // Функция set_addressTemp
               {
            	   uint8_t n = pm;
            	   if(n <= TNUMBER)                  // Если индекс находится в диапазоне допустимых значений Здесь индекс начинается с 1, ЗНАЧЕНИЕ 0 - обнуление адреса!!
            	   {
            		   if(n == 0) HP.sTemp[p].set_address(NULL, 0);   // Сброс адреса
            		   else if(OW_scanTable) HP.sTemp[p].set_address(OW_scanTable[n-1].address, OW_scanTable[n-1].bus_type);
            	   }
            	   //      strcat(strReturn,int2str(pm)); strcat(strReturn,"&"); continue;}   // вернуть номер
            	   strcat(strReturn,addressToHex(HP.sTemp[p].get_address())); strcat(strReturn,"&"); continue;
               }  // вернуть адрес
               else { strcat(strReturn,"E08");strcat(strReturn,"&");   continue;}      // выход за диапазон допустимых номеров, значение не установлено

            }  // end else    
         } //if ((strstr(str,"Temp")>0)
           
        // 2.  Датчики аналоговые, давления смещение param 30, ТОЧНОСТЬ СОТЫЕ дипазон 6
        if (strstr(str,"Press"))          // Проверка для запросов содержащих Press
          {
          if ((param>=36)||(param<30))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
          else  // параметр верный
            {   p=param-30;                             // Убрать смещение
              if (strcmp(str,"get_Press")==0)           // Функция get_Press
              { if (HP.sADC[p].get_present())         // Если датчик есть в конфигурации то выводим значение
                {
                    uint16_t x=HP.sADC[p].get_Press();
                    strcat(strReturn,ftoa(temp,(float)x/100.0,2));
                    strcat(strReturn," [t:");
                    #ifdef EEV_DEF
                    strcat(strReturn,ftoa(temp,(float)PressToTemp(x,HP.dEEV.get_typeFreon())/100.0,2));strcat(strReturn,"]");
                    #else
                    strcat(strReturn," -.-]");
                    #endif
                }
                else strcat(strReturn,"-");             // Датчика нет ставим прочерк
              strcat(strReturn,"&"); continue; }
                  
              if (strcmp(str,"get_adcPress")==0)           // Функция get_adcPress
                { strcat(strReturn,int2str(HP.sADC[p].get_lastADC())); strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_minPress")==0)           // Функция get_minPress
                { if (HP.sADC[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_minPress()/100.0,2)); 
                  else strcat(strReturn,"-");              // Датчика нет ставим прочерк
                strcat(strReturn,"&"); continue; }
                
               if (strcmp(str,"get_maxPress")==0)           // Функция get_maxPress
                { if (HP.sADC[p].get_present())           // Если датчик есть в конфигурации то выводим значение
                  strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_maxPress()/100.0,2)); 
                  else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_zeroPress")==0)           // Функция get_zeroTPress
                { strcat(strReturn,int2str(HP.sADC[p].get_zeroPress())); strcat(strReturn,"&"); continue; }

               if (strcmp(str,"get_transPress")==0)           // Функция get_transTPress
                { strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_transADC(),3)); strcat(strReturn,"&"); continue; }
                  
              if (strcmp(str,"get_pinPress")==0)           // Функция get_pinPress
                { strcat(strReturn,"AD"); strcat(strReturn,int2str(HP.sADC[p].get_pinA())); strcat(strReturn,"&"); continue; }
                
              if (strcmp(str,"get_testPress")==0)           // Функция get_testPress
                { strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_testPress()/100.0,2)); strcat(strReturn,"&"); continue; }

              if (strcmp(str,"get_errcodePress")==0)           // Функция get_errcodePress
                 { strcat(strReturn,int2str(HP.sADC[p].get_lastErr())); strcat(strReturn,"&"); continue; }

              if (strcmp(str,"get_presentPress")==0)           // Функция get_presentPress
                  {
                  if (HP.sADC[p].get_present()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  }   
              if (strcmp(str,"get_notePress")==0)           // Функция get_notePress
                 { strcat(strReturn,HP.sADC[p].get_note()); strcat(strReturn,"&"); continue; }    

             // ---- SET ----------------- Для аналоговых  датчиков - запросы на УСТАНОВКУ парметров
              if (strcmp(str,"set_testPress")==0)           // Функция set_testPress
                 { if (HP.sADC[p].set_testPress(pm*100)==OK)    // Установить значение
                   {strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_testPress()/100.0,2)); strcat(strReturn,"&"); continue; } 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }

              if (strcmp(str,"set_transPress")==0)           // Функция set_transPress float
                 { if (HP.sADC[p].set_transADC(pm)==OK)    // Установить значение
                   {strcat(strReturn,ftoa(temp,(float)HP.sADC[p].get_transADC(),3)); strcat(strReturn,"&"); continue;} 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
              
              if (strcmp(str,"set_zeroPress")==0)           // Функция set_zeroTPress
                 { if (HP.sADC[p].set_zeroPress((int16_t)pm)==OK)    // Установить значение
                   {strcat(strReturn,int2str(HP.sADC[p].get_zeroPress())); strcat(strReturn,"&"); continue;} 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }    
       
              }  // end else
           } //if ((strstr(str,"Press")>0) 
         
           
         //3.  Датчики сухой контакт смещение param 20
        if (strstr(str,"Input"))          // Проверка для запросов содержащих Input
          {
           if ((param>=26)||(param<20))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
           else  // параметр верный
            {   p=param-20; 
              if (strcmp(str,"get_Input")==0)           // Функция get_Input
                  {
                  if (HP.sInput[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                     { if (HP.sInput[p].get_Input()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);}
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  } 
               if (strcmp(str,"get_presentInput")==0)           // Функция get_presentInput
                  {
                  if (HP.sInput[p].get_present()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  } 
               if (strcmp(str,"get_noteInput")==0)           // Функция get_noteInput
                 { strcat(strReturn,HP.sInput[p].get_note()); strcat(strReturn,"&"); continue; }  
                 
               if (strcmp(str,"get_testInput")==0)           // Функция get_testInput
                  {
                  if (HP.sInput[p].get_testInput()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  }  

               if (strcmp(str,"get_alarmInput")==0)           // Функция get_alarmInput
                  {
                  if (HP.sInput[p].get_alarmInput()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  }  
                if (strcmp(str,"get_errcodeInput")==0)           // Функция get_errcodeInput
                 { strcat(strReturn,int2str(HP.sInput[p].get_lastErr())); strcat(strReturn,"&"); continue; }
                   
              if (strcmp(str,"get_pinInput")==0)           // Функция get_pinInput
                { strcat(strReturn,"D"); strcat(strReturn,int2str(HP.sInput[p].get_pinD()));
                  strcat(strReturn,"&"); continue; }    
              if (strcmp(str,"get_typeInput")==0)           // Функция get_pinInput
                { if (HP.sInput[p].get_present()==true)  // датчик есть в кнфигурации
                   switch((int)HP.sInput[p].get_typeInput())
                   {
                    case pALARM: strcat(strReturn,"Alarm"); break;                // 0 Аварийный датчик, его срабатываение приводит к аварии и останове Тн
                    case pSENSOR:strcat(strReturn,"Work");  break;                // 1 Обычный датчик, его значение используется в алгоритмах ТН
                    case pPULSE: strcat(strReturn,"Pulse"); break;                // 2 Импульсный висит на прерывании и считает частоты - выходная величина ЧАСТОТА
                    default:strcat(strReturn,"err_type"); break;                  // Ошибка??
                   }
                   else strcat(strReturn,"none");                                 // датчик отсутвует
                   strcat(strReturn,"&"); continue; }    
               if (strcmp(str,"get_noteInput")==0)           // Функция get_noteInput
                 { strcat(strReturn,HP.sInput[p].get_note()); strcat(strReturn,"&"); continue; }    
                            

             // ---- SET ----------------- Для датчиков сухой контакт - запросы на УСТАНОВКУ парметров
              if (strcmp(str,"set_testInput")==0)           // Функция set_testInput
                 { if (HP.sInput[p].set_testInput((int16_t)pm)==OK)    // Установить значение
                   { if (HP.sInput[p].get_testInput()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); strcat(strReturn,"&"); continue; } 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
                  
              if (strcmp(str,"set_alarmInput")==0)           // Функция set_alarmInput
                 { if (HP.sInput[p].set_alarmInput((int16_t)pm)==OK)    // Установить значение
                   { if (HP.sInput[p].get_alarmInput()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); strcat(strReturn,"&"); continue; } 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
             }  // else end 
           } //if ((strstr(str,"Input")>0)  
           
          // 4 Частотные датчики ДАТЧИКИ ПОТОКА
           if (strstr(str,"Flow"))          // Проверка для запросов содержащих Frequency
          {
           if ((param>=30)||(param<26))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
           else  // параметр верный
            {   p=param-26; 
               if (strcmp(str,"get_Flow")==0)           // Функция get_Flow
                  {
                  if (HP.sFrequency[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,ftoa(temp,(float)HP.sFrequency[p].get_Value()/1000.0,3)); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }
               if (strcmp(str,"get_frFlow")==0)           // Функция get_frFlow
                  {
                  if (HP.sFrequency[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,ftoa(temp,(float)HP.sFrequency[p].get_Frequency()/1000.0,3)); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }
             if (strcmp(str,"get_minFlow")==0)           // Функция get_minFlow
                  {
                  if (HP.sFrequency[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,ftoa(temp,(float)HP.sFrequency[p].get_minValue()/1000.0,3)); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }
            if (strcmp(str,"get_kfFlow")==0)           // Функция get_kfFlow
                  {
                  if (HP.sFrequency[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,ftoa(temp,HP.sFrequency[p].get_kfValue(),3)); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }
            if (strcmp(str,"get_capacityFlow")==0)           // Функция get_capacityFlow
                  {
                  if (HP.sFrequency[p].get_present())        // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,int2str(HP.sFrequency[p].get_Capacity())); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }                  
            if (strcmp(str,"get_testFlow")==0)           // Функция get_testFlow
                  {
                  if (HP.sFrequency[p].get_present())          // Если датчик есть в конфигурации то выводим значение
                   strcat(strReturn,ftoa(temp,(float)HP.sFrequency[p].get_testValue()/1000.0,3)); 
                   else strcat(strReturn,"-");               // Датчика нет ставим прочерк
                  strcat(strReturn,"&") ;    continue;
                  }      
            if (strcmp(str,"get_pinFlow")==0)              // Функция get_pinFlow
                { strcat(strReturn,"D"); strcat(strReturn,int2str(HP.sFrequency[p].get_pinF()));
                  strcat(strReturn,"&"); continue; } 
            if (strcmp(str,"get_errcodeFlow")==0)           // Функция get_errcodeFlow
                { strcat(strReturn,int2str(HP.sFrequency[p].get_lastErr()));
                  strcat(strReturn,"&"); continue; } 
            if (strcmp(str,"get_noteFlow")==0)               // Функция get_noteFlow
                 { strcat(strReturn,HP.sFrequency[p].get_note()); strcat(strReturn,"&"); continue; }   

            // ---- SET ----------------- Для частотных  датчиков - запросы на УСТАНОВКУ парметров
              if (strcmp(str,"set_testFlow")==0)           // Функция set_testFlow
                 { if (HP.sFrequency[p].set_testValue(pm*1000)==OK)    // Установить значение
                   {strcat(strReturn,ftoa(temp,(float)HP.sFrequency[p].get_testValue()/1000.0,3)); strcat(strReturn,"&"); continue; } 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
              if (strcmp(str,"set_kfFlow")==0)           // Функция set_kfFlow float
                 { if (HP.sFrequency[p].set_kfValue(pm)==OK)    // Установить значение
                   {strcat(strReturn,ftoa(temp,HP.sFrequency[p].get_kfValue(),3)); strcat(strReturn,"&"); continue;} 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
              if (strcmp(str,"set_capacityFlow")==0)           // Функция set_capacityFlow float
                 { 
                   if (HP.sFrequency[p].set_Capacity(pm)==OK)    // Установить значение
                   {  strcat(strReturn,int2str(HP.sFrequency[p].get_Capacity()));  strcat(strReturn,"&"); continue;} 
                   else { strcat(strReturn,"E35");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }   
             }  // else end 
           } //if ((strstr(str,"Flow")>0)          
           
          //5.  РЕЛЕ смещение param 36 диапазон 14
          if (strstr(str,"Relay"))          // Проверка для запросов содержащих Relay
             {
             if ((param>=50)||(param<36))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-36; 
               if (strcmp(str,"get_Relay")==0)           // Функция get_relay
                  {
                  if (HP.dRelay[p].get_Relay()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  } 
               if (strcmp(str,"get_presentRelay")==0)           // Функция get_presentRelay
                  {
                  if (HP.dRelay[p].get_present()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero);
                  strcat(strReturn,"&") ;    continue;
                  } 
               if (strcmp(str,"get_noteRelay")==0)           // Функция get_noteRelay
                 { strcat(strReturn,HP.dRelay[p].get_note()); strcat(strReturn,"&"); continue; }  
                 
               if (strcmp(str,"get_pinRelay")==0)           // Функция get_pinRelay
                { strcat(strReturn,"D"); strcat(strReturn,int2str(HP.dRelay[p].get_pinD())); strcat(strReturn,"&"); continue; }    
 
              // ---- SET ----------------- Для реле - запросы на УСТАНОВКУ парметров
              if (strcmp(str,"set_Relay")==0)           // Функция set_Relay
                 { if (HP.dRelay[p].set_Relay((int16_t)pm)==OK)    // Установить значение
                   { if (HP.dRelay[p].get_Relay()==true)  strcat(strReturn,cOne); else  strcat(strReturn,cZero); strcat(strReturn,"&"); continue; } 
                   else { strcat(strReturn,"E05");strcat(strReturn,"&");  continue;}         // выход за диапазон ПРЕДУПРЕЖДЕНИЕ значение не установлено
                  }
              }  // else end 
          } //if ((strstr(str,"Relay")>0)  5

       
          //6.  ЭРВ смещение param 50
          if (strstr(str,"EEV"))          // Проверка для запросов содержащих EEV
             {
             if ((param>=60)||(param<50))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-50; 
               if (strcmp(str,"get_paramEEV")==0)           // Функция get_paramEEV - получить значение параметра ЭРВ
                  {
                   #ifdef EEV_DEF        
                   switch (p)
                     {
                      case 0: strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_tOverheat()/100.0,1)); break;  
                      case 1: strcat(strReturn,int2str(HP.dEEV.get_timeIn()));                     break;  
                      case 2: strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kpro()/100.0,2));      break;     // В СОТЫХ!!!
                      case 3: strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kint()/100.0,2));      break;     // В СОТЫХ!!!
                      case 4: strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kdif()/100.0,2));      break;     // В СОТЫХ!!!
                      case 5: strcat(strReturn,int2str(HP.dEEV.get_manualStep()));                 break;  
                      case 6: strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Correction()/100.0,1));break;  
                      default: strcat(strReturn,"E10");                                            break;   
                     }
                   #else
                   strcat(strReturn,"-");
                   #endif  
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_paramEEV")==0) 
              if (strcmp(str,"set_paramEEV")==0)           // Функция set_paramEEV - установить значение паремтра ЭРВ
                  {
                  #ifdef EEV_DEF   
                   switch (p)
                     {
                      case 0: if(HP.dEEV.set_tOverheat((int)(pm*100))==OK)strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_tOverheat()/100.0,1));  else strcat(strReturn,"E11"); break;  
                      case 1: if(HP.dEEV.set_timeIn(pm)==OK)              strcat(strReturn,int2str(HP.dEEV.get_timeIn()));                      else strcat(strReturn,"E11"); break;  
                      case 2: if(HP.dEEV.set_Kpro((int)(pm*100.0))==OK)   strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kpro()/100.0,2));       else strcat(strReturn,"E11"); break;   // В СОТЫХ!!!
                      case 3: if(HP.dEEV.set_Kint((int)(pm*100.0))==OK)   strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kint()/100.0,2));       else strcat(strReturn,"E11"); break;   // В СОТЫХ!!!
                      case 4: if(HP.dEEV.set_Kdif((int)(pm*100.0))==OK)   strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Kdif()/100.0,2));       else strcat(strReturn,"E11"); break;   // В СОТЫХ!!!
                      case 5: if(HP.dEEV.set_manualStep(pm)==OK)          strcat(strReturn,int2str(HP.dEEV.get_manualStep()));                  else strcat(strReturn,"E11"); break;  
                      case 6: if(HP.dEEV.set_Correction(pm*100)==OK)      strcat(strReturn,ftoa(temp,(float)HP.dEEV.get_Correction()/100.0,1)); else strcat(strReturn,"E11"); break;  
                      default: strcat(strReturn,"E10");                                                                                                                       break;   
                     }
                    #else
                    strcat(strReturn,"-");
                    #endif 
                   strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_paramEEV")==0) 
              }  // else end 
           } //if ((strstr(str,"EEV")>0)  
           
        //7.  Частотный преобразователь смещение param 155
          if (strstr(str,"FC"))          // Проверка для запросов содержащих FC
             {
             if ((param>=170)||(param<155))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-155; 
               if (strcmp(str,"get_paramFC")==0)           // Функция get_paramFC - получить значение параметра FC
                  {
                    strcat(strReturn,HP.dFC.get_paramFC((TYPE_PARAM_FC)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_paramFC")==0) 
              if (strcmp(str,"set_paramFC")==0)           // Функция set_paramFC - установить значение паремтра FC
                  {
                  // Получить устанавливаемое значение
                   if (HP.dFC.set_paramFC((TYPE_PARAM_FC)p,pm))     // преобразование удачно
                    strcat(strReturn,HP.dFC.get_paramFC((TYPE_PARAM_FC)p));
                   else
                      strcat(strReturn,"E27") ; // ошибка преобразования строки
                   strcat(strReturn,"&") ; continue ;
                  } // (strcmp(str,"set_paramFC")==0) 
              }  // else end 
            } //if ((strstr(str,"FC")>0)  

          //8.  Параметры  и опции ТН смещение 60 занимает 40 !!!!!!!!!!!!!!!!!!
          if (strstr(str,"HP"))          // Проверка для запросов содержащих HP
             {
             if ((param>=100)||(param<60))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-60;  // смещение
               if (strcmp(str,"get_paramCoolHP")==0)           // Функция get_paramCoolHP - получить значение параметра охлаждения ТН
                  {
                    strcat(strReturn,HP.Prof.get_paramCoolHP((PARAM_HP)p,HP.dFC.get_present()));
                    strcat(strReturn,"&") ; continue;
                  } // strcmp(str,"get_paramCoolHP")==0)
                  
              if (strcmp(str,"set_paramCoolHP")==0)           // Функция set_paramCoolHP - установить значение паремтра охлаждения ТН
                  {
                    // Получить устанавливаемое значение
                   if (HP.Prof.set_paramCoolHP((PARAM_HP)p,pm))     // преобразование удачно
                      strcat(strReturn,HP.Prof.get_paramCoolHP((PARAM_HP)p,HP.dFC.get_present()));
                   else
                      strcat(strReturn,"E16") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"paramCoolHP")==0) 

               if (strcmp(str,"get_paramHeatHP")==0)           // Функция get_paramHeatHP - получить значение параметра отопления ТН
                  {
                    strcat(strReturn,HP.Prof.get_paramHeatHP((PARAM_HP)p,HP.dFC.get_present()));
                    strcat(strReturn,"&") ; continue;
                  } // strcmp(str,"get_paramHeatHP")==0)
                  
              if (strcmp(str,"set_paramHeatHP")==0)           // Функция set_paramHeatHP - установить значение паремтра отопления ТН
                  {
                    // Получить устанавливаемое значение
                   if (HP.Prof.set_paramHeatHP((PARAM_HP)p,pm))     // преобразование удачно
                      strcat(strReturn,HP.Prof.get_paramHeatHP((PARAM_HP)p,HP.dFC.get_present()));
                   else
                      strcat(strReturn,"E16") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"paramHeatHP")==0) 

               if (strcmp(str,"get_optionHP")==0)           // Функция get_optionHP - получить значение параметра отопления ТН
                  {
                    strcat(strReturn,HP.get_optionHP(OPTION_HP(p-20)));  // 20 смещение для параметров опций
                    strcat(strReturn,"&") ; continue;
                  } // strcmp(str,"get_optionHP")==0)
                  
              if (strcmp(str,"set_optionHP")==0)           // Функция set_optionHP - установить значение паремтра  опций
                  {
                    // Получить устанавливаемое значение
                   if (HP.set_optionHP(OPTION_HP(p-20),pm))     // преобразование удачно, 20 смещение для опций
                      strcat(strReturn,HP.get_optionHP(OPTION_HP(p-20)));  // 20 смещение для параметров  опций
                   else
                      strcat(strReturn,"E17") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_optionHP")==0) 

               }  // else end -----------------------
            } //if ((strstr(str,"HP")>0)  

          //9.  Графики смещение param 100 занимает 40 позиций  используется в одной функции get_Chart
          if (strstr(str,"Chart"))          // Проверка для запросов содержащих Chart
             {
             if ((param>=140)||(param<100))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-100; 
               if (strcmp(str,"get_Chart")==0)           // Функция get_Chart - получить график
                  {
                  HP.get_Chart((TYPE_CHART) p,strReturn, true);
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_Chart")==0) 
               }  // else end 
            } //if ((strstr(str,"Chart")>0)  
            
         //10.  Статистика смещение param 100 занимает 40 позиций  используется в одной функции get_Stat
          if (strstr(str,"Stat"))          // Проверка для запросов содержащих Stat
             {
             if ((param>=155)||(param<140))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-140; 
               if (strcmp(str,"get_Stat")==0)           
                  {
                  #ifdef I2C_EEPROM_64KB    
                  HP.Stat.get_Stat((TYPE_STAT) p,strReturn, true);
                  #else
                  strcat(strReturn,"");
                  #endif
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_Stat")==0) 
               }  // else end 
            } //if ((strstr(str,"Stat")>0)  
            // --------------------------------------------------------------------------------------------------------------------------
           // Запросы где второй параметр строка смещение 170 занимает 20 позиций
           //10.  Настройки сети смещение 170
          if (strstr(str,"Network"))          // Проверка для запросов содержащих Network
             {
             if ((param>=190)||(param<170))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-170; 
               if (strcmp(str,"get_Network")==0)           // Функция get_Network - получить значение параметра Network
                  {
                    strcat(strReturn,HP.get_network((PARAM_NETWORK)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_Network")==0) 
                  
              if (strcmp(str,"set_Network")==0)           // Функция set_Network - установить значение паремтра Network
                  {
                   if (HP.set_network((PARAM_NETWORK)p,strbuf))     // преобразование удачно
                      strcat(strReturn,HP.get_network((PARAM_NETWORK)p));
                   else
                      strcat(strReturn,"E15") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_Network")==0) 
              }  // else end 
            } //if ((strstr(str,"Network")>0)       

           //11.  Настройки дата время смещение 190
          if (strstr(str,"datetime"))          // Проверка для запросов содержащих datetime
             {
             if ((param>=200)||(param<190))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-190; 
               if (strcmp(str,"get_datetime")==0)           // Функция get_datetim - получить значение даты времени
                  {
                    strcat(strReturn,HP.get_datetime((DATE_TIME)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_datetime")==0) 
                  
              if (strcmp(str,"set_datetime")==0)           // Функция set_datetime - установить значение даты и времени
                  {
                   if (HP.set_datetime((DATE_TIME)p,strbuf))     // преобразование удачно
                      strcat(strReturn,HP.get_datetime((DATE_TIME)p));
                   else
                      strcat(strReturn,"E18") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_datetime")==0) 
              }  // else end 
            } //if ((strstr(str,"datetime")>0)   

         //12.  Настройки бойлера смещение 200 занимает 20
          if (strstr(str,"Boiler"))          // Проверка для запросов содержащих Boiler
             {
             if ((param>=220)||(param<200))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-200; 
               if (strcmp(str,"get_Boiler")==0)           // Функция get_Boiler - получить значение настройки бойлера
                  {
                    strcat(strReturn,HP.Prof.get_boiler((BOILER_HP)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_Boiler")==0) 
                  
              if (strcmp(str,"set_Boiler")==0)           // Функция set_Boiler - установить значениена стройки бойлера
                  {
                   if (HP.Prof.set_boiler((BOILER_HP)p,strbuf))     // преобразование удачно
                      strcat(strReturn,HP.Prof.get_boiler((BOILER_HP)p));
                   else
                      strcat(strReturn,"E19") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_Boiler")==0) 
              }  // else end 
            } //if ((strstr(str,"Boiler")>0)   

         //13.  Настройки Уведомлений смещение 220 занимает 30
          if (strstr(str,"Message"))          // Проверка для запросов содержащих messageSetting
             {
             if ((param>=250)||(param<220))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-220; 
               if (strcmp(str,"get_Message")==0)           // Функция get_Message - получить значение настройки уведомлений
                  {
                    strcat(strReturn,HP.message.get_messageSetting((MESSAGE_HP)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_messageSetting")==0) 
                  
              if (strcmp(str,"set_Message")==0)           // Функция set_Message - установить значениена стройки уведомлений
                  {
                   if (HP.message.set_messageSetting((MESSAGE_HP)p,strbuf))     // преобразование удачно
                      strcat(strReturn,HP.message.get_messageSetting((MESSAGE_HP)p));
                   else
                      strcat(strReturn,"E20") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_messageSetting")==0) 
              }  // else end 
            } //if ((strstr(str,"messageSetting")>0)   
            
         //14.  Настройки профилей смещение 250 занимает 10
          if (strstr(str,"Profile"))          // Проверка для запросов содержащих messageSetting
          {
             if ((param>=260)||(param<250))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
             else  // параметр верный
               {   p=param-250; 
               if (strcmp(str,"get_Profile")==0)           // Функция получить настройки профиля
                  {
                    strcat(strReturn,HP.Prof.get_paramProfile((TYPE_PARAM_PROFILE)p));
                    strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"get_Profile")==0) 
                  
              if (strcmp(str,"set_Profile")==0)           // Функция записать настройки профиля
                  {
                   if (HP.Prof.set_paramProfile((TYPE_PARAM_PROFILE)p,strbuf))     // преобразование удачно
                       strcat(strReturn,HP.Prof.get_paramProfile((TYPE_PARAM_PROFILE)p)); 
                   else
                      strcat(strReturn,"E28") ; // ошибка преобразования строки
                  strcat(strReturn,"&") ; continue;
                  } // (strcmp(str,"set_Profile")==0) 
              }  // else end 
           } //if ((strstr(str,"Profile")>0)
           #ifdef USE_ELECTROMETER_SDM  
              //15.  Настройки счетчика смещение 260 занимает 20
              if (strstr(str,"SDM"))          // Проверка для запросов содержащих SDM
                 {
                 if ((param>=280)||(param<260))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
                 else  // параметр верный
                   {   p=param-260; 
                   if (strcmp(str,"get_SDM")==0)           // Функция получить настройки счетчика
                      {
                        strcat(strReturn,HP.dSDM.get_paramSDM((TYPE_PARAM_SDM)p));
                        strcat(strReturn,"&") ; continue;
                      } // (strcmp(str,"get_SDM")==0) 
                      
                  if (strcmp(str,"set_SDM")==0)           // Функция записать настройки счетчика
                      {
                       if (HP.dSDM.set_paramSDM((TYPE_PARAM_SDM)p,strbuf))     // преобразование удачно
                           strcat(strReturn,HP.dSDM.get_paramSDM((TYPE_PARAM_SDM)p)); 
                       else
                          strcat(strReturn,"E31") ; // ошибка преобразования строки
                      strcat(strReturn,"&") ; continue;
                      } // (strcmp(str,"set_SDM")==0) 
                  }  // else end 
                } //if ((strstr(str,"SDM")>0)      
            #endif

              //16.  Настройки клиента смещение 290 занимает 30
              if (strstr(str,"MQTT"))          // Проверка для запросов содержащих MQTT
			   #ifdef MQTT
                 {
                 if ((param>=320)||(param<290))  {strcat(strReturn,"E03");strcat(strReturn,"&");  continue; }  // Не соответсвие имени функции и параметра
                 else  // параметр верный
                   {   p=param-290; 
                   if (strcmp(str,"get_MQTT")==0)           // Функция получить настройки MQTT
                      {
                        strcat(strReturn,HP.clMQTT.get_paramMQTT((TYPE_PARAM_MQTT)p));
                        strcat(strReturn,"&") ; continue;
                      } // (strcmp(str,"get_MQTT")==0) 
                      
                  if (strcmp(str,"set_MQTT")==0)           // Функция записать настройки MQTT
                      {
                       if (HP.clMQTT.set_paramMQTT((TYPE_PARAM_MQTT)p,strbuf))     // преобразование удачно
                           strcat(strReturn,HP.clMQTT.get_paramMQTT((TYPE_PARAM_MQTT)p)); 
                       else
                          strcat(strReturn,"E32") ; // ошибка преобразования строки
                      strcat(strReturn,"&") ; continue;
                      } // (strcmp(str,"set_MQTT")==0) 
                  }  // else end 
				#else
					 strcat(strReturn,"none") ; // не поддерживается
					 strcat(strReturn,"&") ; continue;
				#endif
               } //if ((strstr(str,"MQTT")>0)

            
        // НОВОЕ вставлять сюда!
        // ------------------------ конец разбора -------------------------------------------------
x_FunctionNotFound:
       strcat(strReturn,"E01");                             // функция не найдена ошибка
       strcat(strReturn,"&") ;
       continue;
       } // 2. Функции с параметром
       
  if (str[0]=='&') {break; } // второй символ & подряд признак конца запроса и мы выходим
  strcat(strReturn,"E01");   // Ошибка нет такой команды
  strcat(strReturn,"&") ;
  }
strcat(strReturn,"&") ; // двойной знак закрытие посылки
return count;           // сколько найдено запросов
}

// ===============================================================================================================
const char *header_Authorization_="Authorization: Basic ";
const char *header_POST_="Access-Control-Request-Method: POST";
// Выделение имени файла (или содержания запроса) и типа файла и типа запроса клиента
// thread - номер потока, возсращает тип запроса
uint16_t GetRequestedHttpResource(uint8_t thread)
{
  char *str_token, *pass;
  boolean user, admin;
  uint8_t i;
  uint16_t len;
  
 // journal.jprintf(">%s\n",Socket[thread].inBuf);
  
  if((HP.get_fPass())&&(!HP.safeNetwork))  // идентификация если установлен флаг и перемычка не в нуле
  {
          if (!(pass=strstr((char*)Socket[thread].inBuf,header_Authorization_)))    return UNAUTHORIZED;          // строка авторизации не найдена
          else  // Строка авторизации найдена смотрим логин пароль
          {
             pass=pass+strlen(header_Authorization_);
             user=true; 
             for(i=0;i<HP.Security.hashUserLen;i++) if (pass[i]!=HP.Security.hashUser[i]) {user=false; break;}
             if (user!=true) // это не пользователь
               {
                admin=true; 
                 for(i=0;i<HP.Security.hashAdminLen;i++) if (pass[i]!=HP.Security.hashAdmin[i]) {admin=false; break;}
                 if (admin!=true)  return BAD_LOGIN_PASS; // Не верный логин или пароль
               } //  if (user!=true)
               else  SETBIT1(Socket[thread].flags,fUser);// зашел простой пользователь
           } // else
  } 
 
  // Идентификация пройдена
//if(strstr((char*)Socket[thread].inBuf,"Access-Control-Request-Method: POST")) {request_type = HTTP_POST_; return request_type; }  //обработка предваритаельного запроса перед получением файла
  str_token =  strtok((char*)Socket[thread].inBuf, " ");    // Обрезаем по пробелам
  if (strcmp(str_token, "GET") == 0)   // Ищем GET
       {      
        str_token=strtok(NULL, " ");                       // get the file name
        if (strcmp(str_token, "/") == 0)                   // Имени файла нет, берем файл по умолчанию
              {      
              Socket[thread].inPtr=(char*)INDEX_FILE;      // Указатель на имя файла по умолчанию
              return HTTP_GET;                          
              }
         else if ((len=strlen(str_token)) <= W5200_MAX_LEN-100)   // Проверка на длину запроса или имени файла
               { 
                 Socket[thread].inPtr=(char*)(str_token+1);       // Указатель на имя файла
          //        Serial.println(Socket[thread].inPtr=(char*)(str_token+1));
                 if (Socket[thread].inPtr[0]=='&')     return HTTP_REQEST;       // Проверка на аякс запрос
                 return HTTP_GET; 
                } // if ((len=strlen(str_token)) <= W5200_MAX_LEN-100) 
         else return HTTP_invalid;  // слишком длинная строка HTTP_invalid
         }   //if (strcmp(str_token, "GET") == 0)
   else  if (strcmp(str_token,"POST") == 0)  return HTTP_POST;    // Запрос POST
   else  if (strcmp(str_token,"OPTIONS")==0) return HTTP_POST_;
   return HTTP_invalid ;
}

// ========================== P A R S E R  P O S T =================================
// Разбор и обработка POST запроса buf входная строка strReturn выходная
// Сейчас реализована загрузка настроек
// Возврат - true ok  false - error
boolean parserPOST(uint8_t thread)
{
byte *ptr; 
  // Определение начала данных
  if ((ptr=(byte*)strstr((char*)Socket[thread].inPtr,HEADER_BIN))==NULL) {journal.jprintf("Not found header in file.\n");return false;} // Заголовок не найден
  ptr=ptr+strlen(HEADER_BIN);
   // Проверка загловка
  if ((ptr[0]!=0xaa)||(ptr[1]!=0x00)) {journal.jprintf("Invalid header file setting.\n");return false;}                 // не верный заголовок
  if ((ptr[2]+256*ptr[3])!=VER_SAVE)  {journal.jprintf("Invalid version file setting.\n");return false;}                // не совпадение версий
//  len=ptr[4]+256*ptr[5];  // длина данных  по заголовку
   // Чтение настроек
  if (OK!=HP.loadFromBuf(0,ptr)) return false;    
  // Чтение профиля
  if (OK!=HP.Prof.loadFromBuf(HP.headerEEPROM.len,ptr)) return false;
#ifdef USE_SCHEDULER
  if(HP.Schdlr.loadFromBuf(HP.headerEEPROM.len + HP.Prof.get_lenProfile(), ptr)) return false;
#endif
  HP.Prof.update_list(HP.Prof.get_idProfile());                                                                        // обновить список
  return true;
}

