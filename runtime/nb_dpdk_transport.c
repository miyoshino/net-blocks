/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_byteorder.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 1

#include "nb_runtime.h"
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static uint16_t dpdk_port_id;
static struct rte_mempool *dpdk_mbuf_pool;
static int dpdk_initialized = 0;

static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t rx_desc_count = RX_RING_SIZE;
	uint16_t tx_desc_count = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &rx_desc_count, &tx_desc_count);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, rx_desc_count,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, tx_desc_count,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}

int nb__dpdk_init(int argc, char **argv){
	unsigned port_count;
	uint16_t port_id;

	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	int ret = rte_eal_init(argc, argv);

    if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initializion the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

    /* Check that there is an port. */
	port_count = rte_eth_dev_count_avail();
	if (port_count != 1){
        rte_exit(EXIT_FAILURE, "Error: number of ports must be one\n");
    }
	/* Creates a new mempool in memory to hold the mbufs. */

    /* Allocates mempool to hold the mbufs. 8< */
	dpdk_mbuf_pool = rte_pktmbuf_pool_create("dpdk_mbuf_pool", NUM_MBUFS * port_count,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

    if (dpdk_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    /* Initializing all ports. 8< */
    RTE_ETH_FOREACH_DEV(port_id){
        dpdk_port_id = port_id;
        if (port_init(dpdk_port_id, dpdk_mbuf_pool) != 0)
            rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",dpdk_port_id);
    }
    
    /* >8 End of initializing all ports. */

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

    dpdk_initialized = 1;

	return 0;
}


void nb__dpdk_deinit(void){
    if (dpdk_initialized != 1)
        return ;
    rte_eth_dev_stop(dpdk_port_id);
    rte_eth_dev_close(dpdk_port_id);
    rte_mempool_free(dpdk_mbuf_pool);
    dpdk_mbuf_pool = NULL;
    dpdk_initialized = 0;
    /* clean up the EAL */
	rte_eal_cleanup();
}

void nb__transport_default_init(void) {
	// nb__dpdk_init();
}
void nb__transport_default_deinit(void) {
	// nb__dpdk_deinit();
}

char* nb__poll_packet(int* size, int headroom) {
    *size = 0;
    if (!dpdk_initialized || headroom < 0) {
        return NULL;
    }

    struct rte_mbuf *bufs[BURST_SIZE];
    const uint16_t rx_count = rte_eth_rx_burst(dpdk_port_id, 0,bufs, BURST_SIZE);
	
    // not recieve packets 
    if (unlikely(rx_count == 0))
	    return NULL;

    // segment check (only 1 segmenst)
    if (bufs[0]->nb_segs != 1) {
        rte_pktmbuf_free(bufs[0]);
        return NULL;
    }

    uint16_t len = rte_pktmbuf_data_len(bufs[0]);
    char* buf = malloc((size_t)headroom + len);
    // missed malloc 
    if (buf == NULL){
        rte_pktmbuf_free(bufs[0]);
        return NULL;
    }

    char* data = rte_pktmbuf_mtod(bufs[0], char *);
    memcpy(buf + headroom, data, len);
    rte_pktmbuf_free(bufs[0]);
    *size = len;
    return buf;
}
