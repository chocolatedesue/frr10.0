/*
 * Time Variant Routing Shortest Path First (SPF) - tvr_spf.h
 *
 * Author: Yuxuan Chen <chenyuxuan@cnic.cn>
 *
 * This file is part of Free Range Routing (FRR).
 */

#include "tvr_spf.h"
#include <netinet/in.h>
#include <stdint.h>
#include "zclient.h"
#include "nexthop.h"

DEFINE_MTYPE_STATIC(LIB, TVR_SPF, "Time Variant Routing Shortest Path First (SPF)");

#define TVR_DEFAULT_STATUS 0
#define TVR_UNREACH_STATUS 1
#define TVR_NOTRANS_STATUS 2

static struct tvr_nlink *tvr_nlink_create(struct tvr_link_nlri *nlri) {
    struct tvr_nlink *nlink = XCALLOC(MTYPE_TVR_SPF, sizeof(struct tvr_nlink));
    
    memset(nlink, 0, sizeof(*nlink));
    nlink->remote_node = nlri->remote_node;
    nlink->link_addr = nlri->link_addr;

    nlink->igp_metric = nlri->attr.igp_metric;
    nlink->spf_status = nlri->attr.spf_status;
    nlink -> ifindex = nlri -> ifindex;

    return nlink;
}

static void tvr_nlink_destroy(struct tvr_nlink **nlink) {
    XFREE(MTYPE_TVR_SPF, *nlink);
}

static struct tvr_nprefix *tvr_nprefix_create(struct tvr_prefix_nlri *nlri) {
    struct tvr_nprefix *nprefix = XCALLOC(MTYPE_TVR_SPF, sizeof(struct tvr_nprefix));

    memset(nprefix, 0, sizeof(*nprefix));
    nprefix->prefixlen = nlri->prefixlen;
    nprefix->prefix = nlri->prefix;

    nprefix->spf_status = nlri->attr.spf_status;

    return nprefix;
}

static void tvr_nprefix_destroy(struct tvr_nprefix **nprefix) {
    XFREE(MTYPE_TVR_SPF, *nprefix);
}

static struct tvr_node *tvr_node_create(struct tvr_node_nlri *nlri) {
    struct tvr_node *node = XCALLOC(MTYPE_TVR_SPF, sizeof(struct tvr_node));

    memset(node, 0, sizeof(*node));
    node->local_node = nlri->local_node;
    node->spf_status = nlri->attr.spf_status;
    node->visited = false;
    node->dist = TVR_INF_DIST;
    
    node->prefixes = list_new();
    node->prefixes->cmp = (int (*)(void *, void *)) tvr_nprefix_cmp;
    node->links = list_new();
    node->links->cmp = (int (*)(void *, void *)) tvr_nlink_cmp;

    return node;
};

static void tvr_node_destroy(struct tvr_node **node) {
    struct listnode *curnode, *nextnode;
    struct tvr_nprefix *nprefix;
    struct tvr_nlink *nlink;

	for(ALL_LIST_ELEMENTS((*node)->prefixes, curnode, nextnode, nprefix)) {
        tvr_nprefix_destroy(&nprefix);
    }
    list_delete(&(*node)->prefixes);

	for(ALL_LIST_ELEMENTS((*node)->links, curnode, nextnode, nlink)) {
        tvr_nlink_destroy(&nlink);
    }
    list_delete(&(*node)->links);

    XFREE(MTYPE_TVR_SPF, *node);
}

static struct tvr_route *tvr_route_create(struct tvr_prefix_nlri *nlri) {
    struct tvr_route *route = XCALLOC(MTYPE_TVR_SPF, sizeof(struct tvr_route));

    memset(route, 0, sizeof(*route));
    route->prefixlen = nlri->prefixlen;
    route->prefix = nlri->prefix;
    route->dist = TVR_INF_DIST;

    return route;
}

static void tvr_route_destroy(struct tvr_route **route) {
    XFREE(MTYPE_TVR_SPF, *route);
}

static struct pq_elem *pq_elem_create(struct tvr_node *node) {
    struct pq_elem *elem = XCALLOC(MTYPE_TVR_SPF, sizeof(struct pq_elem));

    memset(elem, 0, sizeof(*elem));
    elem->dist = node->dist;
    elem->local_node = node->local_node;

    return elem;
}

static void pq_elem_destroy(struct pq_elem **elem) {
    XFREE(MTYPE_TVR_SPF, *elem);
}

static uint8_t merge_spf_status(uint8_t a, uint8_t b) {
    if(a == TVR_UNREACH_STATUS || b == TVR_UNREACH_STATUS) {
        return TVR_UNREACH_STATUS;
    }
    if(a == TVR_NOTRANS_STATUS || b == TVR_NOTRANS_STATUS) {
        return TVR_NOTRANS_STATUS;
    }
    return TVR_DEFAULT_STATUS;
}

static void build_from_node_nlri(struct node_rb_head *node_set,
        struct nnlri_rb_head *nlri_set,
        uint64_t t1, uint64_t t2) {
    struct tvr_node_nlri *cur, *next, *last;
    struct tvr_node_nlri key;

    cur = nnlri_rb_first(nlri_set);
    while(cur != NULL) {
        memcpy(&key, cur, sizeof(struct tvr_node_nlri));
        key.time_stamp = UINT64_MAX;
        last = nnlri_rb_find_lt(nlri_set, &key);

        if(cur->time_stamp <= t1) {
            key.time_stamp = t1 + 1;
            cur = nnlri_rb_find_lt(nlri_set, &key);

            struct tvr_node *node = tvr_node_create(cur);
            node_rb_add(node_set, node);

            while(cur != last && node->spf_status != TVR_UNREACH_STATUS) {
                next = nnlri_rb_next_safe(nlri_set, cur);
                if(next->time_stamp <= t2) {
                    node->spf_status = merge_spf_status(
                        next->attr.spf_status, node->spf_status);
                } else {
                    break;
                }
                cur = next;
            }
        }

        cur = nnlri_rb_next_safe(nlri_set, last);                
    }
}

static void build_from_link_nlri(struct node_rb_head *node_set,
        struct lnlri_rb_head *nlri_set,
        uint64_t t1, uint64_t t2) {
    struct tvr_link_nlri *cur, *next, *last;
    struct tvr_link_nlri key;
    struct tvr_node *node;

    cur = lnlri_rb_first(nlri_set);
    while(cur != NULL) {
        memcpy(&key, cur, sizeof(struct tvr_link_nlri));
        key.time_stamp = UINT64_MAX;
        last = lnlri_rb_find_lt(nlri_set, &key);

        if(cur->time_stamp <= t1) {
            key.time_stamp = t1 + 1;
            cur = lnlri_rb_find_lt(nlri_set, &key);
            node = node_rb_find(node_set, 
                &(struct tvr_node) {
                    .local_node = cur->local_node
                }
            );
            if(node != NULL) {
                struct tvr_nlink *nlink = tvr_nlink_create(cur);
                listnode_add(node->links, nlink);
                
                while(cur != last && nlink->spf_status != TVR_UNREACH_STATUS) {
                    next = lnlri_rb_next_safe(nlri_set, cur);
                    if(next->time_stamp <= t2) {
                        nlink->spf_status = merge_spf_status(
                            next->attr.spf_status, nlink->spf_status);
                    } else {
                        break;
                    }
                    cur = next;
                }
            }
        }

        cur = lnlri_rb_next_safe(nlri_set, last);                
    }
}

static void build_from_prefix_nlri(struct node_rb_head *node_set,
        struct route_rb_head *route_set,
        struct pnlri_rb_head *nlri_set,
        uint64_t t1, uint64_t t2) {
    struct tvr_prefix_nlri *cur, *next, *last;
    struct tvr_prefix_nlri key;
    struct tvr_node *node;
    struct tvr_route *route;

    cur = pnlri_rb_first(nlri_set);
    while(cur != NULL) {
        memcpy(&key, cur, sizeof(struct tvr_prefix_nlri));
        key.time_stamp = UINT64_MAX;
        last = pnlri_rb_find_lt(nlri_set, &key);
        
        route = route_rb_find(route_set,
            &(struct tvr_route) {
            .prefixlen = cur->prefixlen,
            .prefix = cur->prefix
            }
        );
        if(route == NULL) {
            route = tvr_route_create(cur);
            route_rb_add(route_set, route);
        }

        if(cur->time_stamp <= t1) {
            key.time_stamp = t1 + 1;
            cur = pnlri_rb_find_lt(nlri_set, &key);
            node = node_rb_find(node_set, 
                &(struct tvr_node) {
                    .local_node = cur->local_node
                }
            );
            if(node != NULL) {
                struct tvr_nprefix *nprefix = tvr_nprefix_create(cur);
                listnode_add(node->prefixes, nprefix);
                
                while(cur != last && nprefix->spf_status != TVR_UNREACH_STATUS) {
                    next = pnlri_rb_next_safe(nlri_set, cur);
                    if(next->time_stamp <= t2) {
                        nprefix->spf_status = merge_spf_status(
                            next->attr.spf_status, nprefix->spf_status);
                    } else {
                        break;
                    }
                    cur = next;
                }
            }
        }

        cur = pnlri_rb_next_safe(nlri_set, last);                
    }
}

static void build(struct tvr_spf *spf, struct tvr_db *db, uint64_t t1, uint64_t t2) {
    build_from_node_nlri(&spf->node_rb_root, &db->nnlri_rb_root, t1, t2);
    build_from_link_nlri(&spf->node_rb_root, &db->lnlri_rb_root, t1, t2);
    build_from_prefix_nlri(&spf->node_rb_root, &spf->route_rb_root, &db->pnlri_rb_root, t1, t2);
}

static void dijkstra(struct tvr_spf *spf, uint32_t src_node) {
    struct node_rb_head *node_set = &spf->node_rb_root;
    struct route_rb_head *route_set = &spf->route_rb_root;
    struct pq_rb_head *pq = &spf->pq_rb_root;
    struct pq_elem *elem;
	struct listnode *listnode, *nestnode;
    struct tvr_node *node, *rnode;
    struct tvr_nprefix *nprefix;
    struct tvr_nlink *nlink, *rnlink;
    struct tvr_route *route;

    node = node_rb_find(node_set, 
        &(struct tvr_node) {
            .local_node = src_node
        }
    );
    if(node == NULL) {
        return;
    }

    node->dist = 0;
    node->next_hop = in6addr_any; // Set next hop to "any" for the source node
    pq_rb_add(pq, pq_elem_create(node));

    while(pq_rb_count(pq) > 0) {
        elem = pq_rb_first(pq);
        node = node_rb_find(node_set,
            &(struct tvr_node) {
                .local_node = elem->local_node
            }
        );
        pq_rb_del(pq, elem);
        pq_elem_destroy(&elem);

        if(node->visited) {
            continue;
        }
        node->visited = true;

        if(node->spf_status == TVR_UNREACH_STATUS) {
            continue;
        }
        for(ALL_LIST_ELEMENTS_RO(node->prefixes, listnode, nprefix)) {
            if(nprefix->spf_status == TVR_UNREACH_STATUS) {
                continue;
            }
            route = route_rb_find(route_set,
                &(struct tvr_route) {
                .prefixlen = nprefix->prefixlen,
                .prefix = nprefix->prefix
                }
            );
            if(route->dist > node->dist) {
                route->dist = node->dist;
                if (node -> local_node == src_node) {
                    route->next_hop = in6addr_loopback; // Set next hop to "any" for the source node
                    route -> ifindex = 1;
                } else {
                    route -> next_hop = node->next_hop;
                    route -> ifindex = node -> ifindex;
                }
            }
        }
        if(node->spf_status == TVR_NOTRANS_STATUS) {
            continue;
        }
        for(ALL_LIST_ELEMENTS_RO(node->links, listnode, nlink)) {
            if(nlink->spf_status == TVR_UNREACH_STATUS) {
                continue;
            }
            rnode = node_rb_find(node_set,
                &(struct tvr_node) {
                    .local_node = nlink->remote_node
                }
            );
            if(rnode == NULL) {
                continue;
            }

            bool bi_check = false;
            for(ALL_LIST_ELEMENTS_RO(rnode->links, nestnode, rnlink)) {
                if(rnlink->spf_status == TVR_UNREACH_STATUS) {
                    continue;
                }
                if(rnlink->remote_node == node->local_node) {
                    // if(memcmp(&rnlink->link_addr, &nlink->link_addr, 16) == 0) {
                        bi_check = true;
                    // }
                }
            }
            if(!bi_check) {
                continue;
            }

            if(node->dist + nlink->igp_metric < rnode->dist) {
                rnode->dist = node->dist + nlink->igp_metric;
                if(node->local_node == src_node) {
                    rnode->next_hop = nlink->link_addr;
                    rnode -> ifindex = nlink->ifindex;
                } else {
                    rnode->next_hop = node->next_hop;
                    rnode -> ifindex = node -> ifindex;
                }
                // rnode -> next_hop = node->next_hop;
                pq_rb_add(pq, pq_elem_create(rnode));
            }
        }
    }
}

struct tvr_spf *tvr_spf_create(struct tvr_db *db, uint32_t src_node,
        uint64_t time_stamp1, uint64_t time_stamp2) {
    struct tvr_spf *spf = XCALLOC(MTYPE_TVR_SPF, sizeof(struct tvr_spf));
    node_rb_init(&spf->node_rb_root);
    route_rb_init(&spf->route_rb_root);
    pq_rb_init(&spf->pq_rb_root);

    build(spf, db, time_stamp1, time_stamp2);
    
    dijkstra(spf, src_node);
    
    return spf;
}

void tvr_spf_destroy(struct tvr_spf **spf) {
    if(*spf == NULL) {
        return;
    }

    struct tvr_node *node;
    struct tvr_route *route;

	frr_each_safe(node_rb, &(*spf)->node_rb_root, node) {
        node_rb_del(&(*spf)->node_rb_root, node);
		tvr_node_destroy(&node);
    }
    frr_each_safe(route_rb, &(*spf)->route_rb_root, route) {
        route_rb_del(&(*spf)->route_rb_root, route);
        tvr_route_destroy(&route);
    }

    node_rb_fini(&(*spf)->node_rb_root);
    route_rb_fini(&(*spf)->route_rb_root);
    pq_rb_fini(&(*spf)->pq_rb_root);
    XFREE(MTYPE_TVR_SPF, *spf);
}

static struct in_addr node_id_to_ipv4(uint32_t node_id) {
    struct in_addr addr;
    addr.s_addr = htonl(node_id);
    return addr;
}

static void tvr_route_to_zapi(struct tvr_route *tvr_route, uint32_t next_hop_node, 
                              struct zapi_route *api, vrf_id_t vrf_id, uint8_t route_type) {
    memset(api, 0, sizeof(struct zapi_route));
    
    api->vrf_id = vrf_id;
    api->type = route_type;  // 例如 ZEBRA_ROUTE_STATIC 或自定义的路由类型
    api->safi = SAFI_UNICAST;
    api->instance = 0;
    
    // 设置目标前缀
    if (IN6_IS_ADDR_V4MAPPED(&tvr_route->prefix)) {
        // IPv4-mapped IPv6 address -> IPv4
        api->prefix.family = AF_INET;
        api->prefix.prefixlen = tvr_route->prefixlen - 96; // 减去IPv4映射前缀长度
        memcpy(&api->prefix.u.prefix4, &tvr_route->prefix.s6_addr[12], 4);
    } else {
        // 纯IPv6地址
        api->prefix.family = AF_INET6;
        api->prefix.prefixlen = tvr_route->prefixlen;
        api->prefix.u.prefix6 = tvr_route->prefix;
    }
    
    // 设置下一跳
    if (next_hop_node != 0) {
        api->nexthop_num = 1;
        struct zapi_nexthop *nexthop = &api->nexthops[0];
        
        nexthop->vrf_id = vrf_id;
        if (api->prefix.family == AF_INET) {
            nexthop->type = NEXTHOP_TYPE_IPV4;
            nexthop->gate.ipv4 = node_id_to_ipv4(next_hop_node);
        } else {
            nexthop->type = NEXTHOP_TYPE_IPV6;
            // 将node_id转换为IPv6地址（需要根据实际场景调整）
            memset(&nexthop->gate.ipv6, 0, 16);
            memcpy(&nexthop->gate.ipv6.s6_addr[12], &next_hop_node, 4);
        }
    }
    
    // 设置metric（距离）
    if (tvr_route->dist != TVR_INF_DIST) {
        SET_FLAG(api->message, ZAPI_MESSAGE_METRIC);
        api->metric = (uint32_t)tvr_route->dist;
    }
}

/* Helper function to convert in6_addr to zapi format */
static void tvr_route_v6_to_zapi(const struct prefix *prefix, const struct in6_addr *next_hop,
                                 struct zapi_route *api, vrf_id_t vrf_id, uint8_t route_type, const ifindex_t ifindex) {
    memset(api, 0, sizeof(*api));
    api->vrf_id = vrf_id;
    api->type = route_type;
    api->safi = SAFI_UNICAST;
    api->prefix = *prefix;
    
    // Create nexthop
    struct zapi_nexthop *nexthop = &api->nexthops[0];
    api->nexthop_num = 1;
    
    if (!IN6_IS_ADDR_UNSPECIFIED(next_hop)) {
        SET_FLAG(api->message, ZAPI_MESSAGE_NEXTHOP);
        nexthop->type = (prefix->family == AF_INET) ? NEXTHOP_TYPE_IPV4_IFINDEX : NEXTHOP_TYPE_IPV6_IFINDEX;
        
        if (prefix->family == AF_INET) {
            // Convert IPv6-mapped IPv4 to IPv4
            if (IN6_IS_ADDR_V4MAPPED(next_hop)) {
                memcpy(&nexthop->gate.ipv4, &next_hop->s6_addr[12], 4);
            }
        } else {
            nexthop->gate.ipv6 = *next_hop;
        }
        nexthop -> ifindex = ifindex;
    }
}

// int tvr_spf_install_routes(struct tvr_spf *spf, struct zclient *zclient, 
//                           vrf_id_t vrf_id, uint8_t route_type) {
//     if (spf == NULL || zclient == NULL) {
//         return -1;
//     }
    
//     struct tvr_route *route;
//     struct zapi_route api;
//     int installed = 0;
    
//     // 遍历所有计算出的路由
//     frr_each(route_rb, &spf->route_rb_root, route) {
//         // 跳过无法到达的路由
//         if (route->dist == TVR_INF_DIST) {
//             continue;
//         }
        
//         // 转换为zapi格式
//         struct prefix prefix;
//         prefix.family = AF_INET6;
//         prefix.prefixlen = route->prefixlen;
//         prefix.u.prefix6 = route->prefix;
        
//         tvr_route_v6_to_zapi(&prefix, &route->next_hop, &api, vrf_id, route_type);
        
//         // 发送路由添加请求到zebra
//         if (zclient_route_send(ZEBRA_ROUTE_ADD, zclient, &api) == ZCLIENT_SEND_SUCCESS) {
//             installed++;
//         }
//     }
    
//     return installed;
// }

int tvr_spf_uninstall_routes(struct tvr_spf *spf, struct zclient *zclient, 
                            vrf_id_t vrf_id, uint8_t route_type) {
    if (spf == NULL || zclient == NULL) {
        return -1;
    }
    
    struct tvr_route *route;
    struct zapi_route api;
    int uninstalled = 0;
    
    // 遍历所有路由进行删除
    frr_each(route_rb, &spf->route_rb_root, route) {
        // 跳过无法到达的路由
        if (route->dist == TVR_INF_DIST) {
            continue;
        }
        
        // 转换为zapi格式，但只需要前缀信息用于删除
        tvr_route_to_zapi(route, 0, &api, vrf_id, route_type);
        
        // 发送路由删除请求到zebra
        if (zclient_route_send(ZEBRA_ROUTE_DELETE, zclient, &api) == ZCLIENT_SEND_SUCCESS) {
            uninstalled++;
        }
    }
    
    return uninstalled;
}

static void tvr_single_route_to_zapi(const struct prefix *prefix, uint32_t next_hop_node,
                                     struct zapi_route *api, vrf_id_t vrf_id, uint8_t route_type) {
    memset(api, 0, sizeof(struct zapi_route));
    
    api->vrf_id = vrf_id;
    api->type = route_type;
    api->safi = SAFI_UNICAST;
    api->instance = 0;
    api->message =  ZAPI_MESSAGE_NEXTHOP;
    
    // 设置目标前缀
    api->prefix = *prefix;
    
    // 设置下一跳
    if (next_hop_node != 0) {
        api->nexthop_num = 1;
        struct zapi_nexthop *nexthop = &api->nexthops[0];
        
        nexthop->vrf_id = vrf_id;
        if (prefix->family == AF_INET) {
            nexthop->type = NEXTHOP_TYPE_IPV4;
            nexthop->gate.ipv4 = node_id_to_ipv4(next_hop_node);
        } else if (prefix->family == AF_INET6) {
            nexthop->type = NEXTHOP_TYPE_IPV6;
            // 将node_id转换为IPv6地址（根据实际场景调整）
            memset(&nexthop->gate.ipv6, 0, 16);
            memcpy(&nexthop->gate.ipv6.s6_addr[12], &next_hop_node, 4);
        }
    }
}

int tvr_spf_install_single_route(struct zclient *zclient, const struct prefix *prefix, 
                                 uint32_t next_hop_node, vrf_id_t vrf_id, 
                                 uint8_t route_type, uint32_t metric) {
    if (zclient == NULL || prefix == NULL) {
        return -1;
    }
    
    struct zapi_route api;
    
    // 转换为zapi格式
    tvr_single_route_to_zapi(prefix, next_hop_node, &api, vrf_id, route_type);
    
    // 设置metric（如果提供）
    if (metric != 0) {
        SET_FLAG(api.message, ZAPI_MESSAGE_METRIC);
        api.metric = metric;
    }
    
    // 发送路由添加请求到zebra
    if (zclient_route_send(ZEBRA_ROUTE_ADD, zclient, &api) == ZCLIENT_SEND_SUCCESS) {
        return 1;
    }
    
    return 0;
}

int tvr_spf_uninstall_single_route(struct zclient *zclient, const struct prefix *prefix,
                                   vrf_id_t vrf_id, uint8_t route_type) {
    if (zclient == NULL || prefix == NULL) {
        return -1;
    }
    
    struct zapi_route api;
    
    // 转换为zapi格式，删除时只需要前缀信息
    tvr_single_route_to_zapi(prefix, 0, &api, vrf_id, route_type);
    
    // 发送路由删除请求到zebra
    if (zclient_route_send(ZEBRA_ROUTE_DELETE, zclient, &api) == ZCLIENT_SEND_SUCCESS) {
        return 1;
    }
    
    return 0;
}

/* 便利函数：从字符串创建prefix并安装路由 */
int tvr_spf_install_route_from_string(struct zclient *zclient, const char *prefix_str,
                                     uint32_t next_hop_node, vrf_id_t vrf_id,
                                     uint8_t route_type, uint32_t metric) {
    struct prefix prefix;
    
    if (str2prefix(prefix_str, &prefix) == 0) {
        return -1; // 前缀格式错误
    }
    
    return tvr_spf_install_single_route(zclient, &prefix, next_hop_node, 
                                       vrf_id, route_type, metric);
}

/* 便利函数：从字符串创建prefix并卸载路由 */
int tvr_spf_uninstall_route_from_string(struct zclient *zclient, const char *prefix_str,
                                        vrf_id_t vrf_id, uint8_t route_type) {
    struct prefix prefix;
    
    if (str2prefix(prefix_str, &prefix) == 0) {
        return -1; // 前缀格式错误
    }
    
    return tvr_spf_uninstall_single_route(zclient, &prefix, vrf_id, route_type);
}

int tvr_spf_install_single_route_v6(struct zclient *zclient, const struct prefix *prefix, 
                                    const struct in6_addr *next_hop, vrf_id_t vrf_id, 
                                    uint8_t route_type, uint32_t metric, const ifindex_t ifindex) {
    if (zclient == NULL || prefix == NULL || next_hop == NULL) {
        return -1;
    }
    
    struct zapi_route api;
    
    // 转换为zapi格式
    tvr_route_v6_to_zapi(prefix, next_hop, &api, vrf_id, route_type, ifindex);
    
    // 设置metric（如果提供）
    if (metric != 0) {
        SET_FLAG(api.message, ZAPI_MESSAGE_METRIC);
        api.metric = metric;
    }
    
    // 发送路由添加请求到zebra
    if (zclient_route_send(ZEBRA_ROUTE_ADD, zclient, &api) == ZCLIENT_SEND_SUCCESS) {
        return 1;
    }
    
    return 0;
}