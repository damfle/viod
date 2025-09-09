#include "viod.h"

static volatile int running = 1;

static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        log_message(LOG_INFO, "Received signal %d, shutting down", sig);
        running = 0;
    } else if (sig == SIGHUP) {
        log_message(LOG_INFO, "Received SIGHUP, reloading configurations");
        // Signal will be handled in main loop
    }
}

static int watch_config_directory(void) {
    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        log_message(LOG_ERR, "Failed to initialize inotify: %s", strerror(errno));
        return -1;
    }
    
    int watch_fd = inotify_add_watch(inotify_fd, CONFIG_DIR, 
                                    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM);
    if (watch_fd < 0) {
        log_message(LOG_ERR, "Failed to watch config directory %s: %s", 
                   CONFIG_DIR, strerror(errno));
        close(inotify_fd);
        return -1;
    }
    
    return inotify_fd;
}

static int reload_configurations(config_list_t *configs) {
    log_message(LOG_INFO, "Reloading configurations");
    
    // Clean up old configs
    cleanup_configs(configs);
    
    // Load new configs
    if (load_all_configs(configs) != 0) {
        log_message(LOG_ERR, "Failed to load configurations");
        return -1;
    }
    
    // Apply all configurations
    if (apply_all_configs(configs) != 0) {
        log_message(LOG_ERR, "Failed to apply configurations");
        return -1;
    }
    
    log_message(LOG_INFO, "Successfully reloaded %zu configuration(s)", configs->count);
    return 0;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv; // Suppress unused parameter warnings
    
    config_list_t configs = {0};
    int inotify_fd = -1;
    
    // Setup signal handlers
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    
    // Open syslog
    openlog("viod", LOG_PID | LOG_CONS, LOG_DAEMON);
    
    log_message(LOG_INFO, "viod starting - SR-IOV VF daemon");
    
    // Create config directory if it doesn't exist
    if (mkdir(CONFIG_DIR, 0755) != 0 && errno != EEXIST) {
        log_message(LOG_ERR, "Failed to create config directory %s: %s", 
                   CONFIG_DIR, strerror(errno));
        return 1;
    }
    
    // Load initial configurations
    if (reload_configurations(&configs) != 0) {
        log_message(LOG_ERR, "Failed to load initial configurations");
        return 1;
    }
    
    // Setup file system monitoring
    inotify_fd = watch_config_directory();
    if (inotify_fd < 0) {
        log_message(LOG_WARNING, "File system monitoring disabled");
    } else {
        log_message(LOG_INFO, "Monitoring %s for configuration changes", CONFIG_DIR);
    }
    
    // Main daemon loop
    while (running) {
        if (inotify_fd >= 0) {
            fd_set readfds;
            struct timeval timeout;
            
            FD_ZERO(&readfds);
            FD_SET(inotify_fd, &readfds);
            
            timeout.tv_sec = 5;  // 5 second timeout
            timeout.tv_usec = 0;
            
            int ret = select(inotify_fd + 1, &readfds, NULL, NULL, &timeout);
            
            if (ret > 0 && FD_ISSET(inotify_fd, &readfds)) {
                // Configuration file changed
                char buffer[4096];
                ssize_t length = read(inotify_fd, buffer, sizeof(buffer));
                
                if (length > 0) {
                    log_message(LOG_INFO, "Configuration directory changed, reloading");
                    // Small delay to ensure file operations are complete
                    sleep(1);
                    reload_configurations(&configs);
                }
            }
        } else {
            // No file monitoring, just sleep
            sleep(5);
        }
    }
    
    // Cleanup on shutdown
    log_message(LOG_INFO, "viod shutting down");
    
    if (inotify_fd >= 0) {
        close(inotify_fd);
    }
    
    cleanup_configs(&configs);
    closelog();
    
    return 0;
}
