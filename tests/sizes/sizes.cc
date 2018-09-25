#include "config.h"

#include "utilities/protocol2text.h"

#include <daemon/connection.h>
#include <daemon/cookie.h>
#include <daemon/front_end_thread.h>
#include <daemon/memcached.h>
#include <daemon/settings.h>
#include <daemon/stats.h>

#include <cstdio>

static void display(const char *name, size_t size) {
    printf("%s\t%d\n", name, (int)size);
}

static long calc_conn_size(void) {
   long ret = sizeof(Connection);
   ret += (sizeof(char *) * TEMP_ALLOC_LIST_INITIAL);
   ret += (sizeof(struct iovec) * IOV_LIST_INITIAL);
   ret += (sizeof(struct msghdr) * MSG_LIST_INITIAL);
   return ret;
}

static unsigned int count_used_opcodes(void) {
    unsigned int used_opcodes = 0;
    for (uint8_t opcode = 0; opcode < 255; opcode++) {
        if (memcached_opcode_2_text(opcode) != NULL) {
            used_opcodes++;
        }
    }
    return used_opcodes;
}

static void display_used_opcodes(void) {
    printf("ClientOpcode map:     (# = Used, . = Free)\n\n");
    printf("   0123456789abcdef");
    for (unsigned int opcode = 0; opcode < 256; opcode++) {
        if (opcode % 16 == 0) {
            printf("\n%02x ", opcode & ~0xf);
        }
        if (memcached_opcode_2_text(opcode) != NULL) {
            putchar('X');
        } else {
            putchar('.');
        }
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    display("Thread stats", sizeof(struct thread_stats));
    display("Global stats", sizeof(struct stats));
    display("Settings", sizeof(Settings));
    display("Libevent thread", sizeof(FrontEndThread));
    display("Connection", calc_conn_size());

    printf("----------------------------------------\n");

    display("Thread stats cumulative\t", sizeof(struct thread_stats));
    printf("Binary protocol opcodes used\t%u / %u\n",
           count_used_opcodes(), 256);
    display_used_opcodes();

    return 0;
}
