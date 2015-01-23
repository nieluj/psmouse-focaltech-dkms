/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright (c) 2014 Mathias Gottschlag <mgottschlag@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Red Hat authors:
 *
 * Hans de Goede <hdegoede@redhat.com>
 */


#include <linux/device.h>
#include <linux/libps2.h>
#include <linux/input/mt.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/list.h>
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include "psmouse.h"
#include "focaltech.h"

static const char * const focaltech_pnp_ids[] = {
	"FLT0101",
	"FLT0102",
	"FLT0103",
	NULL
};

static int compare_func(const char *ida, const char *idb)
{
	int i;

	/* we only need to compare the last 4 chars */
	for (i = 3; i < 7; i++) {
		if (ida[i] != 'X' &&
		    idb[i] != 'X' && toupper(ida[i]) != toupper(idb[i]))
			return 0;
	}
	return 1;
}

int compare_pnp_id(struct pnp_id *pos, const char *id)
{
	if (!pos || !id || (strlen(id) != 7))
		return 0;
	if (memcmp(id, "ANYDEVS", 7) == 0)
		return 1;
	while (pos) {
		if (memcmp(pos->id, id, 3) == 0)
			if (compare_func(pos->id, id) == 1)
				return 1;
		pos = pos->next;
	}
	return 0;
}

static int bus_cb_dev(struct device *dev, void *data)
{
    int *found;
    struct pnp_dev *pnp_dev;
    int i;

    found = (int *) data;
    if (*found != 0) {
    	return 0;
    }

    printk(KERN_INFO "[focaltech] device type = %s", dev->type->name);

    pnp_dev = container_of(dev, struct pnp_dev, dev);
    if (!pnp_dev) {
	printk(KERN_INFO "[focaltech] no pnp_dev");
	return 0;
    }

    for (i = 0; focaltech_pnp_ids[i]; i++) {
    	if (compare_pnp_id(pnp_dev->id, focaltech_pnp_ids[i]) != 0) {
    	    *found = 1;
    	    break;
	}
    }

    return 0;
}

/*
 * Even if the kernel is built without support for Focaltech PS/2 touchpads (or
 * when the real driver fails to recognize the device), we still have to detect
 * them in order to avoid further detection attempts confusing the touchpad.
 * This way it at least works in PS/2 mouse compatibility mode.
 */
int focaltech_detect(struct psmouse *psmouse, bool set_properties)
{
	struct bus_type *bus;
	int ret, found = 0;

	if (!psmouse) {
	    psmouse_err(psmouse, "psmouse is NULL");
	    return -ENODEV;
	}
	if (!psmouse->ps2dev.serio) {
	    psmouse_err(psmouse, "psmouse->ps2dev.serio is NULL");
	    return -ENODEV;
	}
	bus = psmouse->ps2dev.serio->dev.bus;

	ret = bus_for_each_dev(bus, NULL, &found, bus_cb_dev);

	/* This code implements a workaround to correctly detect
	 * the Focaltech touchpad on a muxed port.
	 * Before commit 266e43c4eb81440e81da6c51bc5d4f9be2b7839c,
	 * on a muxed port, the firmware_id was not copied into
	 * psmouse->ps2dev.serio->firmware_id.
	 * The code below loops on all pnp devices and tests if any
	 * pnp_id matches one referenced in the focaltech_pnp_ids array
	 */
	/*
	sym_addr = kallsyms_lookup_name("pnp_global");
	if (!sym_addr) {
	    psmouse_err(psmouse, "cannot resolve pnp_global symbol");
	    return -ENODEV;
	}
	psmouse_info(psmouse, "pnp_global resolved to %p", (void *) sym_addr);
	pnp_global = (struct list_head *) sym_addr;

	sym_addr = kallsyms_lookup_name("compare_pnp_id");
	if (!sym_addr) {
	    psmouse_err(psmouse, "cannot resolve compare_pnp_id symbol");
	    return -ENODEV;
	}
	psmouse_info(psmouse, "compare_pnp_id resolved to %p", (void *) sym_addr);
	compare_pnp_id = (int (*)(struct pnp_id *, const char*)) sym_addr;

	list_for_each_entry(dev, pnp_global, global_list) {
	    for (i = 0; focaltech_pnp_ids[i] && !found; i++) {
	    	if (compare_pnp_id(dev->id, focaltech_pnp_ids[i]) != 0) {
	            psmouse_info(psmouse, "focaltech device detected");
		    found = 1;
		}
	    }
	}
	*/

	/* Original check, will not work on a muxed port before commit
	 * 266e43c4eb81440e81da6c51bc5d4f9be2b7839c */
	if (!found && !psmouse_matches_pnp_id(psmouse, focaltech_pnp_ids)) {
	    psmouse_warn(psmouse, "focaltech device not found");
	    return -ENODEV;
	}

	if (set_properties) {
		psmouse->vendor = "FocalTech";
		psmouse->name = "FocalTech Touchpad";
	}

	return 0;
}

static void focaltech_reset(struct psmouse *psmouse)
{
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
	psmouse_reset(psmouse);
}

#ifdef CONFIG_MOUSE_PS2_FOCALTECH

static void focaltech_report_state(struct psmouse *psmouse)
{
	int i;
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	struct input_dev *dev = psmouse->dev;

	for (i = 0; i < FOC_MAX_FINGERS; i++) {
		struct focaltech_finger_state *finger = &state->fingers[i];
		bool active = finger->active && finger->valid;
		input_mt_slot(dev, i);
		input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);
		if (active) {
			input_report_abs(dev, ABS_MT_POSITION_X, finger->x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					focaltech_invert_y(finger->y));
		}
	}
	input_mt_report_pointer_emulation(dev, true);

	input_report_key(psmouse->dev, BTN_LEFT, state->pressed);
	input_sync(psmouse->dev);
}

static void process_touch_packet(struct focaltech_hw_state *state,
		unsigned char *packet)
{
	int i;
	unsigned char fingers = packet[1];

	state->pressed = (packet[0] >> 4) & 1;
	/* the second byte contains a bitmap of all fingers touching the pad */
	for (i = 0; i < FOC_MAX_FINGERS; i++) {
		state->fingers[i].active = fingers & 0x1;
		if (!state->fingers[i].active) {
			/* even when the finger becomes active again, we still
			 * will have to wait for the first valid position */
			state->fingers[i].valid = false;
		}
		fingers >>= 1;
	}
}

static void process_abs_packet(struct psmouse *psmouse,
		unsigned char *packet)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	unsigned int finger;

	finger = (packet[1] >> 4) - 1;
	if (finger >= FOC_MAX_FINGERS) {
		psmouse_err(psmouse, "Invalid finger in abs packet: %d",
				finger);
		return;
	}

	state->pressed = (packet[0] >> 4) & 1;
	/*
	 * packet[5] contains some kind of tool size in the most significant
	 * nibble. 0xff is a special value (latching) that signals a large
	 * contact area.
	 */
	if (packet[5] == 0xff) {
		state->fingers[finger].valid = false;
		return;
	}
	state->fingers[finger].x = ((packet[1] & 0xf) << 8) | packet[2];
	state->fingers[finger].y = (packet[3] << 8) | packet[4];
	state->fingers[finger].valid = true;
}

static void process_rel_packet(struct psmouse *psmouse,
		unsigned char *packet)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	int finger1, finger2;

	state->pressed = packet[0] >> 7;
	finger1 = ((packet[0] >> 4) & 0x7) - 1;
	if (finger1 < FOC_MAX_FINGERS) {
		state->fingers[finger1].x += (char)packet[1];
		state->fingers[finger1].y += (char)packet[2];
	} else {
		psmouse_err(psmouse, "First finger in rel packet invalid: %d",
				finger1);
	}
	/*
	 * If there is an odd number of fingers, the last relative packet only
	 * contains one finger. In this case, the second finger index in the
	 * packet is 0 (we subtract 1 in the lines above to create array
	 * indices, so the finger will overflow and be above FOC_MAX_FINGERS).
	 */
	finger2 = ((packet[3] >> 4) & 0x7) - 1;
	if (finger2 < FOC_MAX_FINGERS) {
		state->fingers[finger2].x += (char)packet[4];
		state->fingers[finger2].y += (char)packet[5];
	}
}

static void focaltech_process_packet(struct psmouse *psmouse)
{
	struct focaltech_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;

	switch (packet[0] & 0xf) {
	case FOC_TOUCH:
		process_touch_packet(&priv->state, packet);
		break;
	case FOC_ABS:
		process_abs_packet(psmouse, packet);
		break;
	case FOC_REL:
		process_rel_packet(psmouse, packet);
		break;
	default:
		psmouse_err(psmouse, "Unknown packet type: %02x", packet[0]);
		break;
	}

	focaltech_report_state(psmouse);
}

static psmouse_ret_t focaltech_process_byte(struct psmouse *psmouse)
{
	if (psmouse->pktcnt >= 6) { /* Full packet received */
		focaltech_process_packet(psmouse);
		return PSMOUSE_FULL_PACKET;
	}
	/*
	 * we might want to do some validation of the data here, but we do not
	 * know the protocoll well enough
	 */
	return PSMOUSE_GOOD_DATA;
}

static int focaltech_switch_protocol(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];

	param[0] = 0;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;
	param[0] = 1;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETSCALE11))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_ENABLE))
		return -EIO;

	return 0;
}

static void focaltech_disconnect(struct psmouse *psmouse)
{
	focaltech_reset(psmouse);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int focaltech_reconnect(struct psmouse *psmouse)
{
	focaltech_reset(psmouse);
	if (focaltech_switch_protocol(psmouse)) {
		psmouse_err(psmouse,
			    "Unable to initialize the device.");
		return -1;
	}
	return 0;
}

static void set_input_params(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct focaltech_data *priv = psmouse->private;

	__set_bit(EV_ABS, dev->evbit);
	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, priv->x_max, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, priv->y_max, 0, 0);
	input_mt_init_slots(dev, 5, INPUT_MT_POINTER);
	__clear_bit(EV_REL, dev->evbit);
	__clear_bit(REL_X, dev->relbit);
	__clear_bit(REL_Y, dev->relbit);
	__clear_bit(BTN_RIGHT, dev->keybit);
	__clear_bit(BTN_MIDDLE, dev->keybit);
	__set_bit(INPUT_PROP_BUTTONPAD, dev->propbit);
}

static int focaltech_read_register(struct ps2dev *ps2dev, int reg,
		unsigned char *param)
{
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETSCALE11))
		return -EIO;
	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;
	param[0] = reg;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -EIO;
	return 0;
}

static int focaltech_read_size(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct focaltech_data *priv = psmouse->private;
	char param[3];

	if (focaltech_read_register(ps2dev, 2, param))
		return -EIO;
	/* not sure whether this is 100% correct */
	priv->x_max = (unsigned char)param[1] * 128;
	priv->y_max = (unsigned char)param[2] * 128;

	return 0;
}
int focaltech_init(struct psmouse *psmouse)
{
	struct focaltech_data *priv;
	int err;

	psmouse->private = priv = kzalloc(sizeof(struct focaltech_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	focaltech_reset(psmouse);
	err = focaltech_read_size(psmouse);
	if (err) {
		focaltech_reset(psmouse);
		psmouse_err(psmouse,
			    "Unable to read the size of the touchpad.");
		goto fail;
	}
	if (focaltech_switch_protocol(psmouse)) {
		focaltech_reset(psmouse);
		psmouse_err(psmouse,
			    "Unable to initialize the device.");
		err = -ENOSYS;
		goto fail;
	}

	set_input_params(psmouse);

	psmouse->protocol_handler = focaltech_process_byte;
	psmouse->pktsize = 6;
	psmouse->disconnect = focaltech_disconnect;
	psmouse->reconnect = focaltech_reconnect;
	psmouse->cleanup = focaltech_reset;
	/* resync is not supported yet */
	psmouse->resync_time = 0;

	return 0;
fail:
	focaltech_reset(psmouse);
	kfree(priv);
	return err;
}

bool focaltech_supported(void)
{
	return true;
}

#else /* CONFIG_MOUSE_PS2_FOCALTECH */

int focaltech_init(struct psmouse *psmouse)
{
	focaltech_reset(psmouse);

	return 0;
}

bool focaltech_supported(void)
{
	return false;
}

#endif /* CONFIG_MOUSE_PS2_FOCALTECH */
