/* implement WiFiServer with UNIX sockets
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "WiFiClient.h"
#include "WiFiServer.h"

// set for more info
static bool _trace_server = false;

WiFiServer::WiFiServer(int newport)
{
	port = newport;
	socket = -1;
        if (_trace_server)
            printf ("WiFiServer: new instance on port %d\n", port);
}

/* N.B. Arduino version returns void and no ynot
 */
bool WiFiServer::begin(char ynot[])
{
        struct sockaddr_in serv_socket;
        int sfd;
        int reuse = 1;

        if (_trace_server) printf ("WiFiServer: starting server on port %d\n", port);

        /* make socket endpoint */
        if ((sfd = ::socket (AF_INET, SOCK_STREAM, 0)) < 0) {
            printf ("socket: %s\n", strerror(errno));
	    return (false);
	}

        /* bind to given port for any IP address */
        memset (&serv_socket, 0, sizeof(serv_socket));
        serv_socket.sin_family = AF_INET;
        serv_socket.sin_addr.s_addr = htonl (INADDR_ANY);
        serv_socket.sin_port = htons ((unsigned short)port);
        if (::setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse)) < 0) {
            snprintf (ynot, 50, "setsockopt(SO_REUSEADDR): %s", strerror(errno));
	    close (sfd);
	    return (false);
	}
    #ifdef SO_REUSEPORT
        if (::setsockopt(sfd,SOL_SOCKET,SO_REUSEPORT,&reuse,sizeof(reuse)) < 0) {
            snprintf (ynot, 50, "setsockopt(SO_REUSEPORT): %s", strerror(errno));
	    close (sfd);
	    return (false);
	}
    #endif
        if (::bind(sfd,(struct sockaddr*)&serv_socket,sizeof(serv_socket)) < 0) {
            snprintf (ynot, 50, "bind(%d): %s", port, strerror(errno));
	    close (sfd);
	    return (false);
	}

        /* willing to accept connections with a backlog of 5 pending */
        if (::listen (sfd, 5) < 0) {
            snprintf (ynot, 50, "listen: %s", strerror(errno));
	    close (sfd);
	    return (false);
	}

        /* handle write errors inline */
        signal (SIGPIPE, SIG_IGN);

        /* ok */
        if (_trace_server)
            printf ("WiFiServer: new server fd %d\n", socket);
        socket = sfd;
        return (true);
}

WiFiClient WiFiServer::available()
{
        int cli_fd = -1;

        // get a private connection to new client unless server failed to build
        if (socket >= 0) {

            // use select to make a non-blocking check
            fd_set fs;
            FD_ZERO (&fs);
            FD_SET (socket, &fs);
            struct timeval tv;
            tv.tv_sec = tv.tv_usec = 0;

            int s = select (socket+1, &fs, NULL, NULL, &tv);

            if (s == 1 && FD_ISSET (socket, &fs)) {
                struct sockaddr_in cli_socket;
                socklen_t cli_len = sizeof(cli_socket);
                cli_fd = ::accept (socket, (struct sockaddr *)&cli_socket, &cli_len);
                if (cli_fd < 0)
                    printf ("WiFiServer: available() accept() failed: %s\n", strerror(errno));
                else {
                    if (_trace_server)
                        printf ("WiFiServer: available() found new client fd %d\n", cli_fd);
                }
            }

        }

	// return as a client, -1 will test as false
	WiFiClient result(cli_fd);
        return (result);
}

void WiFiServer::stop()
{
        if (socket >= 0) {
            if (_trace_server)
                printf ("WiFiServer: closing fd %d\n", socket);
            shutdown (socket, SHUT_RDWR);
            close (socket);
            socket = -1;
        }
}

// non-standard: block until next connection arrives
WiFiClient WiFiServer::next()
{
        int cli_fd = -1;

        // get a private connection to new client unless server failed to build
        if (socket >= 0) {

            struct sockaddr_in cli_socket;
            socklen_t cli_len = sizeof(cli_socket);
            cli_fd = ::accept (socket, (struct sockaddr *)&cli_socket, &cli_len);
            if (cli_fd < 0)
                printf ("WiFiServer: next() accept() failed: %s\n", strerror(errno));
            else {
                if (_trace_server)
                    printf ("WiFiServer: next() found new client fd %d\n", cli_fd);
            }
        }

	// return as a client, -1 will test as false
	WiFiClient result(cli_fd);
        return (result);
}
