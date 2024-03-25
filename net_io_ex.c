
#include "net_io_ex.h"
#include "net_io.h"
#include "beast-repeater.h"
#include "util.h"
#include "net_io.c"

struct beastClient *beastClients;


void clientSendBuffer(struct client *c, char *buf, const int len) {
	anetWrite(c->fd, buf, len);
}

struct net_service* makeBeastInputServiceEx(read_fn handler) {
	return serviceInit("Beast TCP client input Ex", NULL, NULL, READ_MODE_BEAST, NULL,
			handler);
}

struct net_service* makeBeastOutputServiceEx(void) {
	return serviceInit("Beast TCP client output Ex", NULL, NULL, READ_MODE_IGNORE,
			NULL, NULL);
}

struct net_service* makeBeastServerInputServiceEx(read_fn handler)
{
    return serviceInit("Beast TCP server input", NULL, NULL, READ_MODE_BEAST, NULL, handler);
}

struct net_service* makeBeastServerOutputServiceEx(struct net_writer *writer)
{
    return serviceInit("Beast TCP server output", writer, send_beast_heartbeat, READ_MODE_IGNORE, NULL, NULL);
}

void writeBeastOutput(struct net_service *service, char *data, int len) {
    char *buf;
    
    if (!service) return;
    if (!service->writer)
        return;
    buf = prepareWrite(service->writer, len);
    if (!buf)
        return;
    memcpy(buf, data, len);
    completeWrite(service->writer, buf + len);
}

void modesInitNetEx(void) {
    signal(SIGPIPE, SIG_IGN);
    Modes.clients = NULL;
    Modes.services = NULL;
}

void modesNetPeriodicWorkEx(void) {
    struct client *c, **prev;    
    struct net_service *s;
    struct beastClient *bc;
    uint64_t now = mstime();
    int need_flush = 0;

    // Accept new connections
    modesAcceptClients();

    // Read from clients
    for (c = Modes.clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service->read_handler)
            modesReadFromClient(c);
    }

    // If we have generated no messages for a while, send
    // a heartbeat
    if (Modes.net_heartbeat_interval) {
        for (s = Modes.services; s; s = s->next) {        	
            if (s->writer &&
                s->connections &&
                s->writer->send_heartbeat &&
                (s->writer->lastWrite + Modes.net_heartbeat_interval) <= now) {
                s->writer->send_heartbeat(s);
            }
        }
    }

    // If we have data that has been waiting to be written for a while,
    // write it now.
    for (s = Modes.services; s; s = s->next) {    	
        if (s->writer &&
            s->writer->dataUsed &&
            (need_flush || (s->writer->lastWrite + Modes.net_output_flush_interval) <= now)) {
            flushWrites(s->writer);
        }
    }

    // Unlink and free closed clients
    for (prev = &Modes.clients, c = *prev; c; c = *prev) {
        if (c->fd == -1) {
            // Recently closed, prune from list
            *prev = c->next;
            printf("Connection lost with %p\n", c);
            free(c);
        } else {
            prev = &c->next;
        }
    }
    //static struct beastClient* beastClients;
    //fprintf(stderr, "Chechking BEAST clients... %p\n", beastClients);

    // Check input connections and reconnect
    for (bc = beastClients; bc; bc = bc->next) {
    	if (!bc->clientHandle || !bc->serviceHandle->connections) {
    		if (now >= bc->reconnectTime) {
    			fprintf(stderr, "BEAST %s: connecting to %s:%d...\n", bc->isInput ? "INPUT" : "OUTPUT", bc->ipaddr, bc->ipport);			
    			bc->clientHandle = serviceConnect(bc->serviceHandle, bc->ipaddr,
    					bc->ipport);
    			
    			if (!bc->clientHandle) {
    				fprintf(stderr, "Error establishing connection to %s:%d (%s). Reconnect after 10 seconds...\n", bc->ipaddr, bc->ipport, Modes.aneterr);
    				bc->reconnectTime = now + RECONNECT_TIME_MS;
    			} else {
    				bc->reconnectTime = now;
    				fprintf(stderr, "Connection established to %s:%d\n", bc->ipaddr, bc->ipport);
    			}
    			}
    		} //else if (!bc->serviceHandle->connections) bc->clientHandle = NULL;
    	}
}

void broadcastBeastMessage(char* data, int len) {
	
	struct beastClient *c;
	struct net_service *s;
	
	for (c = beastClients; c; c = c->next)
		if (c->clientHandle && !c->isInput) clientSendBuffer(c->clientHandle, data, len);
	
	for (s = Modes.services; s; s = s->next) {			
	       writeBeastOutput(s, data, len);
	}
}


int handleBeastMessage(struct client *c, char *p) {
	
	UNUSED(c);
	//UNUSED(p);
	
    int dataLen = 2;
    char* dataStart;
    int msgLen = 0;
    int  j;
    char ch;
    
    
    dataStart = p;
    dataStart--;
            
    ch = *p++; /// Get the message type

    switch(ch) {
    case '1': msgLen = MODEAC_MSG_BYTES;
    	break;
    case '2': msgLen = MODES_SHORT_MSG_BYTES;
    	break;
    case '3': msgLen = MODES_LONG_MSG_BYTES;
    	break;
    case '5': msgLen = 21;
    	break;    	 
    }
    
    if (msgLen) {
    	j = 0;
    	while(j <= msgLen + 6) {
    		ch = *p++;
    		j++;
    		dataLen++;
    		if (0x1A == ch) {p++; dataLen++; }    		
    	}
    	    	
    	broadcastBeastMessage(dataStart, dataLen);
    }
    return 0;
}

void freeBeastClients() {
	
struct beastClient *c, *p;
for (c = beastClients; c; c = p) {
	free(c->ipaddr);
	p = c->next;
	free(c);
}
}

struct beastClient* newBeastClient() {

struct beastClient *bClient = malloc(sizeof(struct beastClient));
if (bClient) {
	memset(bClient, 0x00, sizeof(struct beastClient));
}
return bClient;
}



