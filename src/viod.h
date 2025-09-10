#ifndef VIOD_H
#define VIOD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <syslog.h>
#include <signal.h>
#include <sys/inotify.h>

#define CONFIG_DIR "/etc/vio.d"
#define MAX_VFS 256
#define MAX_NAME_LEN 256
#define MAX_LINE_LEN 1024

typedef enum {
    DEVICE_KIND_NET,
    DEVICE_KIND_GPU,
    DEVICE_KIND_DEV
} device_kind_t;

typedef struct {
    int id;
    char driver[MAX_NAME_LEN];
    char mac[18]; // MAC address for network devices
    int vlan;     // VLAN ID for network devices
} vf_config_t;

typedef struct {
    char name[MAX_NAME_LEN];
    device_kind_t kind;
    int num_vfs;
    int promisc; // For network devices
    vf_config_t vfs[MAX_VFS];
    char config_file[MAX_NAME_LEN];
} pf_config_t;

typedef struct {
    pf_config_t *configs;
    size_t count;
    size_t capacity;
} config_list_t;

// Function declarations
int parse_config_file(const char *filename, pf_config_t *config);
int load_all_configs(config_list_t *configs);
int apply_all_configs(config_list_t *configs);
int create_vfs(pf_config_t *config);
int configure_vf(pf_config_t *pf_config, vf_config_t *vf_config);
int enable_promiscuous_mode(const char *interface);
int set_vf_mac(const char *pf_name, int vf_id, const char *mac);
int set_vf_vlan(const char *pf_name, int vf_id, int vlan);
int bind_vf_driver(const char *pci_addr, const char *driver);
int get_vf_pci_address(const char *pf_name, int vf_id, char *vf_pci_addr, size_t addr_size);
int get_pf_pci_address(const char *pf_name, device_kind_t kind, char *pf_pci_addr, size_t addr_size);
int normalize_pci_address(const char *input_addr, char *normalized_addr, size_t addr_size);
void cleanup_configs(config_list_t *configs);
void log_message(int priority, const char *format, ...);

#endif // VIOD_H
