/*
 * TVR Database CRUD Operations Example Header
 */

#ifndef _TVR_DB_CRUD_EXAMPLE_H_
#define _TVR_DB_CRUD_EXAMPLE_H_

#include "tvr_db.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CREATE OPERATIONS */
extern bool tvr_db_add_node(struct tvr_db *db, uint64_t node_id, 
                            uint8_t spf_status, uint64_t seq_num);

extern bool tvr_db_add_link(struct tvr_db *db, uint64_t local_node, uint64_t remote_node,
                            const char *link_addr_str, uint32_t igp_metric, 
                            uint8_t spf_status, uint64_t seq_num);

extern bool tvr_db_add_prefix(struct tvr_db *db, uint64_t local_node, 
                             const char *prefix_str, uint8_t prefixlen,
                             uint8_t spf_status, uint64_t seq_num);

/* READ OPERATIONS */
extern struct tvr_node_nlri *tvr_db_find_node(struct tvr_db *db, uint64_t node_id, 
                                              uint64_t time_stamp);

extern struct tvr_node_nlri *tvr_db_find_latest_node(struct tvr_db *db, uint64_t node_id);

extern void tvr_db_find_node_links(struct tvr_db *db, uint64_t local_node,
                                  void (*callback)(struct tvr_link_nlri *link, void *arg),
                                  void *callback_arg);

extern void tvr_db_find_links_in_time_range(struct tvr_db *db, uint64_t local_node,
                                           uint64_t remote_node, const char *link_addr_str,
                                           uint64_t start_time, uint64_t end_time,
                                           void (*callback)(struct tvr_link_nlri *link, void *arg),
                                           void *callback_arg);

/* UPDATE OPERATIONS */
extern bool tvr_db_update_node(struct tvr_db *db, uint64_t node_id, 
                              uint8_t new_spf_status, uint64_t new_seq_num);

extern bool tvr_db_batch_update_links(struct tvr_db *db, 
                                     struct tvr_link_nlri *links, 
                                     size_t count);

/* DELETE OPERATIONS */
extern bool tvr_db_delete_node(struct tvr_db *db, uint64_t node_id, uint64_t time_stamp);

extern size_t tvr_db_delete_all_node_nlris(struct tvr_db *db, uint64_t node_id);

extern size_t tvr_db_cleanup_old_records(struct tvr_db *db, uint64_t cutoff_time);

/* UTILITY FUNCTIONS */
extern void tvr_db_get_statistics(struct tvr_db *db, 
                                 size_t *node_count, 
                                 size_t *link_count, 
                                 size_t *prefix_count);

extern bool tvr_db_is_empty(struct tvr_db *db);

/* EXAMPLE FUNCTION */
extern void tvr_db_crud_example(void);

#ifdef __cplusplus
}
#endif

#endif /* _TVR_DB_CRUD_EXAMPLE_H_ */
