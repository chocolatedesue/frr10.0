# TVR SPF 路由安装功能使用指南

## 功能概述

修改后的 `sharp_tvr_spf` 命令现在支持第四个参数来控制是否将计算出的路由安装到内核中。该功能使用了我们之前实现的单条路由安装函数，实现了最小化的改动。

## 命令语法

```bash
tvr spf <src_node> <time_stamp1> <time_stamp2> [is_install_route]
```

### 参数说明

- `src_node`: 源节点ID (0-1000000000)
- `time_stamp1`: 开始时间戳 (0-1000000000)  
- `time_stamp2`: 结束时间戳 (0-1000000000)
- `is_install_route`: 是否安装路由到内核 (0=否, 1=是) - **可选参数**

## 使用示例

### 1. 仅计算路由（不安装）

```bash
# 方法1: 不提供第四个参数
tvr spf 1000 0 1000000000

# 方法2: 明确指定不安装
tvr spf 1000 0 1000000000 0
```

输出示例：
```
Running SPF from node 1000 with time stamps 0 to 1000000000
Route 2001:db8:1::/48 is reachable with distance 100, nexthop 192.168.1.1
Route 2001:db8:2::/48 is reachable with distance 200, nexthop 192.168.1.2
Route 2001:db8:3::/48 is unreachable
```

### 2. 计算并安装路由到内核

```bash
tvr spf 1000 0 1000000000 1
```

输出示例：
```
Running SPF from node 1000 with time stamps 0 to 1000000000 (installing routes to kernel)
✓ Installed route 2001:db8:1::/48 with distance 100, nexthop 192.168.1.1
✓ Installed route 2001:db8:2::/48 with distance 200, nexthop 192.168.1.2
✓ Uninstalled unreachable route 2001:db8:3::/48

Route Installation Summary:
  Successfully installed: 2 routes
```

## 功能特性

### ✅ 已实现的功能

1. **智能路由处理**
   - 可达路由：自动安装到内核
   - 不可达路由：自动从内核卸载（如果之前安装过）

2. **详细状态反馈**
   - ✓ 成功安装的路由显示绿色勾号
   - ✗ 安装失败的路由显示红色叉号
   - 显示安装统计信息

3. **错误处理**
   - 检查数据库是否存在
   - 检查zebra客户端连接状态
   - 优雅处理安装失败的情况

4. **向后兼容**
   - 第四个参数是可选的
   - 不提供时默认为仅计算模式

### 🔧 技术实现

- 使用 `tvr_spf_install_single_route()` 函数逐条安装路由
- 使用 `tvr_spf_uninstall_single_route()` 函数处理不可达路由
- 使用BGP路由类型 (`ZEBRA_ROUTE_BGP`) 
- 支持IPv6地址格式
- 自动将距离作为metric传递

## 使用场景

### 1. 开发调试
```bash
# 先测试计算是否正确
tvr spf 1000 0 1000000000

# 确认无误后安装到内核
tvr spf 1000 0 1000000000 1
```

### 2. 网络运维
```bash
# 更新网络拓扑后重新计算并安装路由
tvr spf 2000 1000000 2000000 1
```

### 3. 故障排除
```bash
# 计算特定时间段的路由
tvr spf 1000 500000 600000

# 验证路由是否正确安装
tvr spf 1000 500000 600000 1
```

## 监控和验证

### 查看安装的路由

```bash
# Linux系统命令
ip -6 route show

# FRR命令行
show ipv6 route
show ipv6 route bgp
```

### 检查路由状态

```bash
# 检查特定前缀
ip -6 route get 2001:db8:1::1

# 查看路由统计
show ip route summary
```

## 注意事项

1. **权限要求**: 需要足够的系统权限来修改路由表
2. **zebra连接**: 确保zebra守护进程正在运行且连接正常
3. **路由冲突**: 注意与其他路由协议的冲突
4. **性能影响**: 大量路由安装可能影响系统性能
5. **恢复机制**: 记录安装的路由以便后续清理

## 故障排除

### 常见问题

1. **"zebra client not available"**
   - 检查zebra进程是否运行
   - 检查BGP与zebra的连接

2. **路由安装失败**
   - 检查系统权限
   - 验证前缀格式正确性
   - 确认下一跳地址可达

3. **Database does not exist**
   - 先使用 `tvrdb create` 创建数据库
   - 添加必要的NLRI数据

### 调试命令

```bash
# 创建数据库
tvrdb create

# 查看数据库内容
tvrdb show

# 添加测试数据
tvrdb add node_nlri 1000 0 0 1
tvrdb add prefix_nlri 1000 2001:db8:1::/48 0 0 1
```

## 最小改动总结

此实现通过最小的代码修改实现了路由安装功能：

1. **保持原有接口**: 命令格式不变，仅增加可选参数
2. **复用现有函数**: 使用已实现的单条路由安装函数
3. **增强用户体验**: 添加详细的状态反馈和统计信息
4. **确保向后兼容**: 不影响现有的计算功能

这种实现方式既满足了路由安装的需求，又保持了代码的简洁性和可维护性。
