/**
 ******************************************************************************
 * @file       bl_xfer.c
 * @author     dRonin, http://dRonin.org/, Copyright (C) 2016
 * @author     Tau Labs, http://taulabs.org, Copyright (C) 2013
 * @addtogroup Bootloader Bootloaders
 * @{
 * @addtogroup Common Common
 * @{
 * @brief Data transfer functions for the unified bootloader
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>
 *
 * Additional note on redistribution: The copyright and license notices above
 * must be maintained in each individual source file that is a derivative work
 * of this source file; otherwise redistribution is prohibited.
 */

#include "pios.h"		/* PIOS_COM_TELEM_USB -- FIXME: include is too coarse */

#include "string.h"		/* memcpy */

#include "bl_xfer.h"		/* API definition */

#include "pios_com_msg.h"	/* PIOS_COM_MSG_* */
#include "pios_board_info.h"	/* struct pios_board_info */

#include "pios_flash.h"		/* PIOS_FLASH_* */

#define MIN(x,y) ((x) < (y) ? (x) : (y))

static uint32_t bl_compute_partition_crc(uintptr_t partition_id, uint32_t partition_offset, uint32_t length)
{
	CRC_ResetDR();

	PIOS_FLASH_start_transaction(partition_id);
	while (length) {
		uint8_t buf[128];
		uint32_t bytes_to_read = MIN(sizeof(buf), length);
		PIOS_FLASH_read_data(partition_id,
				partition_offset,
				buf,
				bytes_to_read);
		CRC_CalcBlockCRC((uint32_t *)buf, bytes_to_read >> 2);

		partition_offset += bytes_to_read;
		length           -= bytes_to_read;
	}
	PIOS_FLASH_end_transaction(partition_id);

	return CRC_GetCRC();
}

bool bl_xfer_completed_p(const struct xfer_state * xfer)
{
	return (xfer->in_progress && (xfer->bytes_to_xfer == 0));
}

bool bl_xfer_crc_ok_p(const struct xfer_state * xfer)
{
	if (!xfer->check_crc) {
		/* No CRC provided for this transfer.  Always indicate success. */
		return true;
	}

	uint32_t actual_crc = bl_compute_partition_crc(xfer->partition_id,
						xfer->original_partition_offset,
						xfer->partition_size);

	return (actual_crc == xfer->crc);
}

bool bl_xfer_read_start(struct xfer_state * xfer, const struct msg_xfer_start *xfer_start)
{
	/* Disable any previous transfer */
	xfer->in_progress = false;

	/* Recover a pointer to the bootloader board info blob */
	const struct pios_board_info * bdinfo = &pios_board_info_blob;

	/* Set up the transfer */
	switch (xfer_start->label) {
	case DFU_PARTITION_FW:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->partition_size -= bdinfo->desc_size;
		xfer->original_partition_offset = 0;
		break;
	case DFU_PARTITION_DESC:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = bdinfo->desc_base - bdinfo->fw_base;
		break;
	case DFU_PARTITION_BL:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_BL, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = 0;
		break;
	case DFU_PARTITION_SETTINGS:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_SETTINGS, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = 0;
		break;
	case DFU_PARTITION_AUTOTUNE:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_AUTOTUNE, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = 0;
		break;
	case DFU_PARTITION_LOG:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_LOG, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = 0;
		break;
	default:
		return false;
	}

	uint32_t bytes_to_xfer = (BE32_TO_CPU(xfer_start->packets_in_transfer) - 1) * XFER_BYTES_PER_PACKET +
		xfer_start->words_in_last_packet * sizeof(uint32_t);

	if (bytes_to_xfer > (xfer->partition_size - xfer->original_partition_offset))
		bytes_to_xfer = xfer->partition_size - xfer->original_partition_offset;

	xfer->current_partition_offset = xfer->original_partition_offset;
	xfer->bytes_to_xfer = bytes_to_xfer;
	xfer->next_packet_number = 0;
	xfer->in_progress = true;

	return true;
}

bool bl_xfer_send_next_read_packet(struct xfer_state * xfer)
{
	if (!xfer->in_progress) {
		return false;
	}

	struct bl_messages msg = {
		.flags_command = BL_MSG_READ_CONT,
		.v.xfer_cont = {
			.current_packet_number = CPU_TO_BE32(xfer->next_packet_number),
		},
	};

	uint32_t bytes_this_xfer = MIN(XFER_BYTES_PER_PACKET, xfer->bytes_to_xfer);

	if (bytes_this_xfer == 0) {
		/* No more bytes to send.  We shouldn't be in this function at all */
		return false;
	}

	/* Read the data from flash */
	PIOS_FLASH_start_transaction(xfer->partition_id);

	PIOS_FLASH_read_data(xfer->partition_id,
			xfer->current_partition_offset,
			msg.v.xfer_cont.data,
			bytes_this_xfer);

	PIOS_FLASH_end_transaction(xfer->partition_id);

	PIOS_COM_MSG_Send(PIOS_COM_TELEM_USB, (uint8_t *)&msg, sizeof(msg));

	/* Update our transfer state */
	xfer->bytes_to_xfer            -= bytes_this_xfer;
	xfer->current_partition_offset += bytes_this_xfer;
	xfer->next_packet_number++;

	return true;
}

bool bl_xfer_write_start(struct xfer_state * xfer, const struct msg_xfer_start *xfer_start)
{
	/* Disable any previous transfer */
	xfer->in_progress = false;

	/* Recover a pointer to the bootloader board info blob */
	const struct pios_board_info * bdinfo = &pios_board_info_blob;

	/* Set up the transfer */
	bool partition_needs_erase = true;

	xfer->check_crc = true;
	xfer->crc  = BE32_TO_CPU(xfer_start->expected_crc);
	xfer->original_partition_offset = 0;

	switch (xfer_start->label) {
#ifdef F1_UPGRADER
	case DFU_PARTITION_BL:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_BL, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->partition_size  -= bdinfo->desc_size; /* don't allow overwriting descriptor */
		break;
#endif
	case DFU_PARTITION_FW:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->partition_size  -= bdinfo->desc_size; /* don't allow overwriting descriptor */
		break;
	case DFU_PARTITION_DESC:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		xfer->original_partition_offset = bdinfo->desc_base - bdinfo->fw_base;
		xfer->check_crc        = false;
		partition_needs_erase  = false;
		break;
	case DFU_PARTITION_SETTINGS:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_SETTINGS, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		break;
	case DFU_PARTITION_AUTOTUNE:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_AUTOTUNE, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		break;
	case DFU_PARTITION_LOG:
		PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_LOG, &xfer->partition_id);
		PIOS_FLASH_get_partition_size(xfer->partition_id, &xfer->partition_size);
		break;
	default:
		return false;
	}

	/* How many bytes is the host trying to transfer? */
	uint32_t bytes_to_xfer = (BE32_TO_CPU(xfer_start->packets_in_transfer) - 1) * XFER_BYTES_PER_PACKET +
		xfer_start->words_in_last_packet * sizeof(uint32_t);

	uint32_t max_bytes_in_xfer = (xfer->partition_size - xfer->original_partition_offset);
	if (bytes_to_xfer > max_bytes_in_xfer) {
		return false;
	}

	/* Figure out if we need to erase the *selected* partition before writing to it */
	if (partition_needs_erase) {
		PIOS_FLASH_start_transaction(xfer->partition_id);
		int32_t ret = PIOS_FLASH_erase_partition(xfer->partition_id);
		PIOS_FLASH_end_transaction(xfer->partition_id);
		if (ret != 0)
			return false;
	}

	xfer->current_partition_offset = xfer->original_partition_offset;
	xfer->bytes_to_xfer = bytes_to_xfer;
	xfer->next_packet_number = 0;
	xfer->in_progress = true;

	return true;
}

bool bl_xfer_write_cont(struct xfer_state * xfer, const struct msg_xfer_cont *xfer_cont)
{
	if (!xfer->in_progress) {
		/* no transfer in progress */
		return false;
	}

	if (BE32_TO_CPU(xfer_cont->current_packet_number) != xfer->next_packet_number) {
		/* packet is out of sequence */
		return false;
	}

	uint32_t bytes_this_xfer = MIN(XFER_BYTES_PER_PACKET, xfer->bytes_to_xfer);

	if (bytes_this_xfer == 0) {
		/* Not expecting any more bytes. We shouldn't be in this function at all */
		return false;
	}

	/* Fix up the endian of the data words */
	for (uint8_t i = 0; i < bytes_this_xfer / sizeof(uint32_t); i++) {
		uint32_t *data = &((uint32_t *)xfer_cont->data)[i];
		*data = BE32_TO_CPU(*data);
	}

	/* Write the data to flash */
	PIOS_FLASH_start_transaction(xfer->partition_id);

	PIOS_FLASH_write_data(xfer->partition_id,
			xfer->current_partition_offset,
			xfer_cont->data,
			bytes_this_xfer);

	PIOS_FLASH_end_transaction(xfer->partition_id);

	/* Update accounting for how many bytes we've received */
	xfer->current_partition_offset += bytes_this_xfer;
	xfer->bytes_to_xfer            -= bytes_this_xfer;

	xfer->next_packet_number++;

	return true;
}

bool bl_xfer_wipe_partition(const struct msg_wipe_partition *wipe_partition)
{
	enum pios_flash_partition_labels flash_label;

	switch (wipe_partition->label) {
#ifdef F1_UPGRADER
	case DFU_PARTITION_BL:
		flash_label = FLASH_PARTITION_LABEL_BL;
		break;
#endif
	case DFU_PARTITION_FW:
		flash_label = FLASH_PARTITION_LABEL_FW;
		break;
	case DFU_PARTITION_SETTINGS:
		flash_label = FLASH_PARTITION_LABEL_SETTINGS;
		break;
	case DFU_PARTITION_AUTOTUNE:
		flash_label = FLASH_PARTITION_LABEL_AUTOTUNE;
		break;
	case DFU_PARTITION_LOG:
		flash_label = FLASH_PARTITION_LABEL_LOG;
		break;
	default:
		return false;
	}

	uintptr_t partition_id;
	if (PIOS_FLASH_find_partition_id(flash_label, &partition_id) != 0)
		return false;

	PIOS_FLASH_start_transaction(partition_id);
	PIOS_FLASH_erase_partition(partition_id);
	PIOS_FLASH_end_transaction(partition_id);

	return true;
}

bool bl_xfer_send_capabilities_self(void)
{
	/* Return capabilities of the specific device */
	const struct pios_board_info * bdinfo = &pios_board_info_blob;

	/* Compute the firmware partition CRC */
	uintptr_t fw_partition_id;
	PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &fw_partition_id);
	uint32_t fw_crc = bl_compute_partition_crc(fw_partition_id, 0, bdinfo->fw_size);

	struct bl_messages msg = {
		.flags_command = BL_MSG_CAP_REP,
		.v.cap_rep_specific = {
			.fw_size       = CPU_TO_BE32(bdinfo->fw_size),
			.device_number = 1, /* Always 1 for "self" */
			.bl_version    = bdinfo->bl_rev,
			.desc_size     = bdinfo->desc_size,
			.board_rev     = bdinfo->board_rev,
			.fw_crc        = CPU_TO_BE32(fw_crc),
			.device_id     = CPU_TO_BE16(bdinfo->board_type << 8 | bdinfo->board_rev),
		},
	};

#if defined(BL_INCLUDE_CAP_EXTENSIONS)
	/* Fill in capabilities extensions */
	msg.v.cap_rep_specific.cap_extension_magic = BL_CAP_EXTENSION_MAGIC;

	uintptr_t partition_id;
	uint32_t partition_size;

	/* FW + DESC */
	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_FW, &partition_id) == 0) {

		PIOS_FLASH_get_partition_size(partition_id, &partition_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_FW]   = CPU_TO_BE32(partition_size - bdinfo->desc_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_DESC] = CPU_TO_BE32(bdinfo->desc_size);
	} else {
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_FW]   = 0;
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_DESC] = 0;
	}

	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_BL, &partition_id) == 0) {
		PIOS_FLASH_get_partition_size(partition_id, &partition_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_BL] = CPU_TO_BE32(partition_size);
	} else {
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_BL] = 0;
	}

	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_SETTINGS, &partition_id) == 0) {
		PIOS_FLASH_get_partition_size(partition_id, &partition_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_SETTINGS] = CPU_TO_BE32(partition_size);
	} else {
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_SETTINGS] = 0;
	}

	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_AUTOTUNE, &partition_id) == 0) {
		PIOS_FLASH_get_partition_size(partition_id, &partition_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_AUTOTUNE] = CPU_TO_BE32(partition_size);
	} else {
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_AUTOTUNE] = 0;
	}

	if (PIOS_FLASH_find_partition_id(FLASH_PARTITION_LABEL_LOG, &partition_id) == 0) {
		PIOS_FLASH_get_partition_size(partition_id, &partition_size);
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_LOG] = CPU_TO_BE32(partition_size);
	} else {
		msg.v.cap_rep_specific.partition_sizes[DFU_PARTITION_LOG] = 0;
	}
#endif	/* BL_INCLUDE_CAP_EXTENSIONS */

	PIOS_COM_MSG_Send(PIOS_COM_TELEM_USB, (uint8_t *)&msg, sizeof(msg));

	return true;
}

/**
 * @}
 * @}
 */
