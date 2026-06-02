#include "contiki.h"
#include <stdio.h>
const uint8_t fw1[30000] = {1};
PROCESS(test_proc, "test");
AUTOSTART_PROCESSES(&test_proc);
PROCESS_THREAD(test_proc, ev, data) {
  PROCESS_BEGIN();
  printf("%d\n", fw1[0]);
  PROCESS_END();
}
