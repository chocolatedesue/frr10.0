# TVR SPF 路由安装指南

## 概述

TVR SPF (Time Variant Routing Shortest Path First) 提供了将计算出的路由表安装到Linux内核的功能。通过与zebra守护进程交互，可以实现最小化的路由下表，只包含prefix和nexthop信息。

## 核心函数

### 1. 批量路由安装函数

```c
int tvr_spf_install_routes(struct tvr_spf *spf, struct zclient *zclient, 
                          vrf_id_t vrf_id, uint8_t route_type);
```

**功能**: 将SPF计算出的路由表安装到内核

**参数**:
- `spf`: TVR SPF实例
- `zclient`: zebra客户端连接
- `vrf_id`: VRF ID (通常使用 `VRF_DEFAULT`)
- `route_type`: 路由类型 (例如 `ZEBRA_ROUTE_STATIC`)

**返回值**: 成功安装的路由数量，失败返回-1

### 2. 批量路由卸载函数

```c
int tvr_spf_uninstall_routes(struct tvr_spf *spf, struct zclient *zclient, 
                            vrf_id_t vrf_id, uint8_t route_type);
```

**功能**: 从内核卸载之前安装的路由

**参数**: 与安装函数相同

**返回值**: 成功卸载的路由数量，失败返回-1

### 3. 单条路由安装函数

```c
int tvr_spf_install_single_route(struct zclient *zclient, const struct prefix *prefix,
                                uint32_t next_hop_node, vrf_id_t vrf_id,
                                uint8_t route_type, uint32_t metric);
```

**功能**: 安装单条路由到内核

**参数**:
- `zclient`: zebra客户端连接
- `prefix`: 目标前缀
- `next_hop_node`: 下一跳节点ID
- `vrf_id`: VRF ID
- `route_type`: 路由类型
- `metric`: 路由距离/代价

**返回值**: 成功返回1，失败返回0，参数错误返回-1

### 4. 单条路由卸载函数

```c
int tvr_spf_uninstall_single_route(struct zclient *zclient, const struct prefix *prefix,
                                  vrf_id_t vrf_id, uint8_t route_type);
```

**功能**: 从内核卸载单条路由

**参数**:
- `zclient`: zebra客户端连接
- `prefix`: 目标前缀
- `vrf_id`: VRF ID
- `route_type`: 路由类型

**返回值**: 成功返回1，失败返回0，参数错误返回-1

### 5. 字符串格式路由安装函数

```c
int tvr_spf_install_route_from_string(struct zclient *zclient, const char *prefix_str,
                                     uint32_t next_hop_node, vrf_id_t vrf_id,
                                     uint8_t route_type, uint32_t metric);
```

**功能**: 使用字符串格式的前缀安装路由

**参数**:
- `prefix_str`: 前缀字符串 (如 "192.168.1.0/24", "2001:db8::/32")
- 其他参数与单条路由安装函数相同

### 6. 字符串格式路由卸载函数

```c
int tvr_spf_uninstall_route_from_string(struct zclient *zclient, const char *prefix_str,
                                       vrf_id_t vrf_id, uint8_t route_type);
```

**功能**: 使用字符串格式的前缀卸载路由

## 使用步骤

### 批量路由操作

### 步骤1: 初始化zebra客户端

```c
#include "zclient.h"
#include "vrf.h"

struct zclient *zclient;

// 初始化zebra客户端
zclient = zclient_new(master, &zclient_options_default, NULL, 0);
zclient_init(zclient, ZEBRA_ROUTE_STATIC, 0, &zebra_privs);
```

### 步骤2: 计算SPF路由

```c
#include "tvr_spf.h"

// 创建SPF实例并计算路由
struct tvr_spf *spf = tvr_spf_create(db, src_node, time_stamp1, time_stamp2);
```

### 步骤3: 安装路由到内核

```c
// 安装路由
int installed = tvr_spf_install_routes(spf, zclient, VRF_DEFAULT, ZEBRA_ROUTE_STATIC);

if (installed > 0) {
    printf("成功安装 %d 条路由到内核\\n", installed);
} else {
    printf("路由安装失败\\n");
}
```

### 步骤4: 清理资源

```c
// 卸载路由 (可选)
tvr_spf_uninstall_routes(spf, zclient, VRF_DEFAULT, ZEBRA_ROUTE_STATIC);

// 销毁SPF实例
tvr_spf_destroy(&spf);

// 清理zebra客户端
zclient_stop(zclient);
zclient_free(zclient);
```

## 单条路由操作

### 安装单条路由

```c
// 方法1: 使用字符串格式前缀
int result = tvr_spf_install_route_from_string(zclient, "192.168.1.0/24", 
                                              1001, VRF_DEFAULT, 
                                              ZEBRA_ROUTE_STATIC, 100);

// 方法2: 使用prefix结构
struct prefix prefix;
str2prefix("10.0.0.0/8", &prefix);
int result = tvr_spf_install_single_route(zclient, &prefix, 1002, 
                                         VRF_DEFAULT, ZEBRA_ROUTE_STATIC, 50);
```

### 卸载单条路由

```c
// 方法1: 使用字符串格式前缀
int result = tvr_spf_uninstall_route_from_string(zclient, "192.168.1.0/24",
                                                VRF_DEFAULT, ZEBRA_ROUTE_STATIC);

// 方法2: 使用prefix结构
struct prefix prefix;
str2prefix("10.0.0.0/8", &prefix);
int result = tvr_spf_uninstall_single_route(zclient, &prefix, 
                                           VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
```

### 批量单条路由操作

```c
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

// 批量安装
for (int i = 0; routes[i].prefix != NULL; i++) {
    int result = tvr_spf_install_route_from_string(zclient, routes[i].prefix,
                                                  routes[i].next_hop, VRF_DEFAULT,
                                                  ZEBRA_ROUTE_STATIC, routes[i].metric);
    if (result > 0) {
        printf("安装成功: %s\\n", routes[i].prefix);
    }
}
```

## 路由表格式

安装到内核的路由包含以下信息:

- **前缀 (Prefix)**: IPv4或IPv6网络前缀
- **前缀长度 (Prefix Length)**: 网络掩码长度
- **下一跳 (Next Hop)**: 下一跳节点ID (转换为IP地址)
- **距离 (Metric)**: 路由距离/代价

## 支持的地址类型

1. **IPv4**: 支持标准IPv4地址和前缀
2. **IPv6**: 支持纯IPv6地址
3. **IPv4-mapped IPv6**: 自动转换为IPv4格式

## 示例代码

### 完整使用示例

#### 批量路由安装示例

```c
#include "tvr_spf.h"
#include "zclient.h"
#include "vrf.h"

int install_tvr_routes(struct tvr_db *db, uint32_t src_node) {
    struct zclient *zclient;
    struct tvr_spf *spf;
    int result;
    
    // 1. 初始化zebra客户端
    zclient = zclient_new(master, &zclient_options_default, NULL, 0);
    if (zclient_socket_connect(zclient) < 0) {
        return -1;
    }
    
    // 2. 计算SPF路由
    spf = tvr_spf_create(db, src_node, 0, UINT64_MAX);
    if (spf == NULL) {
        zclient_free(zclient);
        return -1;
    }
    
    // 3. 安装路由到内核
    result = tvr_spf_install_routes(spf, zclient, VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    
    // 4. 清理资源
    tvr_spf_destroy(&spf);
    zclient_stop(zclient);
    zclient_free(zclient);
    
    return result;
}
```

#### 单条路由操作示例

```c
#include "tvr_spf.h"
#include "zclient.h"
#include "vrf.h"

// 安装单条IPv4路由
int install_ipv4_route_example(struct zclient *zclient) {
    return tvr_spf_install_route_from_string(zclient, "192.168.100.0/24", 
                                            1001, VRF_DEFAULT, 
                                            ZEBRA_ROUTE_STATIC, 50);
}

// 安装单条IPv6路由
int install_ipv6_route_example(struct zclient *zclient) {
    return tvr_spf_install_route_from_string(zclient, "2001:db8:100::/48",
                                            1002, VRF_DEFAULT,
                                            ZEBRA_ROUTE_STATIC, 60);
}

// 卸载单条路由
int uninstall_route_example(struct zclient *zclient) {
    int result1 = tvr_spf_uninstall_route_from_string(zclient, "192.168.100.0/24",
                                                     VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    int result2 = tvr_spf_uninstall_route_from_string(zclient, "2001:db8:100::/48",
                                                     VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
    return result1 + result2;
}

// 动态路由管理示例
int dynamic_route_management_example(struct zclient *zclient) {
    // 定义路由配置
    struct route_config {
        const char *prefix;
        uint32_t next_hop;
        uint32_t metric;
        bool active;
    } routes[] = {
        {"10.1.0.0/16", 1001, 10, true},
        {"10.2.0.0/16", 1002, 20, false},  // 暂不激活
        {"172.16.0.0/12", 1003, 30, true},
        {"2001:db8:1::/48", 1004, 40, true},
        {NULL, 0, 0, false}
    };
    
    int operations = 0;
    
    // 根据配置安装/卸载路由
    for (int i = 0; routes[i].prefix != NULL; i++) {
        if (routes[i].active) {
            // 安装路由
            int result = tvr_spf_install_route_from_string(zclient, routes[i].prefix,
                                                          routes[i].next_hop, VRF_DEFAULT,
                                                          ZEBRA_ROUTE_STATIC, routes[i].metric);
            if (result > 0) {
                printf("已安装路由: %s -> %u (metric: %u)\\n", 
                       routes[i].prefix, routes[i].next_hop, routes[i].metric);
                operations++;
            }
        } else {
            // 确保路由已卸载
            tvr_spf_uninstall_route_from_string(zclient, routes[i].prefix,
                                               VRF_DEFAULT, ZEBRA_ROUTE_STATIC);
        }
    }
    
    return operations;
}
```

### 路由表查看示例

```c
void print_routing_table(struct tvr_spf *spf) {
    struct tvr_route *route;
    char prefix_str[INET6_ADDRSTRLEN];
    
    printf("TVR 路由表:\\n");
    printf("%-40s %-15s %-10s\\n", "目标前缀", "下一跳", "距离");
    
    frr_each(route_rb, &spf->route_rb_root, route) {
        if (route->dist == TVR_INF_DIST) {
            continue; // 跳过不可达路由
        }
        
        inet_ntop(AF_INET6, &route->prefix, prefix_str, sizeof(prefix_str));
        printf("%-40s %-15u %-10llu\\n", 
               prefix_str, route->next_hop, route->dist);
    }
}
```

## 注意事项

1. **权限要求**: 需要适当的系统权限来修改路由表
2. **VRF支持**: 支持多VRF环境
3. **错误处理**: 始终检查函数返回值
4. **资源清理**: 使用后及时清理SPF实例和zebra客户端
5. **重复安装**: 重复安装相同路由会更新现有路由

## 调试和监控

### 查看安装的路由

```bash
# 查看IPv4路由表
ip route show

# 查看IPv6路由表  
ip -6 route show

# 查看特定VRF的路由
ip route show vrf <vrf_name>
```

### FRR命令查看

```bash
# 进入FRR shell
vtysh

# 查看路由表
show ip route
show ipv6 route

# 查看zebra状态
show zebra
```

## 常见问题

### Q: 路由安装失败怎么办？
A: 检查zebra连接状态、权限设置和路由格式是否正确。

### Q: 如何处理路由冲突？
A: 使用适当的路由类型和metric值来控制路由优先级。

### Q: 支持哪些路由类型？
A: 支持所有zebra定义的路由类型，推荐使用 `ZEBRA_ROUTE_STATIC`。

### Q: 如何实现路由更新？
A: 先卸载旧路由，再安装新路由，或直接安装新路由覆盖旧路由。

### Q: 单条路由和批量路由的区别？
A: 
- **批量路由**: 基于SPF计算结果，一次性安装/卸载整个路由表
- **单条路由**: 针对特定前缀和下一跳的精确控制，适合动态路由管理

### Q: 如何选择使用字符串还是prefix结构？
A: 
- **字符串格式**: 适合简单的静态配置，如 `"192.168.1.0/24"`
- **prefix结构**: 适合动态计算的前缀，性能更好

### Q: 支持哪些前缀格式？
A: 
- IPv4: `"192.168.1.0/24"`, `"10.0.0.0/8"`
- IPv6: `"2001:db8::/32"`, `"fe80::/64"`
- 主机路由: `"192.168.1.1/32"`, `"2001:db8::1/128"`

### Q: 如何处理下一跳节点ID到IP地址的转换？
A: 当前实现将节点ID作为IPv4地址的最后32位，IPv6地址的最后32位。可以根据实际网络拓扑调整转换逻辑。

### Q: 如何实现路由的条件安装？
A: 使用动态路由管理模式，根据网络状态或配置条件决定是否安装特定路由。

### Q: 支持多VRF环境吗？
A: 是的，所有函数都支持指定VRF ID，可以在不同VRF中安装路由。

### Q: 如何监控路由安装状态？
A: 检查函数返回值，使用系统命令 `ip route show` 或FRR命令 `show ip route` 验证。
