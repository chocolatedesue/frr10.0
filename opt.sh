#!/bin/bash

# ==============================================================================
# Docker & System Optimization Script
#
# 功能:
# 1. 调整 sysctl 内核参数 (文件句柄, PID, 内存映射, 网络等).
# 2. 调整 /etc/security/limits.conf 中的资源限制 (文件句柄, 进程数).
# 3. 配置 Docker 的 daemon.json 实现日志轮转.
#
# 特性: 幂等、自动备份、智能更新、清晰输出.
# ==============================================================================

# --- 配置变量: 中规模仿真优化 (100-1000容器) ---
# 基础系统参数
TARGET_FILE_MAX=2097152          # 系统最大文件句柄数 (2M)
TARGET_PID_MAX=4194304           # 系统最大进程数 (4M)
TARGET_MAX_MAP_COUNT=524288      # 内存映射区域数量 (512K)
TARGET_INOTIFY_WATCHES=1048576   # inotify监控文件数 (1M)

# 网络连接参数 - 中规模优化
TARGET_NF_CONNTRACK_MAX=1048576  # 连接跟踪表大小 (1M) - 适合中规模
TARGET_RMEM_MAX=33554432         # 接收缓冲区最大值 (32MB)
TARGET_WMEM_MAX=33554432         # 发送缓冲区最大值 (32MB)
TARGET_NETDEV_BACKLOG=8000       # 网络设备队列长度 - 中规模优化
TARGET_NEIGH_GC_THRESH3=32768    # 邻居表垃圾回收阈值 (32K)

# 进程资源限制 - FRR容器优化
TARGET_NOFILE=524288             # 文件句柄限制 (512K) - 适合FRR
TARGET_NPROC=65536               # 进程数限制 (64K)

# --- 颜色定义 ---
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# --- 脚本必须以root权限运行 ---
if [[ $EUID -ne 0 ]]; then
   echo -e "${RED}错误: 此脚本必须以root权限运行!${NC}"
   echo -e "请尝试使用: ${YELLOW}sudo ./optimize_docker.sh${NC}"
   exit 1
fi

# --- 检查 jq 是否安装 ---
if ! command -v jq &> /dev/null; then
    echo -e "${RED}错误: jq 未安装. jq 是修改Docker配置所必需的工具.${NC}"
    echo -e "请先安装 jq:"
    echo -e "${YELLOW}对于 CentOS/RHEL: sudo yum install -y jq${NC}"
    echo -e "${YELLOW}对于 Debian/Ubuntu: sudo apt-get update && sudo apt-get install -y jq${NC}"
    exit 1
fi

echo -e "${GREEN}=======================================================${NC}"
echo -e "${GREEN}    开始优化系统以支持大规模 Docker 容器运行        ${NC}"
echo -e "${GREEN}=======================================================${NC}"

# --- 第一部分: 配置 sysctl 内核参数 ---
echo -e "\n${YELLOW}>>> [1/3] 正在配置内核参数 (/etc/sysctl.conf)...${NC}"
SYSCTL_CONF="/etc/sysctl.conf"

# 定义一个函数来更新sysctl参数
update_sysctl() {
    local param=$1
    local value=$2
    echo -e "[-] 检查参数: ${param}"
    # 检查文件中是否已存在该参数
    if grep -q "^\s*${param}\s*=" "$SYSCTL_CONF"; then
        # 存在，则替换旧值
        sudo sed -i "s/^\s*${param}\s*=.*/${param} = ${value}/" "$SYSCTL_CONF"
        echo -e "    ${GREEN}已更新: ${param} -> ${value}${NC}"
    else
        # 不存在，则追加新值
        echo "${param} = ${value}" | sudo tee -a "$SYSCTL_CONF" > /dev/null
        echo -e "    ${GREEN}已添加: ${param} = ${value}${NC}"
    fi
}

# === 基础系统参数 ===
update_sysctl "fs.file-max" $TARGET_FILE_MAX
update_sysctl "kernel.pid_max" $TARGET_PID_MAX
update_sysctl "vm.max_map_count" $TARGET_MAX_MAP_COUNT
update_sysctl "fs.inotify.max_user_watches" $TARGET_INOTIFY_WATCHES

# === 网络连接跟踪优化 ===
update_sysctl "net.netfilter.nf_conntrack_max" $TARGET_NF_CONNTRACK_MAX
update_sysctl "net.netfilter.nf_conntrack_tcp_timeout_established" 7200
update_sysctl "net.netfilter.nf_conntrack_tcp_timeout_time_wait" 120
update_sysctl "net.netfilter.nf_conntrack_tcp_timeout_close_wait" 60
update_sysctl "net.netfilter.nf_conntrack_tcp_timeout_fin_wait" 120

# === 网络缓冲区优化 ===
update_sysctl "net.core.rmem_default" 262144
update_sysctl "net.core.rmem_max" $TARGET_RMEM_MAX
update_sysctl "net.core.wmem_default" 262144
update_sysctl "net.core.wmem_max" $TARGET_WMEM_MAX
update_sysctl "net.core.netdev_max_backlog" $TARGET_NETDEV_BACKLOG
update_sysctl "net.core.netdev_budget" 600

# === TCP优化 ===
update_sysctl "net.ipv4.tcp_rmem" "4096 87380 16777216"
update_sysctl "net.ipv4.tcp_wmem" "4096 65536 16777216"
update_sysctl "net.ipv4.tcp_congestion_control" "bbr"
update_sysctl "net.ipv4.tcp_slow_start_after_idle" 0
update_sysctl "net.ipv4.tcp_tw_reuse" 1
update_sysctl "net.ipv4.tcp_fin_timeout" 30
update_sysctl "net.ipv4.tcp_keepalive_time" 1200
update_sysctl "net.ipv4.tcp_keepalive_probes" 9
update_sysctl "net.ipv4.tcp_keepalive_intvl" 75

# === 路由和转发优化 ===
update_sysctl "net.ipv4.ip_forward" 1
update_sysctl "net.ipv6.conf.all.forwarding" 1
update_sysctl "net.ipv4.route.max_size" 2147483647
update_sysctl "net.ipv6.route.max_size" 2147483647
update_sysctl "net.ipv4.conf.all.ignore_routes_with_linkdown" 1
update_sysctl "net.ipv6.conf.all.ignore_routes_with_linkdown" 1

# === 邻居表优化 ===
update_sysctl "net.ipv4.neigh.default.gc_thresh1" $((TARGET_NEIGH_GC_THRESH3/16))
update_sysctl "net.ipv4.neigh.default.gc_thresh2" $((TARGET_NEIGH_GC_THRESH3/4))
update_sysctl "net.ipv4.neigh.default.gc_thresh3" $TARGET_NEIGH_GC_THRESH3
update_sysctl "net.ipv6.neigh.default.gc_thresh1" $((TARGET_NEIGH_GC_THRESH3/16))
update_sysctl "net.ipv6.neigh.default.gc_thresh2" $((TARGET_NEIGH_GC_THRESH3/4))
update_sysctl "net.ipv6.neigh.default.gc_thresh3" $TARGET_NEIGH_GC_THRESH3

# === 容器网络优化 ===
update_sysctl "net.bridge.bridge-nf-call-iptables" 1
update_sysctl "net.bridge.bridge-nf-call-ip6tables" 1
update_sysctl "net.bridge.bridge-nf-call-arptables" 1

# === 内存管理优化 ===
update_sysctl "vm.swappiness" 10
update_sysctl "vm.dirty_ratio" 15
update_sysctl "vm.dirty_background_ratio" 5
update_sysctl "vm.vfs_cache_pressure" 50
update_sysctl "vm.min_free_kbytes" 65536

# === 进程调度优化 ===
update_sysctl "kernel.sched_migration_cost_ns" 5000000
update_sysctl "kernel.sched_autogroup_enabled" 0

# 使内核参数立即生效
echo -e "[-] 正在应用内核参数..."
sudo sysctl -p
echo -e "${GREEN}内核参数已应用!${NC}"

# --- 容器网络仿真专项优化 ---
echo -e "\n${YELLOW}>>> [1.5/3] 正在应用容器网络仿真专项优化...${NC}"

# 加载必要的内核模块
echo -e "[-] 加载网络相关内核模块..."
sudo modprobe br_netfilter 2>/dev/null || true
sudo modprobe ip_conntrack 2>/dev/null || true
sudo modprobe nf_conntrack 2>/dev/null || true
sudo modprobe xt_conntrack 2>/dev/null || true

# 设置网络命名空间限制
echo -e "[-] 优化网络命名空间..."
echo 1048576 | sudo tee /proc/sys/user/max_net_namespaces > /dev/null 2>&1 || true

# 优化iptables性能
echo -e "[-] 优化iptables性能..."
# 设置iptables哈希表大小
echo 65536 | sudo tee /sys/module/nf_conntrack/parameters/hashsize > /dev/null 2>&1 || true

# 禁用不必要的网络功能以提升性能
echo -e "[-] 禁用不必要的网络功能..."
echo 0 | sudo tee /proc/sys/net/ipv4/conf/all/log_martians > /dev/null 2>&1 || true
echo 0 | sudo tee /proc/sys/net/ipv4/conf/default/log_martians > /dev/null 2>&1 || true

echo -e "${GREEN}容器网络仿真专项优化完成!${NC}"


# --- 第二部分: 配置资源限制 ---
echo -e "\n${YELLOW}>>> [2/3] 正在配置资源限制 (/etc/security/limits.conf)...${NC}"
LIMITS_CONF="/etc/security/limits.conf"

# 备份
if [ ! -f "${LIMITS_CONF}.bak.$(date +%F)" ]; then
    sudo cp "$LIMITS_CONF" "${LIMITS_CONF}.bak.$(date +%F-%T)"
    echo -e "[-] 已备份 ${LIMITS_CONF}"
fi

# 定义一个函数来更新limits
update_limits() {
    local domain=$1
    local type=$2
    local item=$3
    local value=$4
    echo -e "[-] 检查限制: ${domain} ${type} ${item}"
    # 检查是否已存在该限制
    if grep -qE "^\s*${domain}\s+${type}\s+${item}" "$LIMITS_CONF"; then
        # 存在，则替换
        sudo sed -i "/^\s*${domain}\s+${type}\s+${item}/c\\${domain}    ${type}    ${item}    ${value}" "$LIMITS_CONF"
        echo -e "    ${GREEN}已更新: ${domain} ${type} ${item} -> ${value}${NC}"
    else
        # 不存在，则追加
        echo "${domain}    ${type}    ${item}    ${value}" | sudo tee -a "$LIMITS_CONF" > /dev/null
        echo -e "    ${GREEN}已添加: ${domain}    ${type}    ${item}    ${value}${NC}"
    fi
}

update_limits "*" "soft" "nofile" $TARGET_NOFILE
update_limits "*" "hard" "nofile" $TARGET_NOFILE
update_limits "root" "soft" "nofile" $TARGET_NOFILE
update_limits "root" "hard" "nofile" $TARGET_NOFILE

update_limits "*" "soft" "nproc" $TARGET_NPROC
update_limits "*" "hard" "nproc" $TARGET_NPROC
update_limits "root" "soft" "nproc" $TARGET_NPROC
update_limits "root" "hard" "nproc" $TARGET_NPROC

echo -e "${GREEN}资源限制配置完成!${NC}"
echo -e "${YELLOW}注意: /etc/security/limits.conf 的更改需要您重新登录或重启系统才能生效。${NC}"


# --- 第三部分: 配置 Docker Daemon ---
echo -e "\n${YELLOW}>>> [3/3] 正在配置 Docker 日志轮转 (/etc/docker/daemon.json)...${NC}"
DAEMON_CONF="/etc/docker/daemon.json"

# 确保目录存在
sudo mkdir -p /etc/docker

# 如果文件不存在，则创建
if [ ! -f "$DAEMON_CONF" ]; then
    echo "{}" | sudo tee "$DAEMON_CONF" > /dev/null
    echo "[-] 已创建 ${DAEMON_CONF}"
fi

# 备份
if [ ! -f "${DAEMON_CONF}.bak.$(date +%F)" ]; then
    sudo cp "$DAEMON_CONF" "${DAEMON_CONF}.bak.$(date +%F-%T)"
    echo -e "[-] 已备份 ${DAEMON_CONF}"
fi

# 使用jq智能合并JSON
LOG_CONFIG='{"log-driver": "json-file", "log-opts": {"max-size": "10m", "max-file": "3"}}'
sudo jq ". + ${LOG_CONFIG}" "$DAEMON_CONF" > "${DAEMON_CONF}.tmp" && sudo mv "${DAEMON_CONF}.tmp" "$DAEMON_CONF"

echo -e "${GREEN}Docker日志配置完成!${NC}"
echo -e "    驱动: json-file"
echo -e "    大小: 10m"
echo -e "    文件数: 3"
echo -e "${YELLOW}注意: Docker配置更改需要重启Docker服务才能生效。${NC}"

# --- 完成 ---
echo -e "\n${GREEN}=======================================================${NC}"
echo -e "${GREEN}          🎉 系统优化脚本执行完毕! 🎉              ${NC}"
echo -e "${GREEN}=======================================================${NC}"
echo -e "\n为了使所有配置完全生效，请执行以下操作:"
echo -e "1. ${YELLOW}重启Docker服务:${NC} sudo systemctl restart docker"
echo -e "2. ${YELLOW}为了让 limits.conf 生效，建议您重新登录SSH会话，或者直接重启服务器。${NC}"
echo -e "\n强烈建议通过 ${RED}重启服务器${NC} 来确保所有设置都已正确加载。"
echo -e "${YELLOW}命令: sudo reboot${NC}"