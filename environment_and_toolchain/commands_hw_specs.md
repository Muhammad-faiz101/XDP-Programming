### **Linux System Hardware Commands**


#### **CPU**
* `lscpu`  
  Displays processor architecture, core counts, thread counts, and clock speeds.

#### **RAM Memory**
* `free -h`  
  Shows total, used, and available RAM in human-readable units.
* `sudo dmidecode -t memory`  
  Provides detailed hardware specs for exact stick speeds, sizes, and slot usage.

#### **Storage & Disks**
* `lsblk`  
  Lists all hard drives, SSDs, and active partitions in a tree layout.

#### **Graphics & PCI**
* `lspci -nnk | grep -A3 VGA`  
  Identifies the exact graphics card model and currently active driver module.

#### **USB Devices**
* `lsusb`  
  Lists all external and internal devices currently plugged into your USB ports.

#### **NIC **
* `sudo lshw -C network`  
   print out the technical specifications of  network components:.

* `lspci -nnk | grep -A3 -i net`  
    Lists all PCI-connected network hardware (like your Ethernet and Wi-Fi cards), along with the exact kernel driver currently managing them.