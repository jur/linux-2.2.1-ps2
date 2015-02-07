/*
 * driver/usb/usb-core.c
 *
 * (C) Copyright David Waite 1999
 * based on code from usb.c, by Linus Torvalds
 *
 * The purpose of this file is to pull any and all generic modular code from
 * usb.c and put it in a separate file. This way usb.c is kept as a generic
 * library, while this file handles starting drivers, etc.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * USB core
 */
extern int usb_hub_init(void);
extern void usb_hub_cleanup(void);
extern int usb_major_init(void);
extern void usb_major_cleanup(void);


/*
 * Cleanup
 */
static void __exit usb_exit(void)
{
	usb_major_cleanup();
	usbdevfs_cleanup();
	usb_hub_cleanup();

#ifdef CONFIG_PS2
#ifndef CONFIG_USB_MODULE
#ifdef CONFIG_USB_PEGASUS
	usb_pegasus_init();
#endif
#ifdef CONFIG_USB_SCANNER
	usb_scanner_init();
#endif
#ifdef CONFIG_USB_AUDIO
	usb_audio_init();
#endif
#ifdef CONFIG_USB_ACM
	usb_acm_init();
#endif
#ifdef CONFIG_USB_PRINTER
	usb_printer_init();
#endif
#ifdef CONFIG_USB_SERIAL
	usb_serial_init();
#endif
#ifdef CONFIG_USB_CPIA
	usb_cpia_init();
#endif
#ifdef CONFIG_USB_OV511
	usb_ov511_init();
#endif
#ifdef CONFIG_USB_DC2XX
	usb_dc2xx_init();
#endif
#ifdef CONFIG_USB_SCSI
	usb_stor_init();
#endif
#ifdef CONFIG_USB_DABUSB
	dabusb_init();
#endif
#ifdef CONFIG_USB_PLUSB
	plusb_init();
#endif
#if defined(CONFIG_USB_HID) || defined(CONFIG_USB_MOUSE) || defined(CONFIG_USB_KBD)
	input_init();
#endif
#ifdef CONFIG_USB_HID
	hid_init();
#endif
#ifdef CONFIG_USB_MOUSE
	usb_mouse_init();
#endif
#ifdef CONFIG_USB_KBD
	usb_kbd_init();
#endif
#ifdef CONFIG_USB_UHCI
	uhci_init();
#endif
#ifdef CONFIG_USB_OHCI
	ohci_hcd_init(); 
#endif
#endif
#endif
}

/*
 * Init
 */
int usb_init(void)
{
	usb_major_init();
	usbdevfs_init();
	usb_hub_init();

#ifdef CONFIG_PS2
#ifndef CONFIG_USB_MODULE
#ifdef CONFIG_USB_PEGASUS
	usb_pegasus_init();
#endif
#ifdef CONFIG_USB_SCANNER
	usb_scanner_init();
#endif
#ifdef CONFIG_USB_AUDIO
	usb_audio_init();
#endif
#ifdef CONFIG_USB_ACM
	usb_acm_init();
#endif
#ifdef CONFIG_USB_PRINTER
	usb_printer_init();
#endif
#ifdef CONFIG_USB_SERIAL
	usb_serial_init();
#endif
#ifdef CONFIG_USB_CPIA
	usb_cpia_init();
#endif
#ifdef CONFIG_USB_OV511
	usb_ov511_init();
#endif
#ifdef CONFIG_USB_DC2XX
	usb_dc2xx_init();
#endif
#ifdef CONFIG_USB_SCSI
	usb_stor_init();
#endif
#ifdef CONFIG_USB_DABUSB
	dabusb_init();
#endif
#ifdef CONFIG_USB_PLUSB
	plusb_init();
#endif
#if defined(CONFIG_USB_HID) || defined(CONFIG_USB_MOUSE) || defined(CONFIG_USB_KBD)
	input_init();
#endif
#ifdef CONFIG_USB_HID
	hid_init();
#endif
#ifdef CONFIG_USB_MOUSE
	usb_mouse_init();
#endif
#ifdef CONFIG_USB_KBD
	usb_kbd_init();
#endif
#ifdef CONFIG_USB_UHCI
	uhci_init();
#endif
#ifdef CONFIG_USB_OHCI
	ohci_hcd_init(); 
#endif
#endif
#endif
	return 0;
}

module_init (usb_init);
module_exit (usb_exit);
