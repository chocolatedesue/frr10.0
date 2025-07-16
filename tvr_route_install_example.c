/*
 * TVR SPF Route Installation Example
 * 
 * This example demonstrates how to use TVR SPF to compute routes
 * and install them into the kernel via zebra.
 */

#include "tvr_spf.h"
#include "zclient.h"
#include "vrf.h"

/* 示例：使用TVR SPF计算路由并安装到内核 */
int tvr_install_route_table_example(struct tvr_db *db, uint32_t src_node, 
                                   uint64_t t1, uint64_t t2, 
                                   struct zclient *zclient) {
    /* 1. 创建SPF实例并计算路由 */
    struct tvr_spf *spf = tvr_spf_create(db, src_node, t1, t2);
    if (spf == NULL) {
        return -1;
    }
    
    /* 2. 将计算出的路由安装到内核 */
    int installed = tvr_spf_install_routes(spf, zclient, VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    
    if (installed > 0) {
        printf("Successfully installed %d routes to kernel\n", installed);
    } else {
        printf("Failed to install routes\n");
    }
    
    /* 3. 清理资源 */
    tvr_spf_destroy(&spf);
    
    return installed;
}

/* 示例：卸载之前安装的路由 */
int tvr_uninstall_route_table_example(struct tvr_db *db, uint32_t src_node,
                                     uint64_t t1, uint64_t t2,
                                     struct zclient *zclient) {
    /* 1. 重新创建相同的SPF实例 */
    struct tvr_spf *spf = tvr_spf_create(db, src_node, t1, t2);
    if (spf == NULL) {
        return -1;
    }
    
    /* 2. 卸载路由 */
    int uninstalled = tvr_spf_uninstall_routes(spf, zclient, VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    
    if (uninstalled > 0) {
        printf("Successfully uninstalled %d routes from kernel\n", uninstalled);
    } else {
        printf("Failed to uninstall routes\n");
    }
    
    /* 3. 清理资源 */
    tvr_spf_destroy(&spf);
    
    return uninstalled;
}

/* 示例：遍历路由表并打印prefix和nexthop信息 */
void tvr_print_routing_table_example(struct tvr_spf *spf) {
    struct tvr_route *route;
    char prefix_str[INET6_ADDRSTRLEN];
    
    printf("TVR Routing Table:\n");
    printf("%-40s %-15s %-10s\n", "Prefix", "Next Hop", "Distance");
    printf("%-40s %-15s %-10s\n", "------", "--------", "--------");
    
    frr_each(route_rb, &spf->route_rb_root, route) {
        if (route->dist == TVR_INF_DIST) {
            continue; // 跳过不可达路由
        }
        
        // 格式化前缀
        inet_ntop(AF_INET6, &route->prefix, prefix_str, sizeof(prefix_str));
        
        printf("%-40s %-15u %-10llu\n", 
               prefix_str, 
               route->next_hop, 
               (unsigned long long)route->dist);
    }
}

/* 示例：安装单条路由 */
int tvr_install_single_route_example(struct zclient *zclient, 
                                    const char *prefix_str, 
                                    uint32_t next_hop_node) {
    printf("Installing single route: %s via node %u\n", prefix_str, next_hop_node);
    
    int result = tvr_spf_install_route_from_string(zclient, prefix_str, next_hop_node,
                                                  VRF_DEFAULT, ZEBRA_ROUTE_STATIC, 100);
    
    if (result > 0) {
        printf("Successfully installed route %s\n", prefix_str);
    } else if (result == 0) {
        printf("Failed to install route %s\n", prefix_str);
    } else {
        printf("Invalid prefix format: %s\n", prefix_str);
    }
    
    return result;
}

/* 示例：卸载单条路由 */
int tvr_uninstall_single_route_example(struct zclient *zclient, 
                                      const char *prefix_str) {
    printf("Uninstalling single route: %s\n", prefix_str);
    
    int result = tvr_spf_uninstall_route_from_string(zclient, prefix_str,
                                                    VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    
    if (result > 0) {
        printf("Successfully uninstalled route %s\n", prefix_str);
    } else if (result == 0) {
        printf("Failed to uninstall route %s\n", prefix_str);
    } else {
        printf("Invalid prefix format: %s\n", prefix_str);
    }
    
    return result;
}

/* 示例：使用prefix结构安装路由 */
int tvr_install_route_with_prefix_example(struct zclient *zclient,
                                         struct prefix *prefix,
                                         uint32_t next_hop_node,
                                         uint32_t metric) {
    char prefix_str[PREFIX_STRLEN];
    prefix2str(prefix, prefix_str, sizeof(prefix_str));
    
    printf("Installing route with prefix struct: %s via node %u (metric %u)\n", 
           prefix_str, next_hop_node, metric);
    
    int result = tvr_spf_install_single_route(zclient, prefix, next_hop_node,
                                             VRF_DEFAULT, ZEBRA_ROUTE_STATIC, metric);
    
    if (result > 0) {
        printf("Successfully installed route %s\n", prefix_str);
    } else {
        printf("Failed to install route %s\n", prefix_str);
    }
    
    return result;
}

/* 示例：批量操作单条路由 */
int tvr_batch_single_routes_example(struct zclient *zclient) {
    // 定义要安装的路由
    struct {
        const char *prefix;
        uint32_t next_hop;
        uint32_t metric;
    } routes[] = {
        {"192.168.1.0/24", 1001, 10},
        {"10.0.0.0/8", 1002, 20},
        {"2001:db8::/32", 1003, 30},
        {NULL, 0, 0} // 结束标记
    };
    
    int installed = 0;
    
    printf("Installing multiple single routes:\n");
    
    // 批量安装路由
    for (int i = 0; routes[i].prefix != NULL; i++) {
        int result = tvr_spf_install_route_from_string(zclient, routes[i].prefix,
                                                      routes[i].next_hop, VRF_DEFAULT,
                                                      ZEBRA_ROUTE_STATIC, routes[i].metric);
        if (result > 0) {
            printf("  ✓ Installed: %s via %u (metric %u)\n", 
                   routes[i].prefix, routes[i].next_hop, routes[i].metric);
            installed++;
        } else {
            printf("  ✗ Failed: %s\n", routes[i].prefix);
        }
    }
    
    printf("Successfully installed %d out of %d routes\n", installed, 
           (int)(sizeof(routes)/sizeof(routes[0]) - 1));
    
    return installed;
}

/* 示例：清理批量安装的路由 */
int tvr_cleanup_batch_routes_example(struct zclient *zclient) {
    const char *routes[] = {
        "192.168.1.0/24",
        "10.0.0.0/8", 
        "2001:db8::/32",
        NULL // 结束标记
    };
    
    int uninstalled = 0;
    
    printf("Cleaning up batch routes:\n");
    
    // 批量卸载路由
    for (int i = 0; routes[i] != NULL; i++) {
        int result = tvr_spf_uninstall_route_from_string(zclient, routes[i],
                                                        VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
        if (result > 0) {
            printf("  ✓ Uninstalled: %s\n", routes[i]);
            uninstalled++;
        } else {
            printf("  ✗ Failed to uninstall: %s\n", routes[i]);
        }
    }
    
    printf("Successfully uninstalled %d routes\n", uninstalled);
    
    return uninstalled;
}
