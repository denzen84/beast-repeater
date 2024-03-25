// Part of dump1090, a Mode S message decoder for RTLSDR devices.
//
// net_io.c: network handling.
//
// Copyright (c) 2014-2016 Oliver Jowett <oliver@mutability.co.uk>
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
/* for PRIX64 */
#include <inttypes.h>

#include <assert.h>
#include <stdarg.h>


static void moveNetClient(struct client *c, struct net_service *new_service);

//
//=========================================================================
//
// Networking "stack" initialization
//

// Init a service with the given read/write characteristics, return the new service.
// Doesn't arrange for the service to listen or connect
struct net_service *serviceInit(const char *descr, struct net_writer *writer, heartbeat_fn hb, read_mode_t mode, const char *sep, read_fn handler)
{
    struct net_service *service;

    if (!(service = calloc(sizeof(*service), 1))) {
        fprintf(stderr, "Out of memory allocating service %s\n", descr);
        exit(1);
    }

    service->next = Modes.services;
    Modes.services = service;

    service->descr = descr;
    service->listener_count = 0;
    service->connections = 0;
    service->writer = writer;
    service->read_sep = sep;
    service->read_mode = mode;
    service->read_handler = handler;

    if (service->writer) {
        if (! (service->writer->data = malloc(MODES_OUT_BUF_SIZE)) ) {
            fprintf(stderr, "Out of memory allocating output buffer for service %s\n", descr);
            exit(1);
        }

        service->writer->service = service;
        service->writer->dataUsed = 0;
        service->writer->lastWrite = mstime();
        service->writer->send_heartbeat = hb;
    }

    return service;
}

// Create a client attached to the given service using the provided socket FD
struct client *createSocketClient(struct net_service *service, int fd)
{
    anetSetSendBuffer(Modes.aneterr, fd, (MODES_NET_SNDBUF_SIZE << Modes.net_sndbuf_size));
    return createGenericClient(service, fd);
}

// Create a client attached to the given service using the provided FD (might not be a socket!)
struct client *createGenericClient(struct net_service *service, int fd)
{
    struct client *c;

    anetNonBlock(Modes.aneterr, fd);

    if (!(c = (struct client *) malloc(sizeof(*c)))) {
        fprintf(stderr, "Out of memory allocating a new %s network client\n", service->descr);
        exit(1);
    }

    c->service    = NULL;
    c->next       = Modes.clients;
    c->fd         = fd;
    c->buflen     = 0;
    c->modeac_requested = 0;
    c->verbatim_requested = true;
    c->local_requested = true;
    Modes.clients = c;

    moveNetClient(c, service);

    return c;
}

// Initiate an outgoing connection which will use the given service.
// Return the new client or NULL if the connection failed
struct client *serviceConnect(struct net_service *service, char *addr, int port)
{
    int s;
    char buf[20];

    // Bleh.
    snprintf(buf, 20, "%d", port);
    s = anetTcpConnect(Modes.aneterr, addr, buf);
    if (s == ANET_ERR)
        return NULL;

    return createSocketClient(service, s);
}

// Set up the given service to listen on an address/port.
// _exits_ on failure!
void serviceListen(struct net_service *service, char *bind_addr, char *bind_ports)
{
    int *fds = NULL;
    int n = 0;
    char *p, *end;
    char buf[128];

    if (service->listener_count > 0) {
        fprintf(stderr, "Tried to set up the service %s twice!\n", service->descr);
        exit(1);
    }
    
    if (!bind_ports || !strcmp(bind_ports, "") || !strcmp(bind_ports, "0"))
        return;

    p = bind_ports;
    while (p && *p) {
        int newfds[16];
        int nfds, i;

        end = strpbrk(p, ", ");
        if (!end) {
            strncpy(buf, p, sizeof(buf));
            buf[sizeof(buf)-1] = 0;
            p = NULL;
        } else {
            size_t len = end - p;
            if (len >= sizeof(buf))
                len = sizeof(buf) - 1;
            memcpy(buf, p, len);
            buf[len] = 0;
            p = end + 1;
        }

        nfds = anetTcpServer(Modes.aneterr, buf, bind_addr, newfds, sizeof(newfds));
        if (nfds == ANET_ERR) {
            fprintf(stderr, "Error opening the listening port %s (%s): %s\n",
                    buf, service->descr, Modes.aneterr);
            exit(1);
        }

        fds = realloc(fds, (n+nfds) * sizeof(int));
        if (!fds) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }

        for (i = 0; i < nfds; ++i) {
            anetNonBlock(Modes.aneterr, newfds[i]);
            fds[n++] = newfds[i];
        }
    }

    service->listener_count = n;
    service->listener_fds = fds;
}


//
//=========================================================================
//
// This function gets called from time to time when the decoding thread is
// awakened by new data arriving. This usually happens a few times every second
//
static struct client * modesAcceptClients(void) {
    int fd;
    struct net_service *s;

    for (s = Modes.services; s; s = s->next) {
        int i;
        for (i = 0; i < s->listener_count; ++i) {
            while ((fd = anetTcpAccept(Modes.aneterr, s->listener_fds[i])) >= 0) {
                createSocketClient(s, fd);
            }
        }
    }

    return Modes.clients;
}
//
//=========================================================================
//
// On error free the client, collect the structure, adjust maxfd if needed.
//
static void modesCloseClient(struct client *c) {
    if (!c->service) {
        fprintf(stderr, "warning: double close of net client\n");
        return;
    }

    // Clean up, but defer removing from the list until modesNetCleanup().
    // This is because there may be stackframes still pointing at this
    // client (unpredictably: reading from client A may cause client B to
    // be freed)

    close(c->fd);
    c->service->connections--;

    // mark it as inactive and ready to be freed
    c->fd = -1;
    c->service = NULL;
    c->modeac_requested = 0;
}
//
//=========================================================================
//
// Send the write buffer for the specified writer to all connected clients
//
static void flushWrites(struct net_writer *writer) {
    struct client *c;

    for (c = Modes.clients; c; c = c->next) {
        if (!c->service)
            continue;
        if (c->service == writer->service) {
#ifndef _WIN32
            int nwritten = write(c->fd, writer->data, writer->dataUsed);
#else
            int nwritten = send(c->fd, writer->data, writer->dataUsed, 0 );
#endif
            if (nwritten != writer->dataUsed) {
                modesCloseClient(c);
            }
        }
    }

    writer->dataUsed = 0;
    writer->lastWrite = mstime();
}

// Prepare to write up to 'len' bytes to the given net_writer.
// Returns a pointer to write to, or NULL to skip this write.
static void *prepareWrite(struct net_writer *writer, int len) {
    if (!writer ||
        !writer->service ||
        !writer->service->connections ||
        !writer->data)
        return NULL;

    if (len > MODES_OUT_BUF_SIZE)
        return NULL;

    if (writer->dataUsed + len >= MODES_OUT_BUF_SIZE) {
        // Flush now to free some space
        flushWrites(writer);
    }

    return writer->data + writer->dataUsed;
}

// Complete a write previously begun by prepareWrite.
// endptr should point one byte past the last byte written
// to the buffer returned from prepareWrite.
static void completeWrite(struct net_writer *writer, void *endptr) {
    writer->dataUsed = endptr - writer->data;

    if (writer->dataUsed >= Modes.net_output_flush_size) {
        flushWrites(writer);
    }
}

static void send_beast_heartbeat(struct net_service *service)
{
    static char heartbeat_message[] = { 0x1a, '1', 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    char *data;

    if (!service->writer)
        return;

    data = prepareWrite(service->writer, sizeof(heartbeat_message));
    if (!data)
        return;

    memcpy(data, heartbeat_message, sizeof(heartbeat_message));
    completeWrite(service->writer, data + sizeof(heartbeat_message));
}



//
//=========================================================================
//


// Move a network client to a new service
static void moveNetClient(struct client *c, struct net_service *new_service)
{
    if (c->service == new_service)
        return;

    if (c->service) {
        // Flush to ensure correct message framing
        if (c->service->writer)
            flushWrites(c->service->writer);
        --c->service->connections;
    }

    if (new_service) {
        // Flush to ensure correct message framing
        if (new_service->writer)
            flushWrites(new_service->writer);
        ++new_service->connections;
    }

    c->service = new_service;
}

//
//=========================================================================
//
// This function polls the clients using read() in order to receive new
// messages from the net.
//
// The message is supposed to be separated from the next message by the
// separator 'sep', which is a null-terminated C string.
//
// Every full message received is decoded and passed to the higher layers
// calling the function's 'handler'.
//
// The handler returns 0 on success, or 1 to signal this function we should
// close the connection with the client in case of non-recoverable errors.
//
static void modesReadFromClient(struct client *c) {
    int left;
    int nread;
    int bContinue = 1;

    while (bContinue) {
        left = MODES_CLIENT_BUF_SIZE - c->buflen - 1; // leave 1 extra byte for NUL termination in the ASCII case

        // If our buffer is full discard it, this is some badly formatted shit
        if (left <= 0) {
            c->buflen = 0;
            left = MODES_CLIENT_BUF_SIZE;
            // If there is garbage, read more to discard it ASAP
        }
#ifndef _WIN32
        nread = read(c->fd, c->buf+c->buflen, left);
#else
        nread = recv(c->fd, c->buf+c->buflen, left, 0);
        if (nread < 0) {errno = WSAGetLastError();}
#endif

        // If we didn't get all the data we asked for, then return once we've processed what we did get.
        if (nread != left) {
            bContinue = 0;
        }

        if (nread == 0) { // End of file
            modesCloseClient(c);
            return;
        }

#ifndef _WIN32
        if (nread < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) // No data available (not really an error)
#else
        if (nread < 0 && errno == EWOULDBLOCK) // No data available (not really an error)
#endif
        {
            return;
        }

        if (nread < 0) { // Other errors
            modesCloseClient(c);
            return;
        }

        c->buflen += nread;

        char *som = c->buf;           // first byte of next message
        char *eod = som + c->buflen;  // one byte past end of data
        char *p;

        switch (c->service->read_mode) {
        case READ_MODE_IGNORE:
            // drop the bytes on the floor
            som = eod;
            break;

        case READ_MODE_BEAST:
            // This is the Beast Binary scanning case.
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.

            while (som < eod && ((p = memchr(som, (char) 0x1a, eod - som)) != NULL)) { // The first byte of buffer 'should' be 0x1a
                som = p; // consume garbage up to the 0x1a
                ++p; // skip 0x1a

                if (p >= eod) {
                    // Incomplete message in buffer, retry later
                    break;
                }

                char *eom; // one byte past end of message
                if        (*p == '1') {
                    eom = p + MODEAC_MSG_BYTES      + 8;         // point past remainder of message
                } else if (*p == '2') {
                    eom = p + MODES_SHORT_MSG_BYTES + 8;
                } else if (*p == '3') {
                    eom = p + MODES_LONG_MSG_BYTES  + 8;
                } else if (*p == '4') {
                    eom = p + MODES_LONG_MSG_BYTES  + 8;
                } else if (*p == '5') {
                    eom = p + MODES_LONG_MSG_BYTES  + 8;
                } else {
                    // Not a valid beast message, skip 0x1a and try again
                    ++som;
                    continue;
                }

                // we need to be careful of double escape characters in the message body
                for (p = som + 1; p < eod && p < eom; p++) {
                    if (0x1A == *p) {
                        p++;
                        eom++;
                    }
                }

                if (eom > eod) { // Incomplete message in buffer, retry later
                    break;
                }

                // Have a 0x1a followed by 1/2/3/4/5 - pass message to handler.
                if (c->service->read_handler(c, som + 1)) {
                    modesCloseClient(c);
                    return;
                }

                // advance to next message
                som = eom;
            }
            break;

        case READ_MODE_BEAST_COMMAND:
            while (som < eod && ((p = memchr(som, (char) 0x1a, eod - som)) != NULL)) { // The first byte of buffer 'should' be 0x1a
                char *eom; // one byte past end of message

                som = p; // consume garbage up to the 0x1a
                ++p; // skip 0x1a

                if (p >= eod) {
                    // Incomplete message in buffer, retry later
                    break;
                }

                if (*p == '1') {
                    eom = p + 2;
                } else {
                    // Not a valid beast command, skip 0x1a and try again
                    ++som;
                    continue;
                }

                // we need to be careful of double escape characters in the message body
                for (p = som + 1; p < eod && p < eom; p++) {
                    if (0x1A == *p) {
                        p++;
                        eom++;
                    }
                }

                if (eom > eod) { // Incomplete message in buffer, retry later
                    break;
                }

                // Have a 0x1a followed by 1 - pass message to handler.
                if (c->service->read_handler(c, som + 1)) {
                    modesCloseClient(c);
                    return;
                }

                // advance to next message
                som = eom;
            }
            break;

        case READ_MODE_ASCII:
            //
            // This is the ASCII scanning case, AVR RAW or HTTP at present
            // If there is a complete message still in the buffer, there must be the separator 'sep'
            // in the buffer, note that we full-scan the buffer at every read for simplicity.

            // Always NUL-terminate so we are free to use strstr()
            // nb: we never fill the last byte of the buffer with read data (see above) so this is safe
            *eod = '\0';

            while (som < eod && (p = strstr(som, c->service->read_sep)) != NULL) { // end of first message if found
                *p = '\0';                         // The handler expects null terminated strings
                if (c->service->read_handler(c, som)) {         // Pass message to handler.
                    modesCloseClient(c);           // Handler returns 1 on error to signal we .
                    return;                        // should close the client connection
                }
                som = p + strlen(c->service->read_sep);               // Move to start of next message
            }

            break;
        }

        if (som > c->buf) {                        // We processed something - so
            c->buflen = eod - som;                 //     Update the unprocessed buffer length
            memmove(c->buf, som, c->buflen);       //     Move what's remaining to the start of the buffer
        } else {                                   // If no message was decoded process the next client
            return;
        }
    }
}





//
// =============================== Network IO ===========================
//
