

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "sys/node-id.h"
#include "storage/cfs/cfs.h"
#include "lib/crc16.h"

#include <stdio.h>
#include <string.h>

#define UDP_SERVER_PORT 5678
#define UDP_CLIENT_PORT 8765
#define FILENAME "new-firmware.bin"

typedef struct {
  uint16_t block_id;
  uint8_t data_len;
  uint16_t checksum;
} ota_header_t;

static struct simple_udp_connection udp_conn;
static int fd = -1;
static uint16_t expected_block = 0;
static uint32_t total_received = 0;
static uint16_t total_file_crc = 0;

static uint16_t calculate_checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for(int i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen) {

  ota_header_t header;

  if(datalen < sizeof(ota_header_t)) {
    return;
  }

  memcpy(&header, data, sizeof(ota_header_t));
  const uint8_t *payload = data + sizeof(ota_header_t);

  uint16_t calc_sum = calculate_checksum(payload, header.data_len);

  if(calc_sum != header.checksum) {
    printf("OTA: Checksum error on block %u\n", header.block_id);
    return;
  }

  if(header.data_len == 0) {
    printf("OTA: Transfer Complete. Total Bytes: %lu\n", (unsigned long)total_received);
    printf("OTA: File CRC16 Checksum: 0x%04X\n", total_file_crc);
    if(fd != -1) {
      cfs_close(fd);
      fd = -1;
    }
    printf("Yüklenmeye hazır yeni firmware alımı tamamlandı.\n");

    uint16_t ack = 0xFFFF;
    simple_udp_sendto(&udp_conn, &ack, sizeof(ack), sender_addr);
    return;
  }

  if(header.block_id == expected_block) {
    if(fd == -1) {

      cfs_remove(FILENAME);
      fd = cfs_open(FILENAME, CFS_WRITE);
      if(fd < 0) {
        printf("OTA: Failed to open CFS file!\n");
        return;
      }
    }

    int written = cfs_write(fd, payload, header.data_len);
    if(written != header.data_len) {
      printf("OTA: CFS Write error!\n");
      return;
    }

    total_received += written;
    total_file_crc = crc16_data(payload, header.data_len, total_file_crc);
    printf("OTA: Received block %u (%u bytes)\n", header.block_id, header.data_len);

    uint16_t ack = expected_block;
    expected_block++;

    simple_udp_sendto(&udp_conn, &ack, sizeof(ack), sender_addr);
  } else if (header.block_id < expected_block) {

    uint16_t ack = header.block_id;
    simple_udp_sendto(&udp_conn, &ack, sizeof(ack), sender_addr);
  }
}

PROCESS(udp_server_process, "UDP OTA Receiver");
AUTOSTART_PROCESSES(&udp_server_process);

PROCESS_THREAD(udp_server_process, ev, data) {
  PROCESS_BEGIN();

  if(node_id != 1) {
    printf("OTA: I am not the receiver (Node ID: %u). Stopping.\n", node_id);
    PROCESS_EXIT();
  }

  NETSTACK_ROUTING.root_start();

  simple_udp_register(&udp_conn, UDP_SERVER_PORT, NULL,
                      UDP_CLIENT_PORT, udp_rx_callback);

  printf("OTA Receiver started on Node %u. Waiting for firmware...\n", node_id);

  while(1) {
    PROCESS_WAIT_EVENT();
  }

  PROCESS_END();
}
