/*
 *  PlayStation 2 Game Controller driver
 *
 *        Copyright (C) 2000, 2001  Sony Computer Entertainment Inc.
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License Version 2. See the file "COPYING" in the main
 *  directory of this archive for more details.
 *
 * $Id: joystick.c,v 1.6.4.1 2001/09/19 10:08:22 takemura Exp $
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h>

#if defined(CONFIG_JOYSTICK) || defined(CONFIG_JOYSTICK_MODULE)
#include <linux/joystick.h>
#include <asm/addrspace.h>
#include <asm/uaccess.h>

#include "pad.h"
#include "padcall.h"

struct ps2pad_js_info {
	int dummy;
};

static struct js_port* ps2pad_js_port = NULL;
static int ps2pad_js_open(struct js_dev *dev);
static int ps2pad_js_read(void *xinfo, int **axes, int **buttons);
static int ps2pad_js_close(struct js_dev *dev);

#define NBUTTONS	10

static int Mode = 0;	// 0: 2-axis / 1: 6-axis mode
#ifdef MODULE
MODULE_PARM(Mode, "i");
#endif

void
ps2pad_js_init()
{
	int i, j;
	struct ps2pad_js_info info;
	int NAXES = (Mode == 0) ? 2 : 6;

	memset(&info, 0, sizeof(struct ps2pad_js_info));

	ps2pad_js_port = js_register_port(ps2pad_js_port, &info, ps2pad_npads,
					  sizeof(struct ps2pad_js_info),
					  ps2pad_js_read);
	for (i = 0; i < ps2pad_npads; i++) {
		char name[32];
		sprintf(name, "controller %d", ps2pad_pads[i].port + 1);
		js_register_device(ps2pad_js_port, i, NAXES, NBUTTONS, name,
				   ps2pad_js_open, ps2pad_js_close);
		for (j = 0; j < NAXES; j++) {
			ps2pad_js_port->corr[i][j].type = JS_CORR_BROKEN;
			ps2pad_js_port->corr[i][j].prec = 0;
			ps2pad_js_port->corr[i][j].coef[0] = 0;
			ps2pad_js_port->corr[i][j].coef[1] = 0;
			ps2pad_js_port->corr[i][j].coef[2] = (32767/127)<<14;
			ps2pad_js_port->corr[i][j].coef[3] = (32767/127)<<14;
		}
	}
}

static int
ps2pad_js_open(struct js_dev *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static int
ps2pad_js_read(void *xinfo, int **axes, int **buttons)
{
	int i;
	int res;
	int button;
	u_char data[PS2PAD_DATASIZE];

	for (i = 0; i < ps2pad_npads; i++) {
		axes[i][0] = 0;
		axes[i][1] = 0;
		buttons[i][0] = 0;
		if (Mode != 0) {
			axes[i][2] = axes[i][3] = axes[i][4] = axes[i][5] = 0;
		}

		/*
		 * check pad status
		 */
		res = ps2padlib_GetState(ps2pad_pads[i].port, ps2pad_pads[i].slot);
		if (ps2pad_stat_conv(res) != PS2PAD_STAT_READY) {
			/* pad is not ready. (maybe disconnected) */
			continue;
		}

		res = ps2padlib_Read(ps2pad_pads[i].port, ps2pad_pads[i].slot, 
				 data);
		if (res == 0 || data[0] != 0) {
			/* pad data is invalid */
			continue;
		}

		/*
		 * fill axes and buttons
		 */
		button = ((int)data[2] << 8) | data[3];
		if (Mode == 0) {	// 2-axes mode
		axes[i][0]  = (button&PS2PAD_BUTTON_RIGHT	) ? 0 : 127;
		axes[i][0] -= (button&PS2PAD_BUTTON_LEFT	) ? 0 : 127;
		axes[i][1]  = (button&PS2PAD_BUTTON_DOWN	) ? 0 : 127;
		axes[i][1] -= (button&PS2PAD_BUTTON_UP		) ? 0 : 127;
		if ((data[1] & 0xf) > 1) {	// analog input
			axes[i][0] += (int)data[6] - 127;
			axes[i][1] += (int)data[7] - 127;
		}
		} else {		// 6-axes mode
		if ((data[1] & 0xf) > 1) {	// analog input
			axes[i][0] = (int)data[6] - 127;	// analog Left
			axes[i][1] = (int)data[7] - 127;
			axes[i][2] = (int)data[4] - 127;	// analog Right
			axes[i][3] = (int)data[5] - 127;
		}
			axes[i][4]  = (button&PS2PAD_BUTTON_RIGHT) ? 0 : 127;
			axes[i][4] -= (button&PS2PAD_BUTTON_LEFT ) ? 0 : 127;
			axes[i][5]  = (button&PS2PAD_BUTTON_DOWN ) ? 0 : 127;
			axes[i][5] -= (button&PS2PAD_BUTTON_UP	 ) ? 0 : 127;
		}

		buttons[i][0]  = (button&PS2PAD_BUTTON_SQUARE	) ? 0 : 0x001;
		buttons[i][0] |= (button&PS2PAD_BUTTON_CROSS	) ? 0 : 0x002;
		buttons[i][0] |= (button&PS2PAD_BUTTON_TRIANGLE	) ? 0 : 0x004;
		buttons[i][0] |= (button&PS2PAD_BUTTON_CIRCLE	) ? 0 : 0x008;
		buttons[i][0] |= (button&PS2PAD_BUTTON_L1	) ? 0 : 0x010;
		buttons[i][0] |= (button&PS2PAD_BUTTON_R1	) ? 0 : 0x020;
		buttons[i][0] |= (button&PS2PAD_BUTTON_L2	) ? 0 : 0x040;
		buttons[i][0] |= (button&PS2PAD_BUTTON_R2	) ? 0 : 0x080;
		buttons[i][0] |= (button&PS2PAD_BUTTON_SELECT	) ? 0 : 0x100;
		buttons[i][0] |= (button&PS2PAD_BUTTON_START	) ? 0 : 0x200;
	}

	return 0;
}

static int
ps2pad_js_close(struct js_dev *dev)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

void
ps2pad_js_quit()
{
	int i;

	/*
	 * unregister all port and device
	 */
	while (ps2pad_js_port != NULL) {
		for (i = 0; i < ps2pad_js_port->ndevs; i++)
			if (ps2pad_js_port->devs[i] != NULL)
				js_unregister_device(ps2pad_js_port->devs[i]);
		ps2pad_js_port = js_unregister_port(ps2pad_js_port);
	}
}
#endif
