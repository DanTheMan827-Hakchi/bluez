/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2019  Intel Corporation. All rights reserved.
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdbool.h>

#include <ell/ell.h>

#include "src/shared/shell.h"
#include "src/shared/util.h"

#include "mesh/mesh-defs.h"

#include "tools/mesh/util.h"
#include "tools/mesh/model.h"
#include "tools/mesh/keys.h"
#include "tools/mesh/mesh-db.h"
#include "tools/mesh/remote.h"
#include "tools/mesh/config-model.h"
#include "tools/mesh/cfgcli.h"

#define MIN_COMPOSITION_LEN 16
#define NO_RESPONSE 0xFFFFFFFF

/* Default timeout for getting a response to a sent config command (seconds) */
#define DEFAULT_TIMEOUT 2

struct cfg_cmd {
	uint32_t opcode;
	uint32_t rsp;
	const char *desc;
};

struct pending_req {
	struct l_timeout *timer;
	const struct cfg_cmd *cmd;
	uint16_t addr;
};

static struct l_queue *requests;

static void *send_data;
static model_send_msg_func_t send_msg;

static void *key_data;
static key_send_func_t send_key_msg;

static uint32_t rsp_timeout = DEFAULT_TIMEOUT;
static uint16_t target = UNASSIGNED_ADDRESS;
static uint32_t parms[8];

static struct cfg_cmd cmds[] = {
	{ OP_APPKEY_ADD, OP_APPKEY_STATUS, "AppKeyAdd" },
	{ OP_APPKEY_DELETE, OP_APPKEY_STATUS, "AppKeyDelete" },
	{ OP_APPKEY_GET, OP_APPKEY_LIST, "AppKeyGet"},
	{ OP_APPKEY_LIST, NO_RESPONSE, "AppKeyList"},
	{ OP_APPKEY_STATUS, NO_RESPONSE, "AppKeyStatus"},
	{ OP_APPKEY_UPDATE, OP_APPKEY_STATUS, "AppKeyUpdate" },
	{ OP_DEV_COMP_GET, OP_DEV_COMP_STATUS, "DeviceCompositionGet" },
	{ OP_DEV_COMP_STATUS, NO_RESPONSE, "DeviceCompositionStatus" },
	{ OP_CONFIG_BEACON_GET, OP_CONFIG_BEACON_STATUS, "BeaconGet" },
	{ OP_CONFIG_BEACON_SET, OP_CONFIG_BEACON_STATUS, "BeaconSet" },
	{ OP_CONFIG_BEACON_STATUS, NO_RESPONSE, "BeaconStatus" },
	{ OP_CONFIG_DEFAULT_TTL_GET, OP_CONFIG_DEFAULT_TTL_STATUS,
							"DefaultTTLGet" },
	{ OP_CONFIG_DEFAULT_TTL_SET, OP_CONFIG_DEFAULT_TTL_STATUS,
							"DefaultTTLSet" },
	{ OP_CONFIG_DEFAULT_TTL_STATUS, NO_RESPONSE, "DefaultTTLStatus" },
	{ OP_CONFIG_FRIEND_GET, OP_CONFIG_FRIEND_STATUS, "FriendGet" },
	{ OP_CONFIG_FRIEND_SET, OP_CONFIG_FRIEND_STATUS, "FrienSet" },
	{ OP_CONFIG_FRIEND_STATUS, NO_RESPONSE, "FriendStatus" },
	{ OP_CONFIG_PROXY_GET, OP_CONFIG_PROXY_STATUS, "ProxyGet" },
	{ OP_CONFIG_PROXY_SET, OP_CONFIG_PROXY_STATUS, "ProxySet" },
	{ OP_CONFIG_PROXY_STATUS, NO_RESPONSE, "ProxyStatus" },
	{ OP_CONFIG_KEY_REFRESH_PHASE_GET, OP_CONFIG_KEY_REFRESH_PHASE_STATUS,
							"KeyRefreshPhaseGet" },
	{ OP_CONFIG_KEY_REFRESH_PHASE_SET, OP_CONFIG_KEY_REFRESH_PHASE_STATUS,
							"KeyRefreshPhaseSet" },
	{ OP_CONFIG_KEY_REFRESH_PHASE_STATUS, NO_RESPONSE,
						"KeyRefreshPhaseStatus" },
	{ OP_CONFIG_MODEL_PUB_GET, OP_CONFIG_MODEL_PUB_STATUS, "ModelPubGet" },
	{ OP_CONFIG_MODEL_PUB_SET, OP_CONFIG_MODEL_PUB_STATUS, "ModelPubSet" },
	{ OP_CONFIG_MODEL_PUB_STATUS, NO_RESPONSE, "ModelPubStatus" },
	{ OP_CONFIG_MODEL_PUB_VIRT_SET, OP_CONFIG_MODEL_PUB_STATUS,
							"ModelPubVirtualSet" },
	{ OP_CONFIG_MODEL_SUB_ADD, OP_CONFIG_MODEL_SUB_STATUS, "ModelSubAdd" },
	{ OP_CONFIG_MODEL_SUB_DELETE, OP_CONFIG_MODEL_SUB_STATUS,
							"ModelSubDelete" },
	{ OP_CONFIG_MODEL_SUB_DELETE_ALL, OP_CONFIG_MODEL_SUB_STATUS,
							"ModelSubDeleteAll" },
	{ OP_CONFIG_MODEL_SUB_OVERWRITE, OP_CONFIG_MODEL_SUB_STATUS,
							"ModelSubOverwrite" },
	{ OP_CONFIG_MODEL_SUB_STATUS, NO_RESPONSE, "ModelSubStatus" },
	{ OP_CONFIG_MODEL_SUB_VIRT_ADD, OP_CONFIG_MODEL_SUB_STATUS,
							"ModelSubVirtAdd" },
	{ OP_CONFIG_MODEL_SUB_VIRT_DELETE, OP_CONFIG_MODEL_SUB_STATUS,
							"ModelSubVirtDelete" },
	{ OP_CONFIG_MODEL_SUB_VIRT_OVERWRITE, OP_CONFIG_MODEL_SUB_STATUS,
						"ModelSubVirtOverwrite" },
	{ OP_CONFIG_NETWORK_TRANSMIT_GET, OP_CONFIG_NETWORK_TRANSMIT_STATUS,
							"NetworkTransmitGet" },
	{ OP_CONFIG_NETWORK_TRANSMIT_SET, OP_CONFIG_NETWORK_TRANSMIT_STATUS,
							"NetworkTransmitSet" },
	{ OP_CONFIG_NETWORK_TRANSMIT_STATUS, NO_RESPONSE,
						"NetworkTransmitStatus" },
	{ OP_CONFIG_RELAY_GET, OP_CONFIG_RELAY_STATUS, "RelayGet" },
	{ OP_CONFIG_RELAY_SET, OP_CONFIG_RELAY_STATUS, "RelaySet" },
	{ OP_CONFIG_RELAY_STATUS, NO_RESPONSE, "RelayStatus" },
	{ OP_CONFIG_MODEL_SUB_GET, OP_CONFIG_MODEL_SUB_LIST, "ModelSubGet" },
	{ OP_CONFIG_MODEL_SUB_LIST, NO_RESPONSE, "ModelSubList" },
	{ OP_CONFIG_VEND_MODEL_SUB_GET, OP_CONFIG_VEND_MODEL_SUB_LIST,
							"VendorModelSubGet" },
	{ OP_CONFIG_VEND_MODEL_SUB_LIST, NO_RESPONSE, "VendorModelSubList" },
	{ OP_CONFIG_POLL_TIMEOUT_LIST, OP_CONFIG_POLL_TIMEOUT_STATUS,
							"PollTimeoutList" },
	{ OP_CONFIG_POLL_TIMEOUT_STATUS, NO_RESPONSE, "PollTimeoutStatus" },
	{ OP_CONFIG_HEARTBEAT_PUB_GET, OP_CONFIG_HEARTBEAT_PUB_STATUS,
							"HeartbeatPubGet" },
	{ OP_CONFIG_HEARTBEAT_PUB_SET, OP_CONFIG_HEARTBEAT_PUB_STATUS,
							"HeartbeatPubSet" },
	{ OP_CONFIG_HEARTBEAT_PUB_STATUS, NO_RESPONSE, "HeartbeatPubStatus" },
	{ OP_CONFIG_HEARTBEAT_SUB_GET, OP_CONFIG_HEARTBEAT_SUB_GET,
							"HeartbeatSubGet" },
	{ OP_CONFIG_HEARTBEAT_SUB_SET, OP_CONFIG_HEARTBEAT_SUB_GET,
							"HeartbeatSubSet" },
	{ OP_CONFIG_HEARTBEAT_SUB_STATUS, NO_RESPONSE, "HeartbeatSubStatus" },
	{ OP_MODEL_APP_BIND, OP_MODEL_APP_STATUS, "ModelAppBind" },
	{ OP_MODEL_APP_STATUS, NO_RESPONSE, "ModelAppStatus" },
	{ OP_MODEL_APP_UNBIND, OP_MODEL_APP_STATUS, "ModelAppUnbind" },
	{ OP_NETKEY_ADD, OP_NETKEY_STATUS, "NetKeyAdd" },
	{ OP_NETKEY_DELETE, OP_NETKEY_STATUS, "NetKeyDelete" },
	{ OP_NETKEY_GET, OP_NETKEY_LIST, "NetKeyGet" },
	{ OP_NETKEY_LIST, NO_RESPONSE, "NetKeyList" },
	{ OP_NETKEY_STATUS, NO_RESPONSE, "NetKeyStatus" },
	{ OP_NETKEY_UPDATE, OP_NETKEY_STATUS, "NetKeyUpdate" },
	{ OP_NODE_IDENTITY_GET, OP_NODE_IDENTITY_STATUS, "NodeIdentityGet" },
	{ OP_NODE_IDENTITY_SET, OP_NODE_IDENTITY_STATUS, "NodeIdentitySet" },
	{ OP_NODE_IDENTITY_STATUS, NO_RESPONSE, "NodeIdentityStatus" },
	{ OP_NODE_RESET, OP_NODE_RESET_STATUS, "NodeReset" },
	{ OP_NODE_RESET_STATUS, NO_RESPONSE, "NodeResetStatus" },
	{ OP_MODEL_APP_GET, OP_MODEL_APP_LIST, "ModelAppGet" },
	{ OP_MODEL_APP_LIST, NO_RESPONSE, "ModelAppList" },
	{ OP_VEND_MODEL_APP_GET, OP_VEND_MODEL_APP_LIST, "VendorModelAppGet" },
	{ OP_VEND_MODEL_APP_LIST, NO_RESPONSE, "VendorModelAppList" }
};

static const struct cfg_cmd *get_cmd(uint32_t opcode)
{
	uint32_t n;

	for (n = 0; n < L_ARRAY_SIZE(cmds); n++) {
		if (opcode == cmds[n].opcode)
			return &cmds[n];
	}

	return NULL;
}

static const char *opcode_str(uint32_t opcode)
{
	const struct cfg_cmd *cmd;

	cmd = get_cmd(opcode);
	if (!cmd)
		return "Unknown";

	return cmd->desc;
}

static void free_request(void *a)
{
	struct pending_req *req = a;

	l_timeout_remove(req->timer);
	l_free(req);
}

static struct pending_req *get_req_by_rsp(uint16_t addr, uint32_t rsp)
{
	const struct l_queue_entry *entry;

	entry = l_queue_get_entries(requests);

	for (; entry; entry = entry->next) {
		struct pending_req *req = entry->data;

		if (req->addr == addr && req->cmd->rsp == rsp)
			return req;
	}

	return NULL;
}

static void wait_rsp_timeout(struct l_timeout *timeout, void *user_data)
{
	struct pending_req *req = user_data;

	bt_shell_printf("No response for \"%s\" from %4.4x\n",
						req->cmd->desc, req->addr);

	l_queue_remove(requests, req);
	free_request(req);
}

static void add_request(uint32_t opcode)
{
	struct pending_req *req;
	const struct cfg_cmd *cmd;

	cmd = get_cmd(opcode);
	if (!cmd)
		return;

	req = l_new(struct pending_req, 1);
	req->cmd = cmd;
	req->addr = target;
	req->timer = l_timeout_create(rsp_timeout,
				wait_rsp_timeout, req, NULL);
	l_queue_push_tail(requests, req);
}

static uint32_t print_mod_id(uint8_t *data, bool vid, const char *offset)
{
	uint32_t mod_id;

	if (!vid) {
		mod_id = get_le16(data);
		bt_shell_printf("%sModel Id\t%4.4x\n", offset, mod_id);
		mod_id = 0xffff0000 | mod_id;
	} else {
		mod_id = get_le16(data + 2);
		bt_shell_printf("%sModel Id\t%4.4x %4.4x\n", offset,
							get_le16(data), mod_id);
		mod_id = get_le16(data) << 16 | mod_id;
	}
	return mod_id;
}

static void print_composition(uint8_t *data, uint16_t len)
{
	uint16_t features;
	int i = 0;

	bt_shell_printf("Received composion:\n");

	/* skip page -- We only support Page Zero */
	data++;
	len--;

	bt_shell_printf("\tCID: %4.4x", get_le16(&data[0]));
	bt_shell_printf("\tPID: %4.4x", get_le16(&data[2]));
	bt_shell_printf("\tVID: %4.4x", get_le16(&data[4]));
	bt_shell_printf("\tCRPL: %4.4x", get_le16(&data[6]));

	features = get_le16(&data[8]);
	data += 10;
	len -= 10;

	bt_shell_printf("\tFeature support:\n");
	bt_shell_printf("\t\trelay: %s\n", (features & FEATURE_RELAY) ?
								"yes" : "no");
	bt_shell_printf("\t\tproxy: %s\n", (features & FEATURE_PROXY) ?
								"yes" : "no");
	bt_shell_printf("\t\tfriend: %s\n", (features & FEATURE_FRIEND) ?
								"yes" : "no");
	bt_shell_printf("\t\tlpn: %s\n", (features & FEATURE_LPN) ?
								"yes" : "no");

	while (len) {
		uint8_t m, v;

		bt_shell_printf("\t Element %d:\n", i);
		bt_shell_printf("\t\tlocation: %4.4x\n", get_le16(data));
		data += 2;
		len -= 2;

		m = *data++;
		v = *data++;
		len -= 2;

		if (m)
			bt_shell_printf("\t\tSIG defined models:\n");

		while (len >= 2 && m--) {
			print_mod_id(data, false, "\t\t  ");
			data += 2;
			len -= 2;
		}

		if (v)
			bt_shell_printf("\t\t Vendor defined models:\n");

		while (len >= 4 && v--) {
			print_mod_id(data, true, "\t\t  ");
			data += 4;
			len -= 4;
		}

		i++;
	}
}

static void print_pub(uint16_t ele_addr, uint32_t mod_id,
						struct model_pub *pub)
{
	bt_shell_printf("\tElement: %4.4x\n", ele_addr);
	bt_shell_printf("\tPub Addr: %4.4x\n", pub->u.addr16);

	if (mod_id > 0xffff0000)
		bt_shell_printf("\tModel: %8.8x\n", mod_id);
	else
		bt_shell_printf("\tModel: %4.4x\n",
				(uint16_t) (mod_id & 0xffff));

	bt_shell_printf("\tApp Key Idx: %4.4x\n", pub->app_idx);
	bt_shell_printf("\tTTL: %2.2x\n", pub->ttl);
}

static bool msg_recvd(uint16_t src, uint16_t idx, uint8_t *data,
							uint16_t len)
{
	uint32_t opcode;
	const struct cfg_cmd *cmd;
	uint16_t app_idx, net_idx, addr;
	uint16_t ele_addr;
	uint32_t mod_id;
	struct model_pub pub;
	int n;
	uint16_t i;
	struct pending_req *req;

	if (mesh_opcode_get(data, len, &opcode, &n)) {
		len -= n;
		data += n;
	} else
		return false;

	bt_shell_printf("Received %s\n", opcode_str(opcode));

	req = get_req_by_rsp(src, (opcode & ~OP_UNRELIABLE));
	if (req) {
		cmd = req->cmd;
		free_request(req);
		l_queue_remove(requests, req);
	} else
		cmd = NULL;

	switch (opcode & ~OP_UNRELIABLE) {
	default:
		return false;

	case OP_DEV_COMP_STATUS:
		if (len < MIN_COMPOSITION_LEN)
			break;

		print_composition(data, len);

		break;

	case OP_APPKEY_STATUS:
		if (len != 4)
			break;

		bt_shell_printf("Node %4.4x AppKey status %s\n", src,
						mesh_status_str(data[0]));
		net_idx = get_le16(data + 1) & 0xfff;
		app_idx = get_le16(data + 2) >> 4;

		bt_shell_printf("NetKey\t%3.3x\n", net_idx);
		bt_shell_printf("AppKey\t%3.3x\n", app_idx);

		if (data[0] != MESH_STATUS_SUCCESS)
			break;

		if (!cmd)
			break;

		if (cmd->opcode == OP_APPKEY_ADD) {
			if (remote_add_app_key(src, app_idx))
				mesh_db_node_app_key_add(src, app_idx);
		} else if (cmd->opcode == OP_APPKEY_DELETE) {
			if (remote_del_app_key(src, app_idx))
				mesh_db_node_app_key_del(src, app_idx);
		}

		break;

	case OP_NETKEY_STATUS:
		if (len != 3)
			break;

		bt_shell_printf("Node %4.4x NetKey status %s\n", src,
						mesh_status_str(data[0]));
		net_idx = get_le16(data + 1) & 0xfff;

		bt_shell_printf("\tNetKey %3.3x\n", net_idx);

		if (data[0] != MESH_STATUS_SUCCESS)
			break;

		if (!cmd)
			break;

		if (cmd->opcode == OP_NETKEY_ADD) {
			if (remote_add_net_key(src, net_idx))
				mesh_db_node_net_key_add(src, net_idx);
		} else if (cmd->opcode == OP_NETKEY_DELETE) {
			if (remote_del_net_key(src, net_idx))
				mesh_db_node_net_key_del(src, net_idx);
		}

		break;

	case OP_MODEL_APP_STATUS:
		if (len != 7 && len != 9)
			break;

		bt_shell_printf("Node %4.4x: Model App status %s\n", src,
						mesh_status_str(data[0]));
		addr = get_le16(data + 1);
		app_idx = get_le16(data + 3);

		bt_shell_printf("Element Addr\t%4.4x\n", addr);

		print_mod_id(data + 5, len == 9, "");

		bt_shell_printf("AppIdx\t\t%3.3x\n ", app_idx);

		break;

	case OP_NODE_IDENTITY_STATUS:
		if (len != 4)
			return true;

		bt_shell_printf("NetIdx %4.4x, NodeIdState 0x%02x, status %s\n",
				get_le16(data + 1), data[3],
				mesh_status_str(data[0]));
		break;

	case OP_CONFIG_BEACON_STATUS:
		if (len != 1)
			return true;

		bt_shell_printf("Node %4.4x: Config Beacon Status 0x%02x\n",
				src, data[0]);
		break;

	case OP_CONFIG_RELAY_STATUS:
		if (len != 2)
			return true;

		bt_shell_printf("Node %4.4x: Relay 0x%02x, cnt %d, steps %d\n",
				src, data[0], data[1]>>5, data[1] & 0x1f);
		break;

	case OP_CONFIG_PROXY_STATUS:
		if (len != 1)
			return true;

		bt_shell_printf("Node %4.4x Proxy state 0x%02x\n",
				src, data[0]);
		break;

	case OP_CONFIG_DEFAULT_TTL_STATUS:
		if (len != 1)
			return true;

		bt_shell_printf("Node %4.4x Default TTL %d\n", src, data[0]);

		break;

	case OP_CONFIG_MODEL_PUB_STATUS:
		if (len != 12 && len != 14)
			return true;

		bt_shell_printf("\nNode %4.4x Publication status %s\n",
				src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		ele_addr = get_le16(data + 1);

		mod_id = print_mod_id(data + 10, len == 14, "");

		pub.u.addr16 = get_le16(data + 3);
		pub.app_idx = get_le16(data + 5);
		pub.ttl = data[7];
		pub.period = data[8];
		n = (data[8] & 0x3f);

		print_pub(ele_addr, mod_id, &pub);

		switch (data[8] >> 6) {
		case 0:
			bt_shell_printf("Period\t\t%d ms\n", n * 100);
			break;
		case 2:
			n *= 10;
			/* fall through */
		case 1:
			bt_shell_printf("Period\t\t%d sec\n", n);
			break;
		case 3:
			bt_shell_printf("Period\t\t%d min\n", n * 10);
			break;
		}

		bt_shell_printf("Rexmit count\t%d\n", data[9] >> 5);
		bt_shell_printf("Rexmit steps\t%d\n", data[9] & 0x1f);

		break;

	/* Per Mesh Profile 4.3.2.19 */
	case OP_CONFIG_MODEL_SUB_STATUS:
		bt_shell_printf("\nNode %4.4x Subscription status %s\n",
				src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		ele_addr = get_le16(data + 1);
		addr = get_le16(data + 3);
		bt_shell_printf("Element Addr\t%4.4x\n", ele_addr);

		print_mod_id(data + 5, len == 9, "");

		bt_shell_printf("Subscr Addr\t%4.4x\n", addr);

		break;

	/* Per Mesh Profile 4.3.2.27 */
	case OP_CONFIG_MODEL_SUB_LIST:

		bt_shell_printf("\nNode %4.4x Subscription List status %s\n",
				src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		bt_shell_printf("Element Addr\t%4.4x\n", get_le16(data + 1));
		bt_shell_printf("Model ID\t%4.4x\n", get_le16(data + 3));

		for (i = 5; i < len; i += 2)
			bt_shell_printf("Subscr Addr\t%4.4x\n",
							get_le16(data + i));
		break;

	/* Per Mesh Profile 4.3.2.50 */
	case OP_MODEL_APP_LIST:
		bt_shell_printf("\nNode %4.4x Model AppIdx status %s\n",
						src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		bt_shell_printf("Element Addr\t%4.4x\n", get_le16(data + 1));
		bt_shell_printf("Model ID\t%4.4x\n", get_le16(data + 3));

		for (i = 5; i < len; i += 2)
			bt_shell_printf("Model AppIdx\t%4.4x\n",
							get_le16(data + i));
		break;

	/* Per Mesh Profile 4.3.2.63 */
	case OP_CONFIG_HEARTBEAT_PUB_STATUS:
		bt_shell_printf("\nNode %4.4x Heartbeat publish status %s\n",
				src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		bt_shell_printf("Destination\t%4.4x\n", get_le16(data + 1));
		bt_shell_printf("Count\t\t%2.2x\n", data[3]);
		bt_shell_printf("Period\t\t%2.2x\n", data[4]);
		bt_shell_printf("TTL\t\t%2.2x\n", data[5]);
		bt_shell_printf("Features\t%4.4x\n", get_le16(data + 6));
		bt_shell_printf("Net_Idx\t%4.4x\n", get_le16(data + 8));
		break;

	/* Per Mesh Profile 4.3.2.66 */
	case OP_CONFIG_HEARTBEAT_SUB_STATUS:
		bt_shell_printf("\nNode %4.4x Heartbeat subscribe status %s\n",
				src, mesh_status_str(data[0]));

		if (data[0] != MESH_STATUS_SUCCESS)
			return true;

		bt_shell_printf("Source\t\t%4.4x\n", get_le16(data + 1));
		bt_shell_printf("Destination\t%4.4x\n", get_le16(data + 3));
		bt_shell_printf("Period\t\t%2.2x\n", data[5]);
		bt_shell_printf("Count\t\t%2.2x\n", data[6]);
		bt_shell_printf("Min Hops\t%2.2x\n", data[7]);
		bt_shell_printf("Max Hops\t%2.2x\n", data[8]);
		break;

	/* Per Mesh Profile 4.3.2.54 */
	case OP_NODE_RESET_STATUS:
		bt_shell_printf("Node %4.4x reset status %s\n",
				src, mesh_status_str(data[0]));

		break;
	}

	return true;
}

static uint32_t read_input_parameters(int argc, char *argv[])
{
	uint32_t i;

	if (!argc)
		return 0;

	--argc;
	++argv;

	if (!argc || argv[0][0] == '\0')
		return 0;

	for (i = 0; i < L_ARRAY_SIZE(parms) && i < (uint32_t) argc; i++) {
		if (sscanf(argv[i], "%x", &parms[i]) != 1)
			break;
	}

	return i;
}

static void cmd_timeout_set(int argc, char *argv[])
{
	if (read_input_parameters(argc, argv) != 1)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	rsp_timeout = parms[0];

	bt_shell_printf("Timeout to wait for remote node's response: %d secs\n",
								rsp_timeout);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_dst_set(int argc, char *argv[])
{
	uint32_t dst;
	char *end;

	dst = strtol(argv[1], &end, 16);

	if (end != (argv[1] + 4)) {
		bt_shell_printf("Bad unicast address %s: "
				"expected format 4 digit hex\n", argv[1]);
		target = UNASSIGNED_ADDRESS;

		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	bt_shell_printf("Configuring node %4.4x\n", dst);
	target = dst;
	set_menu_prompt("config", argv[1]);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static bool config_send(uint8_t *buf, uint16_t len, uint32_t opcode)
{
	const struct cfg_cmd *cmd;
	bool res;

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return false;
	}

	cmd = get_cmd(opcode);
	if (!cmd)
		return false;

	if (get_req_by_rsp(target, cmd->rsp)) {
		bt_shell_printf("Another command is pending\n");
		return false;
	}

	res = send_msg(send_data, target, APP_IDX_DEV_REMOTE, buf, len);
	if (!res)
		bt_shell_printf("Failed to send \"%s\"\n", opcode_str(opcode));

	if (cmd->rsp != NO_RESPONSE)
		add_request(opcode);

	return res;
}

static void cmd_default(uint32_t opcode)
{
	uint16_t n;
	uint8_t msg[32];

	n = mesh_opcode_set(opcode, msg);

	if (!config_send(msg, n, opcode))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_composition_get(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];

	n = mesh_opcode_set(OP_DEV_COMP_GET, msg);

	/* By default, use page 0 */
	msg[n++] = (read_input_parameters(argc, argv) == 1) ? parms[0] : 0;

	if (!config_send(msg, n, OP_DEV_COMP_GET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_netkey_del(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	n = mesh_opcode_set(OP_NETKEY_DELETE, msg);

	if (read_input_parameters(argc, argv) != 1) {
		bt_shell_printf("Bad arguments %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	put_le16(parms[0], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_NETKEY_DELETE))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_appkey_del(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	uint16_t app_idx, net_idx;

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	n = mesh_opcode_set(OP_APPKEY_DELETE, msg);

	if (read_input_parameters(argc, argv) != 1) {
		bt_shell_printf("Bad arguments %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	app_idx = (uint16_t) parms[0];
	net_idx = keys_get_bound_key(app_idx);

	/* Pack bound NetKey and AppKey into 3 octets */
	msg[n] = net_idx;
	msg[n + 1] = ((net_idx >> 8) & 0xf) | ((app_idx << 4) & 0xf0);
	msg[n + 2] = app_idx >> 4;

	n += 3;

	if (!config_send(msg, n, OP_APPKEY_DELETE))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_key_add(uint32_t opcode, int argc, char *argv[])
{
	uint16_t key_idx;
	bool is_appkey, update;
	const struct cfg_cmd *cmd;

	if (IS_UNASSIGNED(target)) {
		bt_shell_printf("Destination not set\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	if (!send_key_msg) {
		bt_shell_printf("Send key callback not set\n");
		return;
	}

	if (read_input_parameters(argc, argv) != 1) {
		bt_shell_printf("Bad arguments %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	cmd = get_cmd(opcode);
	if (!cmd)
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	if (get_req_by_rsp(target, cmd->rsp)) {
		bt_shell_printf("Another key command is pending\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	key_idx = (uint16_t) parms[0];

	update = (opcode == OP_NETKEY_UPDATE || opcode == OP_APPKEY_UPDATE);
	is_appkey = (opcode == OP_APPKEY_ADD || opcode == OP_APPKEY_UPDATE);

	if (!send_key_msg(key_data, target, key_idx, is_appkey, update))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	add_request(opcode);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_netkey_add(int argc, char *argv[])
{
	cmd_key_add(OP_NETKEY_ADD, argc, argv);
}

static void cmd_netkey_update(int argc, char *argv[])
{
	cmd_key_add(OP_NETKEY_UPDATE, argc, argv);
}

static void cmd_appkey_add(int argc, char *argv[])
{
	cmd_key_add(OP_APPKEY_ADD, argc, argv);
}

static void cmd_appkey_update(int argc, char *argv[])
{
	cmd_key_add(OP_APPKEY_UPDATE, argc, argv);
}

static void cmd_bind(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 3 && parm_cnt != 4) {
		bt_shell_printf("Bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	n = mesh_opcode_set(OP_MODEL_APP_BIND, msg);

	put_le16(parms[0], msg + n);
	n += 2;
	put_le16(parms[1], msg + n);
	n += 2;

	if (parm_cnt == 4) {
		put_le16(parms[3], msg + n);
		put_le16(parms[2], msg + n + 2);
		n += 4;
	} else {
		put_le16(parms[2], msg + n);
		n += 2;
	}

	if (!config_send(msg, n, OP_MODEL_APP_BIND))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_beacon_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[2 + 1];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_BEACON_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 1) {
		bt_shell_printf("bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	msg[n++] = parms[0];

	if (!config_send(msg, n, OP_CONFIG_BEACON_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_beacon_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_BEACON_GET);
}

static void cmd_ident_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[2 + 3 + 4];
	int parm_cnt;

	n = mesh_opcode_set(OP_NODE_IDENTITY_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 2) {
		bt_shell_printf("bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	put_le16(parms[0], msg + n);
	n += 2;
	msg[n++] = parms[1];

	if (!config_send(msg, n, OP_NODE_IDENTITY_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_ident_get(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[2 + 2 + 4];
	int parm_cnt;

	n = mesh_opcode_set(OP_NODE_IDENTITY_GET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 1) {
		bt_shell_printf("bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	put_le16(parms[0], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_NODE_IDENTITY_GET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_proxy_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[2 + 1];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_PROXY_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 1) {
		bt_shell_printf("bad arguments");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	msg[n++] = parms[0];

	if (!config_send(msg, n, OP_CONFIG_PROXY_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_proxy_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_PROXY_GET);
}

static void cmd_relay_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[2 + 2 + 4];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_RELAY_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 3) {
		bt_shell_printf("bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	msg[n++] = parms[0];
	msg[n++] = (parms[1] << 5) | parms[2];

	if (!config_send(msg, n, OP_CONFIG_RELAY_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_relay_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_RELAY_GET);
}

static void cmd_ttl_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	parm_cnt = read_input_parameters(argc, argv);
	if (!parm_cnt || parms[0] > TTL_MASK) {
		bt_shell_printf("Bad TTL value\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	n = mesh_opcode_set(OP_CONFIG_DEFAULT_TTL_SET, msg);
	msg[n++] = parms[0];

	if (!config_send(msg, n, OP_CONFIG_DEFAULT_TTL_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_pub_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_MODEL_PUB_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 6 && parm_cnt != 7) {
		bt_shell_printf("Bad arguments\n");
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	put_le16(parms[0], msg + n);
	n += 2;
	/* Publish address */
	put_le16(parms[1], msg + n);
	n += 2;
	/* AppKey index + credential (set to 0) */
	put_le16(parms[2], msg + n);
	n += 2;
	/* TTL */
	msg[n++] = DEFAULT_TTL;
	/* Publish period  step count and step resolution */
	msg[n++] = parms[3];
	/* Publish retransmit count & interval steps */
	msg[n++] = parms[4];

	/* Model Id */
	if (parm_cnt == 7) {
		put_le16(parms[6], msg + n);
		put_le16(parms[5], msg + n + 2);
		n += 4;
	} else {
		put_le16(parms[5], msg + n);
		n += 2;
	}

	if (!config_send(msg, n, OP_CONFIG_MODEL_PUB_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_pub_get(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_MODEL_PUB_GET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 2 && parm_cnt != 3) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Element Address */
	put_le16(parms[0], msg + n);
	n += 2;

	/* Model Id */
	if (parm_cnt == 3) {
		put_le16(parms[2], msg + n);
		put_le16(parms[1], msg + n + 2);
		n += 4;
	} else {
		put_le16(parms[1], msg + n);
		n += 2;
	}

	if (!config_send(msg, n, OP_CONFIG_MODEL_PUB_GET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_sub_add(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_MODEL_SUB_ADD, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 3) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Per Mesh Profile 4.3.2.19 */
	/* Element Address */
	put_le16(parms[0], msg + n);
	n += 2;
	/* Subscription Address */
	put_le16(parms[1], msg + n);
	n += 2;
	/* SIG Model ID */
	put_le16(parms[2], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_CONFIG_MODEL_SUB_ADD))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_sub_get(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_MODEL_SUB_GET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 2) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Per Mesh Profile 4.3.2.27 */
	/* Element Address */
	put_le16(parms[0], msg + n);
	n += 2;
	/* Model ID */
	put_le16(parms[1], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_CONFIG_MODEL_SUB_GET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_mod_appidx_get(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_MODEL_APP_GET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 2) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Per Mesh Profile 4.3.2.49 */
	/* Element Address */
	put_le16(parms[0], msg + n);
	n += 2;
	/* Model ID */
	put_le16(parms[1], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_MODEL_APP_GET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_hb_pub_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_HEARTBEAT_PUB_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 6) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Per Mesh Profile 4.3.2.62 */
	/* Publish address */
	put_le16(parms[0], msg + n);
	n += 2;
	/* Count Log */
	msg[n++] = parms[1];
	/* Period Log */
	msg[n++] = parms[2];
	/* Heartbeat TTL */
	msg[n++] = parms[3];
	/* Features */
	put_le16(parms[4], msg + n);
	n += 2;
	/* NetKey Index */
	put_le16(parms[5], msg + n);
	n += 2;

	if (!config_send(msg, n, OP_CONFIG_HEARTBEAT_PUB_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_hb_pub_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_HEARTBEAT_PUB_GET);
}

static void cmd_hb_sub_set(int argc, char *argv[])
{
	uint16_t n;
	uint8_t msg[32];
	int parm_cnt;

	n = mesh_opcode_set(OP_CONFIG_HEARTBEAT_SUB_SET, msg);

	parm_cnt = read_input_parameters(argc, argv);
	if (parm_cnt != 3) {
		bt_shell_printf("Bad arguments: %s\n", argv[1]);
		return bt_shell_noninteractive_quit(EXIT_FAILURE);
	}

	/* Per Mesh Profile 4.3.2.65 */
	/* Source address */
	put_le16(parms[0], msg + n);
	n += 2;
	/* Destination address */
	put_le16(parms[1], msg + n);
	n += 2;
	/* Period log */
	msg[n++] = parms[2];

	if (!config_send(msg, n, OP_CONFIG_HEARTBEAT_SUB_SET))
		return bt_shell_noninteractive_quit(EXIT_FAILURE);

	return bt_shell_noninteractive_quit(EXIT_SUCCESS);
}

static void cmd_hb_sub_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_HEARTBEAT_SUB_GET);
}

static void cmd_ttl_get(int argc, char *argv[])
{
	cmd_default(OP_CONFIG_DEFAULT_TTL_GET);
}

static void cmd_node_reset(int argc, char *argv[])
{
	cmd_default(OP_NODE_RESET);
}

static bool tx_setup(model_send_msg_func_t send_func, void *user_data)
{
	if (!send_func)
		return false;

	send_msg = send_func;
	send_data = user_data;

	return true;
}

static const struct bt_shell_menu cfg_menu = {
	.name = "config",
	.desc = "Configuration Model Submenu",
	.entries = {
	{"target", "<unicast>", cmd_dst_set,
				"Set target node to configure"},
	{"timeout", "<seconds>", cmd_timeout_set,
				"Set response timeout (seconds)"},
	{"composition-get", "[page_num]", cmd_composition_get,
				"Get composition data"},
	{"netkey-add", "<net_idx>", cmd_netkey_add,
				"Add network key"},
	{"netkey-update", "<net_idx>", cmd_netkey_update,
				"Update network key"},
	{"netkey-del", "<net_idx>", cmd_netkey_del,
				"Delete network key"},
	{"appkey-add", "<app_idx>", cmd_appkey_add,
				"Add application key"},
	{"appkey-update", "<app_idx>", cmd_appkey_update,
				"Add application key"},
	{"appkey-del", "<app_idx>", cmd_appkey_del,
				"Delete application key"},
	{"bind", "<ele_addr> <app_idx> <mod_id> [vendor_id]", cmd_bind,
				"Bind app key to a model"},
	{"mod-appidx-get", "<ele_addr> <model id>", cmd_mod_appidx_get,
				"Get model app_idx"},
	{"ttl-set", "<ttl>", cmd_ttl_set,
				"Set default TTL"},
	{"ttl-get", NULL, cmd_ttl_get,
				"Get default TTL"},
	{"pub-set", "<ele_addr> <pub_addr> <app_idx> <per (step|res)> "
			"<re-xmt (cnt|per)> <mod id> [vendor_id]",
				cmd_pub_set,
				"Set publication"},
	{"pub-get", "<ele_addr> <model>", cmd_pub_get,
				"Get publication"},
	{"proxy-set", "<proxy>", cmd_proxy_set,
				"Set proxy state"},
	{"proxy-get", NULL, cmd_proxy_get,
				"Get proxy state"},
	{"ident-set", "<net_idx> <state>", cmd_ident_set,
				"Set node identity state"},
	{"ident-get", "<net_idx>", cmd_ident_get,
				"Get node identity state"},
	{"beacon-set", "<state>", cmd_beacon_set,
				"Set node identity state"},
	{"beacon-get", NULL, cmd_beacon_get,
				"Get node beacon state"},
	{"relay-set", "<relay> <rexmt count> <rexmt steps>", cmd_relay_set,
				"Set relay"},
	{"relay-get", NULL, cmd_relay_get,
				"Get relay"},
	{"hb-pub-set", "<pub_addr> <count> <period> <ttl> <features> <net_idx>",
				cmd_hb_pub_set,
				"Set heartbeat publish"},
	{"hb-pub-get", NULL, cmd_hb_pub_get,
				"Get heartbeat publish"},
	{"hb-sub-set", "<src_addr> <dst_addr> <period>", cmd_hb_sub_set,
				"Set heartbeat subscribe"},
	{"hb-sub-get", NULL, cmd_hb_sub_get,
				"Get heartbeat subscribe"},
	{"sub-add", "<ele_addr> <sub_addr> <model id>", cmd_sub_add,
				"Add subscription"},
	{"sub-get", "<ele_addr> <model id>", cmd_sub_get,
				"Get subscription"},
	{"node-reset", NULL, cmd_node_reset,
				"Reset a node and remove it from network"},
	{} },
};

static struct model_info cli_info = {
	.ops = {
		.set_send_func = tx_setup,
		.set_pub_func = NULL,
		.recv = msg_recvd,
		.bind = NULL,
		.pub = NULL
	},
	.mod_id = CONFIG_CLIENT_MODEL_ID,
	.vendor_id = VENDOR_ID_INVALID
};

struct model_info *cfgcli_init(key_send_func_t key_send, void *user_data)
{
	if (!key_send)
		return NULL;

	send_key_msg = key_send;
	key_data = user_data;
	requests = l_queue_new();

	bt_shell_add_submenu(&cfg_menu);

	return &cli_info;
}

void cfgcli_cleanup(void)
{
	l_queue_destroy(requests, free_request);
}
