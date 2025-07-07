#ifndef _PEER_ADDR_UTIL_H_
#define _PEER_ADDR_UTIL_H_
#include <netinet/in.h>
#include "lib/stream.h"

static inline void encode_peer_addr_v6(struct stream *s, const struct in6_addr *addr) {
    stream_put(s, addr->s6_addr, 16);
}

#endif // _PEER_ADDR_UTIL_H_
