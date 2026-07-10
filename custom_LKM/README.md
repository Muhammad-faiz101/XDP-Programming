# Custom Linux Kernel Module (LKM)

A minimal, boilerplate loadable kernel module (LKM) written in C that demonstrates how to securely inject and remove custom code dynamically inside Linux kernel space.

## Prerequisites

Before compiling, update your package repository list and install the necessary building frameworks along with your operating system's exact matching development kernel headers:

```bash
sudo apt update
sudo apt install build-essential linux-headers-\$(uname -r)
```

## Project Files

*   **`cust_m1.c`**: The primary C source script containing the initialization/cleanup logic and system level print macros (`printk`).
*   **`Makefile`**: The build automation layout configuration mapping compiling vectors straight to your active system kernel architecture path (`/lib/modules/$(shell uname -r)/build`).

---

## Execution Flow

Follow these sequential steps in your terminal to compile, track, insert, and unload the module:

### 1. Compile the Module
Build your project binaries using the configuration `Makefile`. This outputs a specialized binary kernel object file (`.ko`):
```bash
make
```

### 2. Dynamically Insert into the Kernel
Inject the freshly built module straight into active system RAM layout space (requires administrative privileges):
```bash
sudo insmod cust_m1.ko
```

### 3. Verify Active Module State
Cross-reference the kernel module subsystem mapping registry (`/proc/modules`) to prove the object is running live:
```bash
lsmod | grep cust
```

### 4. Inspect Live Kernel Logs (Load Event)
Read the ring output diagnostics stream to read your initialization function payload log:
```bash
sudo dmesg --follow
```
*Expected Output line:* `[ ] custom mod loaded!`

### 5. Remove the Module
Unlink and purge the module cleanly out of execution memory layout tracks:
```bash
sudo rmmod cust_m1
```

### 6. Inspect Live Kernel Logs (Exit Event)
Check the diagnostic stream logs one last time to confirm your cleanup callback routine exited cleanly:
```bash
sudo dmesg --follow
```
*Expected Output line:* `[ ] custom mod unloaded now`

### 7. Clean Local Directory
Wipe out intermediate binaries, object tracking markers, and local compilation mess generated during compilation:
```bash
make clean
```
