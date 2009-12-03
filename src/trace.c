#include <syslog.h>
#include <glib.h>
#include <string.h>

#include "spmfilter.h"

static trace_t TRACE_SYSLOG = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;  /* default: emerg, alert, crit, err, warning */

#define SYSLOGFORMAT "[%p] %s:[%s] %s(+%d): %s"

#define min(x,y) ((x)<=(y)?(x):(y))

static const char * trace_to_text(trace_t level) {
	const char * const trace_text[] = {
		"EMERGENCY",
		"Alert",
		"Critical",
		"Error",
		"Warning",
		"Notice",
		"Info",
		"Debug",
		"Lookup"
	};
	return trace_text[ilogb((double) level)];
}

void trace(trace_t level, const char * module, const char * function, int line, const char *formatstring, ...) {	
	trace_t syslog_level;
	va_list ap, cp;

	gchar *message;
	static int configured=0;
	size_t l, maxlen=120;

	/* Return now if we're not logging anything. */
	if (! level)
		return;

	va_start(ap, formatstring);
	va_copy(cp, ap);
	message = g_strdup_vprintf(formatstring, cp);
	va_end(cp);

	l = strlen(message);
	
	if (message[l] == '\n')
		message[l] = '\0';

	if (level) {
		/* Convert our extended log levels (>128) to syslog levels */
		switch((int)ilogb((double) level)) {
			case 0:
				syslog_level = LOG_EMERG;
				break;
			case 1:
				syslog_level = LOG_ALERT;
				break;
			case 2:
				syslog_level = LOG_CRIT;
				break;
			case 3:
				syslog_level = LOG_ERR;
				break;
			case 4:
				syslog_level = LOG_WARNING;
				break;
			case 5:
				syslog_level = LOG_NOTICE;
				break;
			case 6:
				syslog_level = LOG_INFO;
				break;
			case 7:
				syslog_level = LOG_DEBUG;
				break;
			case 8:
				syslog_level = LOG_DEBUG;
				break;
			default:
				syslog_level = LOG_DEBUG;
				break;
		}
		size_t w = min(l,maxlen);
		message[w] = '\0';
		
		if (level >= 128 & debug == 1) 
			syslog(syslog_level, SYSLOGFORMAT, g_thread_self(), trace_to_text(level), module, function, line, message);
		else if (level < 128)
			syslog(syslog_level, SYSLOGFORMAT, g_thread_self(), trace_to_text(level), module, function, line, message);
	}
	g_free(message);

}