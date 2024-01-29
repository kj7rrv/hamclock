#include "WiFiUdp.h"

static bool verbose;

WiFiUDP::WiFiUDP()
{
	sockfd = -1;
}

WiFiUDP::~WiFiUDP()
{
	stop();
}

bool WiFiUDP::begin(int port)
{
        // create UDP socket
	sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)  {
	    printf ("UDP: socket(): %s\n", strerror(errno));
	    return (false);
	}

        // bind to port from anywhere
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        int one = 1;
        (void) setsockopt (sockfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
        one = 1;
        (void) setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if (bind(sockfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
	    printf ("UDP: bind(%d): %s\n", port, strerror(errno));
            stop();
	    return (false);
	}

        if (verbose)
            printf ("UDP: new socket %d port %d\n", sockfd, port);

	return (true);
}

bool WiFiUDP::beginMulticast (IPAddress ifIP, IPAddress mcIP, int port)
{
        // not used
        (void)(ifIP);

        // create UDP socket
	sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0)  {
	    printf ("UDP: socket(mcIP): %s\n", strerror(errno));
	    return (false);
	}

        // bind to mcIP
        struct sockaddr_in mcast_group;
        char mca[32];
        snprintf (mca, sizeof(mca), "%u.%u.%u.%u", mcIP[0], mcIP[1], mcIP[2], mcIP[3]);
        memset(&mcast_group, 0, sizeof(mcast_group));
        mcast_group.sin_family = AF_INET;
        mcast_group.sin_port = htons(port);
        mcast_group.sin_addr.s_addr = inet_addr(mca);
        if (bind(sockfd, (struct sockaddr*)&mcast_group, sizeof(mcast_group)) < 0) {
	    printf ("UDP: bind(mcIP): %s\n", strerror(errno));
            stop();
	    return (false);
	}

        // join mcIP from anywhere
        struct ip_mreq mreq;
        mreq.imr_multiaddr = mcast_group.sin_addr;
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
	    printf ("UDP: IP_ADD_MEMBERSHIP: %s\n", strerror(errno));
            stop();
	    return (false);
        }

        if (verbose)
            printf ("UDP: new multicast socket %d\n", sockfd);

        // ok
        return (true);
}


IPAddress WiFiUDP::remoteIP()
{
        IPAddress rip;
        unsigned a[4];
        char *string = inet_ntoa (remoteip.sin_addr);
        sscanf (string, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]);
        rip[0] = a[0];
        rip[1] = a[1];
        rip[2] = a[2];
        rip[3] = a[3];
        return (rip);
}


void WiFiUDP::beginPacket (const char *host, int port)
{
        struct addrinfo hints, *aip;
        char port_str[16];
        
        /* lookup host address.
         * N.B. must call freeaddrinfo(aip) after successful call before returning
         */ 
        memset (&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        snprintf (port_str, sizeof(port_str), "%d", port);
        int error = ::getaddrinfo (host, port_str, &hints, &aip);
        if (error) {
            printf ("UDP: getaddrinfo(%s:%d): %s\n", host, port, gai_strerror(error));
            stop();
            return;
        }
        
        /* connect */
        if (connect (sockfd, aip->ai_addr, aip->ai_addrlen) < 0) {
            printf ("UDP: connect(%s,%d): %s\n", host, port, strerror(errno));
            stop();
        }

        /* clean up */
        freeaddrinfo (aip);
}

void WiFiUDP::write (uint8_t *buf, int n)
{
        if (sockfd < 0)
            return;

        // init no
	w_n = 0;

	sendto_n = ::write(sockfd, buf, n);
        if (sendto_n != n) {
	    printf ("UDP: sendto(%d): only sent %d\n", n, sendto_n);
            stop();
	    return;
	}
	if (sendto_n < 0) {
	    printf ("UDP: sendto(%d): %s\n", n, strerror(errno));
            stop();
	    return;
	}

        // save
	w_n = n;
}

bool WiFiUDP::endPacket()
{
	// compare n sent to original count
	return (sendto_n == w_n);
}

int WiFiUDP::parsePacket()
{
        if (sockfd < 0)
            return (0);

	struct timeval tv;
	fd_set rset;
	tv.tv_sec = 0;		// don't block
	tv.tv_usec = 0;
	FD_ZERO (&rset);
	FD_SET (sockfd, &rset);

	// use select() so we can time out, just using read could hang forever
	int s = ::select (sockfd+1, &rset, NULL, NULL, &tv);
	if (s < 0) {
	    printf ("UDP: select(poll): %s\n", strerror(errno));
            stop();
	    return (0);
	}
	if (s == 0)
	    return (0);

        socklen_t rlen = sizeof(remoteip);
	r_n = ::recvfrom(sockfd, r_buf, sizeof(r_buf), 0, (struct sockaddr *)&remoteip, &rlen);
	if (r_n < 0) {
	    printf ("UDP: recvfrom(): %s\n", strerror(errno));
            stop();
	    return (0);
	}
	return (r_n);
}

int WiFiUDP::read(uint8_t *buf, int n)
{
	memcpy (buf, r_buf, n > r_n ? r_n : n);
	return (r_n);
}

void WiFiUDP::stop()
{
	if (sockfd >= 0) {
	    ::close (sockfd);
            if (verbose)
                printf ("UDP: closing socket %d\n", sockfd);
	    sockfd = -1;
	}
}
