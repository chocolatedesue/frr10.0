# FRR TVR (Time Variant Routing) 接口和使用方法总结

## 概述

TVR (Time Variant Routing) 是FRR中新增的时变路由模块，支持基于时间的网络拓扑变化和路由计算。该模块主要包含两个核心组件：
- **TVR Database (tvr_db)**: 时变路由数据库，存储带时间戳的网络信息
- **TVR SPF (tvr_spf)**: 时变最短路径优先算法，基于时间区间计算路由

## 1. TVR Database 模块

### 1.1 核心数据结构

#### Node NLRI (网络节点信息)
```c
struct tvr_node_nlri {
    uint64_t local_node;        // 本地节点ID
    uint64_t time_stamp;        // 时间戳
    struct {
        uint8_t spf_status;     // SPF状态 (0=默认, 1=不可达, 2=不中转)
        uint64_t seq_num;       // 序列号
    } attr;
};
```

#### Link NLRI (链路信息)
```c
struct tvr_link_nlri {
    uint64_t local_node;        // 本地节点ID
    uint64_t remote_node;       // 远端节点ID
    struct in6_addr link_addr;  // 链路IPv6地址
    uint64_t time_stamp;        // 时间戳
    struct {
        uint32_t igp_metric;    // IGP度量值
        uint8_t spf_status;     // SPF状态
        uint64_t seq_num;       // 序列号
    } attr;
};
```

#### Prefix NLRI (前缀信息)
```c
struct tvr_prefix_nlri {
    uint64_t local_node;        // 本地节点ID
    uint8_t prefixlen;          // 前缀长度
    struct in6_addr prefix;     // IPv6前缀
    uint64_t time_stamp;        // 时间戳
    struct {
        uint8_t spf_status;     // SPF状态
        uint64_t seq_num;       // 序列号
    } attr;
};
```

#### 统一NLRI结构
```c
enum tvr_nlri_type {
    NODE = 1,       // 节点信息
    LINK = 2,       // 链路信息
    PREFIX = 4      // 前缀信息
};

struct tvr_nlri {
    enum tvr_nlri_type type;
    union {
        struct tvr_node_nlri node_nlri;
        struct tvr_link_nlri link_nlri;
        struct tvr_prefix_nlri prefix_nlri;
    } u;
};
```

### 1.2 TVR Database API接口

#### 1.2.1 数据库管理接口

```c
/* 创建TVR数据库 */
struct tvr_db *tvr_db_create(void);

/* 销毁TVR数据库 */
void tvr_db_destroy(struct tvr_db **db);
```

**使用示例：**
```c
// 创建数据库
struct tvr_db *db = tvr_db_create();

// 使用完毕后销毁
tvr_db_destroy(&db);
```

#### 1.2.2 数据处理接口

```c
/* 处理NLRI (增加/更新或删除) */
bool tvr_db_process(struct tvr_db *db, struct tvr_nlri *nlri, bool delete);

/* 老化处理，删除指定时间戳之前的过期数据 */
size_t tvr_db_aging(struct tvr_db *db, uint64_t time_stamp);
```

**使用示例：**
```c
// 添加节点信息
struct tvr_nlri nlri = {
    .type = NODE,
    .u.node_nlri = {
        .local_node = 1001,
        .time_stamp = 1234567890,
        .attr = {
            .spf_status = 0,    // 默认状态
            .seq_num = 1
        }
    }
};
bool success = tvr_db_process(db, &nlri, false);  // false表示添加/更新

// 删除节点信息
success = tvr_db_process(db, &nlri, true);  // true表示删除

// 老化处理，删除时间戳小于current_time的数据
uint64_t current_time = 1234567900;
size_t aged_count = tvr_db_aging(db, current_time);
```

#### 1.2.3 数据显示接口

```c
/* 显示数据库内容到VTY */
void tvr_db_show(struct tvr_db *db, struct vty *vty);
```

**使用示例：**
```c
// 在CLI命令中显示TVR数据库内容
DEFUN(show_tvr_database, show_tvr_database_cmd,
      "show tvr database",
      SHOW_STR
      "Time Variant Routing\n"
      "Database information\n")
{
    struct tvr_db *db = get_tvr_database();
    tvr_db_show(db, vty);
    return CMD_SUCCESS;
}
```

### 1.3 数据库使用流程

```c
// 1. 创建数据库
struct tvr_db *db = tvr_db_create();

// 2. 添加节点信息
struct tvr_nlri node_nlri = {
    .type = NODE,
    .u.node_nlri = {
        .local_node = 1001,
        .time_stamp = get_current_time(),
        .attr = { .spf_status = 0, .seq_num = 1 }
    }
};
tvr_db_process(db, &node_nlri, false);

// 3. 添加链路信息
struct tvr_nlri link_nlri = {
    .type = LINK,
    .u.link_nlri = {
        .local_node = 1001,
        .remote_node = 1002,
        .link_addr = get_link_address(),
        .time_stamp = get_current_time(),
        .attr = { .igp_metric = 100, .spf_status = 0, .seq_num = 1 }
    }
};
tvr_db_process(db, &link_nlri, false);

// 4. 添加前缀信息
struct tvr_nlri prefix_nlri = {
    .type = PREFIX,
    .u.prefix_nlri = {
        .local_node = 1001,
        .prefixlen = 64,
        .prefix = get_ipv6_prefix(),
        .time_stamp = get_current_time(),
        .attr = { .spf_status = 0, .seq_num = 1 }
    }
};
tvr_db_process(db, &prefix_nlri, false);

// 5. 定期老化处理
tvr_db_aging(db, get_current_time() - AGING_THRESHOLD);
```

## 2. TVR SPF 模块

### 2.1 核心数据结构

#### SPF计算结果
```c
struct tvr_spf {
    struct pq_rb_head pq_rb_root;       // 优先队列(内部使用)
    struct node_rb_head node_rb_root;   // 节点集合
    struct route_rb_head route_rb_root; // 路由表
};
```

#### 节点信息（SPF计算中的）
```c
struct tvr_node {
    uint64_t local_node;            // 节点ID
    uint8_t spf_status;             // SPF状态
    bool visited;                   // 是否已访问
    uint64_t dist;                  // 到源节点的距离
    struct in6_addr next_hop;       // 下一跳地址
    struct list *prefixes;          // 该节点的前缀列表
    struct list *links;             // 该节点的链路列表
};
```

#### 路由条目
```c
struct tvr_route {
    uint8_t prefixlen;              // 前缀长度
    struct in6_addr prefix;         // IPv6前缀
    uint64_t dist;                  // 到该前缀的距离
    struct in6_addr next_hop;       // 下一跳地址
};
```

### 2.2 TVR SPF API接口

```c
/* 创建SPF计算实例，基于指定时间区间 */
struct tvr_spf *tvr_spf_create(struct tvr_db *db, 
                               uint64_t src_node,
                               uint64_t time_stamp1, 
                               uint64_t time_stamp2);

/* 销毁SPF计算实例 */
void tvr_spf_destroy(struct tvr_spf **spf);
```

### 2.3 SPF状态定义

```c
#define TVR_DEFAULT_STATUS  0   // 默认状态，正常转发
#define TVR_UNREACH_STATUS  1   // 不可达状态，无法使用
#define TVR_NOTRANS_STATUS  2   // 不中转状态，可到达但不转发
```

### 2.4 SPF使用示例

```c
/* 基本SPF计算示例 */
void example_spf_calculation(struct tvr_db *db)
{
    uint64_t src_node = 1001;           // 源节点
    uint64_t start_time = 1234567890;   // 时间区间开始
    uint64_t end_time = 1234567900;     // 时间区间结束
    
    // 创建SPF计算实例
    struct tvr_spf *spf = tvr_spf_create(db, src_node, start_time, end_time);
    
    // 访问计算结果
    struct tvr_route *route;
    frr_each(route_rb, &spf->route_rb_root, route) {
        if (route->dist < TVR_INF_DIST) {
            printf("Route to prefix %s, distance: %llu, next_hop: %s\n",
                   prefix_to_string(&route->prefix, route->prefixlen),
                   route->dist,
                   ipv6_to_string(&route->next_hop));
        }
    }
    
    // 销毁SPF实例
    tvr_spf_destroy(&spf);
}
```

### 2.5 时间区间路由计算

TVR SPF的核心特性是基于时间区间的路由计算：

```c
/* 计算指定时间区间内的最优路由 */
void calculate_time_based_routes(struct tvr_db *db, uint64_t src_node)
{
    // 场景1: 计算当前时刻的路由
    uint64_t now = get_current_time();
    struct tvr_spf *current_spf = tvr_spf_create(db, src_node, now, now);
    
    // 场景2: 计算未来10秒内的稳定路由
    uint64_t future = now + 10;
    struct tvr_spf *future_spf = tvr_spf_create(db, src_node, now, future);
    
    // 场景3: 计算过去1分钟到现在的路由变化
    uint64_t past = now - 60;
    struct tvr_spf *historical_spf = tvr_spf_create(db, src_node, past, now);
    
    // 比较不同时间区间的路由差异
    compare_spf_results(current_spf, future_spf, historical_spf);
    
    // 清理资源
    tvr_spf_destroy(&current_spf);
    tvr_spf_destroy(&future_spf);
    tvr_spf_destroy(&historical_spf);
}
```

## 3. 完整使用流程示例

### 3.1 初始化和配置

```c
#include "lib/tvr_db.h"
#include "lib/tvr_spf.h"

/* 全局TVR数据库实例 */
static struct tvr_db *global_tvr_db = NULL;

/* 初始化TVR模块 */
void tvr_module_init(void)
{
    global_tvr_db = tvr_db_create();
    if (!global_tvr_db) {
        zlog_err("Failed to create TVR database");
        return;
    }
    zlog_info("TVR module initialized successfully");
}

/* 清理TVR模块 */
void tvr_module_cleanup(void)
{
    if (global_tvr_db) {
        tvr_db_destroy(&global_tvr_db);
        zlog_info("TVR module cleaned up");
    }
}
```

### 3.2 接收和处理网络更新

```c
/* 处理收到的TVR NLRI更新 */
void process_tvr_update(enum tvr_nlri_type type, void *nlri_data, bool is_withdraw)
{
    struct tvr_nlri nlri;
    nlri.type = type;
    
    switch (type) {
    case NODE:
        nlri.u.node_nlri = *(struct tvr_node_nlri *)nlri_data;
        break;
    case LINK:
        nlri.u.link_nlri = *(struct tvr_link_nlri *)nlri_data;
        break;
    case PREFIX:
        nlri.u.prefix_nlri = *(struct tvr_prefix_nlri *)nlri_data;
        break;
    default:
        zlog_warn("Unknown TVR NLRI type: %d", type);
        return;
    }
    
    bool success = tvr_db_process(global_tvr_db, &nlri, is_withdraw);
    if (success) {
        zlog_debug("TVR NLRI %s processed successfully", 
                   is_withdraw ? "withdrawal" : "update");
        
        // 触发路由重计算
        trigger_spf_calculation();
    } else {
        zlog_warn("Failed to process TVR NLRI %s", 
                  is_withdraw ? "withdrawal" : "update");
    }
}
```

### 3.3 路由计算和更新

```c
/* 触发SPF计算 */
void trigger_spf_calculation(void)
{
    uint64_t src_node = get_local_node_id();
    uint64_t current_time = get_current_time();
    uint64_t future_time = current_time + get_route_lifetime();
    
    // 执行SPF计算
    struct tvr_spf *spf = tvr_spf_create(global_tvr_db, src_node, 
                                         current_time, future_time);
    if (!spf) {
        zlog_err("Failed to create TVR SPF instance");
        return;
    }
    
    // 安装路由到系统路由表
    install_tvr_routes(spf);
    
    // 清理SPF实例
    tvr_spf_destroy(&spf);
}

/* 安装TVR路由到系统 */
void install_tvr_routes(struct tvr_spf *spf)
{
    struct tvr_route *route;
    
    frr_each(route_rb, &spf->route_rb_root, route) {
        if (route->dist >= TVR_INF_DIST) {
            continue;  // 跳过不可达路由
        }
        
        // 构造路由条目
        struct prefix_ipv6 p;
        p.family = AF_INET6;
        p.prefixlen = route->prefixlen;
        p.prefix = route->prefix;
        
        // 安装到系统路由表
        install_route_to_kernel(&p, &route->next_hop, route->dist);
        
        zlog_debug("Installed TVR route: %s via %s, metric %llu",
                   prefix_to_string(&p), 
                   ipv6_to_string(&route->next_hop),
                   route->dist);
    }
}
```

### 3.4 定期维护

```c
/* 定期维护任务 */
void tvr_periodic_maintenance(struct thread *thread)
{
    uint64_t current_time = get_current_time();
    uint64_t aging_threshold = current_time - get_aging_interval();
    
    // 执行老化处理
    size_t aged_count = tvr_db_aging(global_tvr_db, aging_threshold);
    if (aged_count > 0) {
        zlog_info("Aged %zu TVR NLRI entries", aged_count);
        
        // 如果有数据被老化，重新计算路由
        trigger_spf_calculation();
    }
    
    // 重新调度下次维护
    thread_add_timer(master, tvr_periodic_maintenance, NULL, 
                     get_maintenance_interval(), NULL);
}
```

## 4. 命令行接口示例

```c
/* 显示TVR数据库 */
DEFUN(show_tvr_database, show_tvr_database_cmd,
      "show tvr database",
      SHOW_STR
      "Time Variant Routing\n"
      "Database information\n")
{
    if (!global_tvr_db) {
        vty_out(vty, "TVR database not initialized\n");
        return CMD_WARNING;
    }
    
    tvr_db_show(global_tvr_db, vty);
    return CMD_SUCCESS;
}

/* 执行SPF计算 */
DEFUN(tvr_spf_calculate, tvr_spf_calculate_cmd,
      "tvr spf calculate <1-4294967295> <1-4294967295> <1-4294967295>",
      "Time Variant Routing\n"
      "Shortest Path First\n"
      "Calculate routes\n"
      "Source node ID\n"
      "Start timestamp\n"
      "End timestamp\n")
{
    uint64_t src_node = strtoull(argv[3]->arg, NULL, 10);
    uint64_t start_time = strtoull(argv[4]->arg, NULL, 10);
    uint64_t end_time = strtoull(argv[5]->arg, NULL, 10);
    
    struct tvr_spf *spf = tvr_spf_create(global_tvr_db, src_node, 
                                         start_time, end_time);
    if (!spf) {
        vty_out(vty, "Failed to create SPF instance\n");
        return CMD_WARNING;
    }
    
    vty_out(vty, "SPF calculation completed for node %llu, time [%llu, %llu]\n",
            src_node, start_time, end_time);
    
    // 显示路由结果
    show_spf_routes(spf, vty);
    
    tvr_spf_destroy(&spf);
    return CMD_SUCCESS;
}
```

## 5. 注意事项和最佳实践

### 5.1 内存管理
- 使用完TVR数据库和SPF实例后及时调用相应的destroy函数
- TVR模块使用FRR的MTYPE内存管理系统，支持内存泄漏检测

### 5.2 时间同步
- 确保所有节点的时间戳同步，建议使用NTP
- 时间戳使用64位整数，支持足够的精度和范围

### 5.3 性能考虑
- 定期执行老化处理，避免数据库无限增长
- SPF计算复杂度为O((V+E)logV)，大网络中注意性能影响
- 考虑使用时间区间来限制SPF计算的数据范围

### 5.4 状态管理
- 正确设置SPF状态：DEFAULT(正常)、UNREACH(不可达)、NOTRANS(不中转)
- 使用序列号确保数据的一致性和顺序

这个TVR模块为FRR提供了强大的时变路由能力，特别适用于卫星网络、间歇性连接网络等场景。
