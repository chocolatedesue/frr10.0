# FRR容器网络仿真内核调优指南

## 概述

本指南详细说明了FRR容器网络仿真环境的内核调优方案，包括主机系统优化和容器内部优化两个层面。适用于100-1000规模的容器网络仿真场景。

## 脚本文件说明

### 主机系统优化
- **`opt.sh`**: 主机内核参数优化脚本
- **适用范围**: 物理主机或虚拟机
- **权限要求**: root权限
- **重启需求**: 需要重启系统

### 容器内部优化
- **`frr_container_optimize.sh`**: 容器内核调优脚本
- **适用范围**: 容器内部环境
- **权限要求**: 容器需要NET_ADMIN权限
- **重启需求**: 无需重启

### 容器管理
- **`run_frr_container.sh`**: 优化的容器启动脚本
- **`generate_frr_configs.sh`**: FRR配置生成器

## 详细使用方法

### 1. 主机系统优化 (opt.sh)

#### 使用步骤
```bash
# 1. 检查脚本权限
ls -la opt.sh

# 2. 运行优化脚本
sudo ./opt.sh

# 3. 重启系统使参数生效
sudo reboot

# 4. 验证优化效果
sysctl net.netfilter.nf_conntrack_max
sysctl net.core.rmem_max
ulimit -n
```

#### 主要优化参数
```bash
# 网络连接优化 (中规模仿真)
net.netfilter.nf_conntrack_max = 1048576     # 1M连接跟踪
net.core.rmem_max = 33554432                 # 32MB接收缓冲区
net.core.wmem_max = 33554432                 # 32MB发送缓冲区
net.core.netdev_max_backlog = 8000           # 网络队列长度
net.ipv4.neigh.default.gc_thresh3 = 32768    # 32K邻居表

# 进程资源优化
fs.file-max = 2097152                        # 2M文件句柄
kernel.pid_max = 4194304                     # 4M进程数
vm.max_map_count = 524288                    # 512K内存映射

# 系统资源限制
* soft nofile 524288                         # 512K文件句柄
* hard nofile 524288
* soft nproc 65536                           # 64K进程数
* hard nproc 65536
```

#### 验证命令
```bash
# 检查网络参数
sysctl -a | grep -E "(nf_conntrack_max|rmem_max|wmem_max|netdev_max_backlog)"

# 检查进程限制
sysctl fs.file-max kernel.pid_max vm.max_map_count

# 检查用户限制
ulimit -n  # 文件句柄
ulimit -u  # 进程数

# 检查连接跟踪使用情况
cat /proc/net/nf_conntrack | wc -l
cat /proc/sys/net/netfilter/nf_conntrack_max
```

### 2. 容器内部优化 (frr_container_optimize.sh)

#### 使用步骤
```bash
# 1. 在容器内运行优化脚本
docker exec -it frr-router-1 /opt/frr_optimize.sh

# 或在容器启动时自动运行
docker run -d --name frr-router \
    --cap-add NET_ADMIN \
    --volume $(pwd)/frr_container_optimize.sh:/opt/frr_optimize.sh:ro \
    frr:latest \
    /bin/bash -c "/opt/frr_optimize.sh && /usr/lib/frr/frrinit.sh start && tail -f /dev/null"

# 2. 验证优化效果
docker exec frr-router-1 verify
```

#### 容器内优化参数
```bash
# 进程资源限制
ulimit -n 65536                             # 文件句柄限制
ulimit -u 8192                              # 进程数限制

# 网络缓冲区优化
net.core.rmem_default = 262144               # 接收缓冲区默认值
net.core.wmem_default = 262144               # 发送缓冲区默认值
net.core.netdev_max_backlog = 2000           # 网络队列长度

# TCP协议栈优化
net.ipv4.tcp_tw_reuse = 1                    # 启用TIME_WAIT重用
net.ipv4.tcp_fin_timeout = 30                # TCP FIN超时30秒
net.ipv4.tcp_keepalive_time = 600            # TCP保活时间10分钟
net.ipv4.tcp_keepalive_probes = 6            # TCP保活探测次数
net.ipv4.tcp_keepalive_intvl = 30            # TCP保活间隔30秒

# 路由和ARP优化
net.ipv4.conf.all.arp_announce = 2           # ARP通告模式
net.ipv4.conf.all.arp_ignore = 1             # ARP忽略模式
net.ipv4.conf.all.rp_filter = 0              # 禁用反向路径过滤
```

#### 容器内监控工具
```bash
# 快速命令 (别名)
monitor    # 系统性能监控
netdiag    # 网络诊断
verify     # 优化验证

# 完整路径
/usr/local/bin/container_monitor.sh          # 系统监控
/usr/local/bin/network_diag.sh               # 网络诊断
/usr/local/bin/verify_optimization.sh        # 优化验证
```

### 3. 容器启动优化 (run_frr_container.sh)

#### 使用步骤
```bash
# 1. 修改容器数量 (可选)
vim run_frr_container.sh
# 修改: CONTAINER_COUNT=20

# 2. 启动优化的容器集群
./run_frr_container.sh

# 3. 监控容器状态
./monitor_containers.sh

# 4. 性能测试
./performance_test.sh

# 5. 清理环境
./cleanup_containers.sh
```

#### 容器启动参数
```bash
# 资源限制
--memory 512m                               # 内存限制
--cpus 1.0                                  # CPU限制
--shm-size 128m                             # 共享内存

# 权限配置
--cap-add NET_ADMIN                         # 网络管理权限
--cap-add SYS_ADMIN                         # 系统管理权限
--cap-add NET_RAW                           # 原始套接字权限

# 进程限制
--ulimit nofile=65536:65536                 # 文件句柄限制
--ulimit nproc=8192:8192                    # 进程数限制

# 网络优化
--sysctl net.ipv4.ip_forward=1              # 启用IP转发
--sysctl net.ipv4.tcp_tw_reuse=1            # TCP重用
--sysctl net.core.rmem_default=262144       # 接收缓冲区
```

## 注意事项和限制

### 1. 权限要求

#### 主机优化
```bash
# 必须使用root权限
sudo ./opt.sh

# 检查sudo权限
sudo -v
```

#### 容器优化
```bash
# 容器需要特权权限
docker run --cap-add NET_ADMIN --cap-add SYS_ADMIN ...

# 或使用特权模式 (不推荐生产环境)
docker run --privileged ...
```

### 2. 系统兼容性

#### 支持的系统
- **Linux内核**: 4.9+ (推荐5.4+)
- **发行版**: Ubuntu 18.04+, CentOS 7+, Alpine 3.15+
- **Docker**: 20.10+ (支持BuildKit)

#### 检查兼容性
```bash
# 检查内核版本
uname -r

# 检查Docker版本
docker --version

# 检查BuildKit支持
docker buildx version
```

### 3. 资源要求

#### 最小硬件要求
```bash
# 中规模仿真 (100-1000容器)
CPU: 8核心以上
内存: 16GB以上
存储: 100GB以上 (SSD推荐)
网络: 1Gbps以上
```

#### 资源监控
```bash
# 监控系统资源
htop
iotop
nethogs

# 监控Docker资源
docker stats
docker system df
```

### 4. 网络限制

#### 容器网络限制
```bash
# 检查网络命名空间限制
cat /proc/sys/user/max_net_namespaces

# 检查网桥数量限制
brctl show | wc -l

# 检查iptables规则数量
iptables -L | wc -l
```

#### 网络性能调优
```bash
# 检查网络接口队列
cat /sys/class/net/eth0/tx_queue_len

# 检查中断绑定
cat /proc/interrupts | grep eth

# 优化网络中断
echo 2 > /proc/irq/24/smp_affinity
```

### 5. 故障排除

#### 常见问题

1. **权限不足错误**
   ```bash
   # 错误: sysctl: permission denied
   # 解决: 确保容器有NET_ADMIN权限
   docker run --cap-add NET_ADMIN ...
   ```

2. **参数设置失败**
   ```bash
   # 错误: sysctl: cannot stat /proc/sys/net/...
   # 解决: 检查内核模块是否加载
   modprobe br_netfilter
   modprobe nf_conntrack
   ```

3. **容器启动失败**
   ```bash
   # 错误: docker: Error response from daemon
   # 解决: 检查资源限制和权限
   docker logs container_name
   ```

#### 调试命令
```bash
# 检查容器内核参数
docker exec container_name sysctl -a | grep net.core

# 检查容器进程限制
docker exec container_name ulimit -a

# 检查容器网络状态
docker exec container_name ip addr show
docker exec container_name ss -tuln
```

### 6. 性能监控

#### 关键指标
```bash
# 网络性能
- 连接跟踪使用率: < 80%
- 网络队列丢包: = 0
- TCP重传率: < 1%

# 系统性能
- CPU使用率: < 80%
- 内存使用率: < 85%
- 文件句柄使用率: < 70%

# 容器性能
- 容器启动时间: < 30秒
- 网络延迟: < 1ms
- 路由收敛时间: < 10秒
```

#### 监控脚本
```bash
# 主机监控
./monitor_containers.sh

# 容器内监控
docker exec frr-router-1 monitor

# 网络诊断
docker exec frr-router-1 netdiag

# 优化验证
docker exec frr-router-1 verify
```

## 最佳实践

### 1. 部署流程
```bash
# 标准部署流程
1. 运行主机优化: sudo ./opt.sh
2. 重启系统: sudo reboot
3. 启动容器: ./run_frr_container.sh
4. 验证优化: ./monitor_containers.sh
5. 配置FRR: ./generate_frr_configs.sh
```

### 2. 监控策略
```bash
# 定期监控 (建议每小时)
*/60 * * * * /path/to/monitor_containers.sh >> /var/log/frr_monitor.log

# 告警阈值
- 连接跟踪使用率 > 80%
- 内存使用率 > 85%
- 网络丢包率 > 0.1%
```

### 3. 故障恢复
```bash
# 快速恢复步骤
1. 检查系统资源: htop, free -h
2. 检查网络状态: ss -tuln, ip route
3. 重启问题容器: docker restart container_name
4. 重新应用优化: docker exec container_name /opt/frr_optimize.sh
```

## 高级配置和调优

### 1. 针对不同规模的参数调整

#### 小规模仿真 (< 100容器)
```bash
# 修改 opt.sh 中的参数
TARGET_NF_CONNTRACK_MAX=524288      # 512K连接跟踪
TARGET_RMEM_MAX=16777216            # 16MB缓冲区
TARGET_NEIGH_GC_THRESH3=16384       # 16K邻居表
TARGET_NOFILE=262144                # 256K文件句柄
```

#### 大规模仿真 (> 1000容器)
```bash
# 修改 opt.sh 中的参数
TARGET_NF_CONNTRACK_MAX=2097152     # 2M连接跟踪
TARGET_RMEM_MAX=67108864            # 64MB缓冲区
TARGET_NEIGH_GC_THRESH3=65536       # 64K邻居表
TARGET_NOFILE=1048576               # 1M文件句柄
```

### 2. 性能基准测试

#### 网络性能测试
```bash
# 容器间延迟测试
docker exec frr-router-1 ping -c 100 -i 0.01 172.20.0.2

# 容器间带宽测试
docker exec frr-router-1 iperf3 -c 172.20.0.2 -t 60 -P 4

# 路由收敛测试
docker exec frr-router-1 vtysh -c "clear ip bgp *"
time docker exec frr-router-1 vtysh -c "show ip bgp summary"
```

#### 系统性能基准
```bash
# 文件句柄压力测试
ulimit -n 65536
for i in {1..1000}; do exec 3< /dev/null & done

# 网络连接压力测试
ss -tuln | wc -l
netstat -an | grep ESTABLISHED | wc -l

# 内存使用基准
free -h
cat /proc/meminfo | grep -E "(MemTotal|MemAvailable|Buffers|Cached)"
```

### 3. 自动化脚本

#### 一键部署脚本
```bash
#!/bin/bash
# deploy_frr_simulation.sh - 一键部署脚本

set -e

echo "=== FRR网络仿真环境一键部署 ==="

# 1. 主机优化
echo "步骤1: 主机系统优化..."
sudo ./opt.sh
echo "请重启系统后继续: sudo reboot"
read -p "系统已重启? (y/N): " confirm
[[ $confirm == [yY] ]] || exit 1

# 2. 启动容器
echo "步骤2: 启动容器集群..."
./run_frr_container.sh

# 3. 等待容器就绪
echo "步骤3: 等待容器就绪..."
sleep 30

# 4. 验证部署
echo "步骤4: 验证部署状态..."
./monitor_containers.sh

echo "=== 部署完成! ==="
```

#### 健康检查脚本
```bash
#!/bin/bash
# health_check.sh - 系统健康检查

echo "=== FRR仿真环境健康检查 ==="

# 检查主机参数
echo "1. 主机内核参数检查:"
check_param() {
    local param=$1
    local expected=$2
    local current=$(sysctl -n $param 2>/dev/null || echo "N/A")
    if [ "$current" = "$expected" ]; then
        echo "  ✓ $param: $current"
    else
        echo "  ✗ $param: $current (期望: $expected)"
    fi
}

check_param "net.netfilter.nf_conntrack_max" "1048576"
check_param "net.core.rmem_max" "33554432"
check_param "fs.file-max" "2097152"

# 检查容器状态
echo -e "\n2. 容器状态检查:"
running_containers=$(docker ps --filter "name=frr-router" --format "{{.Names}}" | wc -l)
echo "  运行中的容器: $running_containers"

# 检查网络连通性
echo -e "\n3. 网络连通性检查:"
if [ $running_containers -gt 1 ]; then
    docker exec frr-router-1 ping -c 3 -W 1 172.20.0.2 >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "  ✓ 容器间网络连通正常"
    else
        echo "  ✗ 容器间网络连通异常"
    fi
fi

# 检查资源使用
echo -e "\n4. 资源使用检查:"
cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)
mem_usage=$(free | grep Mem | awk '{printf "%.1f", $3/$2*100}')
echo "  CPU使用率: ${cpu_usage}%"
echo "  内存使用率: ${mem_usage}%"

# 检查连接跟踪
echo -e "\n5. 连接跟踪检查:"
if [ -f /proc/net/nf_conntrack ]; then
    current_conns=$(cat /proc/net/nf_conntrack | wc -l)
    max_conns=$(cat /proc/sys/net/netfilter/nf_conntrack_max)
    usage_percent=$(echo "scale=1; $current_conns * 100 / $max_conns" | bc)
    echo "  连接跟踪使用: $current_conns/$max_conns (${usage_percent}%)"
fi

echo -e "\n=== 健康检查完成 ==="
```

### 4. 故障排除手册

#### 常见故障及解决方案

| 故障现象 | 可能原因 | 解决方案 |
|----------|----------|----------|
| 容器启动失败 | 权限不足 | 添加 `--cap-add NET_ADMIN` |
| 网络参数设置失败 | 内核模块未加载 | `modprobe br_netfilter` |
| 连接跟踪表满 | 参数设置过小 | 增大 `nf_conntrack_max` |
| 文件句柄不足 | ulimit限制 | 修改 `/etc/security/limits.conf` |
| 网络延迟高 | 队列长度不足 | 增大 `netdev_max_backlog` |
| 内存不足 | 容器过多 | 减少容器数量或增加内存 |

#### 紧急恢复流程
```bash
# 1. 停止所有容器
docker stop $(docker ps -q --filter "name=frr-router")

# 2. 清理网络资源
docker network prune -f

# 3. 重新应用主机优化
sudo sysctl -p

# 4. 重启Docker服务
sudo systemctl restart docker

# 5. 重新启动容器
./run_frr_container.sh
```

## 附录

### A. 完整的参数对照表

| 参数类别 | 参数名 | 默认值 | 优化值 | 说明 |
|----------|--------|--------|--------|------|
| **网络连接** | `net.netfilter.nf_conntrack_max` | 65536 | 1048576 | 连接跟踪表大小 |
| **网络缓冲** | `net.core.rmem_max` | 212992 | 33554432 | 最大接收缓冲区 |
| **网络缓冲** | `net.core.wmem_max` | 212992 | 33554432 | 最大发送缓冲区 |
| **网络队列** | `net.core.netdev_max_backlog` | 1000 | 8000 | 网络设备队列长度 |
| **邻居表** | `net.ipv4.neigh.default.gc_thresh3` | 1024 | 32768 | 邻居表最大条目 |
| **文件系统** | `fs.file-max` | 1048576 | 2097152 | 系统最大文件句柄 |
| **进程管理** | `kernel.pid_max` | 32768 | 4194304 | 系统最大进程数 |
| **内存管理** | `vm.max_map_count` | 65530 | 524288 | 最大内存映射区域 |

### B. 监控指标阈值

| 指标类别 | 指标名称 | 正常范围 | 警告阈值 | 危险阈值 |
|----------|----------|----------|----------|----------|
| **CPU** | 使用率 | < 70% | 70-85% | > 85% |
| **内存** | 使用率 | < 80% | 80-90% | > 90% |
| **网络** | 连接跟踪使用率 | < 70% | 70-85% | > 85% |
| **网络** | 丢包率 | 0% | < 0.1% | > 0.1% |
| **文件** | 句柄使用率 | < 60% | 60-80% | > 80% |
| **容器** | 启动时间 | < 20s | 20-40s | > 40s |

### C. 相关命令速查

```bash
# 系统信息查看
uname -a                    # 系统信息
cat /proc/version          # 内核版本
lscpu                      # CPU信息
free -h                    # 内存信息
df -h                      # 磁盘信息

# 网络状态查看
ss -tuln                   # 网络连接
ip route show              # 路由表
ip addr show               # 网络接口
iptables -L -n             # 防火墙规则

# Docker相关
docker ps                  # 容器列表
docker stats               # 容器资源使用
docker network ls          # 网络列表
docker system df           # 存储使用情况

# 性能监控
htop                       # 进程监控
iotop                      # IO监控
nethogs                    # 网络监控
tcpdump -i any             # 网络抓包
```

这套内核调优方案经过优化，专门针对中规模容器网络仿真环境，可以显著提升网络性能和系统稳定性。通过合理的参数调整和监控，可以支持100-1000规模的容器网络仿真，为FRR路由协议测试提供稳定可靠的运行环境。
