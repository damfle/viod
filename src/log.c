/**
 * viod - SR-IOV Virtual Function daemon
 * 
 * Logging implementation
 * Provides unified logging to both syslog and stderr for daemon and interactive modes.
 */
#include "viod.h"
#include <stdarg.h>

/**
 * Log a message with the specified priority
 * Logs to syslog and optionally to stderr if running interactively
 */
void log_message(int priority, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    /* Log to syslog (for daemon mode) */
    vsyslog(priority, format, args);
    
    /* Also log to stderr for interactive mode */
    if (isatty(STDERR_FILENO)) {
        const char *level_str;
        switch (priority) {
            case LOG_ERR:     level_str = "ERROR"; break;
            case LOG_WARNING: level_str = "WARNING"; break;
            case LOG_INFO:    level_str = "INFO"; break;
            case LOG_DEBUG:   level_str = "DEBUG"; break;
            default:          level_str = "UNKNOWN"; break;
        }
        
        fprintf(stderr, "[%s] ", level_str);
        vfprintf(stderr, format, args);
        fprintf(stderr, "\n");
    }
    
    va_end(args);
}
