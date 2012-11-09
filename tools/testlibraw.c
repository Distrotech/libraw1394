/*						-*- c-basic-offset: 8 -*-
 *
 * libraw1394 - library for raw access to the 1394 bus with the Linux subsystem.
 *
 * Copyright (C) 1999,2000 Andreas Bombe
 *
 * This library is licensed under the GNU Lesser General Public License (LGPL),
 * version 2.1 or later. See the file COPYING.LIB in the distribution for
 * details.
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>

#include "../src/raw1394.h"
#include "../src/csr.h"


#define TESTADDR (CSR_REGISTER_BASE + CSR_CONFIG_ROM)

const char not_compatible[] = "\
This libraw1394 does not work with your version of Linux. You need a different\n\
version that matches your kernel (see kernel help text for the raw1394 option to\n\
find out which is the correct version).\n";

const char not_loaded[] = "\
This probably means that you don't have raw1394 support in the kernel or that\n\
you haven't loaded the raw1394 module.\n";

quadlet_t buffer;

int my_tag_handler(raw1394handle_t handle, unsigned long tag,
                   raw1394_errcode_t errcode)
{
        int err = raw1394_errcode_to_errno(errcode);

        if (err) {
                printf("failed with error: %s\n", strerror(err));
        } else {
                printf("completed with value 0x%08x\n", buffer);
        }

        return 0;
}

static const unsigned char fcp_data[] =
	{ 0x1, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };

int my_fcp_handler(raw1394handle_t handle, nodeid_t nodeid, int response,
                   size_t length, unsigned char *data)
{
	printf("    got fcp %s from node %d of %d bytes:",
	       (response ? "response" : "command"), nodeid & 0x3f, length);

	if (memcmp(fcp_data, data, sizeof fcp_data) != 0)
		printf("ERROR: fcp payload not correct\n");

        while (length) {
                printf(" %02x", *data);
                data++;
                length--;
        }

        printf("\n");

        return 0;
}

static void
test_fcp(raw1394handle_t handle)
{
	printf("\n  - testing FCP monitoring on local node\n");
	raw1394_set_fcp_handler(handle, my_fcp_handler);
	raw1394_start_fcp_listen(handle);
	raw1394_write(handle, raw1394_get_local_id(handle),
		      CSR_REGISTER_BASE + CSR_FCP_COMMAND, sizeof(fcp_data),
		      (quadlet_t *)fcp_data);
	raw1394_write(handle, raw1394_get_local_id(handle),
		      CSR_REGISTER_BASE + CSR_FCP_RESPONSE, sizeof(fcp_data),
		      (quadlet_t *)fcp_data);
}

static void
read_topology_map(raw1394handle_t handle)
{
	quadlet_t map[70];
	nodeid_t local_id;
	int node_count, self_id_count, i, retval;

	local_id = raw1394_get_local_id(handle) | 0xffc0;

	retval = raw1394_read(handle, local_id,
			      CSR_REGISTER_BASE + CSR_TOPOLOGY_MAP, 12, &map[0]);
	if (retval < 0) {
		perror("\n  - topology map: raw1394_read failed with error");
		return;
	}

	self_id_count = ntohl(map[2]) & 0xffff;
	node_count = ntohl(map[2]) >> 16;
	retval = raw1394_read(handle, local_id,
			      CSR_REGISTER_BASE + CSR_TOPOLOGY_MAP + 12,
			      self_id_count * sizeof map[0], &map[3]);
	if (retval < 0) {
		perror("\n  - topology map: raw1394_read failed with error");
		return;
	}

	printf("\n  - topology map: %d nodes, %d self ids, generation %d\n",
	       node_count, self_id_count, ntohl(map[1]));
	for (i = 0; i < self_id_count; i++)
		printf("    0x%08x\n", ntohl(map[3 + i]));
}

static void
test_config_rom(raw1394handle_t handle)
{
	quadlet_t rom[0x100] = { 0, };
	size_t rom_size;
	unsigned char rom_version;
	int i, retval;

	printf("\n  - testing config rom\n");
	retval = raw1394_get_config_rom(handle, rom, 0x100,
					&rom_size, &rom_version);
	printf("    get_config_rom returned %d, romsize %d, rom_version %d\n",
	       retval, rom_size, rom_version);
	printf("    here are the first 10 quadlets:\n");
	for (i = 0; i < 10; i++)
		printf("    0x%08x\n", rom[i]);

	retval = raw1394_update_config_rom(handle, rom, rom_size, rom_version);
	printf("    update_config_rom returned %d\n", retval);
}

static void
read_cycle_timer(raw1394handle_t handle)
{
	u_int32_t ct;
	u_int64_t local_time;
	time_t seconds;
	int retval;

	retval = raw1394_read_cycle_timer(handle, &ct, &local_time);
	if (retval < 0) {
		perror("\n  - raw1394_read_cycle_timer failed with error");
		return;
	}

	printf("\n  - cycle timer: %d seconds, %d cycles, %d sub-cycles\n",
	       ct >> 25, (ct >> 12) & 0x1fff, ct & 0xfff);
	seconds = local_time / 1000000;
	printf("    local time: %lld us = %s",
	       (unsigned long long)local_time, ctime(&seconds));
}

int test_card(int card)
{
	raw1394handle_t handle;
	struct raw1394_portinfo pinf;
	tag_handler_t std_handler;
	struct pollfd pfd;
	int i, n, numcards, retval;

	handle = raw1394_new_handle();

	if (!handle) {
		if (!errno) {
			printf(not_compatible);
		} else {
			perror("couldn't get handle");
			printf(not_loaded);
		}
		return -1;
	}

	if (card == 0) {
		printf("successfully got handle\n");
		printf("current generation number: %d\n",
		       raw1394_get_generation(handle));
	}

	numcards = raw1394_get_port_info(handle, &pinf, 1);
	if (numcards < card)
		perror("couldn't get card info");
	else if (card == 0)
		printf("%d card%s found\n",
		       numcards, numcards == 1 ? "" : "s");

	if (numcards <= card)
		goto out;

	printf("\ncard %d, name: %s\n", card, pinf.name);

	if (raw1394_set_port(handle, card) < 0) {
		perror("couldn't set port");
		goto out;
	}

	n = raw1394_get_nodecount(handle);
	printf("%d nodes on bus, local ID is %d, IRM is %d\n",
	       n,
	       raw1394_get_local_id(handle) & 0x3f,
	       raw1394_get_irm_id(handle) & 0x3f);

	if (n > 0) {
		printf("\n  - doing transactions with custom tag handler\n");
		std_handler = raw1394_set_tag_handler(handle, my_tag_handler);
	}
	for (i = 0; i < n; i++) {
		printf("    read from node %d... ", i);
		fflush(stdout);
		buffer = 0;

		if (raw1394_start_read(handle, 0xffc0 | i, TESTADDR, 4,
				       &buffer, 0) < 0) {
			perror("failed");
			continue;
		}
		if (raw1394_loop_iterate(handle))
			perror("failed");
	}

	if (n > 0) {
		printf("\n  - using standard tag handler and synchronous calls\n");
		raw1394_set_tag_handler(handle, std_handler);
	}
	for (i = 0; i < n; i++) {
		printf("    read from node %d... ", i);
		fflush(stdout);
		buffer = 0;

		retval = raw1394_read(handle, 0xffc0 | i, TESTADDR, 4, &buffer);
		if (retval < 0)
			perror("failed with error");
		else
			printf("completed with value 0x%08x\n", buffer);
	}

	test_fcp(handle);
	read_topology_map(handle);
	test_config_rom(handle);
	read_cycle_timer(handle);

	printf("\n  - posting 0xdeadbeef as an echo request\n");
	raw1394_echo_request(handle, 0xdeadbeef);

	printf("    polling for leftover messages\n");
	pfd.fd = raw1394_get_fd(handle);
	pfd.events = POLLIN;
	pfd.revents = 0;
	while (1) {
		retval = poll(&pfd, 1, 10);
		if (retval < 1)
			break;
		retval = raw1394_loop_iterate(handle);
		if (retval != 0)
			printf("    raw1394_loop_iterate() returned 0x%08x\n",
			       retval);
	}

	if (retval < 0)
		perror("poll failed");
out:
	raw1394_destroy_handle(handle);
	return numcards;
}

int main(int argc, char **argv)
{
	int card = 0, numcards;

	do
		numcards = test_card(card);
	while (++card < numcards);

	return numcards < 0;
}
