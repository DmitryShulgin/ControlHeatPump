/*
 * Расписание
 * Автор vad711, vad7@yahoo.com
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
//
#ifndef Scheduler_h
#define Scheduler_h

#include "Config.h"

#define SCHDLR_Profile_off	-1
#define SCHDLR_NotActive	-2

#ifdef USE_SCHEDULER

#define TIMETABLES_MAXSIZE	250 // bytes
#define MAX_CALENDARS		5   // максимум 9
#define bScheduler_active	1

struct Scheduler_Calendar_Item {
	uint8_t		WD_Hour; 	// День недели (3bits, 0 = monday)  + час (5bits)
	int8_t		Profile;	// Номер профиля, -1 = выключен
} __attribute__((packed));

struct Scheduler_Data {
	uint8_t		Flags;							// bScheduler_*
	uint8_t		Active;							// Активное расписание
	char 		Names[MAX_CALENDARS][25];		// Названия (русские - уникод 2 байта)
	uint8_t		Timetable[TIMETABLES_MAXSIZE]; 	// буфер для раписаний: {len bytes},{{WD+H},{Profile}},..., {len},{{WD+H},{Profile}},...,
} __attribute__((packed));

class Scheduler
{
public:
	Scheduler();
	int8_t   calc_active_profile(void);					// Возвращает профиль (или -1) по календарю и текущему времени
	uint8_t  Timetable_ptr(uint8_t num);				// Возвращает указатель на запись календаря в TimeTable по его номеру
	void	 web_get_param(char *param, char* result);	// Вернуть строку параметра для веба
	uint8_t  web_set_param(char *param, char *val);		// Установить параметр из веба
	const char* get_note(void); 						// Получить описание
	const char* get_name(void) { return  "Scheduler"; } // Получить имя
	int8_t   save(void);  			               		// Записать настройки в eeprom i2c
	int8_t   load(uint8_t *data = NULL);  		     	// Считать настройки из eeprom i2c
	int32_t  loadFromBuf(int32_t addr, byte *buf);  	// Считать настройки из буфера на входе адрес с какого, на выходе конечный адрес, число меньше 0 это код ошибки
	uint16_t get_crc16(uint8_t *data);             		// Рассчитать контрольную сумму для данных на входе входная сумма на выходе новая
	int8_t   check_crc16_eeprom(void);
	uint16_t get_data_size(void) { return sizeof(sch_data); }
private:
	Scheduler_Data sch_data;
};

#endif
#endif
