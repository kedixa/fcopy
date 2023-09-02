#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>

bool get_local_addr(std::vector<std::string> &addrs) {
    int sockfd;
    struct ifreq ifr, *ifr_it, *ifr_end;
    struct ifconf ifc;
    char buf[10240];

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return false;

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
        close(sockfd);
        return false;
    }

    ifr_it = ifc.ifc_req;
    ifr_end = ifr_it + ifc.ifc_len / sizeof (struct ifreq);

    char ipbuf[64];
    const char *ip;
    struct sockaddr *addr;
    while (ifr_it != ifr_end) {
        ip = nullptr;
        strcpy(ifr.ifr_name, ifr_it->ifr_name);

        if (ioctl(sockfd, SIOCGIFADDR, &ifr) == 0) {
            addr = (struct sockaddr *)&ifr.ifr_addr;
            if (addr->sa_family == AF_INET)
                ip = inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr, ipbuf, sizeof(ipbuf));
            else if (addr->sa_family == AF_INET6)
                ip = inet_ntop(AF_INET6, &((struct sockaddr_in6 *)addr)->sin6_addr, ipbuf, sizeof(ipbuf));
        }

        if (ip)
            addrs.push_back(ip);

        ifr_it++;
    }

    close(sockfd);
    return true;
}
