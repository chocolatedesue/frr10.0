#!/bin/bash

# ==============================================================================
# FRR容器内部内核优化脚本
#
# 功能:
# 1. 容器内部系统参数优化
# 2. 网络协议栈调优
# 3. 内存和进程限制优化
# 4. 性能监控工具配置
#
# 注意: 仅进行内核调优，不修改FRR配置文件
# 适用场景: 100-1000规模容器网络仿真
# ==============================================================================

# --- 颜色定义 ---
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- 配置变量 ---
# 容器内资源限制 (适合中规模仿真)
CONTAINER_NOFILE=65536           # 容器内文件句柄限制
CONTAINER_NPROC=8192             # 容器内进程数限制

# 网络缓冲区参数
NET_RMEM_DEFAULT=262144          # 接收缓冲区默认值
NET_WMEM_DEFAULT=262144          # 发送缓冲区默认值
NET_BACKLOG=2000                 # 网络队列长度

# TCP优化参数
TCP_FIN_TIMEOUT=30               # TCP FIN超时时间
TCP_KEEPALIVE_TIME=600           # TCP保活时间
TCP_KEEPALIVE_PROBES=6           # TCP保活探测次数
TCP_KEEPALIVE_INTVL=30           # TCP保活间隔

echo -e "${GREEN}=======================================================${NC}"
echo -e "${GREEN}    FRR容器内部内核优化脚本 (中规模仿真优化)         ${NC}"
echo -e "${GREEN}=======================================================${NC}"

# --- 第一部分: 容器内系统优化 ---
echo -e "\n${YELLOW}>>> [1/3] 容器内系统参数优化...${NC}"

# 设置ulimit限制
echo -e "[-] 设置进程资源限制..."
ulimit -n $CONTAINER_NOFILE 2>/dev/null || echo -e "${RED}警告: 无法设置文件句柄限制${NC}"
ulimit -u $CONTAINER_NPROC 2>/dev/null || echo -e "${RED}警告: 无法设置进程数限制${NC}"

# 显示当前限制
echo -e "    当前文件句柄限制: $(ulimit -n)"
echo -e "    当前进程数限制: $(ulimit -u)"

# 容器内网络参数优化 (如果有权限)
echo -e "[-] 优化容器内网络参数..."
sysctl_set() {
    local param=$1
    local value=$2
    if sysctl -w "${param}=${value}" >/dev/null 2>&1; then
        echo -e "    ${GREEN}已设置: ${param} = ${value}${NC}"
        return 0
    else
        echo -e "    ${YELLOW}跳过: ${param} (权限不足)${NC}"
        return 1
    fi
}

# 网络缓冲区优化
echo -e "[-] 网络缓冲区优化..."
sysctl_set "net.core.rmem_default" $NET_RMEM_DEFAULT
sysctl_set "net.core.wmem_default" $NET_WMEM_DEFAULT
sysctl_set "net.core.netdev_max_backlog" $NET_BACKLOG

# TCP协议栈优化
echo -e "[-] TCP协议栈优化..."
sysctl_set "net.ipv4.tcp_tw_reuse" 1
sysctl_set "net.ipv4.tcp_fin_timeout" $TCP_FIN_TIMEOUT
sysctl_set "net.ipv4.tcp_keepalive_time" $TCP_KEEPALIVE_TIME
sysctl_set "net.ipv4.tcp_keepalive_probes" $TCP_KEEPALIVE_PROBES
sysctl_set "net.ipv4.tcp_keepalive_intvl" $TCP_KEEPALIVE_INTVL

# 路由和ARP优化
echo -e "[-] 路由和ARP优化..."
sysctl_set "net.ipv4.conf.all.arp_announce" 2
sysctl_set "net.ipv4.conf.all.arp_ignore" 1
sysctl_set "net.ipv4.conf.all.arp_filter" 0
sysctl_set "net.ipv4.conf.all.rp_filter" 0

# IPv6优化 (如果启用)
echo -e "[-] IPv6协议优化..."
sysctl_set "net.ipv6.conf.all.disable_ipv6" 0
sysctl_set "net.ipv6.conf.default.disable_ipv6" 0
sysctl_set "net.ipv6.conf.lo.disable_ipv6" 0

echo -e "${GREEN}容器内系统参数优化完成!${NC}"

# --- 第二部分: 内核模块和服务优化 ---
echo -e "\n${YELLOW}>>> [2/3] 内核模块和服务优化...${NC}"

# 加载网络相关内核模块 (如果有权限)
echo -e "[-] 加载网络内核模块..."
load_module() {
    local module=$1
    if modprobe "$module" 2>/dev/null; then
        echo -e "    ${GREEN}已加载: ${module}${NC}"
    else
        echo -e "    ${YELLOW}跳过: ${module} (权限不足或已加载)${NC}"
    fi
}

load_module "ip_tables"
load_module "ip6_tables"
load_module "iptable_filter"
load_module "ip6table_filter"
load_module "br_netfilter"

# 优化网络接口队列
echo -e "[-] 优化网络接口队列..."
for iface in $(ls /sys/class/net/ | grep -E '^(eth|veth|br)'); do
    if [ -w "/sys/class/net/$iface/tx_queue_len" ]; then
        echo 1000 > "/sys/class/net/$iface/tx_queue_len" 2>/dev/null
        echo -e "    ${GREEN}已设置 $iface 队列长度: 1000${NC}"
    fi
done

# 优化CPU调度 (如果有权限)
echo -e "[-] 优化CPU调度参数..."
if [ -w "/proc/sys/kernel/sched_migration_cost_ns" ]; then
    echo 5000000 > /proc/sys/kernel/sched_migration_cost_ns 2>/dev/null
    echo -e "    ${GREEN}已设置CPU迁移成本: 5ms${NC}"
fi

# 优化内存管理
echo -e "[-] 优化内存管理参数..."
sysctl_set "vm.swappiness" 10
sysctl_set "vm.dirty_ratio" 15
sysctl_set "vm.dirty_background_ratio" 5

echo -e "${GREEN}内核模块和服务优化完成!${NC}"

# --- 第三部分: 性能监控和工具配置 ---
echo -e "\n${YELLOW}>>> [3/3] 性能监控和工具配置...${NC}"

# 创建系统性能监控脚本
echo -e "[-] 创建系统性能监控脚本..."
cat > /usr/local/bin/container_monitor.sh << 'EOF'
#!/bin/bash
# 容器内系统性能监控脚本

echo "=== 容器内系统性能监控 ==="
echo "时间: $(date)"

echo -e "\n1. 系统资源使用:"
echo "  内存使用: $(free -h | grep Mem | awk '{print $3"/"$2}')"
echo "  CPU负载: $(uptime | awk -F'load average:' '{print $2}')"
echo "  磁盘使用: $(df -h / | tail -1 | awk '{print $5}')"

echo -e "\n2. 网络接口状态:"
ip link show | grep -E '^[0-9]+:' | awk '{print "  "$2" "$9}'

echo -e "\n3. 网络统计:"
cat /proc/net/dev | head -2
cat /proc/net/dev | grep -E "(eth|veth|br|lo)" | head -5

echo -e "\n4. 进程资源限制:"
echo "  文件句柄限制: $(ulimit -n)"
echo "  进程数限制: $(ulimit -u)"
echo "  当前文件句柄使用: $(lsof 2>/dev/null | wc -l)"

echo -e "\n5. 网络参数状态:"
echo "  TCP TIME_WAIT: $(netstat -an 2>/dev/null | grep TIME_WAIT | wc -l)"
echo "  TCP ESTABLISHED: $(netstat -an 2>/dev/null | grep ESTABLISHED | wc -l)"

echo -e "\n6. 内核网络参数:"
for param in net.core.rmem_default net.core.wmem_default net.ipv4.tcp_fin_timeout; do
    value=$(sysctl -n $param 2>/dev/null || echo "N/A")
    echo "  $param: $value"
done
EOF

chmod +x /usr/local/bin/container_monitor.sh
echo -e "    ${GREEN}已创建: /usr/local/bin/container_monitor.sh${NC}"

# 创建网络诊断脚本
echo -e "[-] 创建网络诊断脚本..."
cat > /usr/local/bin/network_diag.sh << 'EOF'
#!/bin/bash
# 容器网络诊断脚本

echo "=== 容器网络诊断 ==="
echo "时间: $(date)"

echo -e "\n1. 网络接口信息:"
ip addr show | grep -E '^[0-9]+:|inet '

echo -e "\n2. 路由表:"
ip route show | head -10

echo -e "\n3. ARP表:"
ip neigh show | head -10

echo -e "\n4. 网络连接统计:"
ss -tuln | head -10

echo -e "\n5. 网络错误统计:"
cat /proc/net/snmp | grep -E '^(Tcp|Udp|Ip):'

echo -e "\n6. 网络接口错误:"
for iface in $(ls /sys/class/net/ | grep -E '^(eth|veth|br)'); do
    if [ -r "/sys/class/net/$iface/statistics/rx_errors" ]; then
        rx_errors=$(cat /sys/class/net/$iface/statistics/rx_errors)
        tx_errors=$(cat /sys/class/net/$iface/statistics/tx_errors)
        echo "  $iface: RX错误=$rx_errors, TX错误=$tx_errors"
    fi
done

echo -e "\n7. 内核网络参数检查:"
for param in net.core.rmem_default net.core.wmem_default net.core.netdev_max_backlog \
             net.ipv4.tcp_tw_reuse net.ipv4.tcp_fin_timeout; do
    value=$(sysctl -n $param 2>/dev/null || echo "N/A")
    echo "  $param: $value"
done
EOF

chmod +x /usr/local/bin/network_diag.sh
echo -e "    ${GREEN}已创建: /usr/local/bin/network_diag.sh${NC}"

# 创建系统优化验证脚本
echo -e "[-] 创建系统优化验证脚本..."
cat > /usr/local/bin/verify_optimization.sh << 'EOF'
#!/bin/bash
# 系统优化验证脚本

echo "=== 容器内核优化验证 ==="
echo "时间: $(date)"

echo -e "\n1. 进程资源限制验证:"
echo "  文件句柄限制: $(ulimit -n)"
echo "  进程数限制: $(ulimit -u)"

echo -e "\n2. 网络参数验证:"
check_sysctl() {
    local param=$1
    local expected=$2
    local current=$(sysctl -n $param 2>/dev/null || echo "N/A")
    if [ "$current" = "$expected" ]; then
        echo "  ✓ $param: $current"
    else
        echo "  ✗ $param: $current (期望: $expected)"
    fi
}

check_sysctl "net.core.rmem_default" "262144"
check_sysctl "net.core.wmem_default" "262144"
check_sysctl "net.core.netdev_max_backlog" "2000"
check_sysctl "net.ipv4.tcp_tw_reuse" "1"
check_sysctl "net.ipv4.tcp_fin_timeout" "30"

echo -e "\n3. 网络接口队列验证:"
for iface in $(ls /sys/class/net/ | grep -E '^(eth|veth|br)'); do
    if [ -r "/sys/class/net/$iface/tx_queue_len" ]; then
        queue_len=$(cat /sys/class/net/$iface/tx_queue_len)
        echo "  $iface 队列长度: $queue_len"
    fi
done

echo -e "\n4. 内核模块验证:"
for module in ip_tables iptable_filter br_netfilter; do
    if lsmod | grep -q "^$module"; then
        echo "  ✓ $module: 已加载"
    else
        echo "  ✗ $module: 未加载"
    fi
done

echo -e "\n5. 系统性能指标:"
echo "  当前负载: $(uptime | awk -F'load average:' '{print $2}')"
echo "  内存使用: $(free | grep Mem | awk '{printf "%.1f%%", $3/$2*100}')"
echo "  TCP连接数: $(netstat -an 2>/dev/null | grep -c ESTABLISHED)"

echo -e "\n=== 优化建议 ==="
echo "- 定期运行 /usr/local/bin/container_monitor.sh 监控性能"
echo "- 使用 /usr/local/bin/network_diag.sh 诊断网络问题"
echo "- 根据实际负载调整网络参数"
EOF

chmod +x /usr/local/bin/verify_optimization.sh
echo -e "    ${GREEN}已创建: /usr/local/bin/verify_optimization.sh${NC}"

# 创建环境变量设置脚本
echo -e "[-] 创建环境变量设置脚本..."
cat > /etc/profile.d/container_optimization.sh << 'EOF'
#!/bin/bash
# 容器优化环境变量

# 设置文件句柄限制
ulimit -n 65536 2>/dev/null || true
ulimit -u 8192 2>/dev/null || true

# 设置FRR相关环境变量
export FRR_CONFIG_DIR=/etc/frr
export FRR_STATE_DIR=/var/run/frr
export FRR_LOG_DIR=/var/log/frr

# 添加优化工具到PATH
export PATH="/usr/local/bin:$PATH"

# 设置别名
alias monitor='container_monitor.sh'
alias netdiag='network_diag.sh'
alias verify='verify_optimization.sh'
EOF

chmod +x /etc/profile.d/container_optimization.sh
echo -e "    ${GREEN}已创建: /etc/profile.d/container_optimization.sh${NC}"

# --- 完成 ---
echo -e "\n${GREEN}=======================================================${NC}"
echo -e "${GREEN}          🎉 FRR容器内核优化脚本执行完毕! 🎉          ${NC}"
echo -e "${GREEN}=======================================================${NC}"

echo -e "\n${BLUE}已创建的工具:${NC}"
echo -e "1. ${YELLOW}系统监控:${NC} /usr/local/bin/container_monitor.sh (或使用别名: monitor)"
echo -e "2. ${YELLOW}网络诊断:${NC} /usr/local/bin/network_diag.sh (或使用别名: netdiag)"
echo -e "3. ${YELLOW}优化验证:${NC} /usr/local/bin/verify_optimization.sh (或使用别名: verify)"
echo -e "4. ${YELLOW}环境配置:${NC} /etc/profile.d/container_optimization.sh"

echo -e "\n${BLUE}内核优化要点:${NC}"
echo -e "- 文件句柄限制: ${CONTAINER_NOFILE}"
echo -e "- 进程数限制: ${CONTAINER_NPROC}"
echo -e "- 网络接收缓冲区: ${NET_RMEM_DEFAULT} bytes"
echo -e "- 网络发送缓冲区: ${NET_WMEM_DEFAULT} bytes"
echo -e "- TCP FIN超时: ${TCP_FIN_TIMEOUT} 秒"
echo -e "- TCP保活时间: ${TCP_KEEPALIVE_TIME} 秒"

echo -e "\n${BLUE}快速使用:${NC}"
echo -e "- ${YELLOW}重新加载环境:${NC} source /etc/profile.d/container_optimization.sh"
echo -e "- ${YELLOW}监控系统:${NC} monitor"
echo -e "- ${YELLOW}诊断网络:${NC} netdiag"
echo -e "- ${YELLOW}验证优化:${NC} verify"

echo -e "\n${YELLOW}注意事项:${NC}"
echo -e "- 容器需要NET_ADMIN权限才能修改网络参数"
echo -e "- 某些参数可能需要特权容器才能设置"
echo -e "- 建议在容器启动时运行此脚本"
echo -e "- FRR配置文件需要单独配置，此脚本仅优化内核参数"

echo -e "\n${GREEN}内核优化完成! 容器已为高性能网络仿真做好准备。${NC}"
