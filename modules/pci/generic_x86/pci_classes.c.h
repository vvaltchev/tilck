
/*
 * List of PCI IDs generated by scripts/dev/generate_pci_ids, part of the
 * Tilck project.
 *
 * Source data fetched from the `pciids` project:
 *    https://github.com/pciutils/pciids/blob/master/pci.ids?raw=true
 *
 * Follows the original comments from the top of the `pci.ids` file.
 *
 *
 *    List of PCI ID's
 *
 *    Version: 2020.08.13
 *    Date:    2020-08-13 03:15:02
 *
 *    Maintained by Albert Pool, Martin Mares, and other volunteers from
 *    the PCI ID Project at https://pci-ids.ucw.cz/.
 *
 *    New data are always welcome, especially if they are accurate. If you have
 *    anything to contribute, please follow the instructions at the web site.
 *
 *    This file can be distributed under either the GNU General Public License
 *    (version 2 or higher) or the 3-clause BSD License.
 *
 *    The database is a compilation of factual data, and as such the copyright
 *    only covers the aggregation and formatting. The copyright is held by
 *    Martin Mares and Albert Pool.
 *
 */

#include <tilck/mods/pci.h>

const struct pci_device_class pci_device_classes_list[] =
{
   { 0x00, 0x00, 0x00, "Unclassified device", "Non-VGA unclassified device", NULL },
   { 0x00, 0x01, 0x00, "Unclassified device", "VGA compatible unclassified device", NULL },
   { 0x01, 0x00, 0x00, "Mass storage controller", "SCSI storage controller", NULL },
   { 0x01, 0x01, 0x00, "Mass storage controller", "IDE interface", "ISA Compat. mode-only" },
   { 0x01, 0x01, 0x05, "Mass storage controller", "IDE interface", "PCI native mode-only" },
   { 0x01, 0x01, 0x0a, "Mass storage controller", "IDE interface", "ISA Compat. mode, w/ support of PCI native mode" },
   { 0x01, 0x01, 0x0f, "Mass storage controller", "IDE interface", "PCI native mode, w/ support of ISA compat. mode" },
   { 0x01, 0x01, 0x80, "Mass storage controller", "IDE interface", "ISA Compat. mode-only w/ bus master" },
   { 0x01, 0x01, 0x85, "Mass storage controller", "IDE interface", "PCI native mode-only w/ bus master" },
   { 0x01, 0x01, 0x8a, "Mass storage controller", "IDE interface", "ISA Compat. mode, w/ support of PCI native mode, w/ bus master" },
   { 0x01, 0x01, 0x8f, "Mass storage controller", "IDE interface", "PCI native mode, w/ support of ISA compat. mode, w/ bus master" },
   { 0x01, 0x02, 0x00, "Mass storage controller", "Floppy disk controller", NULL },
   { 0x01, 0x03, 0x00, "Mass storage controller", "IPI bus controller", NULL },
   { 0x01, 0x04, 0x00, "Mass storage controller", "RAID bus controller", NULL },
   { 0x01, 0x05, 0x20, "Mass storage controller", "ATA controller", "ADMA single stepping" },
   { 0x01, 0x05, 0x30, "Mass storage controller", "ATA controller", "ADMA continuous operation" },
   { 0x01, 0x06, 0x00, "Mass storage controller", "SATA controller", "Vendor specific" },
   { 0x01, 0x06, 0x01, "Mass storage controller", "SATA controller", "AHCI 1.0" },
   { 0x01, 0x06, 0x02, "Mass storage controller", "SATA controller", "Serial Storage Bus" },
   { 0x01, 0x07, 0x01, "Mass storage controller", "Serial Attached SCSI controller", "Serial Storage Bus" },
   { 0x01, 0x08, 0x01, "Mass storage controller", "Non-Volatile memory controller", "NVMHCI" },
   { 0x01, 0x08, 0x02, "Mass storage controller", "Non-Volatile memory controller", "NVM Express" },
   { 0x01, 0x80, 0x00, "Mass storage controller", "Mass storage controller", NULL },
   { 0x02, 0x00, 0x00, "Network controller", "Ethernet controller", NULL },
   { 0x02, 0x01, 0x00, "Network controller", "Token ring network controller", NULL },
   { 0x02, 0x02, 0x00, "Network controller", "FDDI network controller", NULL },
   { 0x02, 0x03, 0x00, "Network controller", "ATM network controller", NULL },
   { 0x02, 0x04, 0x00, "Network controller", "ISDN controller", NULL },
   { 0x02, 0x05, 0x00, "Network controller", "WorldFip controller", NULL },
   { 0x02, 0x06, 0x00, "Network controller", "PICMG controller", NULL },
   { 0x02, 0x07, 0x00, "Network controller", "Infiniband controller", NULL },
   { 0x02, 0x08, 0x00, "Network controller", "Fabric controller", NULL },
   { 0x02, 0x80, 0x00, "Network controller", "Network controller", NULL },
   { 0x03, 0x00, 0x00, "Display controller", "VGA compatible controller", "VGA controller" },
   { 0x03, 0x00, 0x01, "Display controller", "VGA compatible controller", "8514 controller" },
   { 0x03, 0x01, 0x00, "Display controller", "XGA compatible controller", NULL },
   { 0x03, 0x02, 0x00, "Display controller", "3D controller", NULL },
   { 0x03, 0x80, 0x00, "Display controller", "Display controller", NULL },
   { 0x04, 0x00, 0x00, "Multimedia controller", "Multimedia video controller", NULL },
   { 0x04, 0x01, 0x00, "Multimedia controller", "Multimedia audio controller", NULL },
   { 0x04, 0x02, 0x00, "Multimedia controller", "Computer telephony device", NULL },
   { 0x04, 0x03, 0x00, "Multimedia controller", "Audio device", NULL },
   { 0x04, 0x80, 0x00, "Multimedia controller", "Multimedia controller", NULL },
   { 0x05, 0x00, 0x00, "Memory controller", "RAM memory", NULL },
   { 0x05, 0x01, 0x00, "Memory controller", "FLASH memory", NULL },
   { 0x05, 0x80, 0x00, "Memory controller", "Memory controller", NULL },
   { 0x06, 0x00, 0x00, "Bridge", "Host bridge", NULL },
   { 0x06, 0x01, 0x00, "Bridge", "ISA bridge", NULL },
   { 0x06, 0x02, 0x00, "Bridge", "EISA bridge", NULL },
   { 0x06, 0x03, 0x00, "Bridge", "MicroChannel bridge", NULL },
   { 0x06, 0x04, 0x00, "Bridge", "PCI bridge", "Normal decode" },
   { 0x06, 0x04, 0x01, "Bridge", "PCI bridge", "Subtractive decode" },
   { 0x06, 0x05, 0x00, "Bridge", "PCMCIA bridge", NULL },
   { 0x06, 0x06, 0x00, "Bridge", "NuBus bridge", NULL },
   { 0x06, 0x07, 0x00, "Bridge", "CardBus bridge", NULL },
   { 0x06, 0x08, 0x00, "Bridge", "RACEway bridge", "Transparent mode" },
   { 0x06, 0x08, 0x01, "Bridge", "RACEway bridge", "Endpoint mode" },
   { 0x06, 0x09, 0x40, "Bridge", "Semi-transparent PCI-to-PCI bridge", "Primary bus towards host CPU" },
   { 0x06, 0x09, 0x80, "Bridge", "Semi-transparent PCI-to-PCI bridge", "Secondary bus towards host CPU" },
   { 0x06, 0x0a, 0x00, "Bridge", "InfiniBand to PCI host bridge", NULL },
   { 0x06, 0x80, 0x00, "Bridge", "Bridge", NULL },
   { 0x07, 0x00, 0x00, "Communication controller", "Serial controller", "8250" },
   { 0x07, 0x00, 0x01, "Communication controller", "Serial controller", "16450" },
   { 0x07, 0x00, 0x02, "Communication controller", "Serial controller", "16550" },
   { 0x07, 0x00, 0x03, "Communication controller", "Serial controller", "16650" },
   { 0x07, 0x00, 0x04, "Communication controller", "Serial controller", "16750" },
   { 0x07, 0x00, 0x05, "Communication controller", "Serial controller", "16850" },
   { 0x07, 0x00, 0x06, "Communication controller", "Serial controller", "16950" },
   { 0x07, 0x01, 0x00, "Communication controller", "Parallel controller", "SPP" },
   { 0x07, 0x01, 0x01, "Communication controller", "Parallel controller", "BiDir" },
   { 0x07, 0x01, 0x02, "Communication controller", "Parallel controller", "ECP" },
   { 0x07, 0x01, 0x03, "Communication controller", "Parallel controller", "IEEE1284" },
   { 0x07, 0x01, 0xfe, "Communication controller", "Parallel controller", "IEEE1284 Target" },
   { 0x07, 0x02, 0x00, "Communication controller", "Multiport serial controller", NULL },
   { 0x07, 0x03, 0x00, "Communication controller", "Modem", "Generic" },
   { 0x07, 0x03, 0x01, "Communication controller", "Modem", "Hayes/16450" },
   { 0x07, 0x03, 0x02, "Communication controller", "Modem", "Hayes/16550" },
   { 0x07, 0x03, 0x03, "Communication controller", "Modem", "Hayes/16650" },
   { 0x07, 0x03, 0x04, "Communication controller", "Modem", "Hayes/16750" },
   { 0x07, 0x04, 0x00, "Communication controller", "GPIB controller", NULL },
   { 0x07, 0x05, 0x00, "Communication controller", "Smard Card controller", NULL },
   { 0x07, 0x80, 0x00, "Communication controller", "Communication controller", NULL },
   { 0x08, 0x00, 0x00, "Generic system peripheral", "PIC", "8259" },
   { 0x08, 0x00, 0x01, "Generic system peripheral", "PIC", "ISA PIC" },
   { 0x08, 0x00, 0x02, "Generic system peripheral", "PIC", "EISA PIC" },
   { 0x08, 0x00, 0x10, "Generic system peripheral", "PIC", "IO-APIC" },
   { 0x08, 0x00, 0x20, "Generic system peripheral", "PIC", "IO(X)-APIC" },
   { 0x08, 0x01, 0x00, "Generic system peripheral", "DMA controller", "8237" },
   { 0x08, 0x01, 0x01, "Generic system peripheral", "DMA controller", "ISA DMA" },
   { 0x08, 0x01, 0x02, "Generic system peripheral", "DMA controller", "EISA DMA" },
   { 0x08, 0x02, 0x00, "Generic system peripheral", "Timer", "8254" },
   { 0x08, 0x02, 0x01, "Generic system peripheral", "Timer", "ISA Timer" },
   { 0x08, 0x02, 0x02, "Generic system peripheral", "Timer", "EISA Timers" },
   { 0x08, 0x02, 0x03, "Generic system peripheral", "Timer", "HPET" },
   { 0x08, 0x03, 0x00, "Generic system peripheral", "RTC", "Generic" },
   { 0x08, 0x03, 0x01, "Generic system peripheral", "RTC", "ISA RTC" },
   { 0x08, 0x04, 0x00, "Generic system peripheral", "PCI Hot-plug controller", NULL },
   { 0x08, 0x05, 0x00, "Generic system peripheral", "SD Host controller", NULL },
   { 0x08, 0x06, 0x00, "Generic system peripheral", "IOMMU", NULL },
   { 0x08, 0x80, 0x00, "Generic system peripheral", "System peripheral", NULL },
   { 0x09, 0x00, 0x00, "Input device controller", "Keyboard controller", NULL },
   { 0x09, 0x01, 0x00, "Input device controller", "Digitizer Pen", NULL },
   { 0x09, 0x02, 0x00, "Input device controller", "Mouse controller", NULL },
   { 0x09, 0x03, 0x00, "Input device controller", "Scanner controller", NULL },
   { 0x09, 0x04, 0x00, "Input device controller", "Gameport controller", "Generic" },
   { 0x09, 0x04, 0x10, "Input device controller", "Gameport controller", "Extended" },
   { 0x09, 0x80, 0x00, "Input device controller", "Input device controller", NULL },
   { 0x0a, 0x00, 0x00, "Docking station", "Generic Docking Station", NULL },
   { 0x0a, 0x80, 0x00, "Docking station", "Docking Station", NULL },
   { 0x0b, 0x00, 0x00, "Processor", "386", NULL },
   { 0x0b, 0x01, 0x00, "Processor", "486", NULL },
   { 0x0b, 0x02, 0x00, "Processor", "Pentium", NULL },
   { 0x0b, 0x10, 0x00, "Processor", "Alpha", NULL },
   { 0x0b, 0x20, 0x00, "Processor", "Power PC", NULL },
   { 0x0b, 0x30, 0x00, "Processor", "MIPS", NULL },
   { 0x0b, 0x40, 0x00, "Processor", "Co-processor", NULL },
   { 0x0c, 0x00, 0x00, "Serial bus controller", "FireWire (IEEE 1394)", "Generic" },
   { 0x0c, 0x00, 0x10, "Serial bus controller", "FireWire (IEEE 1394)", "OHCI" },
   { 0x0c, 0x01, 0x00, "Serial bus controller", "ACCESS Bus", NULL },
   { 0x0c, 0x02, 0x00, "Serial bus controller", "SSA", NULL },
   { 0x0c, 0x03, 0x00, "Serial bus controller", "USB controller", "UHCI" },
   { 0x0c, 0x03, 0x10, "Serial bus controller", "USB controller", "OHCI" },
   { 0x0c, 0x03, 0x20, "Serial bus controller", "USB controller", "EHCI" },
   { 0x0c, 0x03, 0x30, "Serial bus controller", "USB controller", "XHCI" },
   { 0x0c, 0x03, 0x40, "Serial bus controller", "USB controller", "USB4 Host Interface" },
   { 0x0c, 0x03, 0x80, "Serial bus controller", "USB controller", "Unspecified" },
   { 0x0c, 0x03, 0xfe, "Serial bus controller", "USB controller", "USB Device" },
   { 0x0c, 0x04, 0x00, "Serial bus controller", "Fibre Channel", NULL },
   { 0x0c, 0x05, 0x00, "Serial bus controller", "SMBus", NULL },
   { 0x0c, 0x06, 0x00, "Serial bus controller", "InfiniBand", NULL },
   { 0x0c, 0x07, 0x00, "Serial bus controller", "IPMI Interface", "SMIC" },
   { 0x0c, 0x07, 0x01, "Serial bus controller", "IPMI Interface", "KCS" },
   { 0x0c, 0x07, 0x02, "Serial bus controller", "IPMI Interface", "BT (Block Transfer)" },
   { 0x0c, 0x08, 0x00, "Serial bus controller", "SERCOS interface", NULL },
   { 0x0c, 0x09, 0x00, "Serial bus controller", "CANBUS", NULL },
   { 0x0d, 0x00, 0x00, "Wireless controller", "IRDA controller", NULL },
   { 0x0d, 0x01, 0x00, "Wireless controller", "Consumer IR controller", NULL },
   { 0x0d, 0x10, 0x00, "Wireless controller", "RF controller", NULL },
   { 0x0d, 0x11, 0x00, "Wireless controller", "Bluetooth", NULL },
   { 0x0d, 0x12, 0x00, "Wireless controller", "Broadband", NULL },
   { 0x0d, 0x20, 0x00, "Wireless controller", "802.1a controller", NULL },
   { 0x0d, 0x21, 0x00, "Wireless controller", "802.1b controller", NULL },
   { 0x0d, 0x80, 0x00, "Wireless controller", "Wireless controller", NULL },
   { 0x0e, 0x00, 0x00, "Intelligent controller", "I2O", NULL },
   { 0x0f, 0x01, 0x00, "Satellite communications controller", "Satellite TV controller", NULL },
   { 0x0f, 0x02, 0x00, "Satellite communications controller", "Satellite audio communication controller", NULL },
   { 0x0f, 0x03, 0x00, "Satellite communications controller", "Satellite voice communication controller", NULL },
   { 0x0f, 0x04, 0x00, "Satellite communications controller", "Satellite data communication controller", NULL },
   { 0x10, 0x00, 0x00, "Encryption controller", "Network and computing encryption device", NULL },
   { 0x10, 0x10, 0x00, "Encryption controller", "Entertainment encryption device", NULL },
   { 0x10, 0x80, 0x00, "Encryption controller", "Encryption controller", NULL },
   { 0x11, 0x00, 0x00, "Signal processing controller", "DPIO module", NULL },
   { 0x11, 0x01, 0x00, "Signal processing controller", "Performance counters", NULL },
   { 0x11, 0x10, 0x00, "Signal processing controller", "Communication synchronizer", NULL },
   { 0x11, 0x20, 0x00, "Signal processing controller", "Signal processing management", NULL },
   { 0x11, 0x80, 0x00, "Signal processing controller", "Signal processing controller", NULL },
   { 0x12, 0x00, 0x00, "Processing accelerators", "Processing accelerators", NULL },
   { 0x12, 0x01, 0x00, "Processing accelerators", "AI Inference Accelerator", NULL },
   { 0x13, 0x00, 0x00, "Non-Essential Instrumentation", NULL, NULL },
   { 0x40, 0x00, 0x00, "Coprocessor", NULL, NULL },
   { 0xff, 0x00, 0x00, "Unassigned class", NULL, NULL },
};

