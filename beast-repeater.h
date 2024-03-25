
#ifndef BEASTREPEATER_H
#define BEASTREPEATER_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <limits.h>
#include <strings.h>

#include "compat/compat.h"

#include "anet.h"
#include "net_io.h"

#define MODES_NET_HEARTBEAT_INTERVAL 60000      // milliseconds
#define MODES_CLIENT_BUF_SIZE 1024

#define MODES_NON_ICAO_ADDRESS       (1<<24) // Set on addresses to indicate they are not ICAO addresses

#define MODES_INTERACTIVE_REFRESH_TIME 250      // Milliseconds
#define MODES_INTERACTIVE_DISPLAY_TTL 60000     // Delete from display after 60 seconds

#define MODES_NET_HEARTBEAT_INTERVAL 60000      // milliseconds

#define MODES_CLIENT_BUF_SIZE  1024
#define MODES_NET_SNDBUF_SIZE (1024*64)
#define MODES_NET_SNDBUF_MAX  (7)

#define MODES_LONG_MSG_BYTES     14
#define MODES_SHORT_MSG_BYTES    7
#define MODES_LONG_MSG_BITS     (MODES_LONG_MSG_BYTES    * 8)
#define MODES_SHORT_MSG_BITS    (MODES_SHORT_MSG_BYTES   * 8)
#define MODES_LONG_MSG_SAMPLES  (MODES_LONG_MSG_BITS     * 2)
#define MODES_SHORT_MSG_SAMPLES (MODES_SHORT_MSG_BITS    * 2)
#define MODES_LONG_MSG_SIZE     (MODES_LONG_MSG_SAMPLES  * sizeof(uint16_t))
#define MODES_SHORT_MSG_SIZE    (MODES_SHORT_MSG_SAMPLES * sizeof(uint16_t))
#define MODES_OUT_BUF_SIZE         (1500)
#define MODES_OUT_FLUSH_SIZE       (MODES_OUT_BUF_SIZE - 256)
#define MODES_OUT_FLUSH_INTERVAL   (60000)
#define MODEAC_MSG_BYTES          2



#define UNUSED(x) (void)(x)

// Program global state
struct _Modes {                             // Internal state
    atomic_int      exit;            // Exit from the main loop when true (2 = unclean exit)



    // Networking
    char           aneterr[ANET_ERR_LEN];
    struct net_service *services;    // Active services
    struct client *clients;          // Our clients

#ifdef _WIN32
    WSADATA        wsaData;          // Windows socket initialisation
#endif

    // Configuration
    int   nfix_crc;                  // Number of crc bit error(s) to correct
    int   check_crc;                 // Only display messages with good CRC
    int   fix_df;                    // Try to correct damage to the DF field, as well as the main message body
    int   enable_df24;               // Enable decoding of DF24..DF31 (Comm-D ELM)
    int   raw;                       // Raw output format
    int   mode_ac;                   // Enable decoding of SSR Modes A & C
    int   mode_ac_auto;              // allow toggling of A/C by Beast commands
    int   net;                       // Enable networking
    int   net_only;                  // Enable just networking
    uint64_t net_heartbeat_interval; // TCP heartbeat interval (milliseconds)
    int   net_output_flush_size;     // Minimum Size of output data
    uint64_t net_output_flush_interval; // Maximum interval (in milliseconds) between outputwrites
    char *net_output_raw_ports;      // List of raw output TCP ports
    char *net_input_raw_ports;       // List of raw input TCP ports
    char *net_output_sbs_ports;      // List of SBS output TCP ports
    char *net_output_stratux_ports;  // List of Stratux output TCP ports
    char *net_input_beast_ports;     // List of Beast input TCP ports
    char *net_output_beast_ports;    // List of Beast output TCP ports
    char *net_bind_address;          // Bind address
    int   net_sndbuf_size;           // TCP output buffer size (64Kb * 2^n)
    int   net_verbatim;              // if true, Beast output connections default to verbatim mode
    int   forward_mlat;              // allow forwarding of mlat messages to output ports
    int   quiet;                     // Suppress stdout

    // User details
    double fUserLat;                // Users receiver/antenna lat/lon needed for initial surface location
    double fUserLon;                // Users receiver/antenna lat/lon needed for initial surface location
    int    bUserFlags;              // Flags relating to the user details
    double maxRange;                // Absolute maximum decoding range, in *metres*
};

extern struct _Modes Modes;

#endif