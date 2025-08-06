# FRR容器网络仿真优化方案

## 概述

本方案专为100-1000规模的容器网络仿真环境设计，提供完整的内核调优和性能优化解决方案。

## 文件说明

### 主机系统优化
- **`opt.sh`**: 主机系统内核参数优化脚本
- 优化网络缓冲区、连接跟踪、邻居表等关键参数
- 适配中规模容器仿真需求

### 容器内部优化
- **`frr_container_optimize.sh`**: 容器内部内核优化脚本
- **仅进行内核调优，不修改FRR配置文件**
- 优化网络协议栈、进程限制、系统参数

### 容器管理
- **`run_frr_container.sh`**: 优化的容器启动脚本
- **`generate_frr_configs.sh`**: FRR配置文件生成器

## 使用流程

### 1. 主机系统优化

```bash
# 运行主机优化脚本
sudo ./opt.sh

# 重启系统使所有参数生效
sudo reboot
```

### 2. 启动容器仿真环境

```bash
# 启动容器 (默认10个，可修改CONTAINER_COUNT)
./run_frr_container.sh

# 等待容器启动完成
sleep 30
```

### 3. 验证优化效果

```bash
# 监控容器状态
./monitor_containers.sh

# 进入容器验证内核优化
docker exec -it frr-router-1 bash

# 在容器内运行验证
verify  # 或 /usr/local/bin/verify_optimization.sh
```

### 4. 配置FRR (可选)

```bash
# 生成FRR配置文件
./generate_frr_configs.sh -t ring -p bgp -n 10

# 部署配置到容器
cd frr_configs_ring_bgp_10nodes
./deploy_configs.sh
```

## 容器内优化工具

容器内自动创建以下监控和诊断工具：

### 快速命令 (别名)
```bash
monitor    # 系统性能监控
netdiag    # 网络诊断
verify     # 优化验证
```

### 完整路径
```bash
/usr/local/bin/container_monitor.sh     # 系统监控
/usr/local/bin/network_diag.sh          # 网络诊断  
/usr/local/bin/verify_optimization.sh   # 优化验证
```

## 优化参数说明

### 主机系统优化 (中规模)
```bash
# 网络连接
连接跟踪表: 1M 条目
接收缓冲区: 32MB
发送缓冲区: 32MB
网络队列: 8000
邻居表: 32K 条目

# 进程资源
文件句柄: 512K
进程数: 64K
```

### 容器内优化
```bash
# 进程限制
文件句柄: 65536
进程数: 8192

# 网络优化
接收缓冲区: 256KB
发送缓冲区: 256KB
TCP FIN超时: 30秒
TCP保活时间: 600秒
```

## 性能预期

### 收敛时间
- **BGP收敛**: 5-10秒 (原30-60秒)
- **OSPF收敛**: 1-3秒 (原10-30秒)
- **故障检测**: 50-200ms (BFD)

### 资源使用
- **内存**: 每容器300-500MB
- **CPU**: 每容器0.5-1.0核心
- **延迟**: 容器间<1ms

### 扩展性
- **支持容器数**: 100-1000个
- **BGP邻居数**: 每容器最多100个
- **路由条目**: 每容器最多10K条

## 故障排除

### 常见问题

1. **权限不足**
   ```bash
   # 确保容器有足够权限
   --cap-add NET_ADMIN --cap-add SYS_ADMIN
   ```

2. **参数设置失败**
   ```bash
   # 检查容器内参数状态
   docker exec container_name verify
   ```

3. **网络连通性问题**
   ```bash
   # 网络诊断
   docker exec container_name netdiag
   ```

### 监控命令

```bash
# 主机监控
./monitor_containers.sh

# 容器内监控
docker exec frr-router-1 monitor

# 性能测试
./performance_test.sh
```

## 清理环境

```bash
# 停止并删除所有容器
./cleanup_containers.sh
```

## 注意事项

1. **容器权限**: 需要NET_ADMIN权限进行网络参数调优
2. **内核版本**: 建议使用Linux 4.9+内核
3. **内存要求**: 主机至少8GB内存用于中规模仿真
4. **配置分离**: 内核优化与FRR配置分离，便于灵活管理

## 自定义调优

### 修改容器规模
```bash
# 编辑 run_frr_container.sh
CONTAINER_COUNT=50  # 修改为所需数量
```

### 调整资源限制
```bash
# 编辑 run_frr_container.sh
MEMORY_LIMIT="1g"   # 增加内存限制
CPU_LIMIT="2.0"     # 增加CPU限制
```

### 修改网络参数
```bash
# 编辑 frr_container_optimize.sh
NET_RMEM_DEFAULT=524288  # 增加接收缓冲区
NET_WMEM_DEFAULT=524288  # 增加发送缓冲区
```

这套优化方案专注于内核层面的性能调优，为FRR容器网络仿真提供最佳的运行环境。
