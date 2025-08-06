# FRR容器网络仿真 - 快速参考卡片

## 🚀 快速开始

### 1. 主机优化 (一次性)
```bash
sudo ./opt.sh          # 主机内核优化
sudo reboot             # 重启系统
```

### 2. 启动仿真环境
```bash
./run_frr_container.sh  # 启动容器集群
./monitor_containers.sh # 监控状态
```

### 3. 容器内优化验证
```bash
docker exec frr-router-1 verify    # 验证优化
docker exec frr-router-1 monitor   # 性能监控
docker exec frr-router-1 netdiag   # 网络诊断
```

## 📋 关键命令速查

### 主机系统
```bash
# 检查内核参数
sysctl net.netfilter.nf_conntrack_max
sysctl net.core.rmem_max
sysctl fs.file-max

# 检查资源限制
ulimit -n               # 文件句柄
ulimit -u               # 进程数
free -h                 # 内存使用

# 检查连接跟踪
cat /proc/net/nf_conntrack | wc -l
cat /proc/sys/net/netfilter/nf_conntrack_max
```

### 容器管理
```bash
# 容器操作
docker ps --filter "name=frr-router"           # 查看FRR容器
docker exec -it frr-router-1 bash              # 进入容器
docker exec frr-router-1 vtysh                 # 进入FRR CLI

# 批量操作
docker stop $(docker ps -q --filter "name=frr-router")    # 停止所有
docker restart $(docker ps -q --filter "name=frr-router") # 重启所有
./cleanup_containers.sh                                   # 清理环境
```

### 网络诊断
```bash
# 容器间连通性
docker exec frr-router-1 ping 172.20.0.2
docker exec frr-router-1 traceroute 172.20.0.2
docker exec frr-router-1 mtr 172.20.0.2

# 网络性能测试
docker exec frr-router-1 iperf3 -c 172.20.0.2
docker exec frr-router-1 iperf3 -s                # 服务端
```

## ⚙️ 关键参数

### 主机优化参数 (中规模)
| 参数 | 值 | 说明 |
|------|----|----- |
| `nf_conntrack_max` | 1048576 | 1M连接跟踪 |
| `rmem_max` | 33554432 | 32MB接收缓冲区 |
| `wmem_max` | 33554432 | 32MB发送缓冲区 |
| `netdev_max_backlog` | 8000 | 网络队列长度 |
| `file-max` | 2097152 | 2M文件句柄 |
| `nofile` | 524288 | 512K用户文件句柄 |

### 容器优化参数
| 参数 | 值 | 说明 |
|------|----|----- |
| `ulimit -n` | 65536 | 容器文件句柄 |
| `ulimit -u` | 8192 | 容器进程数 |
| `rmem_default` | 262144 | 256KB接收缓冲区 |
| `tcp_fin_timeout` | 30 | TCP FIN超时 |
| `tcp_tw_reuse` | 1 | 启用TIME_WAIT重用 |

## 🔧 故障排除

### 常见问题
| 问题 | 症状 | 解决方案 |
|------|------|----------|
| **权限不足** | `sysctl: permission denied` | 添加 `--cap-add NET_ADMIN` |
| **模块未加载** | `No such file or directory` | `modprobe br_netfilter` |
| **连接跟踪满** | 网络连接失败 | 增大 `nf_conntrack_max` |
| **文件句柄不足** | `Too many open files` | 检查 `ulimit -n` |
| **容器启动慢** | 启动时间>30s | 检查资源限制 |

### 紧急恢复
```bash
# 1. 停止所有容器
docker stop $(docker ps -q --filter "name=frr-router")

# 2. 重新应用优化
sudo sysctl -p
sudo systemctl restart docker

# 3. 重新启动
./run_frr_container.sh
```

## 📊 监控指标

### 健康阈值
| 指标 | 正常 | 警告 | 危险 |
|------|------|------|------|
| **CPU使用率** | <70% | 70-85% | >85% |
| **内存使用率** | <80% | 80-90% | >90% |
| **连接跟踪使用率** | <70% | 70-85% | >85% |
| **文件句柄使用率** | <60% | 60-80% | >80% |
| **网络丢包率** | 0% | <0.1% | >0.1% |

### 监控命令
```bash
# 系统监控
htop                    # CPU/内存
iotop                   # 磁盘IO
nethogs                 # 网络使用

# 容器监控
docker stats            # 容器资源
./monitor_containers.sh # 批量监控

# 网络监控
ss -tuln | wc -l       # 连接数
netstat -i              # 接口统计
```

## 🎯 性能调优

### 不同规模参数调整

#### 小规模 (<100容器)
```bash
TARGET_NF_CONNTRACK_MAX=524288
TARGET_RMEM_MAX=16777216
TARGET_NOFILE=262144
```

#### 大规模 (>1000容器)
```bash
TARGET_NF_CONNTRACK_MAX=2097152
TARGET_RMEM_MAX=67108864
TARGET_NOFILE=1048576
```

### 容器资源调整
```bash
# 内存密集型
--memory 1g --cpus 2.0

# CPU密集型  
--memory 512m --cpus 4.0

# 网络密集型
--memory 512m --cpus 1.0 --shm-size 256m
```

## 🔍 调试技巧

### 日志查看
```bash
# 系统日志
journalctl -f
dmesg | tail

# Docker日志
docker logs frr-router-1
docker logs --follow frr-router-1

# FRR日志
docker exec frr-router-1 tail -f /var/log/frr/zebra.log
```

### 网络调试
```bash
# 抓包分析
docker exec frr-router-1 tcpdump -i eth0 -w /tmp/capture.pcap
docker cp frr-router-1:/tmp/capture.pcap .

# 路由调试
docker exec frr-router-1 vtysh -c "show ip route"
docker exec frr-router-1 vtysh -c "show ip bgp"
```

### 性能分析
```bash
# CPU性能
docker exec frr-router-1 top -p $(pgrep zebra)

# 内存分析
docker exec frr-router-1 pmap $(pgrep zebra)

# 网络性能
docker exec frr-router-1 ss -i
```

## 📝 配置模板

### 容器启动模板
```bash
docker run -d \
    --name frr-router-${ID} \
    --hostname router-${ID} \
    --cap-add NET_ADMIN \
    --cap-add SYS_ADMIN \
    --memory 512m \
    --cpus 1.0 \
    --ulimit nofile=65536:65536 \
    --sysctl net.ipv4.ip_forward=1 \
    --volume ./frr_container_optimize.sh:/opt/optimize.sh:ro \
    frr:latest \
    /bin/bash -c "/opt/optimize.sh && frr-start && tail -f /dev/null"
```

### FRR基础配置模板
```bash
# 进入FRR配置模式
docker exec frr-router-1 vtysh -c "conf t"

# BGP基础配置
router bgp 65001
 bgp router-id 1.1.1.1
 neighbor 172.20.0.2 remote-as 65002
 neighbor 172.20.0.2 bfd

# OSPF基础配置  
router ospf
 ospf router-id 1.1.1.1
 network 172.20.0.0/16 area 0.0.0.0
```

## 🚨 告警设置

### 系统告警脚本
```bash
#!/bin/bash
# alert.sh - 简单告警脚本

# CPU告警
cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | cut -d'%' -f1)
if (( $(echo "$cpu_usage > 85" | bc -l) )); then
    echo "ALERT: CPU usage high: ${cpu_usage}%"
fi

# 内存告警
mem_usage=$(free | grep Mem | awk '{printf "%.1f", $3/$2*100}')
if (( $(echo "$mem_usage > 90" | bc -l) )); then
    echo "ALERT: Memory usage high: ${mem_usage}%"
fi

# 连接跟踪告警
if [ -f /proc/net/nf_conntrack ]; then
    current=$(cat /proc/net/nf_conntrack | wc -l)
    max=$(cat /proc/sys/net/netfilter/nf_conntrack_max)
    usage=$(echo "scale=1; $current * 100 / $max" | bc)
    if (( $(echo "$usage > 85" | bc -l) )); then
        echo "ALERT: Connection tracking high: ${usage}%"
    fi
fi
```

---
**💡 提示**: 将此文档保存为书签，方便日常运维参考！
