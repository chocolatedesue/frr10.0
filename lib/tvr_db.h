/*
 * Time Variant Routing Database definition - tvr_db.h
 *
 * Author: Yuxuan Chen <chenyuxuan@cnic.cn>
 *
 * This file is part of Free Range Routing (FRR).
 */


#ifndef _FRR_TVR_DB_H_
#define _FRR_TVR_DB_H_

#include "typesafe.h"
#include "prefix.h"
#include "vty.h"

/* Enable shared memory TVR database implementation */
// #ifndef USE_SHARED_TVR_DB
// #define USE_SHARED_TVR_DB 1
// #endif

#ifdef __cplusplus
extern "C" {
#endif

// #if USE_SHARED_TVR_DB
// /* Forward declarations for shared memory implementation */
// extern struct tvr_db *tvr_db_get_shared_instance(void);
// extern bool tvr_db_shared_instance_exists(void);
// extern void tvr_db_release_shared_instance(void);
// extern bool tvr_db_register_process(const char *process_name);
// extern bool tvr_db_unregister_process(void);
// #endif

DEFINE_MTYPE_STATIC(LIB, TVR_DB, "Time Variant Routing Database");

PREDECL_RBTREE_UNIQ(nnlri_rb);

struct tvr_node_nlri {
    uint64_t local_node;
    uint64_t time_stamp;

    struct {
        uint8_t spf_status;
        uint64_t seq_num;
    } attr;

    struct nnlri_rb_item entry;
};

macro_inline int node_nlri_cmp(const struct tvr_node_nlri *lhs,
			    const struct tvr_node_nlri *rhs)
{
    if(lhs->local_node != rhs->local_node) {
    	return numcmp(lhs->local_node, rhs->local_node);
    }
    if (lhs->attr.seq_num != rhs->attr.seq_num) {
        return numcmp(lhs->attr.seq_num, rhs->attr.seq_num);
    }
    return numcmp(lhs->time_stamp, rhs->time_stamp);
}

DECLARE_RBTREE_UNIQ(nnlri_rb, struct tvr_node_nlri, entry, node_nlri_cmp);

PREDECL_RBTREE_UNIQ(lnlri_rb);

struct tvr_link_nlri {
    uint64_t local_node;
    uint64_t remote_node;
    struct in6_addr link_addr;
    uint64_t time_stamp;

    struct {
        uint32_t igp_metric;
        uint8_t spf_status;
        uint64_t seq_num;
    } attr;
    
    struct lnlri_rb_item entry;
};

macro_inline int link_nlri_cmp(const struct tvr_link_nlri *lhs,
			  const struct tvr_link_nlri *rhs)
{
    if(lhs->local_node != rhs->local_node) {
        return numcmp(lhs->local_node, rhs->local_node);
    }
    if(lhs->remote_node != rhs->remote_node) {
        return numcmp(lhs->remote_node, rhs->remote_node);
    }

    int i = memcmp(&lhs->link_addr, &rhs->link_addr, 16);
    if(i) {
        return i;
    }

    if (lhs -> attr.seq_num != rhs -> attr.seq_num) {
        return numcmp(lhs->attr.seq_num, rhs->attr.seq_num);
    }

    return numcmp(lhs->time_stamp, rhs->time_stamp);
}

DECLARE_RBTREE_UNIQ(lnlri_rb, struct tvr_link_nlri, entry, link_nlri_cmp);

PREDECL_RBTREE_UNIQ(pnlri_rb);

struct tvr_prefix_nlri {
    uint64_t local_node;
    uint8_t prefixlen;
    struct in6_addr prefix;
    uint64_t time_stamp;

    struct {
        uint8_t spf_status;
        uint64_t seq_num;
    } attr;

    struct pnlri_rb_item entry;
};

macro_inline int prefix_nlri_cmp(const struct tvr_prefix_nlri *lhs,
			    const struct tvr_prefix_nlri *rhs)
{
    if(lhs->local_node != rhs->local_node) {
        return numcmp(lhs->local_node, rhs->local_node);
    }
	if (lhs->prefixlen != rhs->prefixlen) {
		return numcmp(lhs->prefixlen, rhs->prefixlen);
    }
    int i = memcmp(&lhs->prefix, &rhs->prefix, 16);
    if(i) {
        return i;
    }
    return numcmp(lhs->time_stamp, rhs->time_stamp);
}

DECLARE_RBTREE_UNIQ(pnlri_rb, struct tvr_prefix_nlri, entry, prefix_nlri_cmp);

struct tvr_db {
	struct nnlri_rb_head nnlri_rb_root;	
	struct lnlri_rb_head lnlri_rb_root;
	struct pnlri_rb_head pnlri_rb_root;
};

enum tvr_nlri_type {
    NODE = 1,
    LINK = 2,
    PREFIX = 4
};

struct tvr_nlri {
    enum tvr_nlri_type type;
    union
    {
        struct tvr_node_nlri node_nlri;
        struct tvr_link_nlri link_nlri;
        struct tvr_prefix_nlri prefix_nlri;
    } u;
};



extern struct tvr_db *tvr_db_create(void);

/* Singleton pattern functions */
extern struct tvr_db *tvr_db_get_instance(void);

extern bool tvr_db_instance_exists(void);

extern void tvr_db_destroy_instance(void);

extern struct tvr_db *tvr_db_reset_instance(void);

extern void tvr_db_destroy(struct tvr_db **db);

extern size_t tvr_db_aging(struct tvr_db *db, uint64_t time_stamp);

extern bool tvr_db_process(struct tvr_db *db, struct tvr_nlri *nlri, bool delete);

extern void tvr_db_show(struct tvr_db *db, struct vty *vty);

#ifdef __cplusplus
}
#endif

#endif /* _FRR_TVR_DB_H_ */