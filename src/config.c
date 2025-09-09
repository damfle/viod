#include "viod.h"
#include <ctype.h>

static char *trim_whitespace(char *str) {
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    end[1] = '\0';
    return str;
}

static device_kind_t parse_device_kind(const char *kind_str) {
    if (strcmp(kind_str, "net") == 0) {
        return DEVICE_KIND_NET;
    } else if (strcmp(kind_str, "gpu") == 0) {
        return DEVICE_KIND_GPU;
    } else if (strcmp(kind_str, "dev") == 0) {
        return DEVICE_KIND_DEV;
    }
    return DEVICE_KIND_DEV; // Default fallback
}

static int parse_section(const char *line, char *section) {
    if (line[0] == '[' && line[strlen(line)-1] == ']') {
        strncpy(section, line + 1, MAX_NAME_LEN - 1);
        section[strlen(section) - 1] = '\0'; // Remove closing bracket
        return 1;
    }
    return 0;
}

static int parse_key_value(const char *line, char *key, char *value) {
    char *eq = strchr(line, '=');
    if (!eq) return 0;
    
    *eq = '\0';
    strcpy(key, trim_whitespace((char*)line));
    strcpy(value, trim_whitespace(eq + 1));
    *eq = '='; // Restore original line
    
    return 1;
}

int parse_config_file(const char *filename, pf_config_t *config) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        log_message(LOG_ERR, "Cannot open config file %s: %s", filename, strerror(errno));
        return -1;
    }
    
    char line[MAX_LINE_LEN];
    char section[MAX_NAME_LEN] = "";
    char key[MAX_NAME_LEN], value[MAX_NAME_LEN];
    int current_vf = -1;
    
    // Initialize config
    memset(config, 0, sizeof(pf_config_t));
    strncpy(config->config_file, filename, MAX_NAME_LEN - 1);
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        // Skip empty lines and comments
        char *trimmed = trim_whitespace(line);
        if (strlen(trimmed) == 0 || trimmed[0] == '#') {
            continue;
        }
        
        // Parse section headers
        if (parse_section(trimmed, section)) {
            if (strcmp(section, "pf") == 0) {
                current_vf = -1;
            } else if (strncmp(section, "vf", 2) == 0) {
                current_vf = atoi(section + 2);
                if (current_vf >= 0 && current_vf < MAX_VFS) {
                    config->vfs[current_vf].id = current_vf;
                }
            }
            continue;
        }
        
        // Parse key-value pairs
        if (!parse_key_value(trimmed, key, value)) {
            continue;
        }
        
        if (current_vf == -1) {
            // PF section
            if (strcmp(key, "name") == 0) {
                strncpy(config->name, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "kind") == 0) {
                config->kind = parse_device_kind(value);
            } else if (strcmp(key, "vfs") == 0) {
                config->num_vfs = atoi(value);
            } else if (strcmp(key, "promisc") == 0) {
                config->promisc = (strcmp(value, "on") == 0 || strcmp(value, "yes") == 0);
            }
        } else if (current_vf >= 0 && current_vf < MAX_VFS) {
            // VF section
            vf_config_t *vf = &config->vfs[current_vf];
            if (strcmp(key, "driver") == 0) {
                strncpy(vf->driver, value, MAX_NAME_LEN - 1);
            } else if (strcmp(key, "mac") == 0) {
                strncpy(vf->mac, value, 17);
            } else if (strcmp(key, "vlan") == 0) {
                vf->vlan = atoi(value);
            }
        }
    }
    
    fclose(file);
    
    log_message(LOG_INFO, "Parsed config %s: PF=%s, kind=%d, vfs=%d", 
               filename, config->name, config->kind, config->num_vfs);
    
    return 0;
}

int load_all_configs(config_list_t *configs) {
    DIR *dir = opendir(CONFIG_DIR);
    if (!dir) {
        log_message(LOG_ERR, "Cannot open config directory %s: %s", 
                   CONFIG_DIR, strerror(errno));
        return -1;
    }
    
    // Initialize config list
    configs->capacity = 16;
    configs->configs = malloc(configs->capacity * sizeof(pf_config_t));
    configs->count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;
        
        // Check for .conf extension
        char *ext = strrchr(entry->d_name, '.');
        if (!ext || strcmp(ext, ".conf") != 0) continue;
        
        // Expand capacity if needed
        if (configs->count >= configs->capacity) {
            configs->capacity *= 2;
            configs->configs = realloc(configs->configs, 
                                     configs->capacity * sizeof(pf_config_t));
        }
        
        // Parse config file
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%s", CONFIG_DIR, entry->d_name);
        
        if (parse_config_file(filepath, &configs->configs[configs->count]) == 0) {
            configs->count++;
        }
    }
    
    closedir(dir);
    return 0;
}

void cleanup_configs(config_list_t *configs) {
    if (configs->configs) {
        free(configs->configs);
        configs->configs = NULL;
    }
    configs->count = 0;
    configs->capacity = 0;
}
