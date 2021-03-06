/*
 *  hid.c  Version 0.8
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  USB HID support for the Linux input drivers
 *
 *  Sponsored by SuSE
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#undef DEBUG

#include <linux/usb.h>
#include "hid.h"

#ifdef DEBUG
#include "hid-debug.h"
#else
#define hid_dump_input(a,b)	do { } while (0)
#define hid_dump_device(c)	do { } while (0)
#endif

#ifdef CONFIG_PS2
static unsigned char hid_keyboard[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 84, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,192,138,192,192,128,129,131,137,133,135,136,113,
	115,114,192,192,192,192,192,115,112,125,121,123,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	 29, 42, 56,125, 97, 54,100,126
};
#else
static unsigned char hid_keyboard[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 84, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,192,138,192,192,128,129,131,137,133,135,136,113,
	115,114,192,192,192,192,192,124,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,192,
	 29, 42, 56,125, 97, 54,100,126
};
#endif

static struct {
	__s32 x;
	__s32 y;
}  hid_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

/*
 * Register a new report for a device.
 */

static struct hid_report *hid_register_report(struct hid_device *device, unsigned type, unsigned id)
{
	struct hid_report_enum *report_enum = device->report_enum + type;
	struct hid_report *report;
	 
	if (report_enum->report_id_hash[id])
		return report_enum->report_id_hash[id];

	if (!(report = kmalloc(sizeof(struct hid_report), GFP_KERNEL)))
		return NULL;
	memset(report, 0, sizeof(struct hid_report));

	if (id != 0) report_enum->numbered = 1;

	report->id = id;
	report->type = type;
	report->size = 0;
	report->device = device;
	report_enum->report_id_hash[id] = report;

	list_add_tail(&report->list, &report_enum->report_list);

	return report;
}

/*
 * Register a new field for this report.
 */

static struct hid_field *hid_register_field(struct hid_report *report, unsigned usages, unsigned values)
{
	if (report->maxfield < HID_MAX_FIELDS) {
		struct hid_field *field;

		if (!(field = kmalloc(sizeof(struct hid_field) + usages * sizeof(struct hid_usage)
				+ values * sizeof(unsigned), GFP_KERNEL)))
			return NULL;
		memset(field, 0, sizeof(struct hid_field) + usages * sizeof(struct hid_usage)
				+ values * sizeof(unsigned));

		report->field[report->maxfield++] = field;
		field->usage = (struct hid_usage *)(field + 1);
		field->value = (unsigned *)(field->usage + usages);
		field->report = report;

		return field;
	}

	dbg("too many fields in report");
	return NULL;
}

/*
 * Open a collection. The type/usage is pushed on the stack.
 */

static int open_collection(struct hid_parser *parser, unsigned type)
{
	unsigned usage;

	usage = parser->local.usage[0];

	if (type == HID_COLLECTION_APPLICATION)
		parser->device->application = usage;

	if (parser->collection_stack_ptr < HID_COLLECTION_STACK_SIZE) { /* PUSH on stack */
		struct hid_collection *collection = parser->collection_stack + parser->collection_stack_ptr++;
		collection->type = type;
		collection->usage = usage;
		return 0;
	}

	dbg("collection stack overflow");
	return -1;
}

/*
 * Close a collection.
 */

static int close_collection(struct hid_parser *parser)
{
	if (parser->collection_stack_ptr > 0) {	/* POP from stack */
		parser->collection_stack_ptr--;
		return 0;
	}
	dbg("collection stack underflow");
	return -1;
}

/*
 * Climb up the stack, search for the specified collection type
 * and return the usage.
 */

static unsigned hid_lookup_collection(struct hid_parser *parser, unsigned type)
{
	int n;
	for (n = parser->collection_stack_ptr - 1; n >= 0; n--)
		if (parser->collection_stack[n].type == type)
			return parser->collection_stack[n].usage;
	return 0; /* we know nothing about this usage type */
}

/*
 * Add a usage to the temporary parser table.
 */

static int hid_add_usage(struct hid_parser *parser, unsigned usage)
{
	if (parser->local.usage_index >= MAX_USAGES) {
		dbg("usage index exceeded");
		return -1;
	}
	parser->local.usage[parser->local.usage_index++] = usage;
	return 0;
}

/*
 * Register a new field for this report.
 */

static int hid_add_field(struct hid_parser *parser, unsigned report_type, unsigned flags)
{
	struct hid_report *report;
	struct hid_field *field;
	int usages;
	unsigned offset;
	int i;

	if (!(report = hid_register_report(parser->device, report_type, parser->global.report_id))) {
    		dbg("hid_register_report failed");
		return -1;
	}

	if (HID_MAIN_ITEM_VARIABLE & ~flags) { /* ARRAY */
		if (parser->global.logical_maximum <= parser->global.logical_minimum) {
			dbg("logical range invalid %d %d", parser->global.logical_minimum, parser->global.logical_maximum);
			return -1;
		}
		usages = parser->local.usage_index;
		/* Hint: we can assume usages < MAX_USAGE here */
	} else { /* VARIABLE */
		usages = parser->global.report_count;
	}
	offset = report->size;
	report->size += parser->global.report_size *
			parser->global.report_count;
	if (usages == 0)
		return 0; /* ignore padding fields */
	if ((field = hid_register_field(report, usages,
			     parser->global.report_count)) == NULL)
		return 0;
	field->physical = hid_lookup_collection(parser, HID_COLLECTION_PHYSICAL);
	field->logical = hid_lookup_collection(parser, HID_COLLECTION_LOGICAL);
	for (i = 0; i < usages; i++) field->usage[i].hid = parser->local.usage[i];
	field->maxusage = usages;
	field->flags = flags;
	field->report_offset = offset;
	field->report_type = report_type;
	field->report_size = parser->global.report_size;
	field->report_count = parser->global.report_count;
	field->logical_minimum = parser->global.logical_minimum;
	field->logical_maximum = parser->global.logical_maximum;
	field->physical_minimum = parser->global.physical_minimum;
	field->physical_maximum = parser->global.physical_maximum;
	field->unit_exponent = parser->global.unit_exponent;
	field->unit = parser->global.unit;
	return 0;
}

/*
 * Read data value from item.
 */

static __inline__ __u32 item_udata(struct hid_item *item)
{
	switch (item->size) {
		case 1: return item->data.u8;
		case 2: return item->data.u16;
		case 4: return item->data.u32;
	}
	return 0;
}

static __inline__ __s32 item_sdata(struct hid_item *item)
{
	switch (item->size) {
		case 1: return item->data.s8;
		case 2: return item->data.s16;
		case 4: return item->data.s32;
	}
	return 0;
}

/*
 * Process a global item.
 */

static int hid_parser_global(struct hid_parser *parser, struct hid_item *item)
{
	switch (item->tag) {

		case HID_GLOBAL_ITEM_TAG_PUSH:

			if (parser->global_stack_ptr < HID_GLOBAL_STACK_SIZE) {
				memcpy(parser->global_stack + parser->global_stack_ptr++,
					&parser->global, sizeof(struct hid_global));
				return 0;
			}
			dbg("global enviroment stack overflow");
			return -1;

		case HID_GLOBAL_ITEM_TAG_POP:

			if (parser->global_stack_ptr > 0) {
				memcpy(&parser->global, parser->global_stack + --parser->global_stack_ptr,
					sizeof(struct hid_global));
				return 0;
			}
			dbg("global enviroment stack underflow");
			return -1;

		case HID_GLOBAL_ITEM_TAG_USAGE_PAGE:
			parser->global.usage_page = item_udata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM:
			parser->global.logical_minimum = item_sdata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM:
			parser->global.logical_maximum = item_sdata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM:
			parser->global.physical_minimum = item_sdata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM:
			parser->global.physical_maximum = item_sdata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT:
			parser->global.unit_exponent = item_udata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_UNIT:
			parser->global.unit = item_udata(item);
			return 0;

		case HID_GLOBAL_ITEM_TAG_REPORT_SIZE:
			if ((parser->global.report_size = item_udata(item)) > 32) {
				dbg("invalid report_size %d", parser->global.report_size);
				return -1;
			}
			return 0;

		case HID_GLOBAL_ITEM_TAG_REPORT_COUNT:
			if ((parser->global.report_count = item_udata(item)) > MAX_USAGES) {
				dbg("invalid report_count %d", parser->global.report_count);
				return -1;
			}
			return 0;

		case HID_GLOBAL_ITEM_TAG_REPORT_ID:
			if ((parser->global.report_id = item_udata(item)) == 0) {
				dbg("report_id 0 is invalid");
				return -1;
			}
			return 0;

		default:
			dbg("unknown global tag 0x%x", item->tag);
			return -1;
	}
}

/*
 * Process a local item.
 */

static int hid_parser_local(struct hid_parser *parser, struct hid_item *item)
{
	__u32 data;

	if (item->size == 0) {
		dbg("item data expected for local item");
		return -1;
	}

	data = item_udata(item);

	switch (item->tag) {

		case HID_LOCAL_ITEM_TAG_DELIMITER:

			if (data) {
				/*
				 * We treat items before the first delimiter
				 * as global to all usage sets (branch 0).
				 * In the moment we process only these global
				 * items and the first delimiter set.
				 */
				if (parser->local.delimiter_depth != 0) {
					dbg("nested delimiters");
					return -1;
				}
				parser->local.delimiter_depth++;
				parser->local.delimiter_branch++;
			} else {
				if (parser->local.delimiter_depth < 1) {
					dbg("bogus close delimiter");
					return -1;
				}
				parser->local.delimiter_depth--;
			}
			return 1;

		case HID_LOCAL_ITEM_TAG_USAGE:

			if (parser->local.delimiter_branch < 2) {
				if (item->size <= 2)
					data = (parser->global.usage_page << 16) + data;
				return hid_add_usage(parser, data);
			}
			dbg("alternative usage ignored");
			return 0;

		case HID_LOCAL_ITEM_TAG_USAGE_MINIMUM:

			if (parser->local.delimiter_branch < 2) {
				if (item->size <= 2)
					data = (parser->global.usage_page << 16) + data;
				parser->local.usage_minimum = data;
				return 0;
			}
			dbg("alternative usage ignored");
			return 0;

		case HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM:

			if (parser->local.delimiter_branch < 2) {
				unsigned n;
				if (item->size <= 2)
					data = (parser->global.usage_page << 16) + data;
				for (n = parser->local.usage_minimum; n <= data; n++)
					if (hid_add_usage(parser, n)) {
						dbg("hid_add_usage failed\n");
						return -1;
					}
				return 0;
			}
			dbg("alternative usage ignored");
			return 0;

		default:

			dbg("unknown local item tag 0x%x", item->tag);
			return 0;
	}
}

/*
 * Process a main item.
 */

static int hid_parser_main(struct hid_parser *parser, struct hid_item *item)
{
	__u32 data;
	int ret;

	data = item_udata(item);
	
	switch (item->tag) {
		case HID_MAIN_ITEM_TAG_BEGIN_COLLECTION:
			ret = open_collection(parser, data & 3);
			break;
		case HID_MAIN_ITEM_TAG_END_COLLECTION:
			ret = close_collection(parser);
			break;
		case HID_MAIN_ITEM_TAG_INPUT:
			ret = hid_add_field(parser, HID_INPUT_REPORT, data);
			break;
		case HID_MAIN_ITEM_TAG_OUTPUT:
			ret = hid_add_field(parser, HID_OUTPUT_REPORT, data);
			break;
		case HID_MAIN_ITEM_TAG_FEATURE:
			ret = hid_add_field(parser, HID_FEATURE_REPORT, data);
			break;
		default:
			dbg("unknown main item tag 0x%x", item->tag);
			ret = 0;
	}

	memset(&parser->local, 0, sizeof(parser->local));	/* Reset the local parser environment */

	return ret;
}

/*
 * Process a reserved item.
 */

static int hid_parser_reserved(struct hid_parser *parser, struct hid_item *item)
{
	dbg("reserved item type, tag 0x%x", item->tag);
	return 0;
}

/*
 * Free a report and all registered fields. The field->usage and
 * field->value table's are allocated behind the field, so we need
 * only to free(field) itself.
 */

static void hid_free_report(struct hid_report *report)
{
	unsigned n;

	for (n = 0; n < report->maxfield; n++)
		kfree(report->field[n]);
	kfree(report);
}

/*
 * Free a device structure, all reports, and all fields.
 */

static void hid_free_device(struct hid_device *device)
{
	unsigned i,j;

	for (i = 0; i < HID_REPORT_TYPES; i++) {
		struct hid_report_enum *report_enum = device->report_enum + i;

		for (j = 0; j < 256; j++) {
			struct hid_report *report = report_enum->report_id_hash[j];
			if (report) hid_free_report(report);
		}
	}

	if (device->rdesc) kfree(device->rdesc);
}

/*
 * Fetch a report description item from the data stream. We support long
 * items, though they are not used yet.
 */

static __u8 *fetch_item(__u8 *start, __u8 *end, struct hid_item *item)
{
	if ((end - start) > 0) {

		__u8 b = *start++;
		item->type = (b >> 2) & 3;
		item->tag  = (b >> 4) & 15;

		if (item->tag == HID_ITEM_TAG_LONG) {

			item->format = HID_ITEM_FORMAT_LONG;

			if ((end - start) >= 2) {

				item->size = *start++;
				item->tag  = *start++;

				if ((end - start) >= item->size) {
					item->data.longdata = start;
					start += item->size;
					return start;
				}
			}
		} else {

			item->format = HID_ITEM_FORMAT_SHORT;
			item->size = b & 3;
			switch (item->size) {

				case 0:
					return start;

				case 1: 
					if ((end - start) >= 1) {
						item->data.u8 = *start++;
						return start;
					}
					break;

				case 2: 
					if ((end - start) >= 2) {
						item->data.u16 = le16_to_cpu( *((__u16*)start)++);
						return start;
					}

				case 3: 
					item->size++;
					if ((end - start) >= 4) {
						item->data.u32 = le32_to_cpu( *((__u32*)start)++);
						return start;
					}
			}
		}
	}
	return NULL;
}

/*
 * Parse a report description into a hid_device structure. Reports are
 * enumerated, fields are attached to these reports.
 */

static struct hid_device *hid_parse_report(__u8 *start, unsigned size)
{
	struct hid_device *device;
	struct hid_parser *parser;
	struct hid_item    item;
	__u8 *end;
	unsigned i;
	static int (*dispatch_type[])(struct hid_parser *parser,
				      struct hid_item *item) = {
		hid_parser_main,
		hid_parser_global,
		hid_parser_local,
		hid_parser_reserved
	};

	if (!(device = kmalloc(sizeof(struct hid_device), GFP_KERNEL)))
		return NULL;
	memset(device, 0, sizeof(struct hid_device));

	for (i = 0; i < HID_REPORT_TYPES; i++)
		INIT_LIST_HEAD(&device->report_enum[i].report_list);

	if (!(device->rdesc = (__u8 *)kmalloc(size, GFP_KERNEL))) {
		kfree(device);
		return NULL;
	}
	memcpy(device->rdesc, start, size);

	if (!(parser = kmalloc(sizeof(struct hid_parser), GFP_KERNEL))) {
		kfree(device->rdesc);
		kfree(device);
		return NULL;
	}
	memset(parser, 0, sizeof(struct hid_parser));
	parser->device = device;
	
	end = start + size;
	while ((start = fetch_item(start, end, &item)) != 0) {
		if (item.format != HID_ITEM_FORMAT_SHORT) {
			dbg("unexpected long global item");
			hid_free_device(device);
			kfree(parser);
			return NULL;
		}
		if (dispatch_type[item.type](parser, &item)) {
			dbg("item %u %u %u %u parsing failed\n",
				item.format, (unsigned)item.size, (unsigned)item.type, (unsigned)item.tag);
			hid_free_device(device);
			kfree(parser);
			return NULL;
		}

		if (start == end) {
			if (parser->collection_stack_ptr) {
				dbg("unbalanced collection at end of report description");
				hid_free_device(device);
				kfree(parser);
				return NULL;
			}
			if (parser->local.delimiter_depth) {
				dbg("unbalanced delimiter at end of report description");
				hid_free_device(device);
				kfree(parser);
				return NULL;
			}
			kfree(parser);
			return device;
		}
	}

	dbg("item fetching failed at offset %d\n", (int)(end - start));
	hid_free_device(device);
	kfree(parser);
	return NULL;
}

/*
 * Convert a signed n-bit integer to signed 32-bit integer. Common
 * cases are done through the compiler, the screwed things has to be
 * done by hand.
 */

static __inline__ __s32 snto32(__u32 value, unsigned n)
{
	switch (n) {
		case 8:  return ((__s8)value);
		case 16: return ((__s16)value);
		case 32: return ((__s32)value);
	}
	return value & (1 << (n - 1)) ? value | (-1 << n) : value;
}

/*
 * Convert a signed 32-bit integer to a signed n-bit integer. 
 */

static __inline__ __u32 s32ton(__s32 value, unsigned n)
{
	__s32 a = value >> (n - 1);
	if (a && a != -1) return value > 0 ? 1 << (n - 1) : (1 << n) - 1;
	return value & ((1 << n) - 1);
}

/*
 * Extract/implement a data field from/to a report. We use 64-bit unsigned,
 * 32-bit aligned, so that we can possibly have alignment problems on some
 * odd architectures.
 */

static __inline__ __u32 extract(__u8 *report, unsigned offset, unsigned n)
{
	report += (offset >> 5) << 2; offset &= 31;
	return (le64_to_cpu(*(__u64*)report) >> offset) & ((1 << n) - 1);
}

static __inline__ void implement(__u8 *report, unsigned offset, unsigned n, __u32 value)
{
	report += (offset >> 5) << 2; offset &= 31;
	*(__u64*)report &= cpu_to_le64(~((1ULL << n) - 1) << offset);
	*(__u64*)report |= cpu_to_le64((__u64)value << offset);
}

static void hid_configure_usage(struct hid_device *device, struct hid_field *field, struct hid_usage *usage)
{
	struct input_dev *input = &device->input;
	int max;
	unsigned long *bit;

	switch (usage->hid & HID_USAGE_PAGE) {

		case HID_UP_KEYBOARD:

			if ((usage->hid & HID_USAGE) < 256) {
				if (!(usage->code = hid_keyboard[usage->hid & HID_USAGE]))
					return;
			} else
				usage->code = KEY_UNKNOWN;

			set_bit(EV_REP, input->evbit);
			usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
			break; 

		case HID_UP_BUTTON:

			usage->code = ((usage->hid - 1) & 0xf) + 0x100;
			usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
			
			switch (device->application) {
				case HID_GD_GAMEPAD:  usage->code += 0x10;
				case HID_GD_JOYSTICK: usage->code += 0x10;
				case HID_GD_MOUSE:    usage->code += 0x10;
			}
			break;

		case HID_UP_GENDESK:

			usage->code = usage->hid & 0xf;

			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				usage->type = EV_REL; bit = input->relbit; max = REL_MAX;
				break;
			} 

			usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX;

			if (usage->hid == HID_GD_HATSWITCH) {
				usage->code = ABS_HAT0X;
				usage->hat = 1 + (field->logical_maximum == 4);
			}
			break;

		default:

			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				usage->code = REL_MISC;
				usage->type = EV_REL; bit = input->relbit; max = REL_MAX;
				break;
			}

			if (field->logical_minimum == 0 && field->logical_maximum == 1) {
				usage->code = BTN_MISC;
				usage->type = EV_KEY; bit = input->keybit; max = KEY_MAX;
				break;
			}

			usage->code = ABS_MISC;
			usage->type = EV_ABS; bit = input->absbit; max = ABS_MAX;
			break;
	}

	set_bit(usage->type, input->evbit);

	while (usage->code <= max && test_and_set_bit(usage->code, bit)) {
		usage->code = find_next_zero_bit(bit, max + 1, usage->code);
	}

	if (usage->type == EV_ABS) {
		int a = field->logical_minimum;
		int b = field->logical_maximum;

		input->absmin[usage->code] = a; 
		input->absmax[usage->code] = b;
		input->absfuzz[usage->code] = (b - a) >> 8;
		input->absflat[usage->code] = (b - a) >> 4;
	}

	if (usage->hat) {
		int i;
		for (i = usage->code; i < usage->code + 2; i++) {
			input->absmax[i] = 1;
			input->absmin[i] = -1;
			input->absfuzz[i] = 0;
			input->absflat[i] = 0;
		}
		set_bit(usage->code + 1, input->absbit);
	}
}

static void hid_process_event(struct input_dev *input, struct hid_usage *usage, __s32 value)
{
	hid_dump_input(usage, value);

	if (usage->hat) {
		if (usage->hat == 2) value = value * 2 - 1;
		input_event(input, usage->type, usage->code    , hid_hat_to_axis[value].x);
		input_event(input, usage->type, usage->code + 1, hid_hat_to_axis[value].y);
		return;
	}

	input_event(input, usage->type, usage->code, value);
}

/*
 * Search an array for a value.
 */

static __inline__ int search(__s32 *array, __s32 value, unsigned n)
{
	while (n--) if (*array++ == value) return 0;
	return -1;
}

/*
 * Analyse a received field, and fetch the data from it. The field
 * content is stored for next report processing (we do differential
 * reporting to the layer).
 */

static void hid_input_field(struct hid_device *dev, struct hid_field *field, __u8 *data)
{
	unsigned n;
	unsigned count = field->report_count;
	unsigned offset = field->report_offset;
	unsigned size = field->report_size;
	__s32 min = field->logical_minimum;
	__s32 max = field->logical_maximum;
	__s32 value[count]; /* WARNING: gcc specific */
   
	for (n = 0; n < count; n++)
			value[n] = min < 0 ? snto32(extract(data, offset + n * size, size), size) : 
						    extract(data, offset + n * size, size);

	for (n = 0; n < count; n++) {

		if (HID_MAIN_ITEM_VARIABLE & field->flags) {

			if (field->flags & HID_MAIN_ITEM_RELATIVE) {
				if (!value[n]) continue;
			} else {
				if (value[n] == field->value[n]) continue;
			}
			hid_process_event(&dev->input, &field->usage[n], value[n]);

		} else {

			if (field->value[n] >= min && field->value[n] <= max			/* non-NULL value */
				&& field->usage[field->value[n] - min].hid			/* nonzero usage */
				&& search(value, field->value[n], count))
					hid_process_event(&dev->input, &field->usage[field->value[n] - min], 0);

			if (value[n] >= min && value[n] <= max					/* non-NULL value */
				&& field->usage[value[n] - min].hid				/* nonzero usage */
				&& search(field->value, value[n], count))
					hid_process_event(&dev->input, &field->usage[value[n] - min], 1);
		}
	}

	memcpy(field->value, value, count * sizeof(__s32));
}

/*
 * Interrupt input handler - analyse a received report.
 */

static void hid_irq(struct urb *urb)
{
	struct hid_device *device = urb->context;
	struct hid_report_enum *report_enum = device->report_enum + HID_INPUT_REPORT;
	struct hid_report *report;
	__u8 *data = urb->transfer_buffer;
	int len = urb->actual_length;
	int n;

	if (urb->status) {
		dbg("nonzero status in irq %d", urb->status);
		return;
	}

	if (!len) {
		dbg("empty report");
		return;
	}

#ifdef DEBUG
	printk(KERN_DEBUG __FILE__ ": report (size %u) (%snumbered) = ", len, report_enum->numbered ? "" : "un");
	for (n = 0; n < len; n++)
		printk(" %02x", data[n]);
	printk("\n");
#endif

	n = 0;				/* Normally report number is 0 */

	if (report_enum->numbered) {	/* Device uses numbered reports, data[0] is report number */
		n = *data++;
		len--;
	} 

	if (!(report = report_enum->report_id_hash[n])) {
		dbg("undefined report_id %d received", n);
#ifdef DEBUG
			printk(KERN_DEBUG __FILE__ ": report (size %u) = ", len);
			for (n = 0; n < len; n++)
				printk(" %02x", data[n]);
			printk("\n");
#endif
	
		return;
	}

	if (len < ((report->size - 1) >> 3) + 1) {
		dbg("report %d is too short, (%d < %d)", report->id, len, ((report->size - 1) >> 3) + 1);
		return;
	}

	for (n = 0; n < report->maxfield; n++)
		hid_input_field(device, report->field[n], data);

	return;
}

/*
 * hid_read_report() s intended to read the hid devices values even
 * before the input device is registered, so that the userland interface
 * modules start with real values. This is especially important for joydev.c
 * automagic calibration. Doesn't work yet, though. Don't know why, the control
 * request just times out on most devices I have and returns nonsense on others.
 */

static void hid_read_report(struct hid_device *hid, struct hid_report *report)
{
#if 0
	int rlen = ((report->size - 1) >> 3) + 1 + report_enum->numbered;
	char rdata[rlen];
	struct urb urb;
	int read;

	memset(&urb, 0, sizeof(struct urb));
	memset(rdata, 0, rlen);

	urb.transfer_buffer = rdata;
	urb.actual_length = rlen;
	urb.context = hid;

	dbg("getting report type %d id %d len %d", report->type + 1, report->id, rlen);

	if ((read = usb_get_report(hid->dev, report->type + 1, report->id, hid->ifnum, rdata, rlen)) != rlen) {
		dbg("reading report failed rlen %d read %d", rlen, read);
#ifdef DEBUG
		printk(KERN_DEBUG __FILE__ ": report = ");
		for (j = 0; j < rlen; j++) printk(" %02x", rdata[j]);
		printk("\n");
#endif
		continue;
	}

	hid_irq(&urb);
#endif
}

/*
 * Configure the input layer interface
 * Read all reports and initalize the absoulte field values.
 */

static void hid_init_input(struct hid_device *hid)
{
	struct hid_report_enum *report_enum = hid->report_enum + HID_INPUT_REPORT;
	struct list_head *list;
	int i, j;

	list = report_enum->report_list.next;

	while (list != &report_enum->report_list) {

		struct hid_report *report = (struct hid_report *) list;

		list = list->next;

		for (i = 0; i < report->maxfield; i++)
			for (j = 0; j < report->field[i]->maxusage; j++)
				hid_configure_usage(hid, report->field[i], report->field[i]->usage + j);

		hid_read_report(hid, report);
	}
}

/*
 * Output the field into the report.
 */

static void hid_output_field(struct hid_field *field, __u8 *data)
{
	unsigned count = field->report_count;
	unsigned offset = field->report_offset;
	unsigned size = field->report_size;
	unsigned n;
   
	for (n = 0; n < count; n++) {
		if (field->logical_minimum < 0)	/* signed values */
			implement(data, offset + n * size, size, s32ton(field->value[n], size));
		 else				/* unsigned values */
			implement(data, offset + n * size, size, field->value[n]);
       	}
}

/*
 * Create a report.
 */

void hid_output_report(struct hid_report *report, __u8 *data)
{
	unsigned n;

	/* skip the ID if we have a single report */
	if (report->device->report_enum[report->type].numbered)
		*data++ = report->id;

	for (n = 0; n < report->maxfield; n++)
		hid_output_field(report->field[n], data);
};

/*
 * Set a field value. The report this field belongs to has to be
 * created and transfered to the device, to set this value in the
 * device.
 */

int hid_set_field(struct hid_field *field, unsigned offset, __s32 value)
{
	unsigned size = field->report_size;
	
	if (offset >= field->report_count) {
		dbg("offset exceeds report_count");
		return -1;
	}
	if (field->logical_minimum < 0) {
		if (value != snto32(s32ton(value, size), size)) {
			dbg("value %d is out of range", value);
			return -1;
		}
	}
	if (   (value > field->logical_maximum)
	    || (value < field->logical_minimum)) {
		dbg("value %d is invalid", value);
		return -1;
	}
	field->value[offset] = value;
	return 0;
}

static struct hid_device *usb_hid_configure(struct usb_device *dev, int ifnum)
{
	struct usb_interface_descriptor *interface = &dev->actconfig->interface[ifnum].altsetting[0];
	struct usb_hid_descriptor *hdesc;
	struct hid_device *hid;
	unsigned rsize = 0;
	int n;

	if (interface->bInterfaceClass != USB_INTERFACE_CLASS_HID)
		return NULL;

	if (usb_get_extra_descriptor(interface, USB_DT_HID, &hdesc)
		&& usb_get_extra_descriptor(&interface->endpoint[0], USB_DT_HID, &hdesc)) {
			dbg("class descriptor not present\n");
			return NULL;
	}

	for (n = 0; n < hdesc->bNumDescriptors; n++)
		if (hdesc->desc[n].bDescriptorType == USB_DT_REPORT)
			rsize = le16_to_cpu(hdesc->desc[n].wDescriptorLength);

	if (!rsize || rsize > 1024) {
		dbg("weird size of report descriptor (%u)", rsize);
		return NULL;
	}

	{
		__u8 rdesc[rsize];

		if ((n = usb_get_class_descriptor(dev, ifnum, USB_DT_REPORT, 0, rdesc, rsize)) < 0) {
			dbg("reading report descriptor failed");
			return NULL;
		}

#ifdef DEBUG
		printk(KERN_DEBUG __FILE__ ": report (size %u, read %d) = ", rsize, n);
		for (n = 0; n < rsize; n++)
			printk(" %02x", (unsigned) rdesc[n]);
		printk("\n");
#endif

		if (!(hid = hid_parse_report(rdesc, rsize))) {
			dbg("parsing report descriptor failed");
			return NULL;
		}
	}

	for (n = 0; n < interface->bNumEndpoints; n++) {

		struct usb_endpoint_descriptor *endpoint = &interface->endpoint[n];
		int pipe, maxp;

		if ((endpoint->bmAttributes & 3) != 3)		/* Not an interrupt endpoint */
			continue;

		if (!(endpoint->bEndpointAddress & 0x80)) 	/* Not an input endpoint */
			continue;

		pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
		maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

		FILL_INT_URB(&hid->urb, dev, pipe, hid->buffer, maxp > 32 ? 32 : maxp, hid_irq, hid, endpoint->bInterval);
	
		if (usb_submit_urb(&hid->urb)) {
			dbg("submitting interrupt URB failed");
			continue;
		}

		break;
	}

	if (n == interface->bNumEndpoints) {
		dbg("couldn't find an input interrupt endpoint");
		hid_free_device(hid);
		return NULL;
	}

	hid->version = hdesc->bcdHID;
	hid->country = hdesc->bCountryCode;
	hid->dev = dev;
	hid->ifnum = ifnum;

	return hid;
}

static void* hid_probe(struct usb_device *dev, unsigned int ifnum)
{
	char *hid_name[] = {"Device", "Pointer", "Mouse", "Device", "Joystick",
				"Gamepad", "Keyboard", "Keypad", "Multi-Axis Controller"};
	struct hid_device *hid;

	dbg("HID probe called for ifnum %d", ifnum);

	if (!(hid = usb_hid_configure(dev, ifnum)))
		return NULL;

	hid_dump_device(hid);

	hid_init_input(hid);
	input_register_device(&hid->input);

	printk(KERN_INFO "input%d: USB HID v%d.%d %s\n",
		hid->input.number, hid->version >> 8, hid->version & 0xff,
		(hid->application & 0xffff) <= 8 ? hid_name[hid->application & 0xffff] : "device");

	return hid;
}

static void hid_disconnect(struct usb_device *dev, void *ptr)
{
	struct hid_device *hid = ptr;

	dbg("cleanup called");
	usb_unlink_urb(&hid->urb);
	input_unregister_device(&hid->input);
	hid_free_device(hid);
}

static struct usb_driver hid_driver = {
	name:		"hid",
	probe:		hid_probe,
	disconnect:	hid_disconnect
};

#ifdef MODULE
void cleanup_module(void)
{
	usb_deregister(&hid_driver);
}

int init_module(void)
#else
int hid_init(void)
#endif
{
	usb_register(&hid_driver);
	return 0;
}
