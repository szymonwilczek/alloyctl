/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Dark Project vendor protocol helpers.
 *
 * Encapsulates 256-byte HID Feature Report framing,
 * profile serialization, and GSKY MCU command codes.
 */
#ifndef DARKPROJECT_COMMON_H
#define DARKPROJECT_COMMON_H

#include "lib/keyboard.h"

#define DARKPROJECT_VENDOR_ID 0x342d
#define DARKPROJECT_REPORT_ID 0x07
#define DARKPROJECT_REPORT_SIZE 256

#define DARKPROJECT_CMD_SWITCH_PROFILE 0x01
#define DARKPROJECT_CMD_WRITE_PROFILE 0x02
#define DARKPROJECT_CMD_SNAP_TAP 0x09
#define DARKPROJECT_CMD_WRITE_CUSTOM_LIGHTING 0x0A
#define DARKPROJECT_CMD_READ_CURRENT_PROFILE 0x81
#define DARKPROJECT_CMD_READ_PROFILE 0x82
#define DARKPROJECT_CMD_READ_SNAP_TAP 0x89
#define DARKPROJECT_CMD_READ_CUSTOM_LIGHTING 0x8A

/* Pure packet builders for Dark Project / GSKY frames */
size_t darkproject_build_read_current_profile(uint8_t *buf);
size_t darkproject_build_read_profile(uint8_t profile_num, uint8_t *buf);
size_t darkproject_build_read_snap_tap(uint8_t profile_num, uint8_t *buf);
size_t darkproject_build_switch_profile(uint8_t profile_num, uint8_t *buf);
size_t darkproject_build_custom_lighting_chunk(uint8_t profile_num,
					       uint8_t custom_index,
					       uint8_t chunk_idx,
					       const uint8_t *planar_rgb,
					       uint8_t *buf);
size_t darkproject_build_snap_tap(uint8_t profile_num, uint8_t enabled,
				  uint8_t group_count,
				  const struct alloy_snap_tap_group *groups,
				  uint8_t *buf);

int darkproject_parse_snap_tap_data(const uint8_t *resp, size_t len,
				    struct alloy_config *cfg);

#endif /* DARKPROJECT_COMMON_H */
