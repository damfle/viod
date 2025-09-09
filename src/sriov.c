#include "viod.h"

static int write_sysfs_value(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        log_message(LOG_ERR, "Cannot open %s: %s", path, strerror(errno));
        return -1;
    }
    
    ssize_t len = strlen(value);
    if (write(fd, value, len) != len) {
        log_message(LOG_ERR, "Cannot write to %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    close(fd);
    return 0;
}

int apply_all_configs(config_list_t *configs) {
    log_message(LOG_INFO, "Applying %zu configuration(s)", configs->count);
    
    for (size_t i = 0; i < configs->count; i++) {
        if (create_vfs(&configs->configs[i]) != 0) {
            log_message(LOG_WARNING, "Failed to apply configuration %s", 
                       configs->configs[i].config_file);
        }
    }
    
    return 0;
}

int create_vfs(pf_config_t *config) {
    char sysfs_path[512];
    char num_vfs_str[16];
    
    log_message(LOG_INFO, "Creating %d VFs for PF %s", config->num_vfs, config->name);
    
    // Construct sysfs path based on device type
    if (config->kind == DEVICE_KIND_NET) {
        // For network devices, use the interface name to find PCI path
        snprintf(sysfs_path, sizeof(sysfs_path), 
                "/sys/class/net/%s/device/sriov_numvfs", config->name);
    } else {
        // For GPU and generic devices, assume PCI address format
        snprintf(sysfs_path, sizeof(sysfs_path), 
                "/sys/bus/pci/devices/%s/sriov_numvfs", config->name);
    }
    
    // First, disable existing VFs
    if (write_sysfs_value(sysfs_path, "0") != 0) {
        log_message(LOG_WARNING, "Failed to disable existing VFs for %s", config->name);
    }
    
    // Wait a moment for cleanup
    usleep(100000); // 100ms
    
    // Create new VFs
    snprintf(num_vfs_str, sizeof(num_vfs_str), "%d", config->num_vfs);
    if (write_sysfs_value(sysfs_path, num_vfs_str) != 0) {
        log_message(LOG_ERR, "Failed to create VFs for %s", config->name);
        return -1;
    }
    
    // Wait for VF creation
    usleep(500000); // 500ms
    
    // Configure each VF
    for (int i = 0; i < config->num_vfs; i++) {
        // Ensure VF has a valid ID (in case it wasn't explicitly configured)
        if (config->vfs[i].id < 0) {
            config->vfs[i].id = i;
        }
        
        if (configure_vf(config, &config->vfs[i]) != 0) {
            log_message(LOG_WARNING, "Failed to configure VF %d for %s", i, config->name);
        }
    }
    
    // Enable promiscuous mode if requested (network devices only)
    if (config->kind == DEVICE_KIND_NET && config->promisc) {
        if (enable_promiscuous_mode(config->name) != 0) {
            log_message(LOG_WARNING, "Failed to enable promiscuous mode on %s", config->name);
        }
    }
    
    log_message(LOG_INFO, "Successfully created and configured %d VFs for %s", 
               config->num_vfs, config->name);
    
    return 0;
}

int get_pf_pci_address(const char *pf_name, device_kind_t kind, char *pf_pci_addr, size_t addr_size) {
    char path[512];
    
    if (kind == DEVICE_KIND_NET) {
        // For network devices, resolve interface name to PCI address
        snprintf(path, sizeof(path), "/sys/class/net/%s/device", pf_name);
        
        char resolved_path[512];
        ssize_t len = readlink(path, resolved_path, sizeof(resolved_path) - 1);
        if (len == -1) {
            log_message(LOG_ERR, "Cannot resolve PCI address for network interface %s", pf_name);
            return -1;
        }
        resolved_path[len] = '\0';
        
        // Extract PCI address from path like "../../../0000:05:00.0"
        char *pci_addr = strrchr(resolved_path, '/');
        if (!pci_addr) {
            log_message(LOG_ERR, "Invalid PCI path format for %s", pf_name);
            return -1;
        }
        pci_addr++; // Skip the '/'
        
        strncpy(pf_pci_addr, pci_addr, addr_size - 1);
        pf_pci_addr[addr_size - 1] = '\0';
    } else {
        // For GPU and generic devices, assume pf_name is already a PCI address
        strncpy(pf_pci_addr, pf_name, addr_size - 1);
        pf_pci_addr[addr_size - 1] = '\0';
    }
    
    return 0;
}

int get_vf_pci_address(const char *pf_name, int vf_id, char *vf_pci_addr, size_t addr_size) {
    char pf_pci_addr[64];
    char virtfn_path[512];
    char resolved_path[512];
    device_kind_t kind;
    
    // Try to determine device kind - check if it's a network interface first
    char net_path[512];
    snprintf(net_path, sizeof(net_path), "/sys/class/net/%s", pf_name);
    if (access(net_path, F_OK) == 0) {
        kind = DEVICE_KIND_NET;
    } else {
        // Assume it's already a PCI address for GPU/generic devices
        kind = DEVICE_KIND_GPU; // or DEVICE_KIND_DEV, doesn't matter for PCI address handling
    }
    
    // First get the PF PCI address
    if (get_pf_pci_address(pf_name, kind, pf_pci_addr, sizeof(pf_pci_addr)) != 0) {
        return -1;
    }
    
    // Build path to VF symlink
    snprintf(virtfn_path, sizeof(virtfn_path), "/sys/bus/pci/devices/%s/virtfn%d", pf_pci_addr, vf_id);
    
    // Resolve the symlink to get VF PCI address
    ssize_t len = readlink(virtfn_path, resolved_path, sizeof(resolved_path) - 1);
    if (len == -1) {
        log_message(LOG_ERR, "Cannot find VF %d for PF %s (path: %s): %s", 
                   vf_id, pf_name, virtfn_path, strerror(errno));
        return -1;
    }
    resolved_path[len] = '\0';
    
    // Extract VF PCI address from path like "../0000:05:10.0"
    char *vf_addr = strrchr(resolved_path, '/');
    if (!vf_addr) {
        log_message(LOG_ERR, "Invalid VF PCI path format: %s", resolved_path);
        return -1;
    }
    vf_addr++; // Skip the '/'
    
    strncpy(vf_pci_addr, vf_addr, addr_size - 1);
    vf_pci_addr[addr_size - 1] = '\0';
    
    log_message(LOG_INFO, "Found VF %d PCI address: %s", vf_id, vf_pci_addr);
    return 0;
}

int configure_vf(pf_config_t *pf_config, vf_config_t *vf_config) {
    if (vf_config->id < 0) {
        return 0; // Skip unconfigured VFs
    }
    
    log_message(LOG_INFO, "Configuring VF %d for PF %s", vf_config->id, pf_config->name);
    
    // Set MAC address (network devices only)
    if (pf_config->kind == DEVICE_KIND_NET && strlen(vf_config->mac) > 0) {
        if (set_vf_mac(pf_config->name, vf_config->id, vf_config->mac) != 0) {
            log_message(LOG_WARNING, "Failed to set MAC for VF %d", vf_config->id);
        }
    }
    
    // Set VLAN (network devices only)
    if (pf_config->kind == DEVICE_KIND_NET && vf_config->vlan > 0) {
        if (set_vf_vlan(pf_config->name, vf_config->id, vf_config->vlan) != 0) {
            log_message(LOG_WARNING, "Failed to set VLAN for VF %d", vf_config->id);
        }
    }
    
    // Bind driver if specified
    if (strlen(vf_config->driver) > 0) {
        char vf_pci_addr[64];
        
        if (get_vf_pci_address(pf_config->name, vf_config->id, vf_pci_addr, sizeof(vf_pci_addr)) == 0) {
            if (bind_vf_driver(vf_pci_addr, vf_config->driver) != 0) {
                log_message(LOG_WARNING, "Failed to bind driver %s for VF %d (%s)", 
                           vf_config->driver, vf_config->id, vf_pci_addr);
            }
        } else {
            log_message(LOG_WARNING, "Cannot get PCI address for VF %d, skipping driver binding", 
                       vf_config->id);
        }
    }
    
    return 0;
}

int enable_promiscuous_mode(const char *interface) {
    char command[256];
    snprintf(command, sizeof(command), "ip link set %s promisc on", interface);
    
    int result = system(command);
    if (result != 0) {
        log_message(LOG_ERR, "Failed to enable promiscuous mode on %s", interface);
        return -1;
    }
    
    log_message(LOG_INFO, "Enabled promiscuous mode on %s", interface);
    return 0;
}

int set_vf_mac(const char *pf_name, int vf_id, const char *mac) {
    char command[256];
    snprintf(command, sizeof(command), "ip link set %s vf %d mac %s", 
             pf_name, vf_id, mac);
    
    int result = system(command);
    if (result != 0) {
        log_message(LOG_ERR, "Failed to set MAC %s for VF %d on %s", 
                   mac, vf_id, pf_name);
        return -1;
    }
    
    log_message(LOG_INFO, "Set MAC %s for VF %d on %s", mac, vf_id, pf_name);
    return 0;
}

int set_vf_vlan(const char *pf_name, int vf_id, int vlan) {
    char command[256];
    snprintf(command, sizeof(command), "ip link set %s vf %d vlan %d", 
             pf_name, vf_id, vlan);
    
    int result = system(command);
    if (result != 0) {
        log_message(LOG_ERR, "Failed to set VLAN %d for VF %d on %s", 
                   vlan, vf_id, pf_name);
        return -1;
    }
    
    log_message(LOG_INFO, "Set VLAN %d for VF %d on %s", vlan, vf_id, pf_name);
    return 0;
}

int bind_vf_driver(const char *pci_addr, const char *driver) {
    char unbind_path[256], bind_path[256], driver_path[256], new_id_path[256];
    char current_driver[256];
    char vendor_device[32];
    
    // Special handling for vfio-pci driver
    if (strcmp(driver, "vfio-pci") == 0) {
        // Check if vfio-pci module is loaded, if not try to load it
        snprintf(driver_path, sizeof(driver_path), "/sys/bus/pci/drivers/%s", driver);
        if (access(driver_path, F_OK) != 0) {
            log_message(LOG_INFO, "Loading vfio-pci module");
            system("modprobe vfio-pci");
            usleep(500000); // Wait for module to load
        }
    }
    
    // Check if target driver exists
    snprintf(driver_path, sizeof(driver_path), "/sys/bus/pci/drivers/%s", driver);
    if (access(driver_path, F_OK) != 0) {
        log_message(LOG_ERR, "Driver %s not available in system", driver);
        return -1;
    }
    
    // Check if VF device exists
    snprintf(driver_path, sizeof(driver_path), "/sys/bus/pci/devices/%s", pci_addr);
    if (access(driver_path, F_OK) != 0) {
        log_message(LOG_ERR, "VF device %s not found", pci_addr);
        return -1;
    }
    
    // Get current driver if any
    snprintf(unbind_path, sizeof(unbind_path), 
            "/sys/bus/pci/devices/%s/driver", pci_addr);
    
    ssize_t len = readlink(unbind_path, current_driver, sizeof(current_driver) - 1);
    if (len > 0) {
        current_driver[len] = '\0';
        
        // Extract driver name
        char *driver_name = strrchr(current_driver, '/');
        if (driver_name) {
            driver_name++;
            
            // Skip if already bound to desired driver
            if (strcmp(driver_name, driver) == 0) {
                log_message(LOG_INFO, "VF %s already bound to driver %s", pci_addr, driver);
                return 0;
            }
            
            // Unbind from current driver
            snprintf(unbind_path, sizeof(unbind_path), 
                    "/sys/bus/pci/drivers/%s/unbind", driver_name);
            log_message(LOG_INFO, "Unbinding %s from driver %s", pci_addr, driver_name);
            write_sysfs_value(unbind_path, pci_addr);
            
            // Small delay to ensure unbind completes
            usleep(200000); // 200ms
        }
    }
    
    // For vfio-pci, we might need to add the device ID first
    if (strcmp(driver, "vfio-pci") == 0) {
        // Read vendor and device ID
        char vendor_path[256], device_path[256];
        char vendor_id[8], device_id[8];
        
        snprintf(vendor_path, sizeof(vendor_path), "/sys/bus/pci/devices/%s/vendor", pci_addr);
        snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/device", pci_addr);
        
        FILE *vendor_file = fopen(vendor_path, "r");
        FILE *device_file = fopen(device_path, "r");
        
        if (vendor_file && device_file) {
            if (fgets(vendor_id, sizeof(vendor_id), vendor_file) &&
                fgets(device_id, sizeof(device_id), device_file)) {
                
                // Remove 0x prefix and newlines
                char *v = vendor_id + 2;  // Skip "0x"
                char *d = device_id + 2;  // Skip "0x"
                v[strcspn(v, "\n")] = 0;
                d[strcspn(d, "\n")] = 0;
                
                snprintf(vendor_device, sizeof(vendor_device), "%s %s", v, d);
                
                // Add device ID to vfio-pci driver
                snprintf(new_id_path, sizeof(new_id_path), 
                        "/sys/bus/pci/drivers/vfio-pci/new_id");
                
                log_message(LOG_INFO, "Adding device ID %s to vfio-pci", vendor_device);
                write_sysfs_value(new_id_path, vendor_device);
                usleep(100000); // 100ms
            }
        }
        
        if (vendor_file) fclose(vendor_file);
        if (device_file) fclose(device_file);
    }
    
    // Bind to new driver
    snprintf(bind_path, sizeof(bind_path), 
            "/sys/bus/pci/drivers/%s/bind", driver);
    
    if (write_sysfs_value(bind_path, pci_addr) != 0) {
        log_message(LOG_ERR, "Failed to bind %s to driver %s", pci_addr, driver);
        return -1;
    }
    
    log_message(LOG_INFO, "Successfully bound %s to driver %s", pci_addr, driver);
    return 0;
}
