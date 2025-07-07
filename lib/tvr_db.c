/*
 * Time Variant Routing Database - tvr_db.c
 *
 * Author: Yuxuan Chen <chenyuxuan@cnic.cn>
 *
 * This file is part of Free Range Routing (FRR).
 */

#include "tvr_db.h"
#include <netinet/in.h>
#include <sys/socket.h>
#include "memory.h"

// #if USE_SHARED_TVR_DB
// #include "tvr_db_shared.h"
// #endif



#define DEFINE_TVR_CREATE(prefix, type)			\
static type * prefix##_create(void) {			\
	type * x;									\
	x = XCALLOC(MTYPE_TVR_DB, sizeof(type));	\
	memset(x, 0, sizeof(type));					\
	return x;									\
}

#define DEFINE_TVR_DELETE(prefix, type)			\
static void prefix##_delete(type **x) {			\
	if(*x == NULL) {							\
		return;									\
	}											\
	XFREE(MTYPE_TVR_DB, *x);					\
}

DEFINE_TVR_CREATE(node_nlri, struct tvr_node_nlri)
DEFINE_TVR_DELETE(node_nlri, struct tvr_node_nlri)
DEFINE_TVR_CREATE(link_nlri, struct tvr_link_nlri)
DEFINE_TVR_DELETE(link_nlri, struct tvr_link_nlri)
DEFINE_TVR_CREATE(prefix_nlri, struct tvr_prefix_nlri)
DEFINE_TVR_DELETE(prefix_nlri, struct tvr_prefix_nlri)

#define DEFINE_TVR_PROCESS(type, rb) 							\
static bool process_##type(struct tvr_db *db, 					\
		struct tvr_##type *nlri, bool delete) { 				\
	struct rb##_head *root = &db->rb##_root;					\
	struct tvr_##type *rb_entry; 								\
	bool success; 												\
	rb_entry = rb##_find(root, nlri); 							\
	if(delete) { 												\
		if(rb_entry == NULL) { 									\
			success = false; 									\
		} else { 												\
			rb##_del(root, rb_entry); 							\
			type##_delete(&rb_entry);							\
			success = true; 									\
		} 														\
	} else { 													\
		if(rb_entry == NULL) { 									\
			success = true; 									\
		} else { 												\
			if(rb_entry->attr.seq_num < nlri->attr.seq_num) {	\
				rb##_del(root, rb_entry); 						\
				type##_delete(&rb_entry);						\
				success = true;		 							\
			} 													\
			else { 												\
				success = false; 								\
			} 													\
		} 														\
		if(success) { 											\
			rb_entry = type##_create(); 						\
			memcpy(rb_entry, nlri,								\
				sizeof(struct tvr_##type)); 					\
			rb##_add(root, rb_entry); 							\
		} 														\
	} 															\
	return success; 											\
}

DEFINE_TVR_PROCESS(node_nlri, nnlri_rb)
DEFINE_TVR_PROCESS(link_nlri, lnlri_rb)
DEFINE_TVR_PROCESS(prefix_nlri, pnlri_rb)

#define DEFINE_TVR_AGING(type, rb)								\
static size_t type##_aging(struct tvr_db *db,					\
		uint64_t time_stamp) {									\
	struct rb##_head *root = &db->rb##_root;					\
	struct tvr_##type *cur, *next, *last;						\
	struct tvr_##type key;										\
	size_t aged_nlri_cnt = 0;									\
	cur = rb##_first(root);										\
	while(cur != NULL) {										\
		memcpy(&key, cur, sizeof(struct tvr_##type));			\
		key.time_stamp = UINT64_MAX;							\
		last = rb##_find_lt(root, &key);						\
		while(cur != last) {									\
			next = rb##_next_safe(root, cur);					\
			if(next->time_stamp <= time_stamp) {				\
				rb##_del(root, cur);							\
				type##_delete(&cur);							\
				aged_nlri_cnt++;								\
				cur = next;										\
			} else {											\
				break;											\
			}													\
		}														\
		cur = rb##_next_safe(root, last);						\
	}															\
	return aged_nlri_cnt;										\
}

DEFINE_TVR_AGING(node_nlri, nnlri_rb)
DEFINE_TVR_AGING(link_nlri, lnlri_rb)
DEFINE_TVR_AGING(prefix_nlri, pnlri_rb)

/* Global singleton instance */
static struct tvr_db *g_tvr_db_instance = NULL;

/**
 * Get the singleton instance of TVR database (now uses shared memory)
 * Creates the instance if it doesn't exist
 * 
 * @return Pointer to the singleton TVR database instance
 */
struct tvr_db *tvr_db_get_instance(void) {
#if USE_SHARED_TVR_DB
	/* Use shared memory implementation for true cross-process singleton */
	return tvr_db_get_shared_instance();
#else
	/* Use traditional per-process singleton */
	if (g_tvr_db_instance == NULL) {
		g_tvr_db_instance = tvr_db_create();
	}
	return g_tvr_db_instance;
#endif
}

/**
 * Check if singleton instance exists
 * 
 * @return true if instance exists, false otherwise
 */
bool tvr_db_instance_exists(void) {
	return (g_tvr_db_instance != NULL);
}

/**
 * Destroy the singleton instance
 * Sets the global pointer to NULL after destruction
 */
void tvr_db_destroy_instance(void) {
	if (g_tvr_db_instance != NULL) {
		tvr_db_destroy(&g_tvr_db_instance);
		g_tvr_db_instance = NULL;
	}
}

/**
 * Reset the singleton instance
 * Destroys existing instance and creates a new one
 * 
 * @return Pointer to the new singleton instance
 */
struct tvr_db *tvr_db_reset_instance(void) {
	tvr_db_destroy_instance();
	return tvr_db_get_instance();
}

struct tvr_db *tvr_db_create(void) {
	struct tvr_db *db;

	db = XCALLOC(MTYPE_TVR_DB, sizeof(struct tvr_db));
	db->rwlock = (pthread_rwlock_t)PTHREAD_RWLOCK_INITIALIZER;
	nnlri_rb_init(&db->nnlri_rb_root);
	lnlri_rb_init(&db->lnlri_rb_root);
	pnlri_rb_init(&db->pnlri_rb_root);

	return db;
}

void tvr_db_destroy(struct tvr_db **db) {
	struct tvr_node_nlri *node_nlri;
	struct tvr_link_nlri *link_nlri;
	struct tvr_prefix_nlri *prefix_nlri;
	
	if(*db == NULL) {
		return;
	}

	frr_each_safe(nnlri_rb, &(*db)->nnlri_rb_root, node_nlri) {
		nnlri_rb_del(&(*db)->nnlri_rb_root, node_nlri);
		node_nlri_delete(&node_nlri);
	}
	frr_each_safe(lnlri_rb, &(*db)->lnlri_rb_root, link_nlri) {
		lnlri_rb_del(&(*db)->lnlri_rb_root, link_nlri);
		link_nlri_delete(&link_nlri);
	}
	frr_each_safe(pnlri_rb, &(*db)->pnlri_rb_root, prefix_nlri) {
		pnlri_rb_del(&(*db)->pnlri_rb_root, prefix_nlri);
		prefix_nlri_delete(&prefix_nlri);
	}
	
	nnlri_rb_fini(&(*db)->nnlri_rb_root);
	lnlri_rb_fini(&(*db)->lnlri_rb_root);
	pnlri_rb_fini(&(*db)->pnlri_rb_root);

	XFREE(MTYPE_TVR_DB, *db);
}

size_t tvr_db_aging(struct tvr_db *db, uint64_t time_stamp) {
	size_t aged_nlri_cnt = 0;
	aged_nlri_cnt += node_nlri_aging(db, time_stamp);
	aged_nlri_cnt += link_nlri_aging(db, time_stamp);
	aged_nlri_cnt += prefix_nlri_aging(db, time_stamp);
	return aged_nlri_cnt;
} 

bool tvr_db_find_nlri(struct tvr_db *db, struct tvr_nlri *nlri)
{
	bool found = false;
	switch (nlri->type) {
	case NODE:
		found = nnlri_rb_find(&db->nnlri_rb_root, &nlri->u.node_nlri);
		break;
	case LINK:
		found = lnlri_rb_find(&db->lnlri_rb_root, &nlri->u.link_nlri);
		break;
	case PREFIX:
		found = pnlri_rb_find(&db->pnlri_rb_root, &nlri->u.prefix_nlri);
		break;
	default:
		zlog_warn("Unsupported NLRI Type!");
		break;
	}
	return found;
}


bool tvr_db_process(struct tvr_db *db, struct tvr_nlri *nlri, bool delete)
{
	bool success;
	pthread_rwlock_wrlock(&db -> rwlock);

	switch (nlri->type) {
	case NODE:
		success = process_node_nlri(db, &nlri->u.node_nlri, delete);
		break;
	case LINK:
		success = process_link_nlri(db, &nlri->u.link_nlri, delete);
		break;
	case PREFIX:
		success = process_prefix_nlri(db, &nlri->u.prefix_nlri, delete);
		break;
	default:
		success = false;
		zlog_warn("Unsupported NLRI Type!");
		break;
	}
	pthread_rwlock_unlock(&db -> rwlock);

	return success;
}

static void tvr_show_node_nlri(struct tvr_db *db, struct vty *vty) {
	struct tvr_node_nlri *nlri;
	bool is_first = true;

	frr_each_safe(nnlri_rb, &db->nnlri_rb_root, nlri) {
		if(is_first) {
	 		vty_out(vty,
			"\n"
		    "             [%12s,%12s],%12s,%12s,%12s\n",
			"Local Node",
			"Time Stamp",
			"SPF Status",
			"SEQ Number", "Local IP");
			is_first = false;
		}

		char local_node_ip_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &nlri->local_node, local_node_ip_str, INET_ADDRSTRLEN);

		vty_out(vty,
			"  Node NLRI: [%12u,%12llu],%12u,%12llu,%12s\n",
			nlri->local_node,
			nlri->time_stamp,
			nlri->attr.spf_status,
			nlri->attr.seq_num,
			local_node_ip_str
		);
	}
}

static void tvr_show_link_nlri(struct tvr_db *db, struct vty *vty) {
	struct tvr_link_nlri *nlri;
	char link_addr_str[PREFIX_STRLEN];
	bool is_first = true;

	frr_each_safe(lnlri_rb, &db->lnlri_rb_root, nlri) {
		inet_ntop(AF_INET6, &nlri->link_addr, link_addr_str, PREFIX_STRLEN);
		
		if(is_first) {
 			vty_out(vty,
			"\n"
		    "             [%12s,%12s,%25s,%12s],%12s,%12s,%12s,%12s,%12s\n",
			"Local Node",
			"Remote Node",
			"Link Address",
			"Time Stamp",
			"IGP Metric",
			"SPF Status",
			"SEQ Number",
			"Local IP",
			"Remote IP"
		);
			is_first = false;
		}

		char local_node_ip_str[INET_ADDRSTRLEN], remote_node_ip_str[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &nlri->local_node, local_node_ip_str, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, &nlri->remote_node, remote_node_ip_str, INET_ADDRSTRLEN);

		vty_out(vty,
			"  Link NLRI: [%12u,%12u,%25s,%12llu],%12u,%12u,%12llu,%12s,%12s\n",
			nlri->local_node,
			nlri->remote_node,
			link_addr_str,
			nlri->time_stamp,
			nlri->attr.igp_metric,
			nlri->attr.spf_status,
			nlri->attr.seq_num,
			local_node_ip_str,
			remote_node_ip_str
		);
	}
}

static void tvr_show_prefix_nlri(struct tvr_db *db, struct vty *vty) {
	struct tvr_prefix_nlri *nlri;
	char str[PREFIX_STRLEN];
	struct prefix pref;
	union prefixconstptr ptr;
	bool is_first = true;

	pref.family = AF_INET6;
	ptr.p = &pref;

	frr_each_safe(pnlri_rb, &db->pnlri_rb_root, nlri) {
		if(is_first) {
 			vty_out(vty,
			"\n"
		    "             [%12s,%12s,%12s],%12s,%12s\n",
			"Local Node",
			"IPv6 Prefix",
			"Time Stamp",
			"SPF Status",
			"SEQ Number");
		 is_first = false;
		}

		pref.prefixlen = nlri->prefixlen;
		pref.u.prefix6 = nlri->prefix;
		prefix2str(ptr, str, PREFIX_STRLEN);
		
		vty_out(vty,
			"Prefix NLRI: [%12llu,%12s,%12llu],%12u,%12llu\n",
			nlri->local_node,
			str,
			nlri->time_stamp,
			nlri->attr.spf_status,
			nlri->attr.seq_num
		);
	}
}

void tvr_db_show(struct tvr_db *db, struct vty *vty) {
	vty_out(vty,"\n\tTime Variant Routing Database:\n\n");
	tvr_show_node_nlri(db, vty);
	tvr_show_link_nlri(db, vty);
	tvr_show_prefix_nlri(db, vty);
	
	vty_out(vty,
		"\n\tTotal: %zu node NLRI(s), %zu link NLRI(s), %zu prefix NLRI(s)\n\n",
		nnlri_rb_count(&db->nnlri_rb_root),
		lnlri_rb_count(&db->lnlri_rb_root),
		pnlri_rb_count(&db->pnlri_rb_root)
	);
}


bool tvr_db_assign_node_nlri(struct tvr_node_nlri* nlri, uint64_t local_node,
                    uint64_t time_stamp, uint8_t spf_status,
                    uint64_t seq_num) 
{
	if (nlri == NULL) {
		return false;
	}
	
	nlri->local_node = local_node;
	nlri->time_stamp = time_stamp;
	nlri->attr.spf_status = spf_status;
	nlri->attr.seq_num = seq_num;

	return true;							
}

bool tvr_db_assign_link_nlri(struct tvr_link_nlri* nlri, uint64_t local_node,
					uint64_t remote_node, struct in6_addr link_addr,
					uint64_t time_stamp, uint32_t igp_metric,
					uint8_t spf_status, uint64_t seq_num, ifindex_t ifindex) 
{
	if (nlri == NULL) {
		return false;
	}
	
	nlri->local_node = local_node;
	nlri->remote_node = remote_node;
	nlri->link_addr = link_addr;
	nlri->time_stamp = time_stamp;
	nlri->attr.igp_metric = igp_metric;
	nlri->attr.spf_status = spf_status;
	nlri->attr.seq_num = seq_num;
	nlri->ifindex = ifindex;

	return true;							
}