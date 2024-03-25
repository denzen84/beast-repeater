// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// beast-repeater.c
//
// Copyright (c) 2022 Denis G Dugushkin (denis.dugushkin@gmail.com)
//
// This file is free software: you may copy, redistribute and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 2 of the License, or (at your
// option) any later version.
//
// This file is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

// This file incorporates work covered by the following copyright and
// permission notice:
//
//   Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
//
//   All rights reserved.
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions are
//   met:
//
//    *  Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//    *  Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
//   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "beast-repeater.h"
#include "net_io_ex.h"
#include "util.h"
#include "net_io.h"

struct _Modes Modes;

//
// ============================= Utility functions ==========================
//
static void sigintHandler(int dummy) {
    UNUSED(dummy);
    signal(SIGINT, SIG_DFL);  // reset signal handler - bit extra safety
    Modes.exit = 1;           // Signal to threads that we are done
}


void receiverPositionChanged(float lat, float lon, float alt) {
	/* nothing */
	(void) lat;
	(void) lon;
	(void) alt;
}

//
// =============================== Initialization ===========================
//
static void faupInitConfig(void) {
	// Default everything to zero/NULL
	memset(&Modes, 0, sizeof(Modes));

	// Now initialise things that should not be 0/NULL to their defaults
	Modes.nfix_crc = 1;
	Modes.check_crc = 1;
	Modes.net = 1;
	Modes.net_heartbeat_interval = MODES_NET_HEARTBEAT_INTERVAL;
	Modes.maxRange = 1852 * 360; // 360NM default max range; this also disables receiver-relative positions
	Modes.quiet = 1;
	Modes.net_output_flush_size = 1024;
	Modes.net_output_flush_interval = 50; // milliseconds
	Modes.net_bind_address = "0.0.0.0";
}

//
// ================================ Main ====================================
//
static void showHelp(void) {
printf(
		"beast-repeater - universal splitter for BEAST data streams\n"
		"V20240325 ARM64\n\n"		
		"Usage:\n"
		"--inConnect <host>:<port>      Host and port for input connector\n"
		"--outConnect <host>:<port>     Host and port for output connector\n"
		"--inServer <port>              Input server\n"
		"--outServer <port>             Output server\n"
		"--net-bind-address <ip>        IP address to bind to (default 0.0.0.0, use 127.0.0.1 for private)\n"

		"--help                         Show this help\n"
		"\n");
}

//
//=========================================================================
//
// This function is called a few times every second by main in order to
// perform tasks we need to do continuously, like accepting new clients
// from the net, refreshing the screen in interactive mode, and so forth
//
static void backgroundTasks(void) {
modesNetPeriodicWorkEx();
}

#define randnum(min, max) \
    ((rand() % (int)(((max) + 1) - (min))) + (min))

static char* extractHostPort(char* data, int* port) {
	
	char *current_pos = strchr(data,':');
	*current_pos = '\0';
	char* host = strdup(data);
	*current_pos = ':';
	*port = atoi(++current_pos);
	return host;
}

//
//=========================================================================
//
int main(int argc, char **argv) {

int j;
uint64_t now = mstime();
struct beastClient *bClient = NULL;
struct net_writer *writer;
struct net_service *serverService;

// Set sane defaults
faupInitConfig();
modesInitNetEx();
signal(SIGINT, sigintHandler); // Define Ctrl/C handler (exit program)


// Parse the command line options
for (j = 1; j < argc; j++) {
	int more = j + 1 < argc; // There are more arguments

	if (!strcmp(argv[j], "--inServer") && more) {
		/* create new listen input server */
		fprintf(stderr, "INPUT: Starting server at %s:%s...\n", Modes.net_bind_address, argv[j+1]);
		serverService = makeBeastServerInputServiceEx(
				handleBeastMessage);
		serviceListen(serverService, Modes.net_bind_address, argv[++j]);

	} else if (!strcmp(argv[j], "--outServer") && more) {
		/* create new listen output server */
		fprintf(stderr, "OUTPUT: Starting server at %s:%s...\n", Modes.net_bind_address, argv[j+1]);
		writer = malloc(sizeof(struct net_writer));
		if (writer) {
			memset(writer, 0x00, sizeof(struct net_writer));
			serverService = makeBeastServerOutputServiceEx(
					writer);
			serviceListen(serverService, Modes.net_bind_address, argv[++j]);
		}
	} else if (!strcmp(argv[j], "--inConnect") && more) {
		bClient = newBeastClient();
		if (bClient) {
			bClient->next = beastClients;
			bClient->reconnectTime = now;
			bClient->serviceHandle = makeBeastInputServiceEx(handleBeastMessage);
			bClient->isInput = true;			
			bClient->ipaddr = extractHostPort(argv[++j], &bClient->ipport);			
			beastClients = bClient;
		}
	} else if (!strcmp(argv[j], "--outConnect") && more) {
		bClient = newBeastClient();
		if (bClient) {
			bClient->next = beastClients;
			bClient->reconnectTime = now;
			bClient->serviceHandle = makeBeastOutputServiceEx();			
			bClient->isInput = false;			
			bClient->ipaddr = extractHostPort(argv[++j], &bClient->ipport);			
			beastClients = bClient;
		}
	} else if (!strcmp(argv[j],"--net-bind-address") && more) {
	            free(Modes.net_bind_address);
	            Modes.net_bind_address = strdup(argv[++j]);
	} else if (!strcmp(argv[j], "--help")) {
		showHelp();
		exit(0);
	} else {
		fprintf(stderr, "Unknown or not enough arguments for option '%s'.\n\n",
				argv[j]);
		showHelp();
		exit(1);
	}
}

if (!Modes.clients && !Modes.services) {
	fprintf(stderr, "Not enough arguments. Nothing to do.\n\n");
	showHelp();
	exit(1);	
}

// Run it until we've lost either connection
while (!Modes.exit) {
	struct timespec r = { 0, 100 * 1000 * 1000 };
	backgroundTasks();
	nanosleep(&r, NULL);
}

freeBeastClients();
return 0;
}
//
//=========================================================================
//
