/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include "prefix.h"

#include "core.h"
#include "globdefs.h"
#include "filedefs.h"
#include "objdefs.h"
#include "parsedef.h"

#include "execpt.h"
#include "date.h"

#if defined(_LINUX_DESKTOP) || defined(_LINUX_SERVER) || defined(_DARWIN_SERVER)
#include <time.h>
#define sys_time_t time_t
#define sys_localtime localtime
#define sys_mktime mktime
#define sys_gmtime gmtime
#define sys_timegm timegm
#elif defined(_ANDROID_MOBILE)
#include <time64.h>
#define sys_time_t time64_t
#define sys_localtime localtime64
#define sys_mktime mktime64
#define sys_gmtime gmtime64
#define sys_timegm timegm64
#else
#error 'sysunxdate.cpp' not supported on this platform
#endif

////////////////////////////////////////////////////////////////////////////////

static void tm_to_datetime(bool p_local, const struct tm *p_tm, MCDateTime& r_datetime)
{
	r_datetime . year = 1900 + p_tm -> tm_year;
	r_datetime . month = p_tm -> tm_mon + 1;
	r_datetime . day = p_tm -> tm_mday;
	r_datetime . hour = p_tm -> tm_hour;
	r_datetime . minute = p_tm -> tm_min;
	r_datetime . second = p_tm -> tm_sec;
	
	// MW-2008-03-15: [[ Bug 6075 ]] The bias member is actually in minutes not hours, so this should
	//   be a division by 60.
	if (p_local)
		r_datetime . bias = p_tm -> tm_gmtoff / 60;
	else
		r_datetime . bias = 0;
	
}

static void datetime_to_tm(bool p_local, const MCDateTime& p_datetime, struct tm& r_tm)
{
	r_tm . tm_year = p_datetime . year - 1900;
	r_tm . tm_mon = p_datetime . month - 1;
	r_tm . tm_mday = p_datetime . day;
	r_tm . tm_hour = p_datetime . hour;
	r_tm . tm_min = p_datetime . minute;
	r_tm . tm_sec = p_datetime . second;
	if (p_local)
		r_tm . tm_isdst = -1;
	else
		r_tm . tm_isdst = 0;
}

////////////////////////////////////////////////////////////////////////////////

void MCS_getlocaldatetime(MCDateTime& r_datetime)
{
	struct tm *tmp; 
	sys_time_t t;
	
	t = time(NULL);
	tmp = sys_localtime(&t);
	tm_to_datetime(true, tmp, r_datetime);
}

bool MCS_datetimetouniversal(MCDateTime& x_datetime)
{
	struct tm tm_local ;
	datetime_to_tm( true, x_datetime, tm_local);
	
	sys_time_t t;
	t = sys_mktime(&tm_local);
	
	if ( t == -1 ) 
		return (false);
	
	struct tm *tm_uni ;
	tm_uni = sys_gmtime(&t);
	
	tm_to_datetime(false, tm_uni, x_datetime) ;
	return (true);
}

bool MCS_datetimetolocal(MCDateTime& x_datetime)
{
	struct tm tmp;
	datetime_to_tm(false, x_datetime, tmp);		// Adjust the datetime to tm format
	
	// MW-2008-03-15: [[ Bug 6075 ]] 'mktime' converts local time to seconds, but we need to convert universal
	//   time to seconds. To do this we use the glibc function 'timegm'.
	sys_time_t t;
	t = sys_timegm(&tmp);							// Convert the tm struct to time_t (number of seconds);
	if (t == -1)
		return false;
	
	struct tm *ltm;
	ltm = sys_localtime(&t);						// Create a tm format structure from the time_t returned above
	tm_to_datetime(true, ltm, x_datetime); 		// And convert back to the datetime that Rev wants.
	
	return ( true ); 
}

bool MCS_datetimetoseconds(const MCDateTime& x_datetime, double& r_seconds)
{
	struct tm tmp;
	datetime_to_tm(false, x_datetime, tmp);
	
	// MW-2008-03-15: [[ Bug 6075 ]] 'mktime' converts local time to seconds, but we need to convert universal
	//   time to seconds. To do this we use the glibc function 'timegm'.
	sys_time_t t ;
	t = sys_timegm(&tmp);
	if ( t == -1 ) 
		return False ;
	r_seconds = (double)t ;
	return True ;
}

bool MCS_secondstodatetime(double p_seconds, MCDateTime& r_datetime)
{
	sys_time_t 
	t = (sys_time_t)(p_seconds + 0.5);
	
	struct tm *tmp;
	
	tmp = sys_gmtime(&t);
	tm_to_datetime(false, tmp, r_datetime);
	
	return true;
}

////////////////////////////////////////////////////////////////////////////////

#if defined(_LINUX_DESKTOP) || defined(_LINUX_SERVER)

#include <locale.h>
#include <langinfo.h>
#include <nl_types.h>

static MCDateTimeLocale *s_datetime_locale = nil;

static char *string_prepend(const char *trunk, const char *prefix)
{
	char *t_new_string;
	t_new_string = new char[strlen(trunk) + strlen(prefix) + 1];
	strcpy(t_new_string, prefix);
	strcat(t_new_string, trunk);
	delete trunk;
	return t_new_string;
}


static char *query_locale(uint4 t_index)
{
	char *t_buffer;
	t_buffer = nl_langinfo(t_index);
	return strdup(t_buffer);
}



char * swap_time_tokens ( char * p_instr ) 
{
	char * t_ptr = p_instr ;
	while (*t_ptr != '\0')
	{
		switch (*t_ptr)
		{
			case 'l':
				*t_ptr = 'I' ;
			break;

			case 'P':
				*t_ptr = 'p' ;
			break;
		}
		t_ptr ++ ;
	}
	return (p_instr);
}


static void cache_locale(void)
{
	if (s_datetime_locale != NULL)
		return;

	s_datetime_locale = new MCDateTimeLocale;

	// OK-2007-05-23: Fix for bug 5035. Adjusted to ensure that first element of weekday names is always Sunday.

	s_datetime_locale -> weekday_names[0] = query_locale(DAY_1);
	s_datetime_locale -> abbrev_weekday_names[0] = query_locale(ABDAY_1);

	for (uint4 t_index = 0; t_index < 6; ++t_index)
	{
		s_datetime_locale -> weekday_names[t_index + 1] = query_locale(DAY_2 + t_index);
		s_datetime_locale -> abbrev_weekday_names[t_index + 1] = query_locale(ABDAY_2 + t_index);
	}

	for(uint4 t_index = 0; t_index < 12; ++t_index)
	{
		s_datetime_locale -> month_names[t_index] = query_locale(MON_1 + t_index);
		s_datetime_locale -> abbrev_month_names[t_index] = query_locale(ABMON_1 + t_index);
	}

	
	s_datetime_locale -> date_formats[0] = string_prepend(query_locale(D_FMT), "^");
	s_datetime_locale -> date_formats[1] = "%a, %b %#d, %#Y";
	s_datetime_locale -> date_formats[2] = "%A, %B %#d, %#Y" ;

	s_datetime_locale -> time_formats[0] = string_prepend(swap_time_tokens(query_locale(T_FMT_AMPM)), "!");
	s_datetime_locale -> time_formats[1] = string_prepend(swap_time_tokens(query_locale(T_FMT_AMPM)), "");

	s_datetime_locale -> time24_formats[0] = "!%H:%M" ;
	s_datetime_locale -> time24_formats[1] = "!%H:%M:%S" ;

	s_datetime_locale -> time_morning_suffix = "AM";
	s_datetime_locale -> time_evening_suffix = "PM";
	

}
extern MCDateTimeLocale *g_english_locale;
const MCDateTimeLocale *MCS_getdatetimelocale(void)
{
	if (s_datetime_locale == NULL)
	{
		char *old_locale, *stored_locale;
		old_locale = setlocale(LC_ALL, NULL);		// Query the current locale
		stored_locale = strdup(old_locale);
	
		setlocale(LC_ALL, "");						// Set the locale using the LANG environment
		cache_locale();
		setlocale(LC_ALL, stored_locale);			// Restore the locale
		free(stored_locale);
	}
	
	return s_datetime_locale;
}

#elif defined(_ANDROID_MOBILE) || defined(_DARWIN_SERVER)

const MCDateTimeLocale *MCS_getdatetimelocale(void)
{
	extern MCDateTimeLocale g_english_locale;
	return &g_english_locale;
}

#endif

////////////////////////////////////////////////////////////////////////////////
