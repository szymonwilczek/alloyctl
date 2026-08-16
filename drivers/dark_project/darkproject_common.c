/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dark Project vendor protocol helpers.
 *
 * Implements packet building and translation for Dark Project keyboards.
 */
#include <string.h>

#include "dark_project/darkproject_common.h"

size_t darkproject_build_read_current_profile(uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_READ_CURRENT_PROFILE;
	return DARKPROJECT_REPORT_SIZE;
}

size_t darkproject_build_read_profile(uint8_t profile_num, uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_READ_PROFILE;
	buf[2] = (uint8_t)(profile_num + 1);
	return DARKPROJECT_REPORT_SIZE;
}

size_t darkproject_build_read_snap_tap(uint8_t profile_num, uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_READ_SNAP_TAP;
	buf[2] = profile_num ? profile_num : 1;
	return DARKPROJECT_REPORT_SIZE;
}

size_t darkproject_build_custom_lighting_chunk(uint8_t profile_num,
					       uint8_t custom_index,
					       uint8_t chunk_idx,
					       const uint8_t *planar_rgb,
					       uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_WRITE_CUSTOM_LIGHTING;
	buf[2] = profile_num ? profile_num : 1;
	buf[3] = (uint8_t)(custom_index < 5 ? custom_index + 1 : 1);
	buf[4] = (uint8_t)(chunk_idx + 1);

	if (planar_rgb && chunk_idx < 2)
		memcpy(&buf[8], &planar_rgb[chunk_idx * 192], 192);

	return DARKPROJECT_REPORT_SIZE;
}

size_t darkproject_build_switch_profile(uint8_t profile_num, uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_SWITCH_PROFILE;
	buf[2] = (uint8_t)(profile_num + 1);
	return DARKPROJECT_REPORT_SIZE;
}

size_t darkproject_build_snap_tap(uint8_t profile_num, uint8_t enabled,
				  uint8_t group_count,
				  const struct alloy_snap_tap_group *groups,
				  uint8_t *buf)
{
	memset(buf, 0, DARKPROJECT_REPORT_SIZE);
	buf[0] = DARKPROJECT_REPORT_ID;
	buf[1] = DARKPROJECT_CMD_SNAP_TAP;
	buf[2] = profile_num ? profile_num : 1;
	buf[3] = enabled ? 1 : 0;

	if (groups && group_count > 0) {
		if (group_count > ALLOY_MAX_SNAP_TAP_GROUPS)
			group_count = ALLOY_MAX_SNAP_TAP_GROUPS;
		for (uint8_t i = 0; i < group_count; i++) {
			buf[8 + 3 * i + 0] = groups[i].mode;
			buf[8 + 3 * i + 1] = groups[i].key1 ? groups[i].key1 :
							      0x04;
			buf[8 + 3 * i + 2] = groups[i].key2 ? groups[i].key2 :
							      0x07;
		}
	} else {
		buf[8 + 0] = 0;
		buf[8 + 1] = 0x04; /* 'A' */
		buf[8 + 2] = 0x07; /* 'D' */
	}
	return DARKPROJECT_REPORT_SIZE;
}

int darkproject_parse_snap_tap_data(const uint8_t *resp, size_t len,
				    struct alloy_config *cfg)
{
	uint8_t count = 0;

	if (!resp || len < 12 || !cfg)
		return -1;

	alloy_kbd_cfg(cfg)->snap_tap = resp[8] ? 1 : 0;

	for (uint8_t g = 0; g < ALLOY_MAX_SNAP_TAP_GROUPS; g++) {
		if ((size_t)(9 + 3 * g + 2) >= len)
			break;
		uint8_t k1 = resp[9 + 3 * g + 1];
		uint8_t k2 = resp[9 + 3 * g + 2];
		if (!k1 || !k2)
			break;
		alloy_kbd_cfg(cfg)->snap_tap_groups[g].mode =
			resp[9 + 3 * g + 0] & 3;
		alloy_kbd_cfg(cfg)->snap_tap_groups[g].key1 = k1;
		alloy_kbd_cfg(cfg)->snap_tap_groups[g].key2 = k2;
		count++;
	}

	alloy_kbd_cfg(cfg)->snap_tap_group_count = (count > 0) ? count : 1;
	if (count == 0) {
		alloy_kbd_cfg(cfg)->snap_tap_groups[0].mode = 0;
		alloy_kbd_cfg(cfg)->snap_tap_groups[0].key1 = 0x04; /* A */
		alloy_kbd_cfg(cfg)->snap_tap_groups[0].key2 = 0x07; /* D */
	}

	return 0;
}
