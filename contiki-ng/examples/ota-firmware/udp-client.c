

#include "contiki.h"
#include "net/routing/routing.h"
#include "net/netstack.h"
#include "net/ipv6/simple-udp.h"
#include "sys/node-id.h"
#include "storage/cfs/cfs.h"
#include <stdio.h>
#include <string.h>
#include "firmware_data_small.h"

#define UDP_SERVER_PORT 5678
#define UDP_CLIENT_PORT 8765

#define FIRMWARE_FILE "new-firmware.z1"
#define TOTAL_FW_SIZE 129760
#define BLOCK_SIZE 64
#define RETRANSMIT_TIMEOUT (CLOCK_SECOND * 2)

typedef struct {
  uint16_t block_id;
  uint8_t data_len;
  uint16_t checksum;
} ota_header_t;

static struct simple_udp_connection udp_conn;
static uip_ipaddr_t dest_ipaddr;
static struct etimer timer;

static uint16_t current_block = 0;
static bool waiting_for_ack = false;
static uint32_t bytes_sent = 0;
static int fd_read = -1;

static uint16_t calculate_checksum(const uint8_t *data, uint8_t len) {
  uint16_t sum = 0;
  for(int i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}

static void send_ota_block() {
  uint8_t buffer[sizeof(ota_header_t) + BLOCK_SIZE];
  ota_header_t header;

  if(fd_read == -1) {
    fd_read = cfs_open(FIRMWARE_FILE, CFS_READ);
    if(fd_read < 0) {
      printf("OTA: Error opening firmware file %s. Using embedded ELF data instead.\n", FIRMWARE_FILE);

    }
  }

  uint8_t payload_len = BLOCK_SIZE;
  if(bytes_sent + BLOCK_SIZE > TOTAL_FW_SIZE) {
    payload_len = TOTAL_FW_SIZE - bytes_sent;
  }

  uint8_t *payload = buffer + sizeof(ota_header_t);

  int n = 0;
  if(fd_read >= 0) {
    cfs_seek(fd_read, bytes_sent, CFS_SEEK_SET);
    n = cfs_read(fd_read, payload, payload_len);
  }

  if(n < payload_len) {

    for(int i = 0; i < payload_len; i++) {
      payload[i] = firmware_data_small[(bytes_sent + i) % FIRMWARE_DATA_SIZE_SMALL];
    }
  }

  header.block_id = current_block;
  header.data_len = payload_len;
  header.checksum = calculate_checksum(payload, payload_len);

  memcpy(buffer, &header, sizeof(header));

  printf("OTA: Sending block %u (%u bytes)...\n", current_block, payload_len);
  simple_udp_sendto(&udp_conn, buffer, sizeof(header) + payload_len, &dest_ipaddr);

  waiting_for_ack = true;
  etimer_set(&timer, RETRANSMIT_TIMEOUT);
}

static void send_eof() {
  ota_header_t header;
  header.block_id = 0xFFFF;
  header.data_len = 0;
  header.checksum = 0;

  printf("OTA: Sending EOF...\n");
  simple_udp_sendto(&udp_conn, &header, sizeof(header), &dest_ipaddr);
}

static void udp_rx_callback(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen) {

  if(datalen == sizeof(uint16_t)) {
    uint16_t ack_id;
    memcpy(&ack_id, data, sizeof(ack_id));

    if(ack_id == current_block) {
      printf("OTA: Received ACK for block %u\n", ack_id);
      bytes_sent += (bytes_sent + BLOCK_SIZE > TOTAL_FW_SIZE) ? (TOTAL_FW_SIZE - bytes_sent) : BLOCK_SIZE;
      current_block++;
      waiting_for_ack = false;

      if(bytes_sent >= TOTAL_FW_SIZE) {
        send_eof();
        printf("OTA: Firmware transfer finished successfully!\n");
        if(fd_read != -1) {
          cfs_close(fd_read);
          fd_read = -1;
        }
      } else {

        process_post(PROCESS_CURRENT(), PROCESS_EVENT_CONTINUE, NULL);
      }
    } else if (ack_id == 0xFFFF) {
        printf("OTA: Final ACK received. Receiver is ready.\n");
    }
  }
}

PROCESS(udp_client_process, "UDP OTA Sender");
AUTOSTART_PROCESSES(&udp_client_process);

PROCESS_THREAD(udp_client_process, ev, data) {
  PROCESS_BEGIN();

  if(node_id != 2) {
    printf("OTA: I am not the sender (Node ID: %u). Forwarding mode.\n", node_id);

    PROCESS_EXIT();
  }

  etimer_set(&timer, CLOCK_SECOND * 15);
  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));

  uip_ip6addr(&dest_ipaddr, 0xfd00, 0, 0, 0, 0xc30c, 0, 0, 1);

  simple_udp_register(&udp_conn, UDP_CLIENT_PORT, NULL,
                      UDP_SERVER_PORT, udp_rx_callback);

  printf("OTA Sender started on Node %u. Target found!\n", node_id);

  while(bytes_sent < TOTAL_FW_SIZE) {
    if(!waiting_for_ack) {
      send_ota_block();
    }

    PROCESS_WAIT_EVENT();

    if(ev == PROCESS_EVENT_TIMER && etimer_expired(&timer)) {
      if(waiting_for_ack) {
        printf("OTA: Timeout! Retransmitting block %u...\n", current_block);
        send_ota_block();
      }
    }
  }

  PROCESS_END();
}
