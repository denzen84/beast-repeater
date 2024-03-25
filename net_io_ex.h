
#ifndef DUMP1090_NETIOEXT_H
#define DUMP1090_NETIOEXT_H

#include <stdbool.h>
#include "beast-repeater.h"


#define RECONNECT_TIME_MS 10000

struct beastClient {
	struct beastClient* next;
	struct net_service* serviceHandle;
	struct client* clientHandle;
	char* ipaddr;
	int ipport;
	uint64_t reconnectTime;
	bool isInput;
};

extern struct beastClient *beastClients;

void clientSendBuffer(struct client *c, char *buf, const int len);
struct net_service* makeBeastInputServiceEx(read_fn handler);
struct net_service* makeBeastOutputServiceEx(void);
struct net_service* makeBeastServerInputServiceEx(read_fn handler);
struct net_service* makeBeastServerOutputServiceEx(struct net_writer *writer);
void writeBeastOutput(struct net_service *service, char *data, int len);
void modesInitNetEx(void);
void modesNetPeriodicWorkEx(void);

void broadcastBeastMessage(char* data, int len);
int handleBeastMessage(struct client *c, char *p);
void freeBeastClients();
struct beastClient* newBeastClient();

#endif