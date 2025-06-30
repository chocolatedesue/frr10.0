/*
 * Time Variant Routing Shortest Path First (SPF) definition - tvr_spf.h
 *
 * Author: Yuxuan Chen <chenyuxuan@cnic.cn>
 *
 * This file is part of Free Range Routing (FRR).
 */



#ifndef _FRR_TVR_SPF_H_
#define _FRR_TVR_SPF_H_

#include <stdint.h>
#include "typesafe.h"
#include "prefix.h"
#include "tvr_db.h"

#define TVR_INF_DIST 1000000000000000000ULL

#ifdef __cplusplus
extern "C" {
#endif

PREDECL_RBTREE_UNIQ(pq_rb);

struct pq_elem {
    uint64_t dist;
    uint64_t local_node;
    
    struct pq_rb_item entry;
};

macro_inline int pq_cmp(const struct pq_elem *lhs,
                const struct pq_elem *rhs)
{
    if(lhs->dist != rhs->dist) {
        return numcmp(lhs->dist, rhs->dist);
    }
    return numcmp(lhs->local_node, rhs->local_node);
}

DECLARE_RBTREE_UNIQ(pq_rb, struct pq_elem, entry, pq_cmp);

struct tvr_nprefix {
    uint8_t prefixlen;
    struct in6_addr prefix;

    uint8_t spf_status;
};

macro_inline int tvr_nprefix_cmp(const struct tvr_nprefix *lhs,
                const struct tvr_nprefix *rhs)
{
	if (lhs->prefixlen != rhs->prefixlen) {
		return numcmp(lhs->prefixlen, rhs->prefixlen);
    }
    return memcmp(&lhs->prefix, &rhs->prefix, 16);
}

struct tvr_nlink {
    uint32_t remote_node;
    struct in6_addr link_addr;

    uint32_t igp_metric;
    uint8_t spf_status;
};

macro_inline int tvr_nlink_cmp(const struct tvr_nlink *lhs,
                const struct tvr_nlink *rhs)
{
	if (lhs->remote_node != rhs->remote_node) {
		return numcmp(lhs->remote_node, rhs->remote_node);
    }
    return memcmp(&lhs->link_addr, &rhs->link_addr, 16);
}

PREDECL_RBTREE_UNIQ(node_rb);

struct tvr_node {
    uint32_t local_node;

    uint8_t spf_status;
    
    bool visited;
    uint64_t dist;
    uint32_t next_hop;

    struct list *prefixes;
    struct list *links;

    struct node_rb_item entry;
};

macro_inline int tvr_node_cmp(const struct tvr_node *lhs,
                const struct tvr_node *rhs)
{
    return numcmp(lhs->local_node, rhs->local_node);
}

DECLARE_RBTREE_UNIQ(node_rb, struct tvr_node, entry, tvr_node_cmp);

PREDECL_RBTREE_UNIQ(route_rb);

struct tvr_route {
    uint8_t prefixlen;
    struct in6_addr prefix;

    uint64_t dist;
    uint32_t next_hop;

    struct route_rb_item entry;
};

macro_inline int tvr_route_cmp(const struct tvr_route *lhs,
                const struct tvr_route *rhs)
{
	if (lhs->prefixlen != rhs->prefixlen) {
		return numcmp(lhs->prefixlen, rhs->prefixlen);
    }
    return memcmp(&lhs->prefix, &rhs->prefix, 16);
}

DECLARE_RBTREE_UNIQ(route_rb, struct tvr_route, entry, tvr_route_cmp);

struct tvr_spf {
	struct pq_rb_head pq_rb_root;	
	struct node_rb_head node_rb_root;
	struct route_rb_head route_rb_root;
};

extern struct tvr_spf *tvr_spf_create(struct tvr_db *db, uint32_t src_node, uint64_t time_stamp1, uint64_t time_stamp2);

extern void tvr_spf_destroy(struct tvr_spf **spf);

/* Forward declaration for zclient */
struct zclient;

/* Route installation/uninstallation functions */
extern int tvr_spf_install_routes(struct tvr_spf *spf, struct zclient *zclient, 
                                 vrf_id_t vrf_id, uint8_t route_type);

extern int tvr_spf_uninstall_routes(struct tvr_spf *spf, struct zclient *zclient, 
                                   vrf_id_t vrf_id, uint8_t route_type);

/* Single route installation/uninstallation functions */
extern int tvr_spf_install_single_route(struct zclient *zclient, const struct prefix *prefix,
                                       uint32_t next_hop_node, vrf_id_t vrf_id,
                                       uint8_t route_type, uint32_t metric);

extern int tvr_spf_uninstall_single_route(struct zclient *zclient, const struct prefix *prefix,
                                         vrf_id_t vrf_id, uint8_t route_type);

/* Convenience functions for string-based prefix input */
extern int tvr_spf_install_route_from_string(struct zclient *zclient, const char *prefix_str,
                                            uint32_t next_hop_node, vrf_id_t vrf_id,
                                            uint8_t route_type, uint32_t metric);

extern int tvr_spf_uninstall_route_from_string(struct zclient *zclient, const char *prefix_str,
                                              vrf_id_t vrf_id, uint8_t route_type);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_TVR_SPF_H_ */