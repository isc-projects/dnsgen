#ifndef __packet_h
#define __packet_h

extern int socket_open(int ifindex);
extern int socket_setopt(int fd, int optname, const uint32_t val);
extern int socket_getopt(int fd, int optname, uint32_t& val);

#endif // __packet_h
