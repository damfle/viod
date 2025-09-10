/**
 * viod - SR-IOV Virtual Function daemon
 * 
 * Main header file containing all data structures and function declarations
 * for managing SR-IOV Virtual Functions across network, GPU, and generic devices.
 */
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
#include <openssl/sha.h>

/* Configuration constants */
#define CONFIG_DIR "/etc/vio.d"
#define MAX_VFS 256
#define MAX_NAME_LEN 256
#define MAX_LINE_LEN 1024

/* Device type enumeration */
typedef enum {
    DEVICE_KIND_NET,    /**< Network device (NIC) */
    DEVICE_KIND_GPU,    /**< GPU device */
    DEVICE_KIND_DEV     /**< Generic SR-IOV device */
} device_kind_t;

/* Virtual Function configuration */
typedef struct {
    int id;                         /**< VF index (0-based) */
    char driver[MAX_NAME_LEN];      /**< Driver to bind to VF */
    char mac[18];                   /**< MAC address (network devices only) */
    int vlan;                       /**< VLAN ID (network devices only) */
} vf_config_t;

/* Physical Function configuration */
typedef struct {
    char name[MAX_NAME_LEN];        /**< PCI address (short or full format) */
    device_kind_t kind;             /**< Device type */
    int num_vfs;                    /**< Number of VFs to create */
    int promisc;                    /**< Enable promiscuous mode (network devices) */
    vf_config_t vfs[MAX_VFS];       /**< VF configurations */
    char config_file[MAX_NAME_LEN]; /**< Source configuration file path */
} pf_config_t;

/* Dynamic list of PF configurations */
typedef struct {
    pf_config_t *configs;           /**< Array of configurations */
    size_t count;                   /**< Number of active configurations */
    size_t capacity;                /**< Allocated capacity */
} config_list_t;

/* Function declarations */

/* Configuration management */
int parse_config_file(const char *filename, pf_config_t *config);
int load_all_configs(config_list_t *configs);
void cleanup_configs(config_list_t *configs);

/* SR-IOV operations */
int apply_all_configs(config_list_t *configs);
int create_vfs(pf_config_t *config);
int configure_vf(pf_config_t *pf_config, vf_config_t *vf_config);

/* Network device operations */
int enable_promiscuous_mode(const char *interface);
int set_vf_mac(const char *pf_name, int vf_id, const char *mac);
int set_vf_vlan(const char *pf_name, int vf_id, int vlan);
void generate_stable_mac(const char *pf_pci_addr, int vf_id, char *mac_addr);

/* Driver management */
int bind_vf_driver(const char *pci_addr, const char *driver);

/* PCI address utilities */
int get_vf_pci_address(const char *pf_name, int vf_id, char *vf_pci_addr, size_t addr_size);
int get_pf_pci_address(const char *pf_name, char *pf_pci_addr, size_t addr_size);
int normalize_pci_address(const char *input_addr, char *normalized_addr, size_t addr_size);

/* Logging */
void log_message(int priority, const char *format, ...);

#endif // VIOD_H
