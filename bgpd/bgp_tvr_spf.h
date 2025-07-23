// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BGP TVR SPF API Header
 * Copyright (C) 2025
 */

#ifndef _BGP_TVR_SPF_H
#define _BGP_TVR_SPF_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
struct bgp;

/* TVR SPF execution result structure */
struct tvr_spf_result {
	int installed_count;
	int failed_count;
	int total_routes;
	bool zebra_connected;
	enum {
		TVR_SPF_SUCCESS = 0,
		TVR_SPF_NO_DATABASE,
		TVR_SPF_CREATE_FAILED,
		TVR_SPF_NO_ZEBRA
	} status;
};

/* 
 * TVR SPF API function - can be called from other modules without VTY
 * 
 * Parameters:
 * - bgp: BGP instance
 * - src_node: Source node ID for SPF calculation
 * - time_stamp1: Start timestamp for time range
 * - time_stamp2: End timestamp for time range
 * - is_install_route: Whether to install routes to kernel
 * - enable_logging: Whether to enable debug logging to file
 * 
 * Returns:
 * - struct tvr_spf_result containing execution results and status
 */
extern struct tvr_spf_result tvr_spf_execute(struct bgp *bgp,
					     uint32_t src_node,
					     uint32_t time_stamp1,
					     uint32_t time_stamp2,
					     bool is_install_route,
					     bool enable_logging);

#endif /* _BGP_TVR_SPF_H */
