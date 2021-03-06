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
// --------------------------------------------------------------------------------
// Описание базового класса для работы Теплового Насоса  ==false
// --------------------------------------------------------------------------------
#include "HeatPump.h"

#define PUMPS_ON          Pumps(true,2000)                                                   // Включить насосы
#define PUMPS_OFF         Pumps(false,2000)                                                  // Выключить насосы
// Макросы по работе с компрессором в зависимости от наличия инвертора
#define COMPRESSOR_ON     if(dFC.get_present()) dFC.start_FC();else dRelay[RCOMP].set_ON();   // Включить компрессор в зависимости от наличия инвертора
#define COMPRESSOR_OFF    if(dFC.get_present()) dFC.stop_FC(); else dRelay[RCOMP].set_OFF();  // Выключить компрессор в зависимости от наличия инвертора
#define COMPRESSOR_IS_ON  (dRelay[RCOMP].get_Relay()||dFC.isfOnOff()?true:false)             // Проверка работает ли компрессор

void HeatPump::initHeatPump()
{
  uint8_t i;
  eraseError();
  for(i=0;i<TNUMBER;i++) sTemp[i].initTemp(i);            // Инициализация датчиков температуры

  #ifdef SENSOR_IP
   for(i=0;i<IPNUMBER;i++) sIP[i].initIP();               // Инициализация удаленных датчиков
  #endif
  
  sADC[PEVA].initSensorADC(PEVA,ADC_SENSOR_PEVA);          // Инициализация аналогово датчика PEVA
  sADC[PCON].initSensorADC(PCON,ADC_SENSOR_PCON);          // Инициализация аналогово датчика TCON
 
  for(i=0;i<INUMBER;i++) sInput[i].initInput(i);           // Инициализация контактных датчиков
  for(i=0;i<FNUMBER;i++)  sFrequency[i].initFrequency(i);  // Инициализация частотных датчиков
  for(i=0;i<RNUMBER;i++) dRelay[i].initRelay(i);           // Инициализация реле

  #ifdef I2C_EEPROM_64KB  
     Stat.Init();                                           // Инициализовать статистику
  #endif
  #ifdef EEV_DEF
  dEEV.initEEV();                                           // Инициализация ЭРВ
  #endif

  // Инициалаизация модбаса  перед частотником и счетчиком
  journal.jprintf("Init Modbus RTU via RS485:");  
  if (Modbus.initModbus()==OK) journal.jprintf(" OK\r\n");//  выводим сообщение об установлении связи
  else {journal.jprintf(" not present config\r\n");}         //  нет в конфигурации

  dFC.initFC();                                              // Инициализация FC
  #ifdef USE_ELECTROMETER_SDM
   dSDM.initSDM();                                           // инициалаизация счетчика
  #endif
     message.initMessage();                                  // Инициализация Уведомлений
  #ifdef MQTT
     clMQTT.initMQTT();                                      // Инициализация MQTT
  #endif
  resetSettingHP();                                          // все переменные

}
// Стереть последнюю ошибку
void HeatPump::eraseError()
{
 strcpy(note_error,"OK");          // Строка c описанием ошибки
 strcpy(source_error,""); 
 error=OK;                         // Код ошибки
}

// Получить число ошибок чтения ВСЕХ датчиков темпеартуры
uint32_t HeatPump::get_errorReadDS18B20()
{
  uint8_t i;
  static uint32_t sum;
  sum=0;
  for(i=0;i<TNUMBER;i++) sum=sum+sTemp[i].get_sumErrorRead();     // Суммирование ошибок по всем датчикам
return sum;    
}


// Установить состояние ТН, при необходимости пишем состояние в ЕЕПРОМ
// true - есть изменения false - нет изменений
boolean HeatPump::setState(TYPE_STATE_HP st)
{
  if (st==Status.State) return false;     // Состояние не меняется
  switch (st)
  {
  case pOFF_HP:       Status.State=pOFF_HP; if(GETBIT(Prof.SaveON.flags,fHP_ON)) {SETBIT0(Prof.SaveON.flags,fHP_ON);Prof.save(Prof.get_idProfile());}  break;// 0 ТН выключен, при необходимости записываем в ЕЕПРОМ
  case pSTARTING_HP:  Status.State=pSTARTING_HP; break;                                                                                    // 1 Стартует
  case pSTOPING_HP:   Status.State=pSTOPING_HP;  break;                                                                                    // 2 Останавливается
  case pWORK_HP:      Status.State=pWORK_HP;if(!(GETBIT(Prof.SaveON.flags,fHP_ON))) {SETBIT1(Prof.SaveON.flags,fHP_ON);Prof.save(Prof.get_idProfile());}  break;// 3 Работает, при необходимости записываем в ЕЕПРОМ
  case pERROR_HP:     Status.State=pERROR_HP;    break;                                                                                    // 5 Ошибка ТН
  case pERROR_CODE:                                                                                                                        // 6 - Эта ошибка возникать не должна!
  default:            Status.State=pERROR_HP;    break;                                                                                    // Обязательно должен быть последним, добавляем ПЕРЕД!!!
  }   
 return true; 
}

// Получить код последней ошибки, которая вызвала останов ТН, при удачном запуске обнуляется
char *HeatPump::get_lastErr()   
{
   return note_error;                  
}

// возвращает строку с найденными датчиками
void HeatPump::scan_OneWire(char *result_str)
{
	char *_result_str = result_str + strlen(result_str);

	if(get_State() == pWORK_HP)  // ТН работает
	{
		Stop();     // При сканировании останить ТН
		_delay(20); // задержка после останова ТН
	}
	if(!OW_prepare_buffers()) {
		OneWireBus.Scan(result_str);
#ifdef ONEWIRE_DS2482_SECOND
		OneWireBus2.Scan(result_str);
#endif
		journal.jprintf("OneWire found(%d): %s\n", OW_scanTableIdx, _result_str);
	}
}

// Получить время с последенй перезагрузки в секундах
__attribute__((always_inline)) inline uint32_t HeatPump::get_uptime()
{
  return   rtcSAM3X8.unixtime()-timeON;            
}

// Получить время  последенй перезагрузки
uint32_t HeatPump::get_startDT()
{
  return timeON;            
}


// Установить текущее время как начало старта контроллера, нужно для вычисления UPTIME
void HeatPump::set_uptime(unsigned long ttime)
{
   timeON=ttime;     
}

// Установить синхронизацию по NTP
void HeatPump::set_updateNTP(boolean b)   
{
if (b) SETBIT1(DateTime.flags,fUpdateNTP); else SETBIT0(DateTime.flags,fUpdateNTP);    
}

// Получить флаг возможности синхронизации по NTP
boolean HeatPump::get_updateNTP()
{
  return GETBIT(DateTime.flags,fUpdateNTP);
}

// Установить значение текущий режим работы
void HeatPump::set_testMode(TEST_MODE b)
{
  int i=0; 
  for(i=0;i<TNUMBER;i++) sTemp[i].set_testMode(b);         // датчики температуры
  for(i=0;i<ANUMBER;i++) sADC[i].set_testMode(b);          // Датчик давления
  for(i=0;i<INUMBER;i++) sInput[i].set_testMode(b);        // Датчики сухой контакт
  for(i=0;i<RNUMBER;i++) dRelay[i].set_testMode(b);        // Реле
  #ifdef EEV_DEF
  dEEV.set_testMode(b);                                    // ЭРВ
  #endif
  dFC.set_testMode(b);                                     // Частотник
  testMode=b; 
  // новый режим начинаем без ошибок - не надо при старте ошибки трутся
//  eraseError();
}
// Получить текущий режим работы
TEST_MODE HeatPump::get_testMode()
{
  return testMode;
}
// -------------------------------------------------------------------------
// СОХРАНЕНИЕ ВОССТАНОВЛЕНИЕ  ----------------------------------------------
// -------------------------------------------------------------------------
// Обновить ВСЕ привязки удаленных датчиков
// Вызывается  при восстановлении настроек и изменении привязки, для упрощения сделана перепривязка Всех датчиков
#ifdef SENSOR_IP
   boolean HeatPump::updateLinkIP()     
    {
    uint8_t i;
    for(i=0;i<TNUMBER;i++) sTemp[i].devIP=NULL;                 // Ссылка на привязаный датчик (класс) если NULL привявязки нет
    for(i=0;i<IPNUMBER;i++)
         {
          if ((sIP[i].get_fUse())&&(sIP[i].get_link()!=-1))              // включено использование
            if ((sIP[i].get_link()>=0)&&(sIP[i].get_link()<TNUMBER))     // находится в диапазоне можно привязывать
                   sTemp[sIP[i].get_link()].devIP= &sIP[i];    
         }
    return true;     
    }
#endif

// РАБОТА с НАСТРОЙКАМИ ТН -----------------------------------------------------
// Записать настройки в eeprom i2c на входе адрес с какого, на выходе код ошибки (меньше нуля) или количество записанных  байт
// старотвый адрес I2C_SETTING_EEPROM
// Возвращает ошибку или число записанных байт
int32_t HeatPump::save()
{
	int16_t i;
	int32_t adr, adr_len;
	uint16_t crc_mem;
	adr=I2C_SETTING_EEPROM;
	Option.numProf=Prof.get_idProfile();      // Запомнить текущий профиль, его записываем ЭТО обязательно!!!! нужно для восстановления настроек

	uint8_t tasks_suspended = TaskSuspendAll(); // Запрет других задач

	DateTime.saveTime=rtcSAM3X8.unixtime();   // запомнить время сохранения настроек

	// Заголовок
	headerEEPROM.magic=0xaa;
	headerEEPROM.zero=0x00;
	headerEEPROM.ver=VER_SAVE;
	headerEEPROM.len=0;
	int8_t err = ERR_SAVE_EEPROM;

	while(1) {
		if(writeEEPROM_I2C(adr, (byte*)&headerEEPROM, sizeof(headerEEPROM))) {  break; }  adr=adr+sizeof(headerEEPROM);// записать заголовок

		// Сохранение переменных ТН
		if(writeEEPROM_I2C(adr, (byte*)&DateTime, sizeof(DateTime))) { break; } adr=adr+sizeof(DateTime);  // записать структуры  DateTime
		if(writeEEPROM_I2C(adr, (byte*)& Network, sizeof(Network))) {  break; } adr=adr+sizeof(Network);     // записать структуры Network

		// Сохранить параметры и опции отопления и бойлер, уведомления
		if(writeEEPROM_I2C(adr, (byte*)&Option, sizeof(Option))) { break; }  adr=adr+sizeof(Option);           // записать опции ТН

		if((adr=message.save(adr)) == ERR_SAVE_EEPROM) { break; }

		// Сохранение отдельных объектов ТН
		for(i=0;i<TNUMBER;i++) if((adr = sTemp[i].save(adr)) == ERR_SAVE_EEPROM) { break; }           // Сохранение датчиков температуры
		for(i=0;i<ANUMBER;i++) if((adr = sADC[i].save(adr)) == ERR_SAVE_EEPROM) { break; }            // Сохранение датчика давления
		for(i=0;i<INUMBER;i++) if((adr = sInput[i].save(adr)) == ERR_SAVE_EEPROM) { break; }        // Сохранение контактных датчиков
		for(i=0;i<FNUMBER;i++) if((adr = sFrequency[i].save(adr)) == ERR_SAVE_EEPROM) { break; }    // Сохранение частотных датчиков

		#ifdef SENSOR_IP
		for(i=0;i<IPNUMBER;i++) if((adr = sIP[i].save(adr)) == ERR_SAVE_EEPROM) { break; }         // Сохранение удаленных датчиков
		#endif
		#ifdef EEV_DEF
		if((adr = dEEV.save(adr)) == ERR_SAVE_EEPROM) { break; }                                      // Сохранение ЭВР
		#endif
		if((adr = dFC.save(adr)) == ERR_SAVE_EEPROM) { break; }                                      // Сохранение FC
		#ifdef USE_ELECTROMETER_SDM
		if((adr=dSDM.save(adr)) == ERR_SAVE_EEPROM) { break; }                                    // Сохранение SDM
		#endif
		#ifdef MQTT
		if((adr=clMQTT.save(adr)) == ERR_SAVE_EEPROM) { break; }                                   // Сохранение MQTT
		#endif
		// В конце процедуры записи пишем в структуру заголовка записанную длину в байтах
		adr_len=I2C_SETTING_EEPROM+sizeof(headerEEPROM)-sizeof(headerEEPROM.len);
		headerEEPROM.len=adr-I2C_SETTING_EEPROM+2;  // добавляем два байта для контрольной суммы
		if(writeEEPROM_I2C(adr_len, (byte*)&headerEEPROM.len, sizeof(headerEEPROM.len))) { break; }  // записать длину, без изменения числа записанных байт

		// Расчет контрольной суммы и запись ее в конец
		crc_mem=get_crc16_mem();
		if(writeEEPROM_I2C(adr, (byte*)&crc_mem, sizeof(crc_mem))) { break; }                       // записать crc16, без изменения числа записанных байт

		if((err=check_crc16_eeprom(I2C_SETTING_EEPROM))!=OK) { journal.jprintf(" Verification error, setting not write eeprom/file\n"); break;} // ВЕРИФИКАЦИЯ Контрольные суммы не совпали
		journal.jprintf(" Save setting to eeprom OK, write: %d bytes crc16: 0x%x\n",headerEEPROM.len,crc_mem);                                                      // дошли до конца значит ошибок нет

		// Сохранение текущего профиля
		i=Prof.save(Prof.get_idProfile());
		err = OK;
		break;
	}

	if(tasks_suspended) xTaskResumeAll(); // Разрешение других задач

	if(err) {
		set_Error(err,(char*)nameHeatPump);
		return err;
	}

	// По результатам или ошибка или суммарное число байт
	if(i<OK) return i; else return (int32_t)(headerEEPROM.len+Prof.get_lenProfile());
}

// Считать настройки из eeprom i2c на входе адрес с какого, на выходе код ошибки (меньше нуля)
int8_t HeatPump::load()  
{
	uint16_t i;
	int32_t adr=I2C_SETTING_EEPROM;
	#ifdef LOAD_VERIFICATION
	if((error=check_crc16_eeprom(I2C_SETTING_EEPROM))!=OK) { journal.jprintf(" Error load setting from eeprom, CRC16 is wrong!\n"); return error;} // проверка контрольной суммы
	#endif

	// Прочитать заголовок
	if(readEEPROM_I2C(adr, (byte*)&headerEEPROM, sizeof(headerEEPROM))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(headerEEPROM);     // заголовок
	if(headerEEPROM.magic!=0xaa)   { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Заголовок не верен, данных нет
	if(headerEEPROM.zero!=0x00)    { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Заголовок не верен, данных нет
	#ifdef LOAD_VERIFICATION
	if(headerEEPROM.ver!=VER_SAVE) { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Версии сохранения не совпали
	if(headerEEPROM.len==0)        { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Длина данных равна 0
	#endif

	// Прочитать переменные ТН
	if(readEEPROM_I2C(adr, (byte*)& DateTime, sizeof(DateTime))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(DateTime);  // прочитать структуры  DateTime
	if(readEEPROM_I2C(adr, (byte*)& Network, sizeof(Network))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(Network);     // прочитать структуры  Network
	// Прочитать параметры и опции отопления
	if(readEEPROM_I2C(adr, (byte*)&Option, sizeof(Option))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;} adr=adr+sizeof(Option);           // прочитать опции ТН

	if((adr=message.load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;                                                                                                                                          // Прочитать параметры уведомлений

	// Чтение отдельных объектов ТН
	for(i=0;i<TNUMBER;i++) if((adr=sTemp[i].load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;         // Чтение датчиков температуры
	for(i=0;i<ANUMBER;i++) if((adr=sADC[i].load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;          // Чтение датчика давления
	for(i=0;i<INUMBER;i++) if((adr= sInput[i].load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;       // Чтение контактных датчиков
	for(i=0;i<FNUMBER;i++) if((adr= sFrequency[i].load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;   // Сохранение частотных датчиков

	#ifdef SENSOR_IP
	for(i=0;i<IPNUMBER;i++) if((adr= sIP[i].load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;       // Чтение удаленных датчиков
	updateLinkIP();                                      // Обновить привязку
	#endif
	#ifdef EEV_DEF
	if((adr=dEEV.load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;                                    // Чтение ЭВР
	#endif
	if((adr=dFC.load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;                                     // Чтение FC
	#ifdef USE_ELECTROMETER_SDM
	if((adr=dSDM.load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;                                  // Чтение SDM
	#endif
	#ifdef MQTT
	if((adr=clMQTT.load(adr)) == ERR_LOAD_EEPROM) return ERR_LOAD_EEPROM;                                  // чтение MQTT
	#endif
	// ВСЕ ОК
	#ifdef LOAD_VERIFICATION
	if (readEEPROM_I2C(adr, (byte*)&i, sizeof(i))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  adr=adr+sizeof(i);                    // прочитать crc16
	if (headerEEPROM.len!=adr-I2C_SETTING_EEPROM)  {error=ERR_BAD_LEN_EEPROM;set_Error(ERR_BAD_LEN_EEPROM,(char*)nameHeatPump); return error;}   // Проверка длины
	journal.jprintf(" Load setting from eeprom OK, read: %d bytes crc16: 0x%x\n",adr-I2C_SETTING_EEPROM,i);
	#else
	journal.jprintf(" Load setting from eeprom OK, read: %d bytes VERIFICATION OFF!\n",adr-I2C_SETTING_EEPROM+2);
	#endif

	// Загрузка текущего профиля
	Prof.load(Option.numProf);   // Считали настройки и знаем какой профиль загружать
	return OK;
}
// Считать настройки из буфера на входе адрес с какого, на выходе код ошибки (меньше нуля)
int8_t HeatPump::loadFromBuf(int32_t adr,byte *buf)  
{
  uint16_t i;
  uint32_t aStart=adr;

   // проверка контрольной суммы
   #ifdef LOAD_VERIFICATION 
  if ((error=check_crc16_buf(adr,buf)!=OK)) {journal.jprintf(" Error load setting from file, crc16 is wrong!\n"); return error;}
  #endif
  // Прочитать заголовок
  memcpy((byte*)&headerEEPROM,buf+adr,sizeof(headerEEPROM)); adr=adr+sizeof(headerEEPROM);                                                                // заголовок
  if (headerEEPROM.magic!=0xaa)   { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Заголовок не верен, данных нет
  if (headerEEPROM.zero!=0x00)    { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Заголовок не верен, данных нет
  #ifdef LOAD_VERIFICATION
  if (headerEEPROM.ver!=VER_SAVE) { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Версии сохранения не совпали
  if (headerEEPROM.len==0)        { set_Error(ERR_HEADER_EEPROM,(char*)nameHeatPump); return ERR_HEADER_EEPROM;}                                          // Длина данных равна 0
  #endif
  
  // Прочитать переменные ТН
   memcpy((byte*)&DateTime,buf+adr,sizeof(DateTime)); adr=adr+sizeof(DateTime);                                                                           // прочитать структуры  DateTime
   memcpy((byte*)&Network,buf+adr,sizeof(Network)); adr=adr+sizeof(Network);                                                                              // прочитать структуры  Network
   memcpy((byte*)&Option,buf+adr,sizeof(Option)); adr=adr+sizeof(Option);                                                                                 // прочитать опции ТН
 
   adr=message.loadFromBuf(adr,buf);                                                                                                                      // Прочитать параметры уведомлений
  
 // Чтение отдельных объектов ТН
  for(i=0;i<TNUMBER;i++) adr=sTemp[i].loadFromBuf(adr,buf);         // Чтение датчиков температуры
  for(i=0;i<ANUMBER;i++) adr=sADC[i].loadFromBuf(adr,buf);          // Чтение датчика давления
  for(i=0;i<INUMBER;i++) adr= sInput[i].loadFromBuf(adr,buf);       // Чтение контактных датчиков
  for(i=0;i<FNUMBER;i++) adr= sFrequency[i].loadFromBuf(adr,buf);    // Сохранение частотных датчиков
 
  #ifdef SENSOR_IP
    for(i=0;i<IPNUMBER;i++) adr= sIP[i].loadFromBuf(adr,buf);      // Чтение удаленных датчиков
    updateLinkIP();                                                // Обновить привязку
  #endif
  #ifdef EEV_DEF
  adr=dEEV.loadFromBuf(adr,buf);                                   // Чтение ЭВР
  #endif
  adr=dFC.loadFromBuf(adr,buf);                                    // Чтение FC
  #ifdef USE_ELECTROMETER_SDM  
  adr=dSDM.loadFromBuf(adr,buf);                                    // Чтение SDM
  #endif
  #ifdef MQTT
  adr=clMQTT.loadFromBuf(adr,buf);                                    // Чтение MQTT
  #endif  
 #ifdef LOAD_VERIFICATION
    memcpy((byte*)&i,buf+adr,sizeof(i)); adr=adr+sizeof(i);                                                                                                     // прочитать crc16
    if (headerEEPROM.len!=adr-aStart)  {error=ERR_BAD_LEN_EEPROM; set_Error(ERR_BAD_LEN_EEPROM,(char*)nameHeatPump); return error;}    // Проверка длины
    journal.jprintf(" Load setting from file OK, read: %d bytes crc16: 0x%x\n",adr-aStart,i);                                                                    // ВСЕ ОК
  #else
    journal.jprintf(" Load setting from file OK, read: %d bytes VERIFICATION OFF!\n",adr-aStart+2);
  #endif
  // 
  return OK;       
}
// Функции расчета контрольных сумм ----------------------------------------------
static uint16_t crc= 0xFFFF;  // рабочее значение
// Рассчитать контрольную сумму в ПАМЯТИ по структурам дынных (по структуре!) на выходе crc16
uint16_t HeatPump::get_crc16_mem()
{
  uint16_t i;
  crc= 0xFFFF;
  for(i=0;i<sizeof(headerEEPROM);i++) crc=_crc16(crc,*((byte *)&headerEEPROM+i));  // CRC16 заголовок должен быть заполнен
  for(i=0;i<sizeof(DateTime);i++) crc=_crc16(crc,*((byte*)&DateTime+i));           // CRC16 структуры  DateTime
  for(i=0;i<sizeof(Network);i++) crc=_crc16(crc,*((byte*)&Network+i));             // CRC16 структуры  Network
  for(i=0;i<sizeof(Option);i++) crc=_crc16(crc,*((byte*)&Option+i));               // CRC16 структуры  Option
  crc=message.get_crc16(crc);   
 // Чтение отдельных объектов ТН
  for(i=0;i<TNUMBER;i++) crc=sTemp[i].get_crc16(crc);         // CRC16 датчиков температуры
  for(i=0;i<ANUMBER;i++) crc=sADC[i].get_crc16(crc);          // CRC16 датчика давления
  for(i=0;i<INUMBER;i++) crc=sInput[i].get_crc16(crc);        // CRC16 контактных датчиков
  for(i=0;i<FNUMBER;i++) crc=sFrequency[i].get_crc16(crc);    // Сохранение частотных датчиков
   
  
  #ifdef SENSOR_IP
    for(i=0;i<IPNUMBER;i++) crc= sIP[i].get_crc16(crc);      // CRC16 удаленных датчиков
  #endif
  #ifdef EEV_DEF
  crc=dEEV.get_crc16(crc);                                    // CRC16 ЭВР
  #endif
  crc=dFC.get_crc16(crc);                                     // CRC16 FC 
  
  #ifdef USE_ELECTROMETER_SDM   
    crc=dSDM.get_crc16(crc);                                 // CRC16 SDM  
  #endif
  #ifdef MQTT
    crc=clMQTT.get_crc16(crc);                               // CRC16 MQTT 
  #endif
    
  return crc;          
}

// Проверить контрольную сумму в EEPROM для данных на выходе ошибка, длина определяется из заголовка
int8_t HeatPump::check_crc16_eeprom(int32_t adr)
{
  type_headerEEPROM hEEPROM;
  uint16_t i;
  byte x;
  crc= 0xFFFF;
  if (readEEPROM_I2C(adr, (byte*)&hEEPROM, sizeof(type_headerEEPROM))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}                                 // Прочитать заголовок - длину данных
  for (i=0;i<hEEPROM.len-2;i++) {if (readEEPROM_I2C(adr+i, (byte*)&x, sizeof(x))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}  crc=_crc16(crc,x);} // расчет -2 за вычетом CRC16 из длины
  if (readEEPROM_I2C(adr+hEEPROM.len-2, (byte*)&i, sizeof(i))) { set_Error(ERR_LOAD_EEPROM,(char*)nameHeatPump); return ERR_LOAD_EEPROM;}                                         // чтение -2 за вычетом CRC16 из длины
  if (crc==i) return OK; 
  else        return ERR_CRC16_EEPROM;
}
// Проверить контрольную сумму в буфере для данных на выходе ошибка, длина определяется из заголовка
int8_t HeatPump::check_crc16_buf(int32_t adr, byte* buf)   
{
  type_headerEEPROM hEEPROM;
  uint16_t i;
  byte x;
  crc= 0xFFFF;
  memcpy((byte*)&hEEPROM,buf+adr,sizeof(type_headerEEPROM));                                         // Прочитать заголовок - длину данных
  for (i=0;i<hEEPROM.len-2;i++) {memcpy((byte*)&x,buf+adr+i,sizeof(x)); crc=_crc16(crc,x);}          // расчет -2 за вычетом CRC16 из длины
  memcpy((byte*)&i,buf+adr+hEEPROM.len-2,sizeof(i));                                                 // чтение -2 за вычетом CRC16 из длины
  if (crc==i) return OK; 
  else        return ERR_CRC16_EEPROM;
}

// СЧЕТЧИКИ -----------------------------------
 // запись счетчиков теплового насоса в ЕЕПРОМ
int8_t HeatPump::save_motoHour()
{
uint8_t i;
boolean flag;
motoHour.magic=0xaa;   // заголовок

for (i=0;i<5;i++)   // Делаем 5 попыток записи
 {
  if (!(flag=writeEEPROM_I2C(I2C_COUNT_EEPROM, (byte*)&motoHour, sizeof(motoHour)))) break;   // Запись прошла
  journal.jprintf(" ERROR save countes to eeprom #%d\n",i);    
  _delay(i*50);
 }
if (flag) {set_Error(ERR_SAVE2_EEPROM,(char*)nameHeatPump); return ERR_SAVE2_EEPROM;}  // записать счетчики
  journal.jprintf(" Save counters to eeprom, write: %d bytes\n",sizeof(motoHour)); 
return OK;        
}

// чтение счетчиков теплового насоса в ЕЕПРОМ
int8_t HeatPump::load_motoHour()          
{
 byte x=0xff;
 if (readEEPROM_I2C(I2C_COUNT_EEPROM,  (byte*)&x, sizeof(x)))  { set_Error(ERR_LOAD2_EEPROM,(char*)nameHeatPump); return ERR_LOAD2_EEPROM;}                // прочитать заголовок
 if (x!=0xaa)  {journal.jprintf("Bad header counters in eeprom, skip load\n"); return ERR_HEADER2_EEPROM;}                                                  // заголвок плохой выходим
 if (readEEPROM_I2C(I2C_COUNT_EEPROM,  (byte*)&motoHour, sizeof(motoHour)))  { set_Error(ERR_LOAD2_EEPROM,(char*)nameHeatPump); return ERR_LOAD2_EEPROM;}   // прочитать счетчики
 journal.jprintf(" Load counters from eeprom, read: %d bytes\n",sizeof(motoHour)); 
 return OK; 

}
// Сборос сезонного счетчика моточасов
// параметр true - сброс всех счетчиков
void HeatPump::resetCount(boolean full)
{ 
if (full) // Полный сброс счетчиков
  {  
    motoHour.H1=0;
    motoHour.C1=0;
    #ifdef USE_ELECTROMETER_SDM
    motoHour.E1=dSDM.get_Energy();
    #endif
    motoHour.P1=0;
    motoHour.Z1=0;
    motoHour.D1=rtcSAM3X8.unixtime();           // Дата сброса общих счетчиков
  } 
  // Сезон
  motoHour.H2=0;
  motoHour.C2=0;
  #ifdef USE_ELECTROMETER_SDM
  motoHour.E2=dSDM.get_Energy();
  #endif 
  motoHour.P2=0;
  motoHour.Z2=0;
  motoHour.D2=rtcSAM3X8.unixtime();             // дата сброса сезонных счетчиков
  save_motoHour();  // записать счетчики
}
// Обновление счетчиков моточасов
// Электрическая энергия не обновляется, Тепловая энергия обновляется
volatile uint32_t  t1=0,t2=0,t;
volatile uint8_t   countMin=0;  // счетчик минут
void HeatPump::updateCount()
{
float power;  
if (get_State()==pOFF_HP) {t1=0;t2=0; return;}         // ТН не работает, вообще этого не должно быть
 
t=rtcSAM3X8.unixtime(); 
if (t1==0) t1=t; 
if (t2==0) t2=t; // первоначальная инициализация

// Время работы компрессора и выработанная энергия
   if ((COMPRESSOR_IS_ON)&&((t-t2)>=60)) // прошла 1 минута  и компрессор работает
    {
      t2=t;
      motoHour.C1++;    // моточасы компрессора ВСЕГО
      motoHour.C2++;    // моточасы компрессора сбрасываемый счетчик (сезон)
      
     if(ChartPowerCO.get_present()) // Расчет выработанной энергии Если есть соответсвующее оборудование
     {
        power=(float)(FEED-RET)*(float)sFrequency[FLOWCON].get_Value()/sFrequency[FLOWCON].get_kfCapacity(); // Мгновенная мощность в ВАТТАХ
        motoHour.P1=motoHour.P1+(int)(power/60.0);   // потребленная энергия за минуту
        motoHour.P2=motoHour.P2+(int)(power/60.0);   // потребленная энергия за минуту
     }  
      
    }
   if (!(COMPRESSOR_IS_ON)) t2=t;  // Компрессор не работает то сбросить время

// Время работы ТН
    if ((get_State()==pWORK_HP)&&((t-t1)>=60)) // прошла 1 минута и ТН работает
    {
      t1=t;
      motoHour.H1++;          // моточасы ТН ВСЕГО
      motoHour.H2++;          // моточасы ТН сбрасываемый счетчик (сезон)
      countMin++;
      // Пишем именно здесь т.к. Время работы ТН всегда больше компрессора
      if (countMin>=60) {countMin=0; save_motoHour(); } // Записать  счетчики раз в час, экономим ресурс флехи
    }
}

// После любого изменения часов необходимо пересчитать все времна которые используются
// параметр изменение времени - корректировка
void HeatPump::updateDateTime(int32_t  dTime)
{
  if(dTime!=0)                                   // было изменено время, надо скорректировать переменные времени
    {
    Prof.SaveON.startTime=Prof.SaveON.startTime+dTime; // время пуска ТН (для организации задержки включения включение ЭРВ)
    timeON=timeON+dTime;                               // время включения контроллера для вычисления UPTIME
    startCompressor=startCompressor+dTime;             // время пуска компрессора
    stopCompressor=stopCompressor+dTime;               // время останова компрессора
    } 
}


// -------------------------------------------------------------------------
// НАСТРОЙКИ ТН ------------------------------------------------------------
// -------------------------------------------------------------------------
// Сброс настроек теплового насоса
void HeatPump::resetSettingHP()
{  
  uint8_t i;
  Prof.initProfile();                           // Инициализировать профиль по умолчанию
    
  Status.modWork=pOFF;;                         // Что сейчас делает ТН (7 стадий)
  Status.State=pOFF_HP;                         // Сотояние ТН - выключен
  Status.ret=pNone;                             // точка выхода алгоритма
  motoHour.magic=0xaa;                          // волшебное число
  motoHour.H1=0;                                // моточасы ТН ВСЕГО
  motoHour.H2=0;                                // моточасы ТН сбрасываемый счетчик (сезон)
  motoHour.C1=0;                                // моточасы компрессора ВСЕГО
  motoHour.C2=0;                                // моточасы компрессора сбрасываемый счетчик (сезон)
  motoHour.E1=0.0;                              // Значение потреленный энергии в момент пуска  актуально при использовании счетчика SDM120 (вычитаем текущее и получам итого)
  motoHour.E2=0.0;                              // Значение потреленный энергии в начале сезона актуально при использовании счетчика SDM120 (вычитаем текущее и получам итого)
  motoHour.D1=rtcSAM3X8.unixtime();             // Дата сброса общих счетчиков
  motoHour.D2=rtcSAM3X8.unixtime();             // дата сброса сезонных счетчиков
 
  startPump=false;                              // Признак работы задачи насос
  flagRBOILER=false;                            // не идет нагрев бойлера
  fSD=false;                                    // СД карта не рабоатет
  relay3Way=false;                              // ВЫКЛЮЧЕНО Cостояние трехходового точнее если true то идет нагрев бойлера
  command=pEMPTY;                               // Команд на выполнение нет
  PauseStart=true;                              // начать отсчет задержки пред стартом с начала
  startRAM=0;                                   // Свободная память при старте FREE Rtos - пытаемся определить свободную память при работе
  lastEEV=-1;                                   // значение шагов ЭРВ перед выключением  -1 - первое включение
  num_repeat=0;                                 // текушее число попыток 0 - т.е еще не было работы
  num_resW5200=0;                               // текущее число сбросов сетевого чипа
  num_resMutexSPI=0;                            // текущее число сброса митекса SPI
  num_resMutexI2C=0;                            // текущее число сброса митекса I2C
  num_resMQTT=0;                                // число повторных инициализация MQTT клиента
  num_resPing=0;                                // число не прошедших пингов
  
  
  // Структура для хранения заголовка при сохранении настроек EEPROM
  headerEEPROM.magic=0xaa;                      // признак данных, должно быть  0xaa
  headerEEPROM.ver=VER_SAVE;                    // номер версии для сохранения
  headerEEPROM.len=0;                           // длина данных, сколько записано байт в еепром
  
  // Инициализациия различных времен
  DateTime.saveTime=0;                          // дата и время сохранения настроек в eeprom
  timeON=0;                                     // время включения контроллера для вычисления UPTIME
  countNTP=0;                                   // число секунд с последнего обновления по NTP
  startCompressor=0;                            // время пуска компрессора
  stopCompressor=0;                             // время останова компрессора
  offBoiler=0;                                  // время выключения нагрева ГВС ТН (необходимо для переключения на другие режимы на ходу)
  startDefrost=0;                               // время срабатывания датчика разморозки
  timeNTP=0;                                    // Время обновления по NTP в тиках (0-сразу обновляемся)
  
  safeNetwork=false;                            // режим safeNetwork
 
  
  // Установка сетевых параметров по умолчанию
  SETBIT0(Network.flags,fDHCP);                 // использование DHCP НЕТ
  Network.ip=IPAddress(defaultIP);              // ip адрес
  Network.sdns=IPAddress(defaultSDNS);          // сервер dns
  Network.gateway=IPAddress(defaultGateway);    // шлюз
  Network.subnet=IPAddress(defaultSubnet);      // подсеть
  Network.port=defaultPort;                     // порт веб сервера по умолчанию
  memcpy(Network.mac,defaultMAC,6);             // mac адрес
  Network.resSocket=30;                         // Время очистки сокетов
  Network.resW5200=0;                           // Время сброса чипа
  countResSocket=0;                             // Число сбросов сокетов
  SETBIT1(Network.flags, fInitW5200);           // Ежеминутный контроль SPI для сетевого чипа
  SETBIT0(Network.flags,fPass);                 // !save! Использование паролей
  strcpy(Network.passUser,"user");              // !save! Пароль пользователя
  strcpy(Network.passAdmin,"admin");            // !save! Пароль администратора
  Network.sizePacket=1465;                      // !save! размер пакета для отправки
  SETBIT0(Network.flags,fNoAck);                // !save! флаг Не ожидать ответа ACK
  Network.delayAck=10;                          // !save! задержка мсек перед отправкой пакета
  strcpy(Network.pingAdr,PING_SERVER );         // !save! адрес для пинга
  Network.pingTime=60*60;                       // !save! время пинга в секундах
  SETBIT0(Network.flags,fNoPing);               // !save! Запрет пинга контроллера
  
// Время
  SETBIT1(DateTime.flags,fUpdateNTP);           // Обновление часов по NTP  запрещено
  SETBIT1(DateTime.flags,fUpdateI2C);           // Обновление часов I2C     запрещено
  
  strcpy(DateTime.serverNTP,(char*)NTP_SERVER);  // NTP сервер по умолчанию
  DateTime.timeZone=TIME_ZONE;                   // Часовой пояс
  countNTP=0;                                    // Время с последнего обновления
 

// Опции теплового насоса
  Option.numProf=Prof.get_idProfile(); //  Профиль не загружен по дефолту 0 профиль
  Option.nStart=3;                     //  Число попыток пуска компрессора
  Option.tempRHEAT=1000;               //  Значение температуры для управления RHEAT (по умолчанию режим резерв - 10 градусов в доме)
  Option.pausePump=10;                 //  Время паузы  насоса при выключенном компрессоре МИНУТЫ
  Option.workPump=1;                   //  Время работы  насоса при выключенном компрессоре  МИНУТЫ
  Option.tChart=60;                    //  период накопления статистики по умолчанию 60 секунд
  SETBIT0(Option.flags,fAddHeat);      //  Использование дополнительного тена при нагреве НЕ ИСПОЛЬЗОВАТЬ
  SETBIT0(Option.flags,fTypeRHEAT);    //  Использование дополнительного тена по умолчанию режим резерв
  SETBIT1(Option.flags,fBeep);         //  Звук
  SETBIT1(Option.flags,fNextion);      //  дисплей Nextion
  SETBIT0(Option.flags,fEEV_close);    //  Закрытие ЭРВ при выключении компрессора
  SETBIT0(Option.flags,fSD_card);      //  Сброс статистика на карту
  SETBIT0(Option.flags,fSaveON);       //  флаг записи в EEPROM включения ТН
  SETBIT0(Option.flags,fAddBoiler);    //  флаг флаг догрева ГВС ТЭНом
  Option.tempRHEAT=40*100;             //  Темпеартура ГВС при котором включается бойлер и отключатся ТН
  Option.sleep=5;                      //  Время засыпания минуты
  Option.dim=80;                       //  Якрость %

 // инициализация статистика дополнительно помимо датчиков
  ChartRCOMP.init(!dFC.get_present());                                         // Статистика по включению компрессора только если нет частотника
//  ChartRELAY.init(true);                                                     // Хоть одно реле будет всегда
  ChartdCO.init(sTemp[TCONING].get_present()&sTemp[TCONOUTG].get_present());   // дельта СО
  ChartdGEO.init(sTemp[TEVAOUTG].get_present()&sTemp[TEVAING].get_present());  // дельта геоконтура
  #ifdef EEV_DEF
  ChartOVERHEAT.init(true);                                                    // перегрев
  ChartTPEVA.init( sADC[PEVA].get_present());                                  // температура расчитанная из давления  испарения
  ChartTPCON.init( sADC[PCON].get_present());                                  // температура расчитанная из давления  конденсации
  #endif

  for(i=0;i<FNUMBER;i++)   // По всем частотным датчикам
    {
     if (strcmp(sFrequency[i].get_name(),"FLOWCON")==0)                          // если есть датчик потока по конденсатору
       {
       ChartPowerCO.init(sFrequency[i].get_present()&sTemp[TCONING].get_present()&sTemp[TCONOUTG].get_present());               // выходная мощность насоса
       ChartCOP.init(dFC.get_present()&sFrequency[i].get_present()&sTemp[TCONING].get_present()&sTemp[TCONOUTG].get_present()); // Коэффициент преобразования
    //   Serial.print("StatCOP="); Serial.println(dFC.get_present()&sFrequency[i].get_present()&sTemp[TCONING].get_present()&sTemp[TCONOUTG].get_present()) ;
       }
    else  if (strcmp(sFrequency[i].get_name(),"FLOWEVA") ==0)                         // если есть датчик потока по испарителю
       {
       ChartPowerGEO.init(sFrequency[i].get_present()&sTemp[TEVAOUTG].get_present()&sTemp[TEVAING].get_present());     // выходная мощность насоса
       }     
       
    }
   #ifdef USE_ELECTROMETER_SDM  
   ChartFullCOP.init(ChartPowerCO.get_present());                              // ПОЛНЫЙ Коэффициент преобразования
   #endif
 };

// --------------------------------------------------------------------
// ФУНКЦИИ РАБОТЫ С НАСТРОЙКАМИ ТН ------------------------------------
// --------------------------------------------------------------------
// Сетевые настройки --------------------------------------------------
//Установить параметр из строки
boolean HeatPump::set_network(PARAM_NETWORK p, char *c)
{ 
 uint8_t x;
 float zp; 
 if (strcmp(c,cZero)==0) x=0;
 else if (strcmp(c,cOne)==0) x=1;
 else if (strcmp(c,"2")==0) x=2;
 else if (strcmp(c,"3")==0) x=3;
 else if (strcmp(c,"4")==0) x=4;
 else if (strcmp(c,"5")==0) x=5;
 else if (strcmp(c,"6")==0) x=6;
 else x=-1;
 switch (p)
   {
    case pIP:          return parseIPAddress(c, '.', Network.ip);                 break;  
    case pSDNS:        return parseIPAddress(c, '.', Network.sdns);               break;  
    case pGATEWAY:     return parseIPAddress(c, '.', Network.gateway);            break;                
    case pSUBNET:      return parseIPAddress(c, '.', Network.subnet);             break;  
    case pDHSP:        if (strcmp(c,cZero)==0) { SETBIT0(Network.flags,fDHCP); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Network.flags,fDHCP);  return true;}
                       else return false;  
                       break;  
    case pMAC:         return parseBytes(c, ':', Network.mac, 6, 16);             break;  
    case pRES_SOCKET: 
                       switch (x)
                       {
                        case 0: Network.resSocket=0;     return true;  break;
                        case 1: Network.resSocket=10;    return true;  break;
                        case 2: Network.resSocket=30;    return true;  break;
                        case 3: Network.resSocket=90;    return true;  break;
                        default:                    return false; break;   
                       }                                          break;   
    case pRES_W5200: 
                       switch (x)
                       {
                        case 0: Network.resW5200=0;        return true;  break;
                        case 1: Network.resW5200=60*60*6;  return true;  break;   // 6 часов хранение в секундах
                        case 2: Network.resW5200=60*60*24; return true;  break;   // 24 часа
                        default:                      return false; break;   
                       }                                          break;   
    case pPASS:        if (strcmp(c,cZero)==0) { SETBIT0(Network.flags,fPass); return true;}
                       else if (strcmp(c,cOne)==0) {SETBIT1(Network.flags,fPass);  return true;}
                       else return false;  
                       break;
    case pPASSUSER:    strcpy(Network.passUser,c);set_hashUser(); return true;   break;                 
    case pPASSADMIN:   strcpy(Network.passAdmin,c);set_hashAdmin(); return true; break;   
    case pSIZE_PACKET: zp=my_atof(c);  
                       if (zp==-9876543.00) return   false;    
                       else if((zp<64)||(zp>2048)) return   false;   
                       else Network.sizePacket=(int)zp; return true;
                       break;  
    case pINIT_W5200:    // флаг Ежеминутный контроль SPI для сетевого чипа
                       if (strcmp(c,cZero)==0) { SETBIT0(Network.flags,fInitW5200); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Network.flags,fInitW5200);  return true;}
                       else return false;  
                       break; 
    case pPORT:        zp=my_atof(c);  
                       if (zp==-9876543.00) return        false;    
                       else if((zp<1)||(zp>65535)) return false;   
                       else Network.port=(int)zp; return  true;
                       break;     
    case pNO_ACK:      if (strcmp(c,cZero)==0) { SETBIT0(Network.flags,fNoAck); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Network.flags,fNoAck);  return true;}
                       else return false;  
                       break;  
    case pDELAY_ACK:   zp=my_atof(c);  
                       if (zp==-9876543.00) return            false;    
                       else if((zp<1)||(zp>50)) return        false;   
                       else Network.delayAck=(int)zp; return  true;
                       break;         
   case pPING_ADR:     if (strlen(c)<sizeof(Network.pingAdr)) { strcpy(Network.pingAdr,c); return true;} else return false; break;    
   case pPING_TIME:    switch (x)
                       {
                        case 0: Network.pingTime=0;        return true;  break;
                        case 1: Network.pingTime=1*60;     return true;  break;
                        case 2: Network.pingTime=5*60;     return true;  break;
                        case 3: Network.pingTime=20*60;    return true;  break;
                        case 4: Network.pingTime=60*60;    return true;  break;
                        default:                           return false; break;   
                       }                                          break;   
    case pNO_PING:     if (strcmp(c,cZero)==0) { SETBIT0(Network.flags,fNoPing);      pingW5200(HP.get_NoPing()); return true;}
                       else if (strcmp(c,cOne)==0) { SETBIT1(Network.flags,fNoPing); pingW5200(HP.get_NoPing()); return true;}
                       else return false;  
                       break;                                                                                                                                                                              
    default:           return false;                           break;   
   }
  return false;
}
// Сетевые настройки --------------------------------------------------
//Получить параметр из строки
char* HeatPump::get_network(PARAM_NETWORK p)
{
  
 switch (p)
   {
    case pIP:        return IPAddress2String(Network.ip);                 break;  
    case pSDNS:      return IPAddress2String(Network.sdns);               break;  
    case pGATEWAY:   return IPAddress2String(Network.gateway);            break;                
    case pSUBNET:    return IPAddress2String(Network.subnet);             break;  
    case pDHSP:      if (GETBIT(Network.flags,fDHCP)) return  (char*)cOne;
                     else      return  (char*)cZero;                        break;
    case pMAC:       return MAC2String(Network.mac);                      break;  
    case pRES_SOCKET: 
                     switch (Network.resSocket)
                       {
                        case 0:   return (char*)"never:1;10 sec:0;30 sec:0;90 sec:0;";  break;
                        case 10:  return (char*)"never:0;10 sec:1;30 sec:0;90 sec:0;";  break;
                        case 30:  return (char*)"never:0;10 sec:0;30 sec:1;90 sec:0;";  break;  // 30 секунд
                        case 90:  return (char*)"never:0;10 sec:0;30 sec:0;90 sec:1;";  break;
                        default:  Network.resSocket=30; return (char*)"never:0;10 sec:0;30 sec:1;90 sec:0;"; break; // Этого не должно быть, но если будет то установить по умолчанию
                      }                                                      break;   
    case pRES_W5200: 
                    switch (Network.resW5200)
                       {
                        case 0:       return (char*)"never:1;6 hour:0;24 hour:0;";  break;
                        case 60*60*6: return (char*)"never:0;6 hour:1;24 hour:0;";  break;   // 6 часов
                        case 60*60*24:return (char*)"never:0;6 hour:0;24 hour:1;";  break;   // 24 часа
                        default:      Network.resW5200=0;return (char*)"never:1;6 hour:0;24 hour:0;"; break;   // Этого не должно быть, но если будет то установить по умолчанию
                       }                                     break;    
    case pPASS:      if (GETBIT(Network.flags,fPass)) return  (char*)cOne;
                     else      return  (char*)cZero;                        break;
    case pPASSUSER:  return Network.passUser;                             break;                 
    case pPASSADMIN: return Network.passAdmin;                            break;   
    case pSIZE_PACKET:return int2str(Network.sizePacket);                 break;   

    /*
                     switch (Network.sizePacket)
                       {
                        case 256:   return (char*)"256 byte:1;512 byte:0;1024 byte:0;2048 byte:0;";  break;
                        case 512:   return (char*)"256 byte:0;512 byte:1;1024 byte:0;2048 byte:0;";  break;
                        case 1024:  return (char*)"256 byte:0;512 byte:0;1024 byte:1;2048 byte:0;";  break;
                        case 2048:  return (char*)"256 byte:0;512 byte:0;1024 byte:0;2048 byte:1;";  break;
                        default:    Network.sizePacket=2048; return (char*)"256 byte:0;512 byte:0;1024 byte:0;2048 byte:1;";  break;     // Этого не должно быть, но если будет то установить по умолчанию
                      }  
     */                 
    case pINIT_W5200:if (GETBIT(Network.flags,fInitW5200)) return  (char*)cOne;       // флаг Ежеминутный контроль SPI для сетевого чипа
                     else      return  (char*)cZero;                        break;      
    case pPORT:return int2str(Network.port);                              break;    // Порт веб сервера
    case pNO_ACK:    if (GETBIT(Network.flags,fNoAck)) return  (char*)cOne;
                     else      return  (char*)cZero;                        break;     
    case pDELAY_ACK:return int2str(Network.delayAck);                     break;    
    case pPING_ADR:  return Network.pingAdr;                              break;
    case pPING_TIME: 
                     switch (Network.pingTime)
                       {
                        case 0:      return (char*)"never:1;1 min:0;5 min:0;20 min:0;60 min:0;";  break; // никогда
                        case 1*60:   return (char*)"never:0;1 min:1;5 min:0;20 min:0;60 min:0;";  break; // 1 минута
                        case 5*60:   return (char*)"never:0;1 min:0;5 min:1;20 min:0;60 min:0;";  break; // 5 минут
                        case 20*60:  return (char*)"never:0;1 min:0;5 min:0;20 min:1;60 min:0;";  break; // 20 миут
                        case 60*60:  return (char*)"never:0;1 min:0;5 min:0;20 min:0;60 min:1;";  break; // 60 минут
                        default:  Network.resSocket=0; return (char*)"never:1;1 min:0;5 min:0;20 min:0;60 min:0;"; break; // Этого не должно быть, но если будет то установить по умолчанию
                      }                                                      break;   
    case pNO_PING:   if (GETBIT(Network.flags,fNoPing)) return  (char*)cOne;
                     else      return  (char*)cZero;                        break;                                                                                          
    default:         return  (char*)cInvalid;                              break;   
   }
 return (char*)cInvalid;
}

// Установить параметр дата и время из строки
boolean  HeatPump::set_datetime(DATE_TIME p, char *c)
{
   float tz;
   int16_t m,h,d,mo,y;
   int16_t buf[4];
   uint32_t oldTime=rtcSAM3X8.unixtime(); // запомнить время
   int32_t  dTime=0;
   switch (p)
   {
    case pTIME:       if (!parseInt16_t(c, ':',buf,2,10)) return false; 
                      h=buf[0]; m=buf[1];
                      rtcSAM3X8.set_time (h,m,0);  // внутренние
                      setTime_RtcI2C(rtcSAM3X8.get_hours(), rtcSAM3X8.get_minutes(),rtcSAM3X8.get_seconds()); // внешние
                      dTime=rtcSAM3X8.unixtime()-oldTime;// получить изменение времени
                //      return true;                     
                      break;  
    case pDATE:       if (!parseInt16_t(c, '/',buf,3,10)) return false;
                      d=buf[0]; mo=buf[1]; y=buf[2];
                      rtcSAM3X8.set_date(d,mo,y); // внутренние
                      setDate_RtcI2C(rtcSAM3X8.get_days(), rtcSAM3X8.get_months(),rtcSAM3X8.get_years()); // внешние
                      dTime=rtcSAM3X8.unixtime()-oldTime;// получить изменение времени
              //        return true;                       
                      break;  
    case pNTP:        if(strlen(c)==0) return false;                                                 // пустая строка
                      if(strlen(c)>NTP_SERVER_LEN) return false;                                     // слишком длиная строка
                      else { strcpy(DateTime.serverNTP,c); return true;  }                           // ок сохраняем
                      break;                
    case pUPDATE:     if (strcmp(c,cZero)==0) { SETBIT0(DateTime.flags,fUpdateNTP); return true;}
                      else if (strcmp(c,cOne)==0) { SETBIT1( DateTime.flags,fUpdateNTP);countNTP=0; return true;}
                      else return false;  
                      break;   
    case pTIMEZONE:   tz=my_atof(c);  
                      if (tz==-9876543.00) return   false;
                      else if((tz<-12)||(tz>12)) return   false;   
                      else DateTime.timeZone=(int)tz; return true;
                      break;  
    case pUPDATE_I2C: if (strcmp(c,cZero)==0) { SETBIT0(DateTime.flags,fUpdateI2C); return true;}
                      else if (strcmp(c,cOne)==0) {SETBIT1( DateTime.flags,fUpdateI2C);countNTP=0; return true;}
                      else return false;  
                      break;                      
    default:          return   false; 
                      break;   
   }

  if(dTime!=0)  {updateDateTime(dTime); return true; }    // было изменено время, надо скорректировать переменные времени
  return  false;
}
// Получить параметр дата и время из строки
char*   HeatPump::get_datetime(DATE_TIME p)
{
   switch (p)
   {
    case pTIME:       return NowTimeToStr1();                      break;  
    case pDATE:       return NowDateToStr();                       break;  
    case pNTP:        return DateTime.serverNTP;                   break;                
    case pUPDATE:     if (GETBIT(DateTime.flags,fUpdateNTP)) return  (char*)cOne;
                      else      return  (char*)cZero;                break;  
    case pTIMEZONE:   return  int2str(DateTime.timeZone);          break;  
    case pUPDATE_I2C: if (GETBIT(DateTime.flags,fUpdateI2C)) return  (char*)cOne;
                      else      return  (char*)cZero;                break;      
    default:          return  (char*)cInvalid;                    break;   
   }
 return (char*)cInvalid;
}

// Установить опции ТН из числа (float)
boolean HeatPump::set_optionHP(OPTION_HP p, float x)   
{
 switch (p)
    {
    case pADD_HEAT:       switch ((int)x)  //использование дополнительного нагревателя (значения 1 и 0)
                           {
                            case 0:  SETBIT0(Option.flags,fAddHeat);                                    return true; break;  // использование запрещено
                            case 1:  SETBIT1(Option.flags,fAddHeat);SETBIT0(Option.flags,fTypeRHEAT);   return true; break;  // резерв
                            case 2:  SETBIT1(Option.flags,fAddHeat);SETBIT1(Option.flags,fTypeRHEAT);   return true; break;  // бивалент
                            default: SETBIT1(Option.flags,fAddHeat);SETBIT0(Option.flags,fTypeRHEAT);   return true; break;  // Исправить по умолчанию
                           }  
                           break;
   case pTEMP_RHEAT:      if ((x>=-30.0)&&(x<=30.0))  {Option.tempRHEAT=x*100.0; return true;} else return false;                                          break;                // температура управления RHEAT (градусы)
   case pPUMP_WORK:       if ((x>=0.0)&&(x<=60.0)) {Option.workPump=x; return true;} else return false;                                                  break;                // работа насоса конденсатора при выключенном компрессоре МИНУТЫ
   case pPUMP_PAUSE:      if ((x>=0.0)&&(x<=60.0)) {Option.pausePump=x; return true;} else return false;                                                 break;                // пауза между работой насоса конденсатора при выключенном компрессоре МИНУТЫ
   case pATTEMPT:         if ((x>=0.0)&&(x<10.0)) {Option.nStart=x; return true;} else return false;                                                     break;                // число попыток пуска
   case pTIME_CHART:      if (get_State()==pWORK_HP) startChart(); // Сбросить статистистику, начать отсчет заново
                           switch ((int)x)  // период обновления ститистики
                           {
                            case 0:  Option.tChart=10;    return true; break;
                            case 1:  Option.tChart=60;    return true; break;
                            case 2:  Option.tChart=3*60;  return true; break;
                            case 3:  Option.tChart=10*60; return true; break;
                            case 4:  Option.tChart=30*60; return true; break;
                            case 5:  Option.tChart=60*60; return true; break;       
                            default: Option.tChart=60;    return true; break;    // Исправить по умолчанию
                           }  
                           break;
   case pBEEP:             if (x==0) {SETBIT0(Option.flags,fBeep); return true;} else if (x==1) {SETBIT1(Option.flags,fBeep); return true;} else return false;              break;    // Подача звукового сигнала
   case pNEXTION:          if (x==0) {SETBIT0(Option.flags,fNextion); updateNextion(); return true;} 
                           else if (x==1) {SETBIT1(Option.flags,fNextion); updateNextion(); return true;} 
                           else return false;  break;          // использование дисплея nextion
   case pEEV_CLOSE:        if (x==0) {SETBIT0(Option.flags,fEEV_close); return true;} else if (x==1) {SETBIT1(Option.flags,fEEV_close); return true;} else return false;               break;    // Закрытие ЭРВ при выключении компрессора
   case pEEV_LIGHT_START:  if (x==0) {SETBIT0(Option.flags,fEEV_light_start); return true;} else if (x==1) {SETBIT1(Option.flags,fEEV_light_start); return true;} else return false;   break;    // Облегчение старта компрессора
   case pEEV_START:        if (x==0) {SETBIT0(Option.flags,fEEV_start); return true;} else if (x==1) {SETBIT1(Option.flags,fEEV_start); return true;} else return false;               break;    // Всегда начинать работу ЭРВ со стратовой позици
     
   case pSD_CARD:          if (x==0) {SETBIT0(Option.flags,fSD_card); return true;} else if (x==1) {SETBIT1(Option.flags,fSD_card); return true;} else return false;                   break;    // Сбрасывать статистику на карту
   case pSAVE_ON:          if (x==0) {SETBIT0(Option.flags,fSaveON); return true;} else if (x==1) {SETBIT1(Option.flags,fSaveON); return true;} else return false;                     break;    // флаг записи в EEPROM включения ТН (восстановление работы после перезагрузки)
 
   case pNEXT_SLEEP:       if ((x>=0.0)&&(x<=60.0)) {Option.sleep=x; updateNextion(); return true;} else return false;                                                                 break;    // Время засыпания секунды NEXTION минуты
   case pNEXT_DIM:         if ((x>=5.0)&&(x<=100.0)) {Option.dim=x; updateNextion(); return true;} else return false;                                                                  break;    // Якрость % NEXTION
   case pADD_BOILER:       if (x==0) {SETBIT0(Option.flags,fAddBoiler); return true;} else if (x==1) {SETBIT1(Option.flags,fAddBoiler); return true;} else return false;               break;    // флаг использования тена для догрева ГВС
   case pTEMP_RBOILER:     if ((x>=20.0)&&(x<=60.0))  {Option.tempRBOILER=x*100.0; return true;} else return false;                                                                    break;    // температура включчения догрева бойлера
 
   case pEND2:             return (char*)"end";                                                                                                                                        break;    // Обязательно должен быть последним, добавляем ПЕРЕД!!!
   default:        return  (char*)cInvalid;                                                                                                                                           break;   
   }
 return false; 
}

// Получить опции ТН
char*    HeatPump::get_optionHP(OPTION_HP p)
{
 char static temp[16];
 switch (p)
   {
   case pADD_HEAT:        if(!GETBIT(Option.flags,fAddHeat))     return (char*)"none:1;reserve:0;bivalent:0;";        // использование ТЭН запрещено
                     else if(!GETBIT(Option.flags,fTypeRHEAT))   return (char*)"none:0;reserve:1;bivalent:0;";        // резерв
                          else                                   return (char*)"none:0;reserve:0;bivalent:1;";break;  // бивалент
   case pTEMP_RHEAT:      return ftoa(temp,(float)Option.tempRHEAT/100.0,1);        break;                   // температура управления RHEAT (градусы)
   case pPUMP_WORK:       return  int2str(Option.workPump);                         break;                   // работа насоса конденсатора при выключенном компрессоре МИНУТЫ
   case pPUMP_PAUSE:      return  int2str(Option.pausePump);                        break;                   // пауза между работой насоса конденсатора при выключенном компрессоре МИНУТЫ
   case pATTEMPT:         return  int2str(Option.nStart);                           break;                   // число попыток пуска
   case pTIME_CHART:      switch (Option.tChart)  // период обновления ститистики
                           {
                            case 10:    return (char*)"10 sec:1;1 min:0;3 min:0;10 min:0;30 min:0;60 min:0;"; break;
                            case 60:    return (char*)"10 sec:0;1 min:1;3 min:0;10 min:0;30 min:0;60 min:0;"; break;
                            case 3*60:  return (char*)"10 sec:0;1 min:0;3 min:1;10 min:0;30 min:0;60 min:0;"; break;
                            case 10*60: return (char*)"10 sec:0;1 min:0;3 min:0;10 min:1;30 min:0;60 min:0;"; break;
                            case 30*60: return (char*)"10 sec:0;1 min:0;3 min:0;10 min:0;30 min:1;60 min:0;"; break;
                            case 60*60: return (char*)"10 sec:0;1 min:0;3 min:0;10 min:0;30 min:0;60 min:1;"; break;       
                            default:    Option.tChart=60; return (char*)"10 sec:0;1 min:1;3 min:0;10 min:0;30 min:0;60 min:0;"; break;  // Исправить по умолчанию
                           } 
   case pBEEP:             if(GETBIT(Option.flags,fBeep)) return (char*)cOne; else return (char*)cZero;      break;            // Подача звукового сигнала
   case pNEXTION:          if(GETBIT(Option.flags,fNextion)) return (char*)cOne; else return (char*)cZero;   break;            // использование дисплея nextion
   
   case pEEV_CLOSE:        if(GETBIT(Option.flags,fEEV_close)) return (char*)cOne; else return (char*)cZero; break;            // Закрытие ЭРВ при выключении компрессора
   case pEEV_LIGHT_START:  if(GETBIT(Option.flags,fEEV_light_start)) return (char*)cOne; else return (char*)cZero; break;      // Облегчение старта компрессора
   case pEEV_START:        if(GETBIT(Option.flags,fEEV_start)) return (char*)cOne; else return (char*)cZero; break;            // Всегда начинать работу ЭРВ со стратовой позици
     
   case pSD_CARD:          if(GETBIT(Option.flags,fSD_card)) return (char*)cOne; else return (char*)cZero;   break;            // Сбрасывать статистику на карту
   case pSAVE_ON:          if(GETBIT(Option.flags,fSaveON)) return (char*)cOne; else return (char*)cZero;    break;            // флаг записи в EEPROM включения ТН (восстановление работы после перезагрузки)
   case pNEXT_SLEEP:       return int2str(Option.sleep);                                                     break;            // Время засыпания секунды NEXTION минуты
   case pNEXT_DIM:         return  int2str(Option.dim);                                                      break;            // Якрость % NEXTION

   case pADD_BOILER:       if(GETBIT(Option.flags,fAddBoiler)) return (char*)cOne; else return (char*)cZero; break;            // флаг использования тена для догрева ГВС
   case pTEMP_RBOILER:     return ftoa(temp,(float)Option.tempRBOILER/100.0,1);                           break;            // температура включчения догрева бойлера
 
   case pEND2:             return (char*)"end";                                                           break;            // Обязательно должен быть последним, добавляем ПЕРЕД!!!
   default:                return  (char*)cInvalid;                                                      break;   
   }
 return  (char*)cInvalid;                
}

// Установить рабочий профиль по текущему Prof
void HeatPump::set_profile()
{
	Option.numProf = Prof.get_idProfile();
}

// --------------------------------------------------------------------
// ФУНКЦИИ РАБОТЫ СО СТАТИСТИКОЙ ТН -----------------------------------
// --------------------------------------------------------------------
// обновить статистику, добавить одну точку и если надо записать ее на карту
void  HeatPump::updateChart()
{
 uint8_t i; 

 for(i=0;i<TNUMBER;i++) if(sTemp[i].Chart.get_present())  sTemp[i].Chart.addPoint(sTemp[i].get_Temp());
 for(i=0;i<ANUMBER;i++) if(sADC[i].Chart.get_present()) sADC[i].Chart.addPoint(sADC[i].get_Press());
 for(i=0;i<FNUMBER;i++) if(sFrequency[i].Chart.get_present()) sFrequency[i].Chart.addPoint(sFrequency[i].get_Value()); // Частотные датчики
 #ifdef EEV_DEF
 if(dEEV.Chart.get_present())     dEEV.Chart.addPoint(dEEV.get_EEV());  
 if(ChartOVERHEAT.get_present())  ChartOVERHEAT.addPoint(dEEV.get_Overheat());
 if(ChartTPEVA.get_present())     ChartTPEVA.addPoint(PressToTemp(sADC[PEVA].get_Press(),dEEV.get_typeFreon()));
 if(ChartTPCON.get_present())     ChartTPCON.addPoint(PressToTemp(sADC[PCON].get_Press(),dEEV.get_typeFreon()));
 #endif
 
 if(dFC.ChartFC.get_present())       dFC.ChartFC.addPoint(dFC.get_freqFC());       // факт
 if(dFC.ChartPower.get_present())    dFC.ChartPower.addPoint(dFC.get_power());
 if(dFC.ChartCurrent.get_present())  dFC.ChartCurrent.addPoint(dFC.get_current());
  
 if(ChartRCOMP.get_present())     ChartRCOMP.addPoint((int16_t)dRelay[RCOMP].get_Relay());
 
 if(ChartdCO.get_present())       ChartdCO.addPoint(FEED-RET);
 if(ChartdGEO.get_present())      ChartdGEO.addPoint(sTemp[TEVAING].get_Temp()-sTemp[TEVAOUTG].get_Temp());
 
 
 if(ChartPowerCO.get_present())   // Мощность контура в вт!!!!!!!!!
 {
  powerCO=(float)(FEED-RET)*(float)sFrequency[FLOWCON].get_Value()/sFrequency[FLOWCON].get_kfCapacity();
  #ifdef RHEAT_POWER   // Для Дмитрия. его специфика Вычитаем из общей мощности системы отопления мощность электрокотла
    #ifdef RHEAT
      if (dRelay[RHEAT].get_Relay()]) powerCO=powerCO-RHEAT_POWER;  // если включен электрокотел
    #endif    
  #endif
  ChartPowerCO.addPoint((int16_t)powerCO);
  } 
 if(ChartPowerGEO.get_present())  {powerGEO=(float)(sTemp[TEVAING].get_Temp()-sTemp[TEVAOUTG].get_Temp())*(float)sFrequency[FLOWEVA].get_Value()/sFrequency[FLOWEVA].get_kfCapacity(); ChartPowerGEO.addPoint((int16_t)powerGEO);} // Мощность контура в Вт!!!!!!!!!
 if(ChartCOP.get_present())       {if (dFC.get_power()>0) {ChartCOP.addPoint((int16_t)powerCO/dFC.get_power());}  else ChartCOP.addPoint(0);}  // в сотых долях !!!!!!
 #ifdef USE_ELECTROMETER_SDM 
    if(dSDM.ChartVoltage.get_present())   dSDM.ChartVoltage.addPoint(dSDM.get_Voltage()*100);
    if(dSDM.ChartCurrent.get_present())   dSDM.ChartCurrent.addPoint(dSDM.get_Current()*100);
  //  if(dSDM.sAcPower.get_present())   dSDM.sAcPower.addPoint(dSDM.get_AcPower());
  //  if(dSDM.sRePower.get_present())   dSDM.sRePower.addPoint(dSDM.get_RePower());  
    if(dSDM.ChartPower.get_present())   power220=dSDM.get_Power();  dSDM.ChartPower.addPoint(power220); 
  //  if(dSDM.ChartPowerFactor.get_present())   dSDM.ChartPowerFactor.addPoint(dSDM.get_PowerFactor()*100);    
    if(ChartFullCOP.get_present())     { if ((dSDM.get_Power()>0)&&(COMPRESSOR_IS_ON)) { ChartFullCOP.addPoint((float)(100.0*powerCO)/dSDM.get_Power());} else ChartFullCOP.addPoint(0);} // в сотых долях !!!!!!
 #endif


// ДАННЫЕ Запись статистики в файл
 if(GETBIT(Option.flags,fSD_card))
   {
	 if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBuzy);return;} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
     SPI_switchSD();
 //   _delay(10);   // подождать очистку буфера
        if (!statFile.open(FILE_CHART,O_WRITE| O_AT_END)) 
           {
            journal.jprintf("$ERROR - opening %s for write stat data is failed!\n",FILE_CHART);
           }
         else     // Заголовок
           { 
           statFile.print(NowDateToStr());statFile.print(" ");statFile.print(NowTimeToStr());statFile.print(";");  // дата и время
           for(i=0;i<TNUMBER;i++) if(sTemp[i].Chart.get_present()) {statFile.print((float)sTemp[i].get_Temp()/100.0); statFile.print(";");}
           for(i=0;i<ANUMBER;i++) if(sADC[i].Chart.get_present()) {statFile.print((float)sADC[i].get_Press()/100.0);statFile.print(";");} 
           for(i=0;i<FNUMBER;i++) if(sFrequency[i].Chart.get_present()) {statFile.print((float)sFrequency[i].get_Value()/1000.0);statFile.print(";");} // Частотные датчики
           #ifdef EEV_DEF
           if(dEEV.Chart.get_present())       { statFile.print(dEEV.get_EEV()); statFile.print(";");}
           if(ChartOVERHEAT.get_present())    { statFile.print((float)dEEV.get_Overheat()/100.0); statFile.print(";");}
           if(ChartTPCON.get_present())       { statFile.print((float)(PressToTemp(sADC[PCON].get_Press(),dEEV.get_typeFreon()))/100.0); statFile.print(";");}
           if(ChartTPEVA.get_present())       { statFile.print((float)(PressToTemp(sADC[PEVA].get_Press(),dEEV.get_typeFreon()))/100.0); statFile.print(";");}
           #endif
         
           if(dFC.ChartFC.get_present())      { statFile.print((float)dFC.get_freqFC()/100.0);   statFile.print(";");} 
           if(dFC.ChartPower.get_present())   { statFile.print((float)dFC.get_power()/10.0);    statFile.print(";");}
           if(dFC.ChartCurrent.get_present()) { statFile.print((float)dFC.get_current()/100.0); statFile.print(";");}
           
           if(ChartRCOMP.get_present())       { statFile.print((int16_t)dRelay[RCOMP].get_Relay()); statFile.print(";");}
           if(ChartdCO.get_present())         { statFile.print((float)(FEED-RET)/100.0); statFile.print(";");}
           if(ChartdGEO.get_present())        { statFile.print((float)(sTemp[TEVAING].get_Temp()-sTemp[TEVAOUTG].get_Temp())/100.0); statFile.print(";");}

                
           if(ChartPowerCO.get_present())  { statFile.print((int16_t)(powerCO)); statFile.print(";"); } // Мощность контура в ваттах!!!!!!!!!
           if(ChartPowerGEO.get_present()) { statFile.print((int16_t)(powerGEO)); statFile.print(";");} // Мощность контура в ваттах!!!!!!!!!
           if(ChartCOP.get_present())      { statFile.print((float)(powerCO/dFC.get_power())/100.0); statFile.print(";"); }    // в еденицах
           #ifdef USE_ELECTROMETER_SDM 
           if(dSDM.ChartVoltage.get_present())     { statFile.print((float)dSDM.get_Voltage());    statFile.print(";");} 
           if(dSDM.ChartCurrent.get_present())     { statFile.print((float)dSDM.get_Current());    statFile.print(";");} 
 //          if(dSDM.sAcPower.get_present())     { statFile.print((float)dSDM.get_AcPower());    statFile.print(";");} 
 //          if(dSDM.sRePower.get_present())     { statFile.print((float)dSDM.get_RePower());    statFile.print(";");}   
           if(dSDM.ChartPower.get_present())       { statFile.print((float)dSDM.get_Power());      statFile.print(";");}  
 //          if(dSDM.ChartPowerFactor.get_present()) { statFile.print((float)dSDM.get_PowerFactor());statFile.print(";");}  
           if(ChartFullCOP.get_present())       { if ((dSDM.get_Power()>0)&&(COMPRESSOR_IS_ON)){ statFile.print((float)(powerCO/dSDM.get_Power())/100.0);statFile.print(";");}  else statFile.print("0.0;");}
           #endif
           statFile.println("");
           statFile.flush();
           statFile.close();
           }   
      SPI_switchW5200();        
      SemaphoreGive(xWebThreadSemaphore);                                      // Отдать мютекс
  }  
          
}

// сбросить статистику и запустить новую запись
void HeatPump::startChart()
{
 uint8_t i; 
 for(i=0;i<TNUMBER;i++) sTemp[i].Chart.clear();
 for(i=0;i<ANUMBER;i++) sADC[i].Chart.clear();
 for(i=0;i<FNUMBER;i++) sFrequency[i].Chart.clear();
 #ifdef EEV_DEF
 dEEV.Chart.clear();
 ChartOVERHEAT.clear();
 ChartTPEVA.clear(); 
 ChartTPCON.clear(); 
 #endif
 dFC.ChartFC.clear();
 dFC.ChartPower.clear();
 dFC.ChartCurrent.clear();
 ChartRCOMP.clear();
// ChartRELAY.clear();
 ChartdCO.clear();
 ChartdGEO.clear();
 ChartPowerCO.clear();                                 // выходная мощность насоса
 ChartPowerGEO.clear();                                // Мощность геоконтура
 ChartCOP.clear();                                     // Коэффициент преобразования
 #ifdef USE_ELECTROMETER_SDM 
 dSDM.ChartVoltage.clear();                              // Статистика по напряжению
 dSDM.ChartCurrent.clear();                              // Статистика по току
// dSDM.sAcPower.clear();                              // Статистика по активная мощность
// dSDM.sRePower.clear();                              // Статистика по Реактивная мощность
 dSDM.ChartPower.clear();                                // Статистика по Полная мощность
// dSDM.ChartPowerFactor.clear();                          // Статистика по Коэффициент мощности
 ChartFullCOP.clear();                                     // Коэффициент преобразования
 #endif
 powerCO=0;
 powerGEO=0;
 power220=0;                                   
 vTaskResume(xHandleUpdateStat); // Запустить задачу обновления статистики

 if(GETBIT(Option.flags,fSD_card))  // ЗАГОЛОВОК Запись статистики в файл
   {
	 if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBuzy);return;} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
        SPI_switchSD();
        if (!statFile.open(FILE_CHART,O_WRITE |O_CREAT | O_TRUNC)) 
           {
               journal.jprintf("$ERROR - opening %s for write stat header is failed!\n",FILE_CHART);
           }
         else     // Заголовок
           { 
           statFile.print("time;");
           for(i=0;i<TNUMBER;i++) if(sTemp[i].Chart.get_present()) {statFile.print(sTemp[i].get_name()); statFile.print(";");}
           for(i=0;i<ANUMBER;i++) if(sADC[i].Chart.get_present()) {statFile.print(sADC[i].get_name());statFile.print(";");} 
           for(i=0;i<FNUMBER;i++) if(sFrequency[i].Chart.get_present()) {statFile.print(sFrequency[i].get_name());statFile.print(";");} 
           
           #ifdef EEV_DEF
           if(dEEV.Chart.get_present())        statFile.print("posEEV;");
           if(ChartOVERHEAT.get_present())     statFile.print("OVERHEAT;");
           if(ChartTPEVA.get_present())        statFile.print("T[PEVA];");
           if(ChartTPCON.get_present())        statFile.print("T[PCON];");
           #endif
           
           if(dFC.ChartFC.get_present())       statFile.print("freqFC;");
           if(dFC.ChartPower.get_present())    statFile.print("powerFC;");
           if(dFC.ChartCurrent.get_present())  statFile.print("currentFC;");
           
           if(ChartRCOMP.get_present())        statFile.print("RCOMP;");
           
           if(ChartdCO.get_present())          statFile.print("dCO;");
           if(ChartdGEO.get_present())         statFile.print("dGEO;");
           
           if(ChartPowerCO.get_present())      statFile.print("PowerCO;");
           if(ChartPowerGEO.get_present())     statFile.print("PowerGEO;");
           if(ChartCOP.get_present())          statFile.print("COP;");

           #ifdef USE_ELECTROMETER_SDM 
           if(dSDM.ChartVoltage.get_present())    statFile.print("VOLTAGE;");
           if(dSDM.ChartCurrent.get_present())    statFile.print("CURRENT;");
   //      if(dSDM.sAcPower.get_present())    statFile.print("acPOWER;");
  //       if(dSDM.sRePower.get_present())    statFile.print("rePOWER;");
           if(dSDM.ChartPower.get_present())      statFile.print("fullPOWER;");
  //         if(dSDM.ChartPowerFactor.get_present())statFile.print("kPOWER;");
           if(ChartFullCOP.get_present())      statFile.print("fullCOP;");
           #endif
           
           statFile.println("");
           statFile.flush();
           statFile.close();
           journal.jprintf(" Write header %s  on SD card Ok\n",FILE_CHART);
           }  
  //    _delay(10);   // подождать очистку буфера
      SPI_switchW5200();  
      SemaphoreGive(xWebThreadSemaphore);                                      // Отдать мютекс
  }
     
}


// получить список доступных графиков в виде строки
// cat true - список добавляется в конец, false - строка обнуляется и список добавляется
char * HeatPump::get_listChart(char* str, boolean cat)
{
uint8_t i;  
if (!cat) strcpy(str,"");  //Обнулить строку если есть соответсвующий флаг
 strcat(str,"none:1;");
 for(i=0;i<TNUMBER;i++) if(sTemp[i].Chart.get_present()) {strcat(str,sTemp[i].get_name()); strcat(str,":0;");}
 for(i=0;i<ANUMBER;i++) if(sADC[i].Chart.get_present()) { strcat(str,sADC[i].get_name()); strcat(str,":0;");} 
 for(i=0;i<FNUMBER;i++) if(sFrequency[i].Chart.get_present()) { strcat(str,sFrequency[i].get_name()); strcat(str,":0;");} 
 #ifdef EEV_DEF
 if(dEEV.Chart.get_present())       strcat(str,"posEEV:0;");
 if(ChartOVERHEAT.get_present())    strcat(str,"OVERHEAT:0;");
 if(ChartTPEVA.get_present())       strcat(str,"T[PEVA]:0;");
 if(ChartTPCON.get_present())       strcat(str,"T[PCON]:0;");
 #endif
 if(dFC.ChartFC.get_present())      strcat(str,"freqFC:0;");
 if(dFC.ChartPower.get_present())   strcat(str,"powerFC:0;");
 if(dFC.ChartCurrent.get_present()) strcat(str,"currentFC:0;");
 if(ChartRCOMP.get_present())       strcat(str,"RCOMP:0;");
 if(ChartdCO.get_present())         strcat(str,"dCO:0;");
 if(ChartdGEO.get_present())        strcat(str,"dGEO:0;");
 if(ChartPowerCO.get_present())     strcat(str,"PowerCO:0;");
 if(ChartPowerGEO.get_present())    strcat(str,"PowerGEO:0;");
 if(ChartCOP.get_present())         strcat(str,"COP:0;"); 
  #ifdef USE_ELECTROMETER_SDM 
 if(dSDM.ChartVoltage.get_present())     strcat(str,"VOLTAGE:0;");
 if(dSDM.ChartCurrent.get_present())     strcat(str,"CURRENT:0;");
// if(dSDM.sAcPower.get_present())     strcat(str,"acPOWER:0;");
// if(dSDM.sRePower.get_present())     strcat(str,"rePOWER:0;");
 if(dSDM.ChartPower.get_present())       strcat(str,"fullPOWER:0;");
// if(dSDM.ChartPowerFactor.get_present()) strcat(str,"kPOWER:0;");
 if(ChartFullCOP.get_present())       strcat(str,"fullCOP:0;"); 
 #endif
// for(i=0;i<RNUMBER;i++) if(dRelay[i].Chart.get_present()) { strcat(str,dRelay[i].get_name()); strcat(str,":0;");}  
return str;               
}

// получить данные графика  в виде строки
// cat=true - не обнулять входную строку а добавить в конец
char * HeatPump::get_Chart(TYPE_CHART t,char* str, boolean cat)
{
 if (!cat) strcpy(str,"");  //Обнулить строку если есть соответсвующий флаг false
 switch (t)
   {
   case pNONE:      strcat(str,""); return str;                          break;   
   case pTOUT:      return sTemp[TOUT].Chart.get_PointsStr(100,str);      break;   
   case pTIN:       return sTemp[TIN].Chart.get_PointsStr(100,str);       break;   
   case pTEVAIN:    return sTemp[TEVAIN].Chart.get_PointsStr(100,str);    break;   
   case pTEVAOUT:   return sTemp[TEVAOUT].Chart.get_PointsStr(100,str);   break;   
   case pTCONIN:    return sTemp[TCONIN].Chart.get_PointsStr(100,str);    break;   
   case pTCONOUT:   return sTemp[TCONOUT].Chart.get_PointsStr(100,str);   break;   
   case pTBOILER:   return sTemp[TBOILER].Chart.get_PointsStr(100,str);   break;   
   case pTACCUM:    return sTemp[TACCUM].Chart.get_PointsStr(100,str);    break;   
   case pTRTOOUT:   return sTemp[TRTOOUT].Chart.get_PointsStr(100,str);   break; 
   case pTCOMP:     return sTemp[TCOMP].Chart.get_PointsStr(100,str);     break; 
   case pTEVAING:   return sTemp[TEVAING].Chart.get_PointsStr(100,str);   break; 
   case pTEVAOUTG:  return sTemp[TEVAOUTG].Chart.get_PointsStr(100,str);  break; 
   case pTCONING:   return sTemp[TCONING].Chart.get_PointsStr(100,str);   break; 
   case pTCONOUTG:  return sTemp[TCONOUTG].Chart.get_PointsStr(100,str);  break; 

   case pPEVA:      return sADC[PEVA].Chart.get_PointsStr(100,str);       break;
   case pPCON:      return sADC[PCON].Chart.get_PointsStr(100,str);       break;


   case pFLOWCON:   
                    #ifdef FFLOWCON
                     return sFrequency[FLOWCON].Chart.get_PointsStr(1000,str);
                     #endif
                     break;
   case pFLOWEVA:   
                     #ifdef FLOWEVA
                     return sFrequency[FLOWEVA].Chart.get_PointsStr(1000,str);
                     #endif
                     break;
   case pFLOWPCON:  
                     #ifdef FLOWPCON
                     return sFrequency[FLOWPCON].Chart.get_PointsStr(1000,str);
                     #endif
                     break;

   #ifdef EEV_DEF
   case pposEEV:    return dEEV.Chart.get_PointsStr(1,str);               break;
   case pOVERHEAT:  return ChartOVERHEAT.get_PointsStr(100,str);          break;
   case pTPEVA:     return ChartTPEVA.get_PointsStr(100,str);             break;
   case pTPCON:     return ChartTPCON.get_PointsStr(100,str);             break;
   #endif
   case pfreqFC:    return dFC.ChartFC.get_PointsStr(100,str);            break;
   case ppowerFC:   return dFC.ChartPower.get_PointsStr(10,str);          break;
   case pcurrentFC: return dFC.ChartCurrent.get_PointsStr(100,str);       break;

   case pRCOMP:     return ChartRCOMP.get_PointsStr(1,str);               break;
   case pdCO:       return ChartdCO.get_PointsStr(100,str);               break;
   case pdGEO:      return ChartdGEO.get_PointsStr(100,str);              break; 
   case pPowerCO:   return ChartPowerCO.get_PointsStr(1000,str);          break; 
   case pPowerGEO:  return ChartPowerGEO.get_PointsStr(1000,str);         break;
   case pCOP:       return ChartCOP.get_PointsStr(100,str);               break;
   
   #ifdef USE_ELECTROMETER_SDM 
   case pVOLTAGE:   return dSDM.ChartVoltage.get_PointsStr(100,str);         break; 
   case pCURRENT:   return dSDM.ChartCurrent.get_PointsStr(100,str);         break;
//   case pacPOWER:   return dSDM.sAcPower.get_PointsStr(1,str);           break;
//   case prePOWER:   return dSDM.sRePower.get_PointsStr(1,str);           break; 
   case pfullPOWER: return dSDM.ChartPower.get_PointsStr(1,str);             break;
//   case pkPOWER:    return dSDM.ChartPowerFactor.get_PointsStr(100,str);     break;
   case pfullCOP:   return ChartFullCOP.get_PointsStr(100,str);           break;
   
   #endif
  
   default:        strcat(str,""); return str;                           break;   
   }

return str;      
}

// расчитать хеш для пользователя возвращает длину хеша
uint8_t HeatPump::set_hashUser()
{
char buf[20];
strcpy(buf,NAME_USER);
strcat(buf,":");
strcat(buf,Network.passUser);
base64_encode(Security.hashUser, buf, strlen(buf)); 
Security.hashUserLen=strlen(Security.hashUser);
journal.jprintf("Hash user: %s\n",Security.hashUser);
return Security.hashUserLen;
}
// расчитать хеш для администратора возвращает длину хеша
uint8_t HeatPump::set_hashAdmin()
{
char buf[20];
strcpy(buf,NAME_ADMIN);
strcat(buf,":");
strcat(buf,Network.passAdmin);
base64_encode(Security.hashAdmin,buf,strlen(buf)); 
Security.hashAdminLen=strlen(Security.hashAdmin);
journal.jprintf("Hash admin: %s\n",Security.hashAdmin);
return Security.hashAdminLen;  
}


// Обновить настройки дисплея Nextion
void HeatPump::updateNextion() 
{
  #ifdef NEXTION   
  char temp[16];
  if (GETBIT(Option.flags,fNextion))  // Дисплей подключен
      {
          if(Option.sleep>0)   // установлено засыпание дисплея
              {
              strcpy(temp,"thsp=");
              strcat(temp,int2str(Option.sleep*60)); // секунды
              myNextion.sendCommand(temp);
              myNextion.sendCommand("thup=1");     // sleep режим активировать
              }  
           else
           {
               myNextion.sendCommand("thsp=0")  ;   // sleep режим выключен  - ЭТО  РАБОТАЕТ
               myNextion.sendCommand("thup=0");      // sleep режим активировать
  /* 
               myNextion.sendCommand("rest");         // Запретить режим сна получается только через сброс экрана
               _delay(50);
               myNextion.sendCommand("page 0");   
               myNextion.sendCommand("bkcmd=0");     // Ответов нет от дисплея
               myNextion.sendCommand("sendxy=0");
               myNextion.sendCommand("thup=1");      // sleep режим активировать
  */             
           }
           
          strcpy(temp,"dim=");
          strcat(temp,int2str(Option.dim));
          myNextion.sendCommand(temp);
          myNextion.set_fPageID();
          vTaskResume(xHandleUpdateNextion);   // включить задачу обновления дисплея
       }
  else                        // Дисплей выключен
      {
          vTaskSuspend(xHandleUpdateNextion);   // выключить задачу обновления дисплея
          myNextion.sendCommand("thsp=0")  ;    // sleep режим выключен
          myNextion.sendCommand("dim=0");
          myNextion.sendCommand("sleep=1");  
      }
 #endif     
}

int16_t HeatPump::get_targetTempCool()
{
  if (get_ruleCool()==pHYBRID) return Prof.Cool.Temp1;  
  if(!(GETBIT(Prof.Cool.flags,fTarget))) return Prof.Cool.Temp1; 
  else  return Prof.Cool.Temp2;
}

int16_t  HeatPump::get_targetTempHeat()
{
  if (get_ruleHeat()==pHYBRID) return Prof.Heat.Temp1;  
  if(!(GETBIT(Prof.Heat.flags,fTarget))) return Prof.Heat.Temp1;
  else  return Prof.Heat.Temp2;
}

// Изменить целевую температуру с провекой допустимости значений
// Параметр само ИЗМЕНЕНИЕ температуры
int16_t HeatPump::setTargetTemp(int16_t dt)
 {
  switch ((MODE_HP)get_mode())   // проверка отопления
    {
      case  pOFF:   return 0;                       break;
      case  pHEAT:  
                  if (get_ruleHeat()==pHYBRID) {if((Prof.Heat.Temp1+dt>=0.0*100)&&(Prof.Heat.Temp1+dt<=30.0*100)) Prof.Heat.Temp1=Prof.Heat.Temp1+dt; return Prof.Heat.Temp1;}
                  if(!(GETBIT(Prof.Heat.flags,fTarget))) { if((Prof.Heat.Temp1+dt>=0.0*100)&&(Prof.Heat.Temp1+dt<=30.0*100)) Prof.Heat.Temp1=Prof.Heat.Temp1+dt; return Prof.Heat.Temp1;}
                  else  { if((Prof.Heat.Temp2+dt>=10.0*100)&&(Prof.Heat.Temp2+dt<=50.0*100)) Prof.Heat.Temp2=Prof.Heat.Temp2+dt; return Prof.Heat.Temp2; }   
                  break; 
      case  pCOOL:  
                  if (get_ruleCool()==pHYBRID) {if((Prof.Cool.Temp1+dt>=0.0*100)&&(Prof.Cool.Temp1+dt<=30.0*100)) Prof.Cool.Temp1=Prof.Cool.Temp1+dt; return Prof.Cool.Temp1;}
                  if(!(GETBIT(Prof.Cool.flags,fTarget))) {if((Prof.Cool.Temp1+dt>=0.0*100)&&(Prof.Cool.Temp1+dt<=30.0*100)) Prof.Cool.Temp1=Prof.Cool.Temp1+dt; return Prof.Cool.Temp1;}
                  else  { if((Prof.Cool.Temp2+dt>=0.0*100)&&(Prof.Cool.Temp2+dt<=30.0*100)) Prof.Cool.Temp2=Prof.Cool.Temp2+dt; return Prof.Cool.Temp2; }    
            break; 
     default: break;       
    }  
   return 0;   
 }
 // Переключение на следующий режим работы отопления (последовательный перебор режимов)
 void HeatPump::set_nextMode()
 {
   switch ((MODE_HP)get_mode())  
    {
      case  pOFF:   Prof.SaveON.mode=pHEAT;  break;
      case  pHEAT:  Prof.SaveON.mode=pCOOL;  break; 
      case  pCOOL:  Prof.SaveON.mode=pOFF;   break; 
      default: break;
    }  
 }
 // ИЗМЕНИТЬ целевую температуру бойлера с провекой допустимости значений
 int16_t HeatPump::setTempTargetBoiler(int16_t dt)  
 {
  if ((Prof.Boiler.TempTarget+dt>=5.0*100)&&(Prof.Boiler.TempTarget+dt<=60.0*100))   Prof.Boiler.TempTarget=Prof.Boiler.TempTarget+dt; 
  return Prof.Boiler.TempTarget;     
 }
                                 
// --------------------------------------------------------------------------------------------------------     
// ---------------------------------- ОСНОВНЫЕ ФУНКЦИИ РАБОТЫ ТН ------------------------------------------
// --------------------------------------------------------------------------------------------------------    

// Проверить расписание бойлера true - нужно греть false - греть не надо, если расписание выключено то возвращает true
boolean HeatPump::scheduleBoiler()
{
boolean b;  
if(GETBIT(Prof.Boiler.flags,fSchedule))         // Если используется расписание
 {  // Понедельник 0 воскресенье 6
  b=Prof.Boiler.Schedule[rtcSAM3X8.get_day_of_week()]&(0x01<<rtcSAM3X8.get_hours ())?true:false; 
  if(!b) return false;             // запрещено греть бойлер согласно расписания
 }
return true; 
}
// Все реле выключить
void HeatPump::relayAllOFF()
{
  uint8_t i;
  for(i=0;i<RNUMBER;i++)  dRelay[i].set_OFF();         // Выключить все реле;
  relay3Way=false;                                     // выключить признак трехходового
  journal.jprintf(" All relay off\n");
}                               
// Поставить 4х ходовой в нужное положение для работы в заваисимости от Prof.SaveON.mode
// функция сама определяет что делать в зависимости от режима
// параметр задержка после включения мсек.
#ifdef RTRV    // Если четырехходовой есть в конфигурации
    void HeatPump::set_RTRV(uint16_t d)
    {
       if (Prof.SaveON.mode==pHEAT)        // Реле переключения четырех ходового крана (переделано для инвертора). Для ОТОПЛЕНИЯ надо выключить,  на ОХЛАЖДЕНИЯ включить конечное устройство
        { 
          dRelay[RTRV].set_OFF();         // отопление
        }
        else   // во всех остальных случаях
        {
          dRelay[RTRV].set_ON();          // охлаждение
        }  
       _delay(d);                            // Задержка на 2 сек
    }
#endif
//  Переключение 3-х ходового крана с обеспечением паузы после переключения
boolean HeatPump::switch3WAY(boolean b)
{
 if (b==relay3Way) return relay3Way;           // Нечего делать выходим
 relay3Way=b;                                  // запомнить состояние ВСЕГДА
 if(!relay3Way) offBoiler=rtcSAM3X8.unixtime();// запомнить время выключения ГВС (нужно для переключения)
 else           offBoiler=0;                 
 #ifdef R3WAY
   dRelay[R3WAY].set_Relay(relay3Way);           // Установить в нужное полежение 3-х ходового
   if(get_State()==pWORK_HP)   // Если было перекидывание 3-х ходового и ТН работает до обеспечить дополнительное время (DELAY_3WAY сек) для прокачивания гликоля - т.к разные уставки по температуре подачи
       {
        journal.jprintf(" Pause %d sec after switching the 3-way valve . . .\n",DELAY_3WAY);
        _delay(DELAY_3WAY*1000);  // выравниваем температуру в контуре отопления/ГВС что бы сразу защиты не сработали
       } 
  #else
  // замена трехходового крана
  // ставим сюда код переключения ГВС/отопление в зависимости от relay3Way=true - ГВС
  #endif        
  return relay3Way;     
}
// Проверка и если надо включение EVI если надо то выключение возвращает состояние реле
// Если реле нет то ничего не делает возвращает false
// Проверяет паузу 60 секунд после включения компрессора, и только после этого начинает управлять EVI
#ifdef REVI
boolean HeatPump::checkEVI()
{
  if (!dRelay[REVI].get_present())  return false;                                                                            // Реле отсутвует в конфигурации ничего не делаем
  if (!(COMPRESSOR_IS_ON))     {dRelay[REVI].set_OFF(); return dRelay[REVI].get_Relay();}                                    // Компрессор выключен и реле включено - выключить реле

  // компрессор работает
  if (rtcSAM3X8.unixtime()-startCompressor<60) return dRelay[REVI].get_Relay() ;                                            // Компрессор работает меньше одной минуты еще рано

  // проверяем условия для включения ЭВИ
  if((sTemp[TCONOUTG].get_Temp()>EVI_TEMP_CON)||(sTemp[TEVAOUTG].get_Temp()<EVI_TEMP_EVA)) { dRelay[REVI].set_ON(); return dRelay[REVI].get_Relay();}  // условия выполнены включить ЭВИ
  else  {dRelay[REVI].set_OFF(); return dRelay[REVI].get_Relay();}                                                          // выключить ЭВИ - условий нет
  return dRelay[REVI].get_Relay();
}
#endif

// Включить или выключить насосы первый параметр их желаемое состояние
// Второй параметр параметр задержка после включения/выключения мсек. отдельного насоса (борьба с помехами)
// Идет проверка на необходимость изменения состояния насосов
// Генерятся задержки для защиты компрессора, есть задержки между включенимями насосов для уменьшения помех
void HeatPump::Pumps(boolean b, uint16_t d)
{
	boolean old = dRelay[PUMP_IN].get_Relay(); // Входное (текущее) состояние определяется по Гео  (СО - могут быть варианты)
	if(b == old) return;                                                        // менять нечего выходим

	// пауза перед выключением насосов, если нужно
	if((!b) && (old)) // Насосы выключены и будут выключены, нужна пауза идет останов компрессора (новое значение выкл  старое значение вкл)
	{
		journal.jprintf(" Pause before stop pumps %d sec . . .\n", DELAY_OFF_PUMP);
		_delay(DELAY_OFF_PUMP * 1000); // задержка перед выключениме насосов после выключения компрессора (облегчение останова)
	}

	// переключение насосов если есть что переключать (проверка была выше)
	dRelay[PUMP_IN].set_Relay(b);                                 // Реле включения насоса входного контура  (геоконтур)
	_delay(d);                                      // Задержка на d мсек
#ifdef  TWO_PUMP_IN                                                // второй насос для воздушника если есть
	if (!b) dRelay[PUMP_IN1].set_OFF();    // если насососы выключаем то второй вентилятор ВСЕГДА выключается!!
	else// а если насос включается то смотрим на условия
	{
		if(sTemp[TEVAOUT].get_Temp()<2500) {dRelay[PUMP_IN1].set_ON();} // Реле включения второго насоса входного контура для  воздушника
		else {dRelay[PUMP_IN1].set_OFF();}
	}
	_delay(d);                                 // Задержка на d мсек
#endif
	dRelay[PUMP_OUT].set_Relay(b);                         // Реле включения насоса выходного контура  (отопление и ГВС)
	_delay(d);                                     // Задержка на d мсек
	//  }
	// пауза после включения насосов, если нужно
	if((b) && (!old))                                                // Насосы включены (старт компрессора), нужна пауза
	{
		journal.jprintf(" Pause %d seconds before starting compressor . . .\n", DELAY_ON_PUMP);
		_delay(DELAY_ON_PUMP * 1000);                 // задержка перед включением компрессора
	}

}
// Сброс инвертора если он стоит в ошибке, проверяется наличие инвертора и проверка ошибки
// Проводится различный сброс в зависимсти от конфигурации
int8_t HeatPump::ResetFC() 
{
  if (!dFC.get_present()) return OK;                                                 // Инвертора нет выходим
  
  #ifdef FC_USE_RCOMP                                                               // ЕСЛИ есть провод сброса
          #ifdef SERRFC                                                               // Если есть вход от инвертора об ошибке
            if (sInput[SERRFC].get_lastErr()==OK) return OK;                          // Инвертор сбрасывать не надо
            dRelay[RRESET].set_ON();  _delay(100); dRelay[RRESET].set_OFF();           // Подать импульс сброса
            journal.jprintf("Reset %s use RRESET\r\n",dFC.get_name());
            _delay(100);
            sInput[SERRFC].Read();
            if (sInput[SERRFC].get_lastErr()==OK) return OK;                          // Инвертор сброшен
            else return ERR_reset_FC;                                                 // Сброс не прошел
            #else                                                                      // нет провода сброса - сбрасываем по модбас
              if (dFC.get_err()!=OK)                                                       // инвертор есть ошибки
                {
                dFC.reset_FC();                                                       // подать команду на сброс по модбас
                if (!dFC.get_blockFC())  return OK;                                   // Инвертор НЕ блокирован
                else return ERR_reset_FC;                                             // Сброс не удачный
                }    
              else  return OK;                                                         // Ошибок нет сбрасывать нечего
            #endif // SERRFC
   #else   //  #ifdef FC_USE_RCOMP 
      if (dFC.get_err()!=OK)                                                           //  инвертор есть ошибки
      {
        dFC.reset_FC();                                                               // подать команду на сброс по модбас
      if (!dFC.get_blockFC())  return OK;                                             // Инвертор НЕ блокирован
      else return ERR_reset_FC;                                                       // Сброс не удачный
      }
     else  return OK;                                                                  // Ошибок нет сбрасывать нечего
  #endif  
   return OK;         
  }
           

// START -----------------------------------------
// Запустить ТН - возвращает ок или код ошибки.
// Запускается ВСЕГДА отдельной задачей с приоритетом выше вебсервера
int8_t HeatPump::Start()
{
  volatile MODE_HP mod; 
  
  #ifndef DEMO
  if((dFC.get_present())&&(dFC.get_blockFC()))                         // есть инвертор но он блокирован
       {
        journal.jprintf("%s: is blocked, ignore start\n",dFC.get_name());
        setState(pOFF_HP);                                              // Еще ничего не сделали по этому сразу ставим состоение выключено
        set_Error(ERR_485_BLOCK,(char*)__FUNCTION__); 
        return error;
       }   
  #endif
  
  if ((get_State()==pWORK_HP)||(get_State()==pSTOPING_HP)||(get_State()==pSTARTING_HP)) return error; // Если ТН включен или уже стартует или идет процесс остановки то ничего не делаем (исключается многократный заход в функцию)
  journal.jprintf(pP_DATE,"  Start . . .\n");

   // 1. Переменные  установка, остановка ТН имеет более высокий приоритет чем пуск ! -------------------------
    eraseError();                                      // Обнулить ошибку
      if ((error=ResetFC())!=OK)                       // Сброс инвертора если нужно
      {
        setState(pOFF_HP);  // Еще ничего не сделали по этому сразу ставим состоение выключено
        set_Error(error,(char*)__FUNCTION__);  
        return error; 
      } 
    setState(pSTARTING_HP);                              // Производится старт -  флаг
    Status.ret=pNone;                                    // Состояние алгоритма
    lastEEV=-1;                                          // -1 это признак того что слежение eev еще не рабоатет (выключения компрессора  небыло)
    if (startPump)                                       // Проверка задачи насос
        {
          startPump=false;                               // Поставить признак останова задачи насос
          vTaskSuspend(xHandleUpdatePump);               // Остановить задачу насос
          journal.jprintf(" WARNING! %s: Bad startPump, task vUpdatePump RPUMPO pause  . . .\n",(char*)__FUNCTION__);
        } 
    stopCompressor=0;                                    // Компрессор никогда не выключался пауза при старте не нужна
    offBoiler=0;                                         // Бойлер никогда не выключался
    // Сбросить переменные пид регулятора
    temp_int = 0;                                        // Служебная переменная интегрирования
    errPID=0;                                            // Текущая ошибка ПИД регулятора
    pre_errPID=0;                                        // Предыдущая ошибка ПИД регулятора
    updatePidTime=0;                                     // время обновления ПИДа
    // ГВС Сбросить переменные пид регулятора
    temp_intBoiler = 0;                                  // Служебная переменная интегрирования
    errPIDBoiler=0;                                      // Текущая ошибка ПИД регулятора
    pre_errPIDBoiler=0;                                  // Предыдущая ошибка ПИД регулятора
    updatePidBoiler=0;                                   // время обновления ПИДа
    
    
   // 2. Проверка конфигурации ----------------------------------------
      if ((Prof.SaveON.mode==pOFF)&&(!(GETBIT(Prof.SaveON.flags,fBoilerON))))   // Нет работы для ТН - ничего не включено
         {
          setState(pOFF_HP);  // Еще ничего не сделали по этому сразу ставим состоение выключено
          set_Error(ERR_NO_WORK,(char*)__FUNCTION__);  
          return error; 
         }
       #ifdef EEV_DEF
      if ((!sADC[PEVA].get_present())&&(dEEV.get_ruleEEV()==TEVAOUT_PEVA))  //  Отсутвует датчик давления, и выбран алгоритм ЭРВ который его использует",
        {
        setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
         set_Error(ERR_PEVA_EEV,(char*)__FUNCTION__);        // остановить по ошибке;
         return error;
        }
       #endif
        
      if (!dRelay[PUMP_OUT].get_present())  // отсутсвует насос на конденсаторе, пользователь НЕ может изменить в процессе работы проверка при старте
       {
        setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
        set_Error(ERR_PUMP_CON,(char*)__FUNCTION__);        // остановить по ошибке;
         return error;
        }
      if (!dRelay[PUMP_IN].get_present())   // отсутсвует насос на испарителе, пользователь может изменить в процессе работы
       {
         setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
         set_Error(ERR_PUMP_CON,(char*)__FUNCTION__);        // остановить по ошибке;
         return error;
        }
      if ((!dRelay[RCOMP].get_present())&&(!dFC.get_present()))   // отсутсвует компрессор, пользователь может изменить в процессе работы
        {
         setState(pOFF_HP);    // Еще ничего не сделали по этому сразу ставим состоение выключено
         set_Error(ERR_NO_COMPRESS,(char*)__FUNCTION__);        // остановить по ошибке;
         return error;
        }
 
  // 3.  ПОДГОТОВКА ------------------------------------------------------------------------
   relayAllOFF();                                          // Выключить все реле, в принципе это лишнее
   #ifdef EEV_DEF
   journal.jprintf(" EEV init\n");
   if (get_State()!=pSTARTING_HP) return error;            // Могли нажать кнопку стоп, выход из процесса запуска
   else  dEEV.Start();                                     // Включить ЭРВ  найти 0 по завершению позиция 0!!!
   #endif
   journal.jprintf(" Charts clear and start\n");
   if (get_State()!=pSTARTING_HP) return error;            // Могли нажать кнопку стоп, выход из процесса запуска
   else  startChart();                                      // Запустить статистику
 
   // 4. Определяем что нужно делать -----------------------------------------------------------
   if (get_State()!=pSTARTING_HP) return error;            // Могли нажать кнопку стоп, выход из процесса запуска
   else  mod=get_Work();                                   // определяем что делаем с компрессором
   if (mod>pBOILER) mod=pOFF;                              // При первом пуске могут быть только состояния pOFF,pHEAT,pCOOL,pBOILER
   journal.jprintf( " Start modWork:%d[%s]\n",(int)mod,codeRet[Status.ret]);

   // 5. Конфигурируем ТН -----------------------------------------------------------------------
   if (get_State()!=pSTARTING_HP) return error;            // Могли нажать кнопку стоп, выход из процесса запуска
   else  configHP(mod);                                    // Конфигурируем 3 и 4-х клапаны и включаем насосы ПАУЗА после включения насосов

   // 7. Дополнительнеая проверка перед пуском компрессора ----------------------------------------
   if (get_State()!=pSTARTING_HP) return error;          // Могли нажать кнопку стоп, выход из процесса запуска
     if (get_errcode()!=OK)                              // ОШИБКА компрессор уже работает
     {
      journal.jprintf(" There is an error, the compressor is not on\n");
      set_Error(ERR_COMP_ERR,(char*)__FUNCTION__); return error; 
     }   
     
    // 9. Включение компрессора и запуск обновления EEV -----------------------------------------------------
    if (get_State()!=pSTARTING_HP) return error;                         // Могли нажать кнопку стоп, выход из процесса запуска
    if ((mod==pCOOL)||(mod==pHEAT)||(mod==pBOILER))   compressorON(mod); // Компрессор включить если нет ошибок и надо включаться
          
     // 10. Запуск задачи обновления ТН ---------------------------------------------------------------------------
     vTaskResume(xHandleUpdate);                                       // Запустить задачу Обновления ТН, дальше она все доделает
     journal.jprintf(" Start task update %s\n",(char*)nameHeatPump); 
     
     // 11. Сохранение состояния  -------------------------------------------------------------------------------
     if (get_State()!=pSTARTING_HP) return error;                   // Могли нажать кнопку стоп, выход из процесса запуска
     setState(pWORK_HP);
     journal.jprintf(pP_TIME,"  %s ON . . .\n",(char*)nameHeatPump);
  return OK;
}

                     
// STOP -----------------------------------------
// Остановить ТН  - возвращает код ошибки
int8_t HeatPump::Stop()
{  
  if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) return error;    // Если ТН выключен или выключается ничего не делаем
  setState(pSTOPING_HP);  // Состояние включения
  journal.jprintf(pP_DATE,"   Stop . . .\n"); 
  if (relay3Way) // Если надо выключить трехходовой (облегчение останова)
      {
        relay3Way=false;
        #ifdef R3WAY
           dRelay[R3WAY].set_OFF();    
        #else
        // ставить замену трехходового
        #endif
      }
  if (COMPRESSOR_IS_ON) { COMPRESSOR_OFF;  stopCompressor=rtcSAM3X8.unixtime();}      // Выключить компрессор и запомнить веремя
  
  vTaskSuspend(xHandleUpdate);                           // Остановить задачу обновления ТН
  journal.jprintf(" Stop task update %s\n",(char*)nameHeatPump);   
    
  if(startPump)
  {
     startPump=false;                                    // Поставить признак что насос выключен
     vTaskSuspend(xHandleUpdatePump);                    // Остановить задачу насос
     journal.jprintf(" %s: Task vUpdatePump RPUMPO off . . .\n",(char*)__FUNCTION__);
  }
//  journal.jprintf(" Pause before stop pumps %d sec . . .\n",DELAY_OFF_PUMP); 
//  _delay(DELAY_OFF_PUMP*1000);  // задержка перед выключениме насосов (облегчение останова)
  PUMPS_OFF;                                             // выключить насосы
 // принудительное выключение отдельных узлов ТН если они есть в конфиге
  #ifdef RBOILER  // управление дополнительным ТЭНом бойлера
     if (dRelay[RBOILER].get_Relay()) dRelay[RBOILER].set_OFF();  // выключить тен бойлера
  #endif

  #ifdef RHEAT  // управление  ТЭНом отопления
     if (dRelay[RHEAT].get_Relay()) dRelay[RHEAT].set_OFF();     // выключить тен отопления
  #endif

  #ifdef RPUMPB  // управление  насосом циркуляции ГВС
     if (dRelay[RPUMPB].get_Relay()) dRelay[RPUMPB].set_OFF();     // выключить насос циркуляции ГВС
  #endif

  #ifdef EEV_DEF
  if(GETBIT(Option.flags,fEEV_close))
     { 
     journal.jprintf(" Pause before closing EEV %d sec . . .\n",DELAY_OFF_EEV);   
     _delay(DELAY_OFF_EEV*1000); // пауза перед закрытием ЭРВ  на инверторе компрессор останавливается до 2 минут
     dEEV.set_EEV(EEV_MIN_STEPS);                          // Если нужно, то закрыть ЭРВ
     journal.jprintf(" EEV go EEV_MIN_STEPS\n"); 
     } 
   #endif
   
 // ЭРВ само выключится по State
//  vTaskSuspend(xHandleUpdateEEV);                       // Остановить задачу обновления ЭРВ
//  #ifdef DEBUG 
//      Serial.println(" Stop task update EEV"); 
//  #endif
   
  vTaskSuspend(xHandleUpdateStat);                       // Остановить задачу обновления статистики
  journal.jprintf(" statChart stop\n");
 
  relayAllOFF();                                         // Все выключить, все  (на всякий случай)
  setState(pOFF_HP);
  journal.jprintf(pP_TIME,"   %s OFF . . .\n",(char*)nameHeatPump);       
  return error;
}

// Для старт-стопа Получить информацию что надо делать сейчас
// Реализует логику и приоритеты режимов для СТАРТ_СТОПА
MODE_HP HeatPump::get_Work()
{
    MODE_HP ret=pOFF;
    Status.ret=pNone;           // не определено
     
    // 1. Бойлер
    switch ((int)UpdateBoiler())  // проверка бойлера высший приоритет
    {
      case  pCOMP_OFF:  ret=pOFF;      break;
      case  pCOMP_ON:   ret=pBOILER;   break;
      case  pCOMP_NONE: ret=pNONE_B;   break;
    }
   if ((ret==pBOILER)||(ret==pNONE_B)) return ret;                   // работает бойлер больше ничего анализировать не надо
               
    // Обеспечить переключение с бойлера на отпление
    if((Status.ret==pBp22)||(Status.ret==pBh3)) //если бойлер выключяетя по достижению цели
     {
      switch3WAY(false);                                            // выключить бойлер (задержка в функции) имеено здесь  - а то дальше защиты сработают
     }

    // 2. Отопление/охлаждение
    switch ((int)get_mode())   // проверка отопления
    {
      case  pOFF:       ret=pOFF;      break;
      case  pHEAT:
                  switch ((int)UpdateHeat())
                  {
                    case  pCOMP_OFF:  ret=pOFF;      break;
                    case  pCOMP_ON:   ret=pHEAT;     break;
                    case  pCOMP_NONE: ret=pNONE_H;   break;                 
                  }
                  break;
      case  pCOOL:
                  switch ((int)UpdateCool())
                  {
                    case  pCOMP_OFF:  ret=pOFF;      break;
                    case  pCOMP_ON:   ret=pCOOL;     break;
                    case  pCOMP_NONE: ret=pNONE_C;   break; 
                  }                 
                 break;
    }
   #ifdef RHEAT  // Дополнительный тен для нагрева отопления
    if (GETBIT(Option.flags,fAddHeat))
    {
        if(!GETBIT(Option.flags,fTypeRHEAT)) // резерв
              {
                if (((sTemp[TIN].get_Temp()>Option.tempRHEAT)&&(dRelay[RHEAT].get_Relay()))||((ret==pOFF)&&(dRelay[RHEAT].get_Relay()))) {journal.jprintf(" TIN=%.2f, add heatting off . . .\n",sTemp[TIN].get_Temp()/100.0); dRelay[RHEAT].set_OFF();} // Гистерезис 0.2 градуса что бы не щелкало
                if ((sTemp[TIN].get_Temp()<Option.tempRHEAT-HYSTERESIS_RHEAD)&&(!dRelay[RHEAT].get_Relay())&&(ret!=pOFF))   {journal.jprintf(" TIN=%.2f, add heatting on . . .\n",sTemp[TIN].get_Temp()/100.0);  dRelay[RHEAT].set_ON();}
              }
        else                                // бивалент
              {
                if ( ((sTemp[TOUT].get_Temp()>Option.tempRHEAT)&&(dRelay[RHEAT].get_Relay()))||((ret==pOFF)&&(dRelay[RHEAT].get_Relay())) ) {journal.jprintf(" TOUT=%.2f, add heatting off . . .\n",sTemp[TOUT].get_Temp()/100.0);dRelay[RHEAT].set_OFF();}// Гистерезис 0.2 градуса что бы не щелкало
                if ((sTemp[TOUT].get_Temp()<Option.tempRHEAT-HYSTERESIS_RHEAD)&&(!dRelay[RHEAT].get_Relay())&&(ret!=pOFF))   {journal.jprintf(" TOUT=%.2f, add heatting on . . .\n",sTemp[TOUT].get_Temp()/100.0); dRelay[RHEAT].set_ON();}     
              }
    }  
   #endif
  return ret;  
}
// Управление температурой в зависимости от режима
#define STR_REDUCED "Reduced FC"   // Экономим место
#define STR_FREQUENCY " FC> %.2f Hz\n"            // Экономим место
// Итерация по управлению Бойлером
// возврат что надо делать компрессору, функция НЕ управляет компрессором а только выдает необходимость включения компрессора
MODE_COMP  HeatPump::UpdateBoiler()
{
 float u, u_dif, u_int, u_pro; 
 int16_t newFC;               //Новая частота инвертора

// Проверки на необходимость включения бойлера
 if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) // Если ТН выключен или выключается ничего не делаем
 {
 #ifdef RBOILER  // управление дополнительным ТЭНом бойлера
 dRelay[RBOILER].set_OFF();flagRBOILER=false;  // Выключение
 #endif 
 return pCOMP_OFF;            
} 
 
if ((!scheduleBoiler())||(!GETBIT(Prof.SaveON.flags,fBoilerON))) // Если запрещено греть бойлер согласно расписания ИЛИ  Бойлер выключен, выходим и можно смотреть отопление
{
 #ifdef RBOILER  // управление дополнительным ТЭНом бойлера
 dRelay[RBOILER].set_OFF();flagRBOILER=false; // Выключение
 #endif  
 return pCOMP_OFF;             // запрещено греть бойлер согласно расписания
}

// -----------------------------------------------------------------------------------------------------
// Сброс излишней энергии в систему отопления
// Переключаем 3-х ходовой на отопление на ходу и ждем определенное число минут дальше перекидываем на бойлер
if(GETBIT(Prof.Boiler.flags,fResetHeat))                   // Стоит требуемая опция - Сброс тепла в СО
 {
 // Достигнута максимальная температура подачи - 1 градус или температура нагнетания компрессора больше максимальной - 5 градусов
 if ((FEED>Prof.Boiler.tempIn-100)||(sTemp[TCOMP].get_Temp()>sTemp[TCOMP].get_maxTemp()-500)) 
   {
    journal.jprintf(" Discharge of excess heat in the heating system\n");
    relay3Way=false;
      #ifdef R3WAY
       dRelay[R3WAY].set_OFF();        // Переключится на систему отопления на ходу
      #else
      // замена трехходового
      #endif
    journal.jprintf(" Pause %d  minutes after switching the 3-way valve . . .\n",Prof.Boiler.Reset_Time/60);
    _delay(Prof.Boiler.Reset_Time*1000);  // Сброс требуемое число  минут для системы отопления
    relay3Way=true;
      #ifdef R3WAY
       dRelay[R3WAY].set_ON();        // Переключится на бойлер на ходу
      #else
      // замена трехходового
      #endif
    journal.jprintf(" Switching on the boiler and the heating on\n");
   }      
 }  
 
 #ifdef RBOILER  // ДОГРЕВ - управление дополнительным ТЭНом бойлера
  if (!GETBIT(Prof.Boiler.flags,fAddHeating))  // если нет форсированного нагрева у него приоритет выше
   {
      if  (GETBIT(Option.flags,fAddBoiler))  // Требуется догрев
        {
             if (sTemp[TBOILER].get_Temp()<Prof.Boiler.TempTarget-Prof.Boiler.dTemp)  flagRBOILER=true;    // Доп ТЭН и Температура ниже гистрезиса начала цикла нагрева бойлера ТЭН не включаем
             if (sTemp[TBOILER].get_Temp()<Prof.Boiler.TempTarget)                                         // Доп ТЭН и надо греть (цель не достигнута) то смотрим возможны действия
             {
              if ((flagRBOILER)&&(sTemp[TBOILER].get_Temp()>Option.tempRBOILER)) dRelay[RBOILER].set_ON();  // включения тена если температура бойлера больше температуры догрева и темпеартура бойлера меньше целевой темпеартуры
              if (sTemp[TBOILER].get_Temp()<Option.tempRBOILER-HYSTERESIS_RBOILER) {dRelay[RBOILER].set_OFF();flagRBOILER=false;}   // выключение тена, выход вниз за темпеартуру включения догрева
             }
             else  {dRelay[RBOILER].set_OFF();flagRBOILER=false;}   // Цель достигнута - догрев выключаем
        } 
       else  {dRelay[RBOILER].set_OFF();flagRBOILER=false;}         // Догрев выключен
   } 
 #endif
 
 // Алгоритм гистерезис для старт стопа
 if(!dFC.get_present())
 {
  Status.ret=pNone;                                                                                       // Сбросить состояние
 if (FEED>Prof.Boiler.tempIn)                                         {Status.ret=pBh1; return pCOMP_OFF; }    // Достигнута максимальная температура подачи ВЫКЛ)

// if ((Prof.Boiler.TempTarget-get_dTempBoiler())>sTemp[TBOILER].get_Temp()) {Status.ret=pBh2; return pCOMP_ON;  }    // Температура ниже гистрезиса надо включаться!
 if ((!GETBIT(Prof.Boiler.flags,fAddHeating))&&(GETBIT(Option.flags,fAddBoiler))&&(Option.tempRBOILER<sTemp[TBOILER].get_Temp())) {Status.ret=pBp22; return pCOMP_OFF;}  // Первое - если используется дополнительный ТЭН и Температура выше целевой температуры Тэна надо выключаться!
 if ((Prof.Boiler.TempTarget-Prof.Boiler.dTemp)>sTemp[TBOILER].get_Temp()) {Status.ret=pBh2; return pCOMP_ON;  }    // Второе - Температура ниже гистрезиса надо включаться!
 
 else  if (Prof.Boiler.TempTarget<sTemp[TBOILER].get_Temp())                            {Status.ret=pBh3; return pCOMP_OFF; }  // Температура выше целевой температуры надо выключаться!
  // дошли до сюда значить сохранение предыдущего состяния, температура в диапазоне регулирования может быть или нагрев или остывание
 if (relay3Way)                                                                         {Status.ret=pBh4; return pCOMP_NONE; }  // Если включено реле бойлера значит рабоатет нагрев бойлера
 Status.ret=pBh5;
 return pCOMP_OFF;
 } // if(!dFC.get_present())
 else // ИНвертор ПИД
 {
    Status.ret=pNone;                // Сбросить состояние пида
    // отработка гистререзиса целевой функции
//    if (Prof.Boiler.TempTarget<sTemp[TBOILER].get_Temp()) {Status.ret=pBp3; return pCOMP_OFF; }                        // Температура выше целевой температуры надо выключаться!
    if ((!GETBIT(Prof.Boiler.flags,fAddHeating))&&(GETBIT(Option.flags,fAddBoiler))&&(Option.tempRBOILER<sTemp[TBOILER].get_Temp())) {Status.ret=pBp22; return pCOMP_OFF;}  // если используется дополнительный ТЭН и Температура выше целевой температуры Тэна надо выключаться!
    else if (Prof.Boiler.TempTarget<sTemp[TBOILER].get_Temp())                             {Status.ret=pBh3; return pCOMP_OFF; }   // Температура выше целевой температуры надо выключаться!
    else if(FEED>Prof.Boiler.tempIn) {Status.ret=pBp1; set_Error(ERR_PID_FEED,(char*)__FUNCTION__);return pCOMP_OFF;}         // Достижение максимальной температуры подачи - это ошибка ПИД не рабоатет

    else if (((Prof.Boiler.TempTarget-Prof.Boiler.dTemp)>sTemp[TBOILER].get_Temp())&&(!(relay3Way))) {Status.ret=pBp2; return pCOMP_ON;} // Достигнут гистерезис и компрессор еще не рабоатет на ГВС - Старт бойлера

    else if ((dFC.isfOnOff())&&(!(relay3Way))) return pCOMP_OFF;                               // компрессор рабатает но ГВС греть не надо  - уходим без изменения состояния
    // ПИД ----------------------------------
   // ЗАЩИТА Компресор работает, достигнута максимальная температура подачи, мощность, температура компрессора то уменьшить обороты на FC_STEP_FREQ
   else if ((dFC.isfOnOff())&&(FEED>Prof.Boiler.tempIn-FC_DT_TEMP_BOILER))             // Подача ограничение
   { 
    Status.ret=pBp6;
    journal.jprintf("%s %.2f (FEED: %.2f)\n",STR_REDUCED,FC_STEP_FREQ_BOILER/100.0,FEED/100.0);
    if (dFC.get_targetFreq()-FC_STEP_FREQ_BOILER<FC_MIN_FREQ_BOILER)  return pCOMP_OFF;         // Уменьшать дальше некуда, выключаем компрессор
    dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER); return pCOMP_NONE;                // Уменьшить частоту
   }  
  else if ((dFC.isfOnOff())&&(dFC.get_power()>FC_MAX_POWER_BOILER))                    // Мощность для ГВС меньшая мощность
   { 
    Status.ret=pBp7;
    journal.jprintf("%s %.2f (POWER: %.2f kW)\n",STR_REDUCED,FC_STEP_FREQ_BOILER/100.0,dFC.get_power()/10.0); // КИЛОВАТЫ
    if (dFC.get_targetFreq()-FC_STEP_FREQ_BOILER<FC_MIN_FREQ_BOILER)  return pCOMP_OFF;        // Уменьшать дальше некуда, выключаем компрессор
    dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER); return pCOMP_NONE;               // Уменьшить частоту
   } 
  else if ((dFC.isfOnOff())&&(dFC.get_current()>FC_MAX_CURRENT_BOILER))                // ТОК для ГВС меньшая мощность
   { 
    Status.ret=pBp16;
    journal.jprintf("%s %.2f (CURRENT: %.2f)\n",STR_REDUCED,FC_STEP_FREQ_BOILER/100.0,dFC.get_current()/100.0);
    if (dFC.get_targetFreq()-FC_STEP_FREQ_BOILER<FC_MIN_FREQ_BOILER)  return pCOMP_OFF;        // Уменьшать дальше некуда, выключаем компрессор
    dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER); return pCOMP_NONE;               // Уменьшить частоту
   } 
   
 else if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>sTemp[TCOMP].get_maxTemp()))  // температура компрессора
   { 
    Status.ret=pBp8;
    journal.jprintf("%s %.2f (TCOMP: %.2f)\n",STR_REDUCED,FC_STEP_FREQ_BOILER/100.0,sTemp[TCOMP].get_Temp()/100.0);
    if (dFC.get_targetFreq()-FC_STEP_FREQ_BOILER<FC_MIN_FREQ_BOILER)  return pCOMP_OFF;       // Уменьшать дальше некуда, выключаем компрессор
    dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER); return pCOMP_NONE;              // Уменьшить частоту
   } 
 else if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>sADC[PCON].get_maxPress()-FC_DP_CON_PRESS))    // давление конденсатора до максимальной минус 0.5 бара
   { 
    Status.ret=pBp9;
    journal.jprintf("%s %.2f (PCON:  %.2f)\n",STR_REDUCED,FC_STEP_FREQ_BOILER/100.0,sADC[PCON].get_Press()/100.0);
    if (dFC.get_targetFreq()-FC_STEP_FREQ_BOILER<FC_MIN_FREQ_BOILER)  return pCOMP_OFF;      // Уменьшать дальше некуда, выключаем компрессор
    dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER);  return pCOMP_NONE;            // Уменьшить частоту
   } 
   
  //   else if (((Prof.Boiler.TempTarget-Prof.Boiler.dTemp)>sTemp[TBOILER].get_Temp())&&(!(dFC.isfOnOff())&&(Status.modWork!=pBOILER))) {Status.ret=7; return pCOMP_ON;} // Достигнут гистерезис и компрессор еще не рабоатет на ГВС - Старт бойлера
     else if(!(dFC.isfOnOff())) {Status.ret=pBp5; return pCOMP_OFF; }                                                             // Если компрессор не рабоатет то ничего не делаем и выходим
     else if (rtcSAM3X8.unixtime()-dFC.get_startTime()<FC_ACCEL_TIME/100 ){Status.ret=pBp10; return pCOMP_NONE;  }             // РАЗГОН частоту не трогаем
     
     else if(xTaskGetTickCount()/1000-updatePidBoiler<HP.get_timeBoiler())   {Status.ret=pBp11; return pCOMP_NONE;  }             // время обновления ПИДа еше не пришло
     // Дошли до сюда - ПИД на подачу. Компресор работает
     updatePidBoiler=xTaskGetTickCount()/1000; 
     // Уравнение ПИД регулятора в конечных разностях. ------------------------------------
     // Cp, Ci, Cd – коэффициенты дискретного ПИД регулятора;
     // u(t) = P (t) + I (t) + D (t);
     // P (t) = Kp * e (t);
     // I (t) = I (t — 1) + Ki * e (t);
     // D (t) = Kd * {e (t) — e (t — 1)};
     // T – период дискретизации(период, с которым вызывается ПИД регулятор).
     #ifdef SUPERBOILER     
       Status.ret=pBp14;          
       errPIDBoiler=((float)(Prof.Boiler.tempPID-PressToTemp(HP.sADC[PCON].get_Press(),HP.dEEV.get_typeFreon())))/100.0; // Текущая ошибка (для Жени, по давлению), переводим в градусы
     #else
        Status.ret=pBp12;
        errPIDBoiler=((float)(Prof.Boiler.tempPID-FEED))/100.0;                                       // Текущая ошибка, переводим в градусы ("+" недогрев частоту увеличивать "-" перегрев частоту уменьшать)
     #endif
         // Расчет отдельных компонент
         if (Prof.Boiler.Ki>0)                                                                        // Расчет интегральной составляющей
         {
          temp_int=temp_int+((float)Prof.Boiler.Ki*errPIDBoiler)/100.0;                               // Интегральная составляющая, с накоплением делить на 10
          #define BOILER_MAX_STEP  2                                                                  // Ограничение диапзона изменения 2 герц за один шаг ПИД
          if (temp_int>BOILER_MAX_STEP)  temp_int=BOILER_MAX_STEP; 
          if (temp_int<-1.0*BOILER_MAX_STEP)  temp_int=-1.0*BOILER_MAX_STEP; 
         }
         else temp_int=0;                                                                             // если Кi равен 0 то интегрирование не используем
         u_int=temp_int;
        
         // Дифференцальная составляющая
         u_dif=((float)Prof.Boiler.Kd*(errPIDBoiler-pre_errPIDBoiler))/10.0;                          // Положительная составляющая - ошибка растет (воздействие надо увеличиить)  Отрицательная составляющая - ошибка уменьшается (воздействие надо уменьшить)
         
         // Пропорциональная составляющая (десятые)
         u_pro=(float)Prof.Boiler.Kp*errPIDBoiler/10.0;
         
         // Общее воздействие
         u=u_pro+u_int+u_dif;
         if (u>FC_PID_FREQ_STEP/100.0) u=FC_PID_FREQ_STEP/100.0;                                    // Ограничить увеличение частоты на FC_PID_FREQ_STEP гц
         
         newFC=100.0*u+dFC.get_targetFreq();                                                                  // Округление не нужно и добавление предудущего значения, умногжжение на 100 это перевод в 0.01 герцах
         pre_errPIDBoiler=errPIDBoiler;                                                               // Сохранние ошибки, теперь это прошлая ошибка                                                                           // запомнить предыдущую ошибку
       
       if (newFC>FC_MAX_FREQ_BOILER)   newFC=FC_MAX_FREQ_BOILER;                                                 // ограничение диапазона ОТДЕЛЬНО для ГВС!!!! (меньше мощность)
       if (newFC<FC_MIN_FREQ_BOILER)   newFC=FC_MIN_FREQ_BOILER; //return pCOMP_OFF;                             // Уменьшать дальше некуда, выключаем компрессор
       
       // Смотрим подход к границе защит если идет УВЕЛИЧЕНИЕ частоты
       if (dFC.get_targetFreq()<newFC)                                                                                     // Идет увеличение частоты проверяем подход к границам
         {
         if ((dFC.isfOnOff())&&(FEED>((Prof.Boiler.tempIn-FC_DT_TEMP_BOILER)*FC_PID_STOP/100)) )                                                   {Status.ret=pBp17; return pCOMP_NONE;}   // Подача ограничение
         if ((dFC.isfOnOff())&&(dFC.get_power()>(FC_MAX_POWER_BOILER*FC_PID_STOP/100)))                                                            {Status.ret=pBp18; return pCOMP_NONE;}   // Мощность для ГВС меньшая мощность
         if ((dFC.isfOnOff())&&(dFC.get_current()>(FC_MAX_CURRENT_BOILER*FC_PID_STOP/100)))                                                        {Status.ret=pBp19; return pCOMP_NONE;}   // ТОК для ГВС меньшая мощность
         if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>(sTemp[TCOMP].get_maxTemp()*FC_PID_STOP/100)))                           {Status.ret=pBp20; return pCOMP_NONE;}   // температура компрессора
         if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>((sADC[PCON].get_maxPress()-FC_DP_CON_PRESS)*FC_PID_STOP/100))) {Status.ret=pBp21; return pCOMP_NONE;}   // давление конденсатора до максимальной минус 0.5 бара
         }
       //    надо менять
       if (dFC.get_targetFreq()!=newFC)                                                                                     // Установка частоты если нужно менять
         {
         journal.jprintf((char*)STR_FREQUENCY,newFC/100.0); 
         dFC.set_targetFreq(newFC,false,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER);                                                 
         }
       return pCOMP_NONE;  
 }
   
}
// -----------------------------------------------------------------------------------------------------
// Итерация по управлению НАГРЕВ  пола
// выдает что надо делать компрессору, но ничем не управляетдля старт-стопа для инвертора меняет обороты
MODE_COMP HeatPump::UpdateHeat()
{
int16_t target,t1,targetRealPID;
float u, u_dif, u_int, u_pro;      
int16_t newFC;               //Новая частота инвертора

if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) return pCOMP_OFF;    // Если ТН выключен или выключается ничего не делаем

if ((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&((abs(FEED-RET)>Prof.Heat.dt)&&(COMPRESSOR_IS_ON)))   {set_Error(ERR_DTEMP_CON,(char*)__FUNCTION__);return pCOMP_NONE;}// Привышение разности температур кондесатора при включеноом компрессорае
#ifdef RTRV    
   if ((dRelay[RTRV].get_Relay())&&(COMPRESSOR_IS_ON))      dRelay[RTRV].set_OFF();  // отопление Проверить и если надо установить 4-ходовой клапан только если компрессор рабоатет (защита это лишнее)
#endif
Status.ret=pNone;                                                                                   // Сбросить состояние пида
switch (Prof.Heat.Rule)   // в зависмости от алгоритма
         {
          case pHYSTERESIS:  // Гистерезис нагрев.
               if (GETBIT(Prof.Heat.flags,fTarget)){ target=Prof.Heat.Temp2; t1=RET;}  else { target=Prof.Heat.Temp1; t1=sTemp[TIN].get_Temp();}  // вычислить темературы для сравнения Prof.Heat.Target 0-дом   1-обратка
                    if(t1>target)                  {Status.ret=pHh3;   return pCOMP_OFF;}                            // Достигнута целевая температура  ВЫКЛ
               else if((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&(FEED>Prof.Heat.tempIn)){Status.ret=pHh1;   return pCOMP_OFF;} // Достигнута максимальная температура подачи ВЫКЛ
                 else if(t1<target-Prof.Heat.dTemp) {Status.ret=pHh2;   return pCOMP_ON; }                           // Достигнут гистерезис ВКЛ
               else if(RET<Prof.Heat.tempOut)      {Status.ret=pHh13;  return pCOMP_ON; }                            // Достигнут минимальная темература обратки ВКЛ
               else                                {Status.ret=pHh4;   return pCOMP_NONE;}                           // Ничего не делаем  (сохраняем состояние)
              break;
          case pPID:   // ПИД регулирует подачу, а целевай функция гистререзис
            // отработка гистререзиса целевой функции (дом/обратка)
            if (GETBIT(Prof.Heat.flags,fTarget)) { target=Prof.Heat.Temp2; t1=RET;}  else { target=Prof.Heat.Temp1; t1=sTemp[TIN].get_Temp();} // вычислить темературы для сравнения Prof.Heat.Target 0-дом  1-обратка
            
             if(t1>target)             { Status.ret=pHp3; return pCOMP_OFF;}                            // Достигнута целевая температура  ВЫКЛ
             else if((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&(FEED>Prof.Heat.tempIn)) {Status.ret=pHp1; set_Error(ERR_PID_FEED,(char*)__FUNCTION__);return pCOMP_OFF;}        // Достижение максимальной температуры подачи - это ошибка ПИД не рабоатет
           //  else if ((t1<target-Prof.Heat.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pHp2; return pCOMP_ON; }                     // Достигнут гистерезис и компрессор еще не рабоатет ВКЛ
           //  else if ((t1<target-Prof.Heat.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pHp2; return pCOMP_ON; }                       // Достигнут гистерезис (компрессор не рабоатет) ВКЛ
           //  else if ((t1<target-Prof.Heat.dTemp)&&(dFC.isfOnOff())&&(dRelay[R3WAY].get_Relay())) {Status.ret=pHp2; return pCOMP_ON;} // Достигнут гистерезис (бойлер нагрет) ВКЛ
             else if ((t1<target-Prof.Heat.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pHp2; return pCOMP_ON; }                       // Достигнут гистерезис (компрессор не рабоатет) ВКЛ
             else if ((t1<target-Prof.Heat.dTemp)&&(dFC.isfOnOff())&&(relay3Way)) {Status.ret=pHp2; return pCOMP_ON;} // Достигнут гистерезис (бойлер нагрет) ВКЛ
             
             // ЗАЩИТА Компресор работает, достигнута максимальная температура подачи, мощность, температура компрессора или давление то уменьшить обороты на FC_STEP_FREQ
              else if ((dFC.isfOnOff())&&(FEED>Prof.Heat.tempIn-FC_DT_TEMP))                  // Подача ограничение
             { 
              Status.ret=pHp6;
              journal.jprintf("%s %.2f (FEED: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,FEED/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  return pCOMP_NONE;                     // Уменьшить частоту
             }  
  
            else if ((dFC.isfOnOff())&&(dFC.get_power()>FC_MAX_POWER))                    // Мощность в 100 ватт
             { 
              Status.ret=pHp7;
              journal.jprintf("%s %.2f (POWER: %.2f kW)\n",STR_REDUCED,FC_STEP_FREQ/100.0,dFC.get_power()/10.0); // КИЛОВАТЫ
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  return pCOMP_NONE;                     // Уменьшить частоту
             } 
            else if ((dFC.isfOnOff())&&(dFC.get_current()>FC_MAX_CURRENT))                // ТОК
             { 
              Status.ret=pHp16;
              journal.jprintf("%s %.2f (CURRENT: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,dFC.get_current()/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  return pCOMP_NONE;                     // Уменьшить частоту
             } 
           
           else if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>sTemp[TCOMP].get_maxTemp()))  // температура компрессора
             { 
              Status.ret=pHp8;
              journal.jprintf("%s %.2f (TCOMP: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,sTemp[TCOMP].get_Temp()/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  return pCOMP_NONE;                     // Уменьшить частоту
             } 
          else if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>sADC[PCON].get_maxPress()-FC_DP_CON_PRESS))  // давление конденсатора до максимальной минус 0.5 бара
           {
            Status.ret=pHp9; 
            journal.jprintf("%s %.2f (PCON:  %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,sADC[PCON].get_Press()/100.0);
            if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
            dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  return pCOMP_NONE;               // Уменьшить частоту
           }           
           else if(!(dFC.isfOnOff())) {Status.ret=pHp5; return pCOMP_NONE;  }                                               // Если компрессор не рабоатет то ничего не делаем и выходим
           else if (rtcSAM3X8.unixtime()-dFC.get_startTime()<FC_ACCEL_TIME/100 ){ Status.ret=pHp10; return pCOMP_NONE;}  // РАЗГОН частоту не трогаем

           #ifdef SUPERBOILER                                            // Бойлер греется от предкондесатора
             // if (sTemp[TCOMP].get_Temp()+SUPERBOILER_DT>sTemp[TBOILER].get_Temp())  switch3WAY(true);   else switch3WAY(false);
               if (sTemp[TCOMP].get_Temp()-SUPERBOILER_DT>sTemp[TBOILER].get_Temp())  dRelay[RSUPERBOILER].set_ON(); else dRelay[RSUPERBOILER].set_OFF();// исправил плюс на минус
               if(xTaskGetTickCount()/1000-updatePidTime<HP.get_timeHeat())         { Status.ret=pHp11;   return pCOMP_NONE;}   // время обновления ПИДа еше не пришло
               if (relay3Way) Status.ret=pHp15; else Status.ret=pHp12;                                          // если нужно показывем что бойлер греется от предкондесатора
           #else
               else if(xTaskGetTickCount()/1000-updatePidTime<HP.get_timeHeat())    { Status.ret=pHp11;   return pCOMP_NONE;}   // время обновления ПИДа еше не пришло
               Status.ret=pHp12;   // Дошли до сюда - ПИД на подачу. Компресор работает
           #endif      
                   
            updatePidTime=xTaskGetTickCount()/1000; 
           // Уравнение ПИД регулятора в конечных разностях. ------------------------------------
           // Cp, Ci, Cd – коэффициенты дискретного ПИД регулятора;
           // u(t) = P (t) + I (t) + D (t);
           // P (t) = Kp * e (t);
           // I (t) = I (t — 1) + Ki * e (t);
           // D (t) = Kd * {e (t) — e (t — 1)};
           // T – период дискретизации(период, с которым вызывается ПИД регулятор).
           
           if(GETBIT(Prof.Heat.flags,fWeather))  // включена погодозависимость
           { 
            targetRealPID=Prof.Heat.tempPID+(Prof.Heat.kWeather*(TEMP_WEATHER-sTemp[TOUT].get_Temp())/1000);  // включена погодозависимость, коэффициент в ТЫСЯЧНЫХ результат в сотых градуса
  //          journal.jprintf("targetRealPID=%d \n",targetRealPID); 
  //          journal.jprintf("Prof.Heat.tempPID=%d \n",Prof.Heat.tempPID); 
  //          journal.jprintf("Prof.Heat.kWeather=%d \n",Prof.Heat.kWeather); 
  //          journal.jprintf("TEMP_WEATHER=%d \n",TEMP_WEATHER); 
  //          journal.jprintf("sTemp[TOUT].get_Temp()=%d \n",sTemp[TOUT].get_Temp()); 
            if (targetRealPID>Prof.Heat.tempIn-50) targetRealPID=Prof.Heat.tempIn-50;  // ограничение целевой подачи = максимальная подача - 0.5 градуса
            if (targetRealPID<MIN_WEATHER) targetRealPID=MIN_WEATHER;                 // 12 градусов
            if (targetRealPID>MAX_WEATHER) targetRealPID=MAX_WEATHER;                 // 42 градусов
           }
           else targetRealPID=Prof.Heat.tempPID;                                                        // отключена погодозависмость
           
           errPID=((float)(targetRealPID-FEED))/100.0;                                                  // Текущая ошибка, переводим в градусы
          // Расчет отдельных компонент
           if (Prof.Heat.Ki>0)                                                                           // Расчет интегральной составляющей
           {
            temp_int=temp_int+((float)Prof.Heat.Ki*errPID)/100.0;                                        // Интегральная составляющая, с накоплением делить на 10
            #define HEAT_MAX_STEP  5                                                                     // Ограничение диапзона изменения 5 герц за один шаг ПИД
            if (temp_int>HEAT_MAX_STEP)  temp_int=HEAT_MAX_STEP; 
            if (temp_int<-1.0*HEAT_MAX_STEP)  temp_int=-1.0*HEAT_MAX_STEP; 
           }
           else temp_int=0;                                                                             // если Кi равен 0 то интегрирование не используем
           u_int=temp_int;
          
           // Дифференцальная составляющая
           u_dif=((float)Prof.Heat.Kd*(errPID-pre_errPID))/10.0;                                        // Положительная составляющая - ошибка растет (воздействие надо увеличиить)  Отрицательная составляющая - ошибка уменьшается (воздействие надо уменьшить)
           
           // Пропорциональная составляющая (десятые)
           u_pro=(float)Prof.Heat.Kp*errPID/10.0;
           
           // Общее воздействие
           u=u_pro+u_int+u_dif;
           if (u>FC_PID_FREQ_STEP/100.0) u=FC_PID_FREQ_STEP/100.0;                                    // Ограничить увеличение частоты на FC_PID_FREQ_STEP гц
       
           newFC=100.0*u+dFC.get_targetFreq();                                                                  // Округление не нужно и добавление предудущего значения, умногжжение на 100 это перевод в 0.01 герцах
           pre_errPID=errPID;                                                                           // Сохранние ошибки, теперь это прошлая ошибка

/*           
           errPID=((float)(targetRealPID-FEED))/100.0;                                                   // Текущая ошибка, переводим в градусы
           if (Prof.Heat.Ki>0) temp_int=temp_int+abs((1/(float)Prof.Heat.Ki)*errPID);                             // Интегральная составляющая, с накоплением
           else temp_int=0;                                                                             // если Кi равен 0 то интегрирование не используем
           if (errPID<0) work_int=-1.0*temp_int; else work_int=temp_int;                                // Определение знака (перегрев меньше цели знак минус)
           // Корректировка Kp
           u=100.0*(float)Prof.Heat.Kp*(errPID+work_int+((float)Prof.Heat.Kd*(errPID-pre_errPID)/1.0))/1.0;       // Сложение всех трех компонент
           newFC=round(u)+dFC.get_targetFreq();                                                                  // Округление и добавление предудущего значения
           pre_errPID=errPID;                                                                            // Сохранние ошибки, теперь это прошлая ошибка
*/           
           if (newFC>FC_MAX_FREQ)   newFC=FC_MAX_FREQ;                                                // ограничение диапазона
           if (newFC<FC_MIN_FREQ)   newFC=FC_MIN_FREQ;

           // Смотрим подход к границе защит если идет УВЕЛИЧЕНИЕ частоты
           if (dFC.get_targetFreq()<newFC)                                                                                     // Идет увеличение частоты проверяем подход к границам
             {
             if ((dFC.isfOnOff())&&(FEED>((Prof.Boiler.tempIn-FC_DT_TEMP)*FC_PID_STOP/100)))                                                           {Status.ret=pHp17; return pCOMP_NONE;}   // Подача ограничение
             if ((dFC.isfOnOff())&&(dFC.get_power()>(FC_MAX_POWER*FC_PID_STOP/100)))                                                                   {Status.ret=pHp18; return pCOMP_NONE;}   // Мощность для ГВС меньшая мощность
             if ((dFC.isfOnOff())&&(dFC.get_current()>(FC_MAX_CURRENT*FC_PID_STOP/100)))                                                               {Status.ret=pHp19; return pCOMP_NONE;}   // ТОК для ГВС меньшая мощность
             if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>(sTemp[TCOMP].get_maxTemp()*FC_PID_STOP/100)))                           {Status.ret=pHp20; return pCOMP_NONE;}   // температура компрессора
             if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>((sADC[PCON].get_maxPress()-FC_DP_CON_PRESS)*FC_PID_STOP/100))) {Status.ret=pHp21; return pCOMP_NONE;}   // давление конденсатора до максимальной минус 0.5 бара
             }
            //    надо менять
           if (dFC.get_targetFreq()!=newFC)                                                                     // Установкка частоты если нужно менять
             {
             journal.jprintf((char*)STR_FREQUENCY,newFC/100.0); 
             dFC.set_targetFreq(newFC,false,FC_MIN_FREQ,FC_MAX_FREQ);                                                 
             }
           return pCOMP_NONE;                                                                            // компрессор состояние не меняет
           break;
           case pHYBRID:     
           break; 
           default: break;
         } 
 return pCOMP_NONE;
 }

// -----------------------------------------------------------------------------------------------------
// Итерация по управлению ОХЛАЖДЕНИЕ
// выдает что надо делать компрессору, но ничем не управляет
MODE_COMP HeatPump::UpdateCool()
{
int16_t target,t1,targetRealPID;
float u, u_dif, u_int, u_pro;       
int16_t newFC;               //Новая частота инвертора

if ((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) return pCOMP_OFF;    // Если ТН выключен или выключается ничего не делаем

if ((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&((abs(FEED-RET)>Prof.Cool.dt)&&(COMPRESSOR_IS_ON)))   {set_Error(ERR_DTEMP_CON,(char*)__FUNCTION__);return pCOMP_NONE;}// Привышение разности температур кондесатора при включеноом компрессорае
#ifdef RTRV
  if ((!dRelay[RTRV].get_Relay())&&(COMPRESSOR_IS_ON))  dRelay[RTRV].set_ON();                                        // Охлаждение Проверить и если надо установить 4-ходовой клапан только если компрессор рабоатет (защита это лишнее)
#endif
 Status.ret=pNone;                                                                                   // Сбросить состояние пида
switch (Prof.Cool.Rule)   // в зависмости от алгоритма
         {
          case pHYSTERESIS:  // Гистерезис охлаждение.
               if (GETBIT(Prof.Cool.flags,fTarget)) { target=Prof.Cool.Temp2; t1=RET;}  else { target=Prof.Cool.Temp1; t1=sTemp[TIN].get_Temp();}  // вычислить темературы для сравнения Prof.Heat.Target 0-дом   1-обратка
                    if(t1<target)             {Status.ret=pCh3;   return pCOMP_OFF;}                            // Достигнута целевая температура  ВЫКЛ
               else if((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&(FEED<Prof.Cool.tempIn)){Status.ret=pCh1;return pCOMP_OFF;}// Достигнута минимальная температура подачи ВЫКЛ
               else if(t1>target+Prof.Cool.dTemp)  {Status.ret=pCh2;   return pCOMP_ON; }                       // Достигнут гистерезис ВКЛ
               else if(RET>Prof.Cool.tempOut)      {Status.ret=pCh13;  return pCOMP_ON; }                       // Достигнут Максимальная темература обратки ВКЛ
               else  {Status.ret=pCh4;    return pCOMP_NONE;   }                                                // Ничего не делаем  (сохраняем состояние)
              break;
          case pPID:   // ПИД регулирует подачу, а целевай функция гистререзис
            // отработка гистререзиса целевой функции (дом/обратка)
            if (GETBIT(Prof.Cool.flags,fTarget)) { target=Prof.Cool.Temp2; t1=RET;}  else { target=Prof.Cool.Temp1; t1=sTemp[TIN].get_Temp();} // вычислить темературы для сравнения Prof.Heat.Target 0-дом  1-обратка
            
             if(t1<target)                     { Status.ret=pCp3; return pCOMP_OFF;}                            // Достигнута целевая температура  ВЫКЛ
             else if ((rtcSAM3X8.unixtime()-offBoiler>DELAY_BOILER_OFF)&&(FEED<Prof.Cool.tempIn)) {Status.ret=pCp1; set_Error(ERR_PID_FEED,(char*)__FUNCTION__);return pCOMP_OFF;}         // Достижение минимальной температуры подачи - это ошибка ПИД не рабоатет
           //  else if ((t1<target-Prof.Cool.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pCp2; return pCOMP_ON; }                        // Достигнут гистерезис и компрессор еще не рабоатет ВКЛ
//             else if ((t1>target+Prof.Cool.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pCp2; return pCOMP_ON; }                          // Достигнут гистерезис (компрессор не рабоатет) ВКЛ
//             else if ((t1>target+Prof.Cool.dTemp)&&(dFC.isfOnOff())&&(dRelay[R3WAY].get_Relay())) {Status.ret=pCp2; return pCOMP_ON;}  // Достигнут гистерезис (бойлер нагрет) ВКЛ
             else if ((t1>target+Prof.Cool.dTemp)&&(!(dFC.isfOnOff())))  {Status.ret=pCp2; return pCOMP_ON; }                          // Достигнут гистерезис (компрессор не рабоатет) ВКЛ
             else if ((t1>target+Prof.Cool.dTemp)&&(dFC.isfOnOff())&&(relay3Way)) {Status.ret=pCp2; return pCOMP_ON;}  // Достигнут гистерезис (бойлер нагрет) ВКЛ
             
             // ЗАЩИТА Компресор работает, достигнута минимальная температура подачи, мощность, температура компрессора или давление то уменьшить обороты на FC_STEP_FREQ
              else if ((dFC.isfOnOff())&&(FEED<Prof.Cool.tempIn+FC_DT_TEMP))                  // Подача
             { 
              Status.ret=pCp6;
              journal.jprintf("%s %.2f (FEED: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,FEED/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ_COOL)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);  return pCOMP_NONE;               // Уменьшить частоту
             }  
  
            else if ((dFC.isfOnOff())&&(dFC.get_power()>FC_MAX_POWER))                    // Мощность
             { 
              Status.ret=pCp7;
              journal.jprintf("%s %.2f (POWER: %.2f kW)\n",STR_REDUCED,FC_STEP_FREQ/100.0,dFC.get_power()/10.0); // КИЛОВАТЫ
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ_COOL)  return pCOMP_OFF;           // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);  return pCOMP_NONE;               // Уменьшить частоту
             } 
             else if ((dFC.isfOnOff())&&(dFC.get_current()>FC_MAX_CURRENT))                    // ТОК
             { 
              Status.ret=pCp16;
              journal.jprintf("%s %.2f (CURRENT: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,dFC.get_current()/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ_COOL)  return pCOMP_OFF;           // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);  return pCOMP_NONE;               // Уменьшить частоту
             } 
           else if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>sTemp[TCOMP].get_maxTemp()))  // температура компрессора
             { 
              Status.ret=pCp8;
              journal.jprintf("%s %.2f (TCOMP: %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,sTemp[TCOMP].get_Temp()/100.0);
              if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ_COOL)  return pCOMP_OFF;                // Уменьшать дальше некуда, выключаем компрессор
              dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);  return pCOMP_NONE;               // Уменьшить частоту
             } 
          else if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>sADC[PCON].get_maxPress()-FC_DP_CON_PRESS))  // давление конденсатора до максимальной минус 0.5 бара
           {
            Status.ret=pCp9; 
            journal.jprintf("%s %.2f (PCON:  %.2f)\n",STR_REDUCED,FC_STEP_FREQ/100.0,sADC[PCON].get_Press()/100.0);
            if (dFC.get_targetFreq()-FC_STEP_FREQ<FC_MIN_FREQ_COOL)  return pCOMP_OFF;           // Уменьшать дальше некуда, выключаем компрессор
            dFC.set_targetFreq(dFC.get_targetFreq()-FC_STEP_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);  return pCOMP_NONE;               // Уменьшить частоту
           }           
           else if(!(dFC.isfOnOff())) {Status.ret=pCp5; return pCOMP_NONE;  }                                               // Если компрессор не рабоатет то ничего не делаем и выходим
           else if (rtcSAM3X8.unixtime()-dFC.get_startTime()<FC_ACCEL_TIME/100 ){ Status.ret=pCp10; return pCOMP_NONE;}  // РАЗГОН частоту не трогаем

           #ifdef SUPERBOILER                                            // Бойлер греется от предкондесатора
             if (sTemp[TCOMP].get_Temp()+SUPERBOILER_DT>sTemp[TBOILER].get_Temp())  dRelay[RSUPERBOILER].set_ON(); else dRelay[RSUPERBOILER].set_OFF();
            // if (sTemp[TCOMP].get_Temp()+SUPERBOILER_DT>sTemp[TBOILER].get_Temp())  switch3WAY(true);   else switch3WAY(false);
             if(xTaskGetTickCount()/1000-updatePidTime<HP.get_timeHeat())         { Status.ret=pCp11;   return pCOMP_NONE;}   // время обновления ПИДа еше не пришло
             if (relay3Way) Status.ret=pCp15; else Status.ret=pCp12;                                          // если нужно показывем что бойлер греется от предкондесатора
           #else
              else if(xTaskGetTickCount()/1000-updatePidTime<HP.get_timeHeat())    { Status.ret=pCp11;   return pCOMP_NONE;}   // время обновления ПИДа еше не пришло
               Status.ret=pCp12;   // Дошли до сюда - ПИД на подачу. Компресор работает
           #endif      
                   
            updatePidTime=xTaskGetTickCount()/1000; 
            Serial.println("------ PID ------");
           // Уравнение ПИД регулятора в конечных разностях. ------------------------------------
           // Cp, Ci, Cd – коэффициенты дискретного ПИД регулятора;
           // u(t) = P (t) + I (t) + D (t);
           // P (t) = Kp * e (t);
           // I (t) = I (t — 1) + Ki * e (t);
           // D (t) = Kd * {e (t) — e (t — 1)};
           // T – период дискретизации(период, с которым вызывается ПИД регулятор).
           if(GETBIT(Prof.Cool.flags,fWeather))  // включена погодозависимость
           { 
            targetRealPID=Prof.Cool.tempPID-(Prof.Cool.kWeather*(TEMP_WEATHER-sTemp[TOUT].get_Temp())/1000);  // включена погодозависимость
            if (targetRealPID<Prof.Cool.tempIn+50) targetRealPID=Prof.Cool.tempIn+50;                          // ограничение целевой подачи = минимальная подача + 0.5 градуса
            if (targetRealPID<MIN_WEATHER) targetRealPID=MIN_WEATHER;                                         // границы диапазона
            if (targetRealPID>MAX_WEATHER) targetRealPID=MAX_WEATHER;                                         // 
           }
           else targetRealPID=Prof.Cool.tempPID;                                                             // отключена погодозависмость
       
           errPID=((float)(FEED-targetRealPID))/100.0;                                                // Текущая ошибка, переводим в градусы ПОДАЧА Охлаждение - ошибка на оборот
           
           // Это охлаждение
        //     errPID=((float)(targetRealPID-RET))/100.0;     // Обратка поменялась
        //     journal.jprintf("RET=%.2f\n",RET/100.0);
        //     journal.jprintf("FEED=%.2f\n",FEED/100.0);
        //     journal.jprintf("errPID=%.2f\n",errPID);
       
          // Расчет отдельных компонент
           if (Prof.Cool.Ki>0)                                                                           // Расчет интегральной составляющей
           {
            temp_int=temp_int+((float)Prof.Cool.Ki*errPID)/100.0;                                        // Интегральная составляющая, с накоплением делить на 10
            #define COOL_MAX_STEP  5                                                                     // Ограничение диапзона изменения 5 герц за один шаг ПИД
            if (temp_int>COOL_MAX_STEP)  temp_int=COOL_MAX_STEP; 
            if (temp_int<-1.0*COOL_MAX_STEP)  temp_int=-1.0*COOL_MAX_STEP; 
           }
           else temp_int=0;                                                                             // если Кi равен 0 то интегрирование не используем
           u_int=temp_int;
          
           // Дифференцальная составляющая
           u_dif=((float)Prof.Cool.Kd*(errPID-pre_errPID))/10.0;                                        // Положительная составляющая - ошибка растет (воздействие надо увеличиить)  Отрицательная составляющая - ошибка уменьшается (воздействие надо уменьшить)
           
           // Пропорциональная составляющая (десятые)
           u_pro=(float)Prof.Cool.Kp*errPID/10.0;
           
           // Общее воздействие
           u=u_pro+u_int+u_dif;
           if (u>FC_PID_FREQ_STEP/100.0) u=FC_PID_FREQ_STEP/100.0;                                    // Ограничить увеличение частоты на FC_PID_FREQ_STEP гц
  
           newFC=100.0*u+dFC.get_targetFreq();                                                                  // Округление не нужно и добавление предудущего значения, умногжжение на 100 это перевод в 0.01 герцах
           pre_errPID=errPID;                                                                           // Сохранние ошибки, теперь это прошлая ошибка

          
           if (newFC>FC_MAX_FREQ_COOL)   newFC=FC_MAX_FREQ_COOL;                                       // ограничение диапазона
           if (newFC<FC_MIN_FREQ_COOL)   newFC=FC_MIN_FREQ_COOL; // return pCOMP_OFF;                                              // Уменьшать дальше некуда, выключаем компрессор// newFC=FC_MIN_FREQ;
      
       //    journal.jprintf("newFC=%.2f\n",newFC/100.0);
      
           // Смотрим подход к границе защит если идет УВЕЛИЧЕНИЕ частоты
           if (dFC.get_targetFreq()<newFC)                                                                                     // Идет увеличение частоты проверяем подход к границам
             {
             if ((dFC.isfOnOff())&&(FEED<((Prof.Boiler.tempIn-FC_DT_TEMP)*FC_PID_STOP/100)))                                                           {Status.ret=pCp17; return pCOMP_NONE;}   // Подача ограничение
             if ((dFC.isfOnOff())&&(dFC.get_power()>(FC_MAX_POWER*FC_PID_STOP/100)))                                                                   {Status.ret=pCp18; return pCOMP_NONE;}   // Мощность для ГВС меньшая мощность
             if ((dFC.isfOnOff())&&(dFC.get_current()>(FC_MAX_CURRENT*FC_PID_STOP/100)))                                                               {Status.ret=pCp19; return pCOMP_NONE;}   // ТОК для ГВС меньшая мощность
             if ((dFC.isfOnOff())&&((sTemp[TCOMP].get_Temp()+FC_DT_COMP_TEMP)>(sTemp[TCOMP].get_maxTemp()*FC_PID_STOP/100)))                           {Status.ret=pCp20; return pCOMP_NONE;}   // температура компрессора
             if ((dFC.isfOnOff())&&(sADC[PCON].get_present())&&(sADC[PCON].get_Press()>((sADC[PCON].get_maxPress()-FC_DP_CON_PRESS)*FC_PID_STOP/100))) {Status.ret=pCp21; return pCOMP_NONE;}    // давление конденсатора до максимальной минус 0.5 бара
             }
            //    надо менять
           
           if (dFC.get_targetFreq()!=newFC)                                                                     // Установкка частоты если нужно менять
             {
             journal.jprintf((char*)STR_FREQUENCY,newFC/100.0); 
             dFC.set_targetFreq(newFC,false,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);                                                 
             }
           return pCOMP_NONE;                                                                            // компрессор состояние не меняет
           break;
           case pHYBRID:     
           break; 
           default: break;
         } 
 return pCOMP_NONE;
         
}
// Установить КОД состояния и вывести его в консоль о текущем состоянии + пауза t сек
void HeatPump::setStatusRet(TYPE_RET_HP ret,uint32_t t)
{
     Status.ret=ret;   // Отрабаываем паузу между включенимами
     journal.printf( "Status:%d[%s]\n",(int)Status.modWork,codeRet[Status.ret]);
     _delay(t*1000); // Выводим сообщение раз в 10 сек
}

// Концигурация 4-х, 3-х ходового крана и включение насосов, тен бойлера, тен отопления
// В зависммости от входного параметра конфигурирует краны
void HeatPump::configHP(MODE_HP conf)
{
    // 1. Обеспечение минимальной паузы компрессора, если мало времени то не конфигурируем  и срузу выходим!!
   if (!(COMPRESSOR_IS_ON))    // Только при неработающем компрессоре
   {
    #ifdef DEMO
    if (rtcSAM3X8.unixtime()-stopCompressor<10) {setStatusRet(pMinPauseOn,10);return;  }           // Обеспечение паузы компрессора Хранится в секундах!!! ТЕСТИРОВАНИЕ
    #else
        switch ((int)conf)   // Обеспечение паузы компрессора Хранится в секундах!!!
        {
            case  pCOOL:   if (rtcSAM3X8.unixtime()-stopCompressor<Prof.Cool.pause)  {setStatusRet(pMinPauseOn,10); return;}   break;  
            case  pHEAT:   if (rtcSAM3X8.unixtime()-stopCompressor<Prof.Heat.pause)  {setStatusRet(pMinPauseOn,10); return;}   break; 
            case  pBOILER: if (rtcSAM3X8.unixtime()-stopCompressor<Prof.Boiler.pause){setStatusRet(pMinPauseOn,10); return;}   break;  
            case  pOFF:    
            case  pNONE_C:
            case  pNONE_H:
            case  pNONE_B:
            default:  break;
         }     
    #endif
   }
   
     // 2. Конфигурация в нужный режим
     #ifdef RBOILER // Если надо выключить тен бойлера (если его нет в настройках)
       if((!GETBIT(Prof.Boiler.flags,fAddHeating))&&(!GETBIT(Option.flags,fAddBoiler))) dRelay[RBOILER].set_OFF();   // Выключить ТЭН бойлера если настройки соответсвуют
     #endif   
     switch ((int)conf)
    {
      case  pOFF: // Выключить - установить положение как при включении
                 // перевод 4-х ходового производится после отключения компрессора (см compressorOFF())
                 relay3Way=false;
                 #ifdef R3WAY
                 dRelay[R3WAY].set_OFF();
                 #else
                 // замена трехходового
                 #endif                  
                 _delay(10*1000);                        // Задержка на 10 сек
                 #ifdef SUPERBOILER                                             // Бойлер греется от предкондесатора
                     dRelay[RSUPERBOILER].set_OFF();                              // Евгений добавил выключить супербойлер
                 #endif
                 #ifdef RBOILER
                     if((GETBIT(Prof.Boiler.flags,fAddHeating))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF();   // Выключить ТЭН бойлера
                 //    if (dRelay[RBOILER].get_present()) dRelay[RBOILER].set_OFF(); // Выключить ТЭН бойлера
                 #endif
                 #ifdef RHEAT
                     if (dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
                 #endif 
                 PUMPS_OFF;
                break;    
      case  pHEAT:    // Отопление
                 PUMPS_ON;                                                     // включить насосы
                 _delay(2*1000);                        // Задержка на 2 сек
                  
                 #ifdef RTRV
                  if ((COMPRESSOR_IS_ON)&&(dRelay[RTRV].get_Relay()==true)) ChangesPauseTRV();    // Компрессор рабатает и 4-х ходовой стоит на холоде то хитро переключаем 4-х ходовой в положение тепло
                 dRelay[RTRV].set_OFF();                                        // нагрев
                 _delay(2*1000);                        // Задержка на 2 сек
                 #endif

                 switch3WAY(false);                                            // выключить бойлер это лишнее наверное переключение идет в get_Work() но пусть будет
                 
                 #ifdef SUPERBOILER                                            // Бойлер греется от предкондесатора
                   dRelay[RSUPERBOILER].set_OFF();                             // Евгений добавил выключить супербойлер
                 #endif
                 #ifdef RBOILER
                    if((GETBIT(Prof.Boiler.flags,fAddHeating))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF(); // Выключить ТЭН бойлера (режим форсированного нагрева)
                 #endif
                 #ifdef RHEAT
              //     if((GETBIT(Option.flags,fAddHeat))&&(dRelay[RHEAT].get_present())) dRelay[RHEAT].set_ON(); else dRelay[RHEAT].set_OFF(); // Если надо включить ТЭН отопления
                 #endif
                 if (!(COMPRESSOR_IS_ON))  dFC.set_targetFreq(FC_START_FREQ,true,FC_MIN_FREQ,FC_MAX_FREQ);  // установить стартовую частоту если компрессор выключен
                break;    
      case  pCOOL:    // Охлаждение
                 PUMPS_ON;                                                     // включить насосы
                 _delay(2*1000);                        // Задержка на 2 сек

                 #ifdef RTRV
                 if ((COMPRESSOR_IS_ON)&&(dRelay[RTRV].get_Relay()==false)) ChangesPauseTRV();    // Компрессор рабатает и 4-х ходовой стоит на тепле то хитро переключаем 4-х ходовой в положение холод
                 dRelay[RTRV].set_ON();                                       // охлаждение
                 _delay(2*1000);                        // Задержка на 2 сек
                 #endif 

                  switch3WAY(false);                                           // выключить бойлер
                 #ifdef RBOILER
                  if((GETBIT(Prof.Boiler.flags,fAddHeating))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_OFF(); // Выключить ТЭН бойлера (режим форсированного нагрева)
                 #endif
                 #ifdef RHEAT
                 if (dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
                 #endif 
                 if (!(COMPRESSOR_IS_ON))   dFC.set_targetFreq(FC_START_FREQ,true,FC_MIN_FREQ_COOL,FC_MAX_FREQ_COOL);   // установить стартовую частоту
                break;
       case  pBOILER:   // Бойлер
                 #ifdef SUPERBOILER                                            // Бойлер греется от предкондесатора
                    dRelay[PUMP_IN].set_ON();                                  // Реле включения насоса входного контура  (геоконтур)
                    dRelay[PUMP_OUT].set_OFF();                                // Евгений добавил
                    dRelay[RSUPERBOILER].set_ON();                             // Евгений добавил
                    _delay(2*1000);                     // Задержка на 2 сек
                    if (Status.ret<pBp5) dFC.set_targetFreq(FC_START_FREQ,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER);      // В режиме супер бойлер установить частоту SUPERBOILER_FC если не дошли до пида
                 #else  
                    PUMPS_ON; 
                    // включить насосы
                    if (Status.ret<pBp5) dFC.set_targetFreq(FC_START_FREQ_BOILER,true,FC_MIN_FREQ_BOILER,FC_MAX_FREQ_BOILER);// установить стартовую частоту
                 #endif
                 _delay(2*1000);                        // Задержка на 2 сек

                 #ifdef RTRV
                 if ((COMPRESSOR_IS_ON)&&(dRelay[RTRV].get_Relay()==true)) ChangesPauseTRV();    // Компрессор рабатает и 4-х ходовой стоит на холоде то хитро переключаем 4-х ходовой в положение тепло
                 dRelay[RTRV].set_OFF();                                        // нагрев
                 _delay(2*1000);                        // Задержка на 2 сек
                 #endif
                 
                 switch3WAY(true);                                             // включить бойлер
                 #ifdef RBOILER
                 if((GETBIT(Prof.Boiler.flags,fAddHeating))&&(dRelay[RBOILER].get_present())) dRelay[RBOILER].set_ON();  // Если надо включить ТЭН бойлера (режим форсированного нагрева)
                 #endif
                 #ifdef RHEAT
                 if (dRelay[RHEAT].get_present()) dRelay[RHEAT].set_OFF();     // Выключить ТЭН отопления
                 #endif 
                break;   
         case  pNONE_H:  // Продолжаем греть отопление
         case  pNONE_C:  // Продолжаем охлаждение
         case  pNONE_B:  // Продолжаем греть бойлер
               break;    // конфигурировать ничего не надо, продолжаем движение
         default:  set_Error(ERR_CONFIG,(char*)__FUNCTION__);  break;   // Ошибка!  что то пошло не так
    }
 }

// "Интелектуальная пауза" для перекидывания на "ходу" 4-х ходового
// фактически останов компрессора и обезпечени нужных пауз, компрессор включается далее в vUpdate()
void HeatPump::ChangesPauseTRV()
{
  journal.jprintf("ChangesPauseTRV\n");
  #ifdef EEV_DEF
  dEEV.Pause();                                                    // Поставить на паузу задачу Обновления ЭРВ
  journal.jprintf(" Pause task update EEV\n"); 
  #endif
  COMPRESSOR_OFF;                                                  //  Компрессор выключить
  stopCompressor=rtcSAM3X8.unixtime();                             // Запомнить время выключения компрессора
  #ifdef REVI
   checkEVI();                                                      // выключить ЭВИ
  #endif
    journal.jprintf(" Pause for pressure equalization . . .\n");
  _delay(DELAY_TRV*1000);                   // Пауза 120 секунд для выравнивания давлений
  #ifdef EEV_DEF
  lastEEV=EEV_START;                                               // Выставление ЭРВ на стартовую позицию т.к идет смена режима тепло-холод
  #endif
}
// UPDATE --------------------------------------------------------------------------------
// Итерация по управлению всем ТН   для всего, основной цикл управления.
void HeatPump::vUpdate()
{
     #ifdef FLOW_CONTROL    // если надо проверяем потоки (защита от отказа насосов) ERR_MIN_FLOW
      uint8_t i;  
      if ((COMPRESSOR_IS_ON))                                                                                                                      // Только если компрессор включен
      for(i=0;i<FNUMBER;i++)   // Проверка потока по каждому датчику
          if (sFrequency[i].get_Value()<HP.sFrequency[i].get_minValue())   {set_Error(ERR_MIN_FLOW,(char*)sFrequency[i].get_name());  return; }    // Поток меньше минимального ошибка осанавливаем ТН
     #endif

     #ifdef SEVA  //Если определен лепестковый датчик протока - это переливная схема ТН - надо контролировать проток при работе
       if(dRelay[RPUMPI].get_Relay())                                                                                             // Только если включен насос геоконтура  (PUMP_IN)
       if (sInput[SEVA].get_Input()==SEVA_OFF)  {set_Error(ERR_SEVA_FLOW,(char*)"SEVA");  return; }                               // Выход по ошибке отсутствия протока
     #endif
     
     if((get_State()==pOFF_HP)||(get_State()==pSTARTING_HP)||(get_State()==pSTOPING_HP)) return; // ТН выключен или включается или выключается выходим  ничего не делаем!!!
  
     // Различные проверки на ошибки и защиты
      if ((Prof.SaveON.mode==pOFF)&&(!(GETBIT(Prof.SaveON.flags,fBoilerON))))          {set_Error(ERR_NO_WORK,(char*)__FUNCTION__);  return; } // Нет работы для ТН - ничего не включено, пользователь может изменить в процессе работы
      #ifdef EEV_DEF
      if ((!sADC[PEVA].get_present())&&(dEEV.get_ruleEEV()==TEVAOUT_PEVA))             {set_Error(ERR_PEVA_EEV,dEEV.get_name());     return; } //  Отсутвует датчик давления, и выбран алгоритм ЭРВ который его использует", пользователь может изменить в процессе работы
      #endif
            
      #ifdef REVI
      if (dRelay[REVI].get_present()) checkEVI() ;                           // Проверить необходимость включения ЭВИ
      #endif
      updateCount();                                                         // Обновить счетчики моточасов
      
      #ifdef DEFROST 
         defrost();                                                          // Разморозка только для воздушных ТН
      #endif
                                                                             // Собственно итерация
      {
        
          Status.modWork=get_Work();                                         // определяем что делаем
          save_DumpJournal(false);                                           // Строка состояния

         //  определяем что делаем с компрессором
          switch ((int)Status.modWork)
          {
              case  pOFF: if (COMPRESSOR_IS_ON){  // ЕСЛИ компрессор рабоатет то выключить компрессор,и затем сконфигурировать 3 и 4-х клапаны и включаем насосы
                             compressorOFF();
                             configHP(Status.modWork); 
                             if(!startPump)                                   // запустить задачу насос
                             {
                               startPump=true;                                 // Поставить признак запуска задачи насос
                               vTaskResume(xHandleUpdatePump);                 // Запустить задачу насос
                               journal.jprintf(" %s: Task vUpdatePump RPUMPO on . . .\n",(char*)__FUNCTION__);     // Включить задачу насос кондесатора выключение в переключении насосов
                             }
                          } break;
              case  pHEAT:   
              case  pCOOL:  
              case  pBOILER: // Включаем задачу насос, конфигурируем 3 и 4-х клапаны включаем насосы и потом включить компрессор
							 if (startPump)                                         // Остановить задачу насос
							 {
								startPump=false;                                     // Поставить признак останова задачи насос
								vTaskSuspend(xHandleUpdatePump);                     // Остановить задачу насос
								journal.jprintf(" %s: Task vUpdatePump RPUMPO pause  . . .\n",(char*)__FUNCTION__);
							 }
                             configHP(Status.modWork);                                 // Конфигурируем насосы
                             compressorON(Status.modWork);                             // Включаем компрессор
                             break;
              case  pNONE_H:
              case  pNONE_C:
              case  pNONE_B: break;                                    // компрессор уже включен
              default:  set_Error(ERR_CONFIG,(char*)__FUNCTION__); break;
           }
      }    
}


// Попытка включить компрессор  с учетом всех защит КОНФИГУРАЦИЯ уже установлена
// Вход режим работы ТН
// Возможно компрессор уже включен и происходит только смена режима
const char *EEV_go={" EEV go "};  // экономим место
const char *MinPauseOffCompressor={" Wait %d sec min pause off compressor . . .\n"};  // экономим место
void HeatPump::compressorON(MODE_HP mod)
{
  uint32_t nTime=rtcSAM3X8.unixtime();
  if((get_State()==pOFF_HP)||(get_State()==pSTOPING_HP)) return;  // ТН выключен или выключается выходим ничего не делаем!!!
  
  if (COMPRESSOR_IS_ON) return;                                  // Компрессор уже работает
  else                                                           // надо включать компрессор
  { 
   journal.jprintf(pP_TIME," compressorON > modWork:%d[%s], COMPRESSOR_IS_ON:%d\n",mod,codeRet[Status.ret],COMPRESSOR_IS_ON);

    #ifdef EEV_DEF
    if (lastEEV!=-1)              // Не первое включение компрессора после старта ТН
     {
     // 1. Обеспечение минимальной паузы компрессора
     #ifdef DEMO
       if (nTime-stopCompressor<10) {journal.jprintf(MinPauseOffCompressor);return;} // Обеспечение паузы компрессора Хранится в секундах!!! ТЕСТИРОВАНИЕ
     #else
      switch ((int)mod)   // Обеспечение паузы компрессора Хранится в секундах!!!
      {
          case  pCOOL:   if (nTime-stopCompressor<Prof.Cool.pause)   {journal.jprintf(MinPauseOffCompressor,nTime-stopCompressor-Prof.Cool.pause);return;} break;  
          case  pHEAT:   if (nTime-stopCompressor<Prof.Heat.pause)   {journal.jprintf(MinPauseOffCompressor,nTime-stopCompressor-Prof.Heat.pause);return;} break; 
          case  pBOILER: if (nTime-stopCompressor<Prof.Boiler.pause) {journal.jprintf(MinPauseOffCompressor,nTime-stopCompressor-Prof.Boiler.pause);return;} break;  
          case  pOFF:    
          case  pNONE_C:
          case  pNONE_H:
          case  pNONE_B:
          default: {set_Error(ERR_CONFIG,(char*)__FUNCTION__);} break;
       }     
     #endif

      // 2. Разбираемся с ЭРВ
           journal.jprintf(EEV_go);   
           if (GETBIT(Option.flags,fEEV_light_start)) {dEEV.set_EEV(EEV_PSTART);journal.jprintf("EEV_PSTART: %d\n",EEV_PSTART);  }    // Выйти на пусковую позицию
           else if (GETBIT(Option.flags,fEEV_start))  {dEEV.set_EEV(EEV_START); journal.jprintf("EEV_START: %d\n",EEV_START);    }    // Всегда начинать работу ЭРВ со стратовой позиции
           else                                       {dEEV.set_EEV(lastEEV);   journal.jprintf("lastEEV: %d\n",lastEEV);        }    // установка последнего значения ЭРВ
           if(GETBIT(Option.flags,fEEV_close))           // Если закрывали то пауза для выравнивания давлений
           _delay(DELAY_ON3_EEV);  // Задержка на DELAY_ON3_EEV сек  для выравнивания давлений
       
     }   //  if (lastEEV!=-1)   
     else // первое включение компресора lastEEV=-1
     { // 2. Разбираемся с ЭРВ
        journal.jprintf(EEV_go);      
        if (GETBIT(Option.flags,fEEV_light_start)) { dEEV.set_EEV(EEV_PSTART);  journal.jprintf("EEV_PSTART: %d\n",EEV_PSTART);  }      // Выйти на пусковую позицию
        else                                       { dEEV.set_EEV(EEV_START);   journal.jprintf("EEV_START: %d\n",EEV_START);    }      // Всегда начинать работу ЭРВ со стратовой позиции
     }
      #endif
          
      // 3. Управление компрессором
      if (get_errcode()==OK)                                 // Компрессор включить если нет ошибок
           { 
           // Дополнительные защиты перед пуском компрессора
           if (startPump)                                      // Проверка задачи насос - должен быть выключен
              {
                startPump=false;                               // Поставить признак останова задачи насос
                vTaskSuspend(xHandleUpdatePump);            // Остановить задачу насос
                journal.jprintf(" WARNING! %s: Bad startPump, task vUpdatePump RPUMPO pause  . . .\n",(char*)__FUNCTION__);
              } 
           // Проверка включение насосов с проверкой и предупреждением
           if (!dRelay[PUMP_IN].get_Relay()) {journal.jprintf(" WARNING!! PUMP_IN is off Compressor on\n"); dRelay[PUMP_IN].set_ON();  }
           #ifndef SUPERBOILER  // для супербойлера это лишнее
           if (!dRelay[PUMP_OUT].get_Relay()){journal.jprintf(" WARNING!! PUMP_OUT is off Compressor on\n"); dRelay[PUMP_OUT].set_ON();  }
           #endif
           
           COMPRESSOR_ON;                                        // Включить компрессор
           startCompressor=rtcSAM3X8.unixtime();                 // Запомнить время включения компрессора оно используется для задержки работы ПИД ЭРВ! должно быть перед  vTaskResume(xHandleUpdateEEV) или  dEEV.Resume
           }
       else // if (get_errcode()==OK)
           {
            journal.jprintf(" There is an error, the compressor is not on\n"); 
            set_Error(ERR_COMP_ERR,(char*)__FUNCTION__);return; 
           }

    // 4. Если нужно облегченный пуск  в зависимости от флага fEEV_light_start
    #ifdef EEV_DEF
    if(GETBIT(Option.flags,fEEV_light_start))                  //  ЭРВ ОБЛЕГЧЕННЫЙ ПУСК
         {
         journal.jprintf(" Pause %d second before go starting position EEV . . .\n",DELAY_START_POS);      
         _delay(DELAY_START_POS*1000);  // Задержка после включения компрессора до ухода на рабочую позицию
         journal.jprintf(EEV_go);  
         if ((GETBIT(Option.flags,fEEV_start))||((lastEEV==-1)  ))  {dEEV.set_EEV(EEV_START); journal.jprintf("EEV_START: %d\n",EEV_START);    }    // если первая итерация или установлен соответсвующий флаг то на стартовую позицию
         else                                                       {dEEV.set_EEV(lastEEV);   journal.jprintf("lastEEV: %d\n",lastEEV);        }    // установка последнего значения ЭРВ в противном случае
         }
     #endif      
     // 5. Обеспечение задержки отслеживания ЭРВ
     #ifdef EEV_DEF
     if (lastEEV>0)                                            // НЕ первое включение компрессора после старта ТН
      {  
 //      journal.jprintf(" Pause %d second before enabling tracking EEV . . .\n",DELAY_ON_PID_EEV);    // Задержка внутри задачи!!!
       if (GETBIT(Option.flags,fEEV_start))  dEEV.Resume(EEV_START);     // Снять с паузы задачу Обновления ЭРВ  PID  со стратовой позиции
       else                                  dEEV.Resume(lastEEV);       // Снять с паузы задачу Обновления ЭРВ  PID c последнего значения ЭРВ
       journal.jprintf(" Resume task update EEV\n"); 
       journal.jprintf(pP_TIME,"  %s WORK . . .\n",(char*)nameHeatPump);     // Сообщение о работе
     }
      else  // признак первой итерации
      {
      lastEEV=EEV_START;                                           // ЭРВ рабоатет запомнить
      set_startTime(rtcSAM3X8.unixtime());                         // Запомнить время старта ТН
      vTaskResume(xHandleUpdateEEV);                               // Запустить задачу Обновления ЭРВ
      journal.jprintf(" Start task update EEV\n");
      }
      #else
      lastEEV=1;                                                   // Признак первой итерации
      set_startTime(rtcSAM3X8.unixtime());                         // Запомнить время старта ТН
      #endif
  } // else (COMPRESSOR_IS_ON) 
             
}

// попытка выключить компрессор  с учетом всех защит
const char *MinPauseOnCompressor={" Wait min pause on compressor . . ."};  
void HeatPump::compressorOFF()
{
  if (dFC.get_present()) // Есть частотник
  {
    if (!dFC.isfOnOff()) return; // он выключен
  }
  else  if (!dRelay[RCOMP].get_Relay()) return; // Не частотник и реле компрессора выключено
   
  if((get_State()==pOFF_HP)||(get_State()==pSTARTING_HP)||(get_State()==pSTOPING_HP)) return;     // ТН выключен или включается или выключается выходим ничего не делаем!!!


 journal.jprintf(pP_TIME," compressorOFF > modWork:%d[%s], COMPRESSOR_IS_ON:%d\n",get_mode(),codeRet[Status.ret],COMPRESSOR_IS_ON);  
  #ifdef DEMO
    if (rtcSAM3X8.unixtime()-startCompressor<10)   {return;journal.jprintf(MinPauseOnCompressor);}     // Обеспечение минимального времени работы компрессора 2 минуты ТЕСТИРОВАНИЕ
  #else
    if (rtcSAM3X8.unixtime()-startCompressor<2*60) {return;journal.jprintf(MinPauseOnCompressor);}     // Обеспечение минимального времени работы компрессора 2 минуты
  #endif
  
  #ifdef EEV_DEF
  lastEEV=dEEV.get_EEV();                                             // Запомнить последнюю позицию ЭРВ
  dEEV.Pause();                                                       // Поставить на паузу задачу Обновления ЭРВ
  journal.jprintf(" Pause task update EEV\n"); 
  #endif
  
  
  COMPRESSOR_OFF;                                                     // Компрессор выключить
  stopCompressor=rtcSAM3X8.unixtime();                                // Запомнить время выключения компрессора
  
  #ifdef REVI
      checkEVI();                                                     // выключить ЭВИ
  #endif

  PUMPS_OFF;                                                          // выключить насосы + задержка
  
  #ifdef EEV_DEF
  if(GETBIT(Option.flags,fEEV_close))                                 // Hазбираемся с ЭРВ
     { 
     journal.jprintf(" Pause before closing EEV %d sec . . .\n",DELAY_OFF_EEV);  
     _delay(DELAY_OFF_EEV*1000); // пауза перед закрытием ЭРВ  на инверторе компрессор останавливается до 2 минут
     dEEV.set_EEV(EEV_MIN_STEPS);                         // Если нужно, то закрыть ЭРВ
     journal.jprintf(" EEV go EEV_MIN_STEPS\n"); 
     } 
  #endif
  
  journal.jprintf(pP_TIME,"  %s PAUSE . . .\n",(char*)nameHeatPump);    // Сообщение о паузе
}

// РАЗМОРОЗКА ВОЗДУШНИКА ----------------------------------------------------------
// Все что касается разморозки воздушника
#ifdef DEFROST
void HeatPump::defrost()
{
      if (get_State()==pOFF_HP) return;                                    // если ТН не работает то выходим
      
      #ifdef RTRV            // Нет четырехходового - нет режима охлаждения
        if(dRelay[RTRV].get_Relay()==true) return;                           // режим охлаждения - размораживать не надо
      #endif
         
      if (sInput[SFROZEN].get_Input()==SFROZEN_OFF) {startDefrost=0;return;  }    // размораживать не надо - датчик говорит что все ок
      
      // организация задержки перед включением
      if (startDefrost==0) startDefrost=xTaskGetTickCount();               // первое срабатывание датчика - запоминаем время (тики)
      if (xTaskGetTickCount()-startDefrost<DELAY_DEFROST_ON*1000)  return; //  Еще рано размораживать
      // придется размораживать
       journal.jprintf("Start defrost\n"); 
       #ifdef RTRV
         if ((COMPRESSOR_IS_ON)&&(dRelay[RTRV].get_Relay()==false)) ChangesPauseTRV();    // Компрессор рабатает и 4-х ходовой стоит на тепле то хитро переключаем 4-х ходовой в положение холод
         dRelay[RTRV].set_ON();                                              // охлаждение
         _delay(2*1000);                               // Задержка на 2 сек
       #endif
       
       compressorON(pCOOL);                                                 // включить компрессор на холод
      
      while (sInput[SFROZEN].get_Input()!=SFROZEN_OFF)                     // ждем оттаивания
      {
      _delay(10*1000);                              // Задержка на 10 сек
        journal.jprintf(" Wait process defrost . . .\n"); 
        if((get_State()==pOFF_HP)||(get_State()==pSTARTING_HP)||(get_State()==pSTOPING_HP)) break;     // ТН выключен или включается или выключается выходим из разморозки
      }
      journal.jprintf(" Finish defrost, wait DELAY_DEFROST_OFF min.\n"); 
      _delay(DELAY_DEFROST_OFF*1000);               // Задержка перед выключением
      compressorOFF();                                                     // выключить компрессор
      journal.jprintf("Finish defrost\n"); 
      // выходим ТН сам определит что надо делать
}
#endif

// ОБРАБОТЧИК КОМАНД УПРАВЛЕНИЯ ТН
// Послать команду на управление ТН
void HeatPump::sendCommand(TYPE_COMMAND c)
{
  if (c==command) return;      // Игнорируем повторы
  if (command!=pEMPTY)         // Если команда выполняется (не pEMPTY), то следюющую, что послана игнорируем
     {

       journal.jprintf("Performance command: ");  
           switch(command)
            {
             case pEMPTY:     journal.jprintf("EMPTY");    break;    // Не должно быть!
             case pSTART:     journal.jprintf("START");    break;
             case pAUTOSTART: journal.jprintf("AUTOSTART");break;             
             case pSTOP:      journal.jprintf("STOP");     break;
             case pRESET:     journal.jprintf("RESET");    break;
             case pRESTART:   journal.jprintf("RESTART");  break;
             case pREPEAT:    journal.jprintf("REPEAT");   break;
             case pNETWORK:   journal.jprintf("NETWORK");  break;
             case pJFORMAT:   journal.jprintf("JFORMAT");  break;      
             case pSFORMAT:   journal.jprintf("SFORMAT");  break;                    
             case pSAVE:      journal.jprintf("SAVE");     break;                    
             
             default:         journal.jprintf("UNKNOW");   break;    // Не должно быть!
            }
       journal.jprintf(", ignore command: "); 
           switch(c)
            {
             case pEMPTY:     journal.jprintf("EMPTY\n");    break;    // Не должно быть!
             case pSTART:     journal.jprintf("START\n");    break;
             case pAUTOSTART: journal.jprintf("AUTOSTART\n");break;         
             case pSTOP:      journal.jprintf("STOP\n");     break;
             case pRESET:     journal.jprintf("RESET\n");    break;
             case pRESTART:   journal.jprintf("RESTART\n");  break;
             case pREPEAT:    journal.jprintf("REPEAT\n");   break;
             case pNETWORK:   journal.jprintf("NETWORK\n");  break; 
             case pJFORMAT:   journal.jprintf("JFORMAT\n");  break;         
             case pSFORMAT:   journal.jprintf("SFORMAT\n");  break;                    
             case pSAVE:      journal.jprintf("SAVE\n");     break;                    
    
             default:         journal.jprintf("UNKNOW\n");   break;    // Не должно быть!
            }            
      
       return;  
      }
  if ((c==pSTART)&&(get_State()==pSTOPING_HP)) return;     // Пришла команда на старт а насос останавливается ничего не делаем игнорируем
  command=c;
  vTaskResume(xHandleUpdateCommand);                    // Запустить выполнение команды
}  
// Выполнить команду по управлению ТН true-команда выполнена
int8_t HeatPump::runCommand()
{
  uint16_t i;

       journal.jprintf("Run command: ");  
       switch(command)
        {
         case pEMPTY:     journal.jprintf("EMPTY\n");    break;    // Не должно быть!
         case pSTART:     journal.jprintf("START\n");    break;
         case pAUTOSTART: journal.jprintf("AUTOSTART\n");break;         
         case pSTOP:      journal.jprintf("STOP\n");     break;
         case pRESET:     journal.jprintf("RESET\n");    break;
         case pRESTART:   journal.jprintf("RESTART\n");  break;
         case pREPEAT:    journal.jprintf("REPEAT\n");   break;
         case pNETWORK:   journal.jprintf("NETWORK\n");  break;
         case pJFORMAT:   journal.jprintf("JFORMAT\n");  break; 
         case pSFORMAT:   journal.jprintf("SFORMAT\n");  break;                    
         case pSAVE:      journal.jprintf("SAVE\n");     break;                    
         
         default:         journal.jprintf("UNKNOW\n");   break;    // Не должно быть!
        }
  // Захватываем семафор и разбираем потом команды
      if(SemaphoreTake(xCommandSemaphore,(100*1000/portTICK_PERIOD_MS))==pdPASS)                // Cемафор  захвачен ОЖИДАНИНЕ ДА 100 сек
 //   vTaskSuspend(HP.xHandlePauseStart);  // Останов задачи выполнение отложенного старта
  HP.PauseStart=true;                                // Необходимость начать задачу xHandlePauseStart с начала
  switch(command)
  {
  case pEMPTY:  return true; break;     // 0 Команд нет
  case pSTART:                          // 1 Пуск теплового насоса
                num_repeat=0;           // обнулить счетчик повторных пусков
                Start();                // включить ТН
                command=pEMPTY;         // Сбросить команду
                break;
  case pAUTOSTART:                      // 2 Пуск теплового насоса автоматический
                Start();                // включить ТН
                command=pEMPTY;         // Сбросить команду
                break;                
  case pSTOP:                           // 3 Стоп теплового насоса
                Stop();                 // Выключить ТН
                command=pEMPTY;         // Сбросить команду
                break;
  case pRESET:                          // 4 Сброс контроллера
                Stop();                 // остановить ТН
                journal.jprintf("$SOFTWARE RESET control . . .\r\n"); 
                journal.jprintf("");
                _delay(500);            // задержка что бы вывести сообщение в консоль
                command=pEMPTY;         // Сбросить команду
                Software_Reset() ;      // Сброс
                break;
  case pREPEAT:
                Stop();                                        // Попытка запустит ТН (по числу пусков)
                num_repeat++;                                  // увеличить счетчик повторов пуска ТН
                journal.jprintf("Repeat start %s (attempts remaining %d) . . .\r\n",(char*)nameHeatPump,get_nStart()-num_repeat); 
        //        HP.PauseStart=true;                                // Необходимость начать задачу xHandlePauseStart с начала
                vTaskResume(xHandlePauseStart);                    // Запустить выполнение отложенного старта
                command=pEMPTY;                                    // Сбросить команду
                break;  
  case pRESTART:
               // Stop();                                          // пуск Тн после сброса - есть задержка
                 journal.jprintf("Restart %s . . .\r\n",(char*)nameHeatPump);
//              HP.PauseStart=true;                                // Необходимость начать задачу xHandlePauseStart с начала
                vTaskResume(xHandlePauseStart);                    // Запустить выполнение отложенного старта
                command=pEMPTY;                                    // Сбросить команду
                break;                 
  case pNETWORK:
                journal.jprintf("Update network setting . . .\r\n");
                _delay(1000);               						// задержка что бы вывести сообщение в консоль и на веб морду
                if(SemaphoreTake(xWebThreadSemaphore,(W5200_TIME_WAIT/portTICK_PERIOD_MS))==pdFALSE) {journal.jprintf((char*)cErrorMutex,__FUNCTION__,MutexWebThreadBuzy); command=pEMPTY; return 0;} // Захват мютекса потока или ОЖИДАНИНЕ W5200_TIME_WAIT
                initW5200(true);                                  // Инициализация сети с выводом инфы в консоль
                for (i=0;i<W5200_THREARD;i++) SETBIT1(Socket[i].flags,fABORT_SOCK);                                 // Признак инициализации сокета, надо прерывать передачу в сервере
                SemaphoreGive(xWebThreadSemaphore);                                                                // Мютекс потока отдать
                command=pEMPTY;                                    // Сбросить команду
                break;                 
  case pJFORMAT:                                                   // Форматировать журнал в I2C памяти
                #ifdef I2C_EEPROM_64KB 
                 _delay(2000);           						   // задержка что бы вывести сообщение в консоль и на веб морду
                 journal.Format();                                 // Послать команду форматирование журнала
                #else                                              // Этого не может быть, но на всякий случай
                 journal.Init();                                   // Очистить журнал в оперативке
                #endif 
                command=pEMPTY;                                    // Сбросить команду
                break;      
  case pSFORMAT:                                                   // Форматировать журнал в I2C памяти
                #ifdef I2C_EEPROM_64KB 
                 _delay(2000);              						// задержка что бы вывести сообщение в консоль и на веб морду
                 Stat.Format();                                    // Послать команду форматирование статистики
                #endif 
                command=pEMPTY;                                    // Сбросить команду
                break;                    
  case pSAVE:                                                      // Сохранить настройки
                _delay(2000);              						 // задержка что бы вывести сообщение в консоль и на веб морду
                save();                                            // сохранить настройки
                command=pEMPTY;                                    // Сбросить команду
                break;  
                                     
  default:                                                         // Не известная команда
                journal.jprintf("Unknow command????"); 
                command=pEMPTY;
                break;
  }
  SemaphoreGive(xCommandSemaphore);              // Семафор отдать
return error;  
}

// --------------------------Строковые функции ----------------------------
const char *strRusPause={"Пауза"};
const char *strEngPause={"Pause"};
// Получить строку состояния ТН в виде строки
char *HeatPump::StateToStr()
{
switch ((int)get_State())  //TYPE_STATE_HP  
  {
  case pOFF_HP:     return (char*)"Выключен";  break;                     // 0 ТН выключен
  case pSTARTING_HP:return (char*)"Пуск...";   break;                     // 1 Стартует
  case pSTOPING_HP: return (char*)"Останов...";break;                     // 2 Останавливается
  case pWORK_HP:                                                          // 3 Работает
         switch ((int)get_modWork())                                      // MODE_HP
         {
         case  pOFF: return (char*)strRusPause;      break;               // 0 Пауза
         case  pHEAT: return (char*)"Нагрев+";        break;               // 1 Включить отопление
         case  pCOOL: return (char*)"Заморозка+";     break;               // 2 Включить охлаждение
         case  pBOILER: return (char*)"Нагрев ГВС+"; break;               // 3 Включить бойлер
         case  pNONE_H: if (!(COMPRESSOR_IS_ON)) return (char*)strRusPause; else return (char*)"Отопление";   break;  // 4 Продолжаем греть отопление
         case  pNONE_C: if (!(COMPRESSOR_IS_ON)) return (char*)strRusPause; else return (char*)"Охлаждение";  break;  // 5 Продолжаем охлаждение
         case  pNONE_B: if (!(COMPRESSOR_IS_ON)) return (char*)strRusPause; else return (char*)"ГВС";         break;  // 6 Продолжаем греть бойлер
         default: return (char*)"Error state";          break; 
         }
        break;   
  case pERROR_HP:   return (char*)"Ошибка";    break;                     // 5 Ошибка ТН
  default:          return (char*)"Вн.Ошибка"; break;                     // 6 - Эта ошибка возникать не должна!
  }
  
}
// Получить строку состояния ТН в виде строки АНГЛИСКИЕ буквы
char *HeatPump::StateToStrEN()
{
switch ((int)get_State())  //TYPE_STATE_HP  
  {
  case pOFF_HP:     return (char*)"Off";  break;                         // 0 ТН выключен
  case pSTARTING_HP:return (char*)"Start...";   break;                   // 1 Стартует
  case pSTOPING_HP: return (char*)"Stop...";break;                       // 2 Останавливается
  case pWORK_HP:                                                         // 3 Работает
         switch ((int)get_modWork())                                     // MODE_HP
         {
         case  pOFF: return (char*)strEngPause;        break;            // 0 Выключить
         case  pHEAT: return (char*)"Pre-heat";        break;            // 1 Включить отопление
         case  pCOOL: return (char*)"Pre-cool";        break;            // 2 Включить охлаждение
         case  pBOILER: return (char*)"Pre-boiler.";       break;        // 3 Включить бойлер
         case  pNONE_H: if (!(COMPRESSOR_IS_ON)) return (char*)strEngPause; else return (char*)"Heating";   break;                   // 4 Продолжаем греть отопление
         case  pNONE_C: if (!(COMPRESSOR_IS_ON)) return (char*)strEngPause; else return (char*)"Cooling";   break;                   // 5 Продолжаем охлаждение
         case  pNONE_B: if (!(COMPRESSOR_IS_ON)) return (char*)strEngPause; else return (char*)"Boiler";    break;                   // 6 Продолжаем греть бойлер
         default:       return (char*)"Error state";          break; 
         }
        break;   
  case pERROR_HP:   return (char*)cError;    break;                     // 5 Ошибка ТН
  default:          return (char*)cError;    break;                     // 6 - Эта ошибка возникать не должна!
  }
}

// получить режим тестирования
char * HeatPump::TestToStr()
{
 switch ((int)get_testMode())
             {
              case NORMAL:    return (char*)"NORMAL";    break;
              case SAFE_TEST: return (char*)"SAFE_TEST"; break;
              case TEST:      return (char*)"TEST";      break;
              case HARD_TEST: return (char*)"HARD_TEST"; break;
              default:        return (char*)cError;     break;    
             }    
}
// Записать состояние теплового насоса в журнал
// Параметр на входе true - вывод в журнал и консоль false - консоль
int8_t HeatPump::save_DumpJournal(boolean f)
{
  uint8_t i;
  if(f)  // вывод в журнал
      {
        journal.jprintf(" modWork:%d[%s]",(int)Status.modWork,codeRet[Status.ret]); 
        if(!(dFC.get_present())) journal.printf(" RCOMP:%d",dRelay[RCOMP].get_Relay());  
        #ifdef RPUMPI
        journal.jprintf(" RPUMPI:%d",dRelay[RPUMPI].get_Relay()); 
        #endif
        journal.jprintf(" RPUMPO:%d",dRelay[RPUMPO].get_Relay());
        #ifdef RTRV  
        if (dRelay[RTRV].get_present())     journal.jprintf(" RTRV:%d",dRelay[RTRV].get_Relay());  
        #endif 
        #ifdef R3WAY 
        if (dRelay[R3WAY].get_present())    journal.jprintf(" R3WAY:%d",dRelay[R3WAY].get_Relay()); 
        #endif
        #ifdef RBOILER 
        if (dRelay[RBOILER].get_present())  journal.jprintf(" RBOILER:%d",dRelay[RBOILER].get_Relay());
        #endif
        #ifdef RHEAT
        if (HP.dRelay[RHEAT].get_present()) journal.jprintf(" RHEAT:%d",dRelay[RHEAT].get_Relay());  
        #endif
        #ifdef REVI
        if (dRelay[REVI].get_present())     journal.jprintf(" REVI:%d",dRelay[REVI].get_Relay());   
        #endif
        #ifdef RPUMPB
        if (dRelay[RPUMPB].get_present())   journal.jprintf(" RPUMPB:%d",dRelay[RPUMPB].get_Relay());   
        #endif      
        if(dFC.get_present())               journal.jprintf(" freqFC:%.2f",dFC.get_freqFC()/100.0);  
        if(dFC.get_present())               journal.jprintf(" Power:%.2f",dFC.get_power()/10.0);  
        #ifdef EEV_DEF
        if (dEEV.get_present())             journal.jprintf(" EEV:%d",dEEV.get_EEV());
        #endif
         journal.jprintf(cStrEnd);
         // Доп инфо
        for(i=0;i<TNUMBER;i++)   // Информация по  датчикам температуры
             if (sTemp[i].get_present())      journal.jprintf(" %s:%.2f",sTemp[i].get_name(),sTemp[i].get_Temp()/100.0);
        if (sADC[PEVA].get_present())         journal.jprintf(" PEVA:%.2f",sADC[PEVA].get_Press()/100.0); 
        if (sADC[PCON].get_present())         journal.jprintf(" PCON:%.2f",sADC[PCON].get_Press()/100.0);  
        journal.jprintf(cStrEnd);
      }
   else
     {
        journal.printf(" modWork:%d[%s]",(int)Status.modWork,codeRet[Status.ret]); 
        if(!(dFC.get_present())) journal.printf(" RCOMP:%d",dRelay[RCOMP].get_Relay());  
        #ifdef RPUMPI
        journal.printf(" RPUMPI:%d",dRelay[RPUMPI].get_Relay()); 
        #endif
        journal.printf(" RPUMPO:%d",dRelay[RPUMPO].get_Relay()); 
        #ifdef RTRV 
        if (dRelay[RTRV].get_present())           journal.printf(" RTRV:%d",dRelay[RTRV].get_Relay());  
        #endif 
        #ifdef R3WAY 
        if (dRelay[R3WAY].get_present())          journal.printf(" R3WAY:%d",dRelay[R3WAY].get_Relay());  
        #endif
        #ifdef RBOILER 
        if (dRelay[RBOILER].get_present())        journal.printf(" RBOILER:%d",dRelay[RBOILER].get_Relay());
        #endif
        #ifdef RHEAT
        if (HP.dRelay[RHEAT].get_present())       journal.printf(" RHEAT:%d",dRelay[RHEAT].get_Relay());  
        #endif
        #ifdef REVI
        if (dRelay[REVI].get_present())           journal.printf(" REVI:%d",dRelay[REVI].get_Relay());   
        #endif
        #ifdef RPUMPB
        if (dRelay[RPUMPB].get_present())           journal.printf(" RPUMPB:%d",dRelay[RPUMPB].get_Relay());   
        #endif
        
 //      Serial.print(" dEEV.stepperEEV.isBuzy():");  Serial.print(dEEV.stepperEEV.isBuzy());
 //      Serial.print(" dEEV.setZero: ");  Serial.print(dEEV.setZero);  
        if(dFC.get_present()) journal.printf(" freqFC:%.2f",dFC.get_freqFC()/100.0);  
        if(dFC.get_present()) journal.printf(" Power:%.2f",dFC.get_power()/10.0);
        #ifdef EEV_DEF
        journal.printf(" EEV:%d",dEEV.get_EEV()); 
        #endif
        journal.printf(cStrEnd);
                 
     }
  return OK;
}
#ifdef I2C_EEPROM_64KB 
// Функция вызываемая для первого часа для инициализации первичных счетчиков Только при старте ТН
void HeatPump::InitStatistics()
{
   Stat.updateCO(motoHour.P1); 
   Stat.updateEO(motoHour.E1); 
   Stat.updatemoto(motoHour.C1); 
}
    
void HeatPump::UpdateStatistics()
{
  uint32_t tt=rtcSAM3X8.unixtime();
  uint8_t h=(tt-Stat.get_dateUpdate())/(60*60);  //Получить целое число часов после последнего обновления
  if (h>0)  // прошел час и более надо обновлять
  {
   Stat.updateHour(h);
   Stat.updateTin(h*sTemp[TIN].get_Temp());
   Stat.updateTout(h*sTemp[TOUT].get_Temp());
   Stat.updateTbol(h*sTemp[TBOILER].get_Temp());
   // счетчики обновлять не надо
  }
  
  if(tt-Stat.get_date()>=24*60*60)                                   // Прошло больше суток с начала накопления надо закрывать
    {
       Stat.writeOneDay(motoHour.P1,motoHour.E1,motoHour.C1);        // записать
    }

}
#endif // I2C_EEPROM_64KB 
                   

