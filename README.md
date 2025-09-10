# viod

viod is a systemd-like and systemd-compatible daemon for managing SR-IOV
Virtual Functions (VFs). It has special empathy for network devices, but
also supports GPU VFs and other SR-IOV capable hardware.

It is implemented in pure C and leverages kernel libraries directly for
maximum performance and minimal overhead.

Configuration files are simple `.conf` files located in `/etc/vio.d/`.
viod automatically discovers, loads, and applies all configurations,
monitoring for changes in real-time.

This project is mostly vibe coded. Use at your own risk.

------------------------------------------------------------------------

## Building and Installation

```bash
# Build
make

# Install
sudo make install

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable viod
sudo systemctl start viod
```

------------------------------------------------------------------------

## Configuration Model

Each configuration file describes a Physical Function (PF) and its
Virtual Functions (VFs). All files use the `.conf` extension.

Every \[pf\] section must include a `kind` field to define the device
type:

-   `kind = net` → Network device (NIC PF/VFs)
-   `kind = gpu` → GPU device (GPU PF/VFs)
-   `kind = dev` → Generic SR-IOV device

This allows viod to apply the correct defaults and validations.

**PCI Address Format**: Both short (`05:00.0`) and full (`0000:05:00.0`) 
PCI address formats are supported. Short format matches `lspci` output 
and is automatically expanded to full format internally.

------------------------------------------------------------------------

## Key Features

-   SR-IOV VF lifecycle management
    -   Create and configure VFs from a PF.
    -   Optionally override or bind a custom driver per VF.
-   Networking-specific capabilities (`kind = net`)
    -   Enable promiscuous mode on the PF.
    -   Configure hardware VLAN tagging and untagging.
    -   Assign static MAC addresses to VFs.
    -   Control bandwidth or rate limiting (where hardware allows).
-   GPU support (`kind = gpu`)
    -   Manage creation and driver binding of GPU VFs.
    -   Integrate cleanly with passthrough to VMs or containers.
-   Generic device support (`kind = dev`)
    -   Unified configuration for any SR-IOV capable device.
-   Automatic operation
    -   Discovers and applies all configurations automatically.
    -   Monitors configuration directory for real-time updates.
    -   Integrates with systemd for service management.
-   Lightweight & efficient
    -   Written in pure C with minimal dependencies.
    -   Designed for high-performance environments.

------------------------------------------------------------------------

## Example Configurations

### Network device (`/etc/vio.d/net0.conf`)

    [pf]
    name = 05:00.0
    kind = net
    vfs = 4
    promisc = on

    [vf0]
    driver = igbvf
    mac = 52:54:00:ab:cd:01
    vlan = 100

    [vf1]
    mac = 52:54:00:ab:cd:02

### GPU device (`/etc/vio.d/gpu0.conf`)

    [pf]
    name = 65:00.0
    kind = gpu
    vfs = 2

    [vf0]
    driver = nvidia

### Generic device (`/etc/vio.d/accel.conf`)

    [pf]
    name = 81:00.0
    kind = dev
    vfs = 8

------------------------------------------------------------------------

## Usage

viod operates as a pure daemon with no command-line interface:

-   **Add a device**: Drop a `.conf` file into `/etc/vio.d/` - viod automatically detects and applies it
-   **Modify a device**: Edit the `.conf` file - viod reloads automatically  
-   **Remove a device**: Delete the `.conf` file - viod detects the change
-   **Manual reload**: Send SIGHUP: `sudo systemctl reload viod`
-   **Monitor logs**: `journalctl -u viod -f`

------------------------------------------------------------------------

## Typical Use Cases

-   Networking / NFV: Isolate workloads with NIC VFs.
-   GPU sharing: Provide GPU VFs to containers or VMs.
-   Cloud & virtualization: Expose hardware VFs (NICs, GPUs,
    accelerators) to tenants.
-   High-performance computing (HPC): Offload workloads to hardware VFs.
-   CI/CD: Rapidly provision and recycle VFs across multiple device
    classes.