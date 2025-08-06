#!/bin/bash

# ==============================================================================
# FRR容器启动脚本 - 中规模网络仿真优化版
#
# 功能:
# 1. 启动优化的FRR容器
# 2. 应用性能调优参数
# 3. 配置网络命名空间
# 4. 设置监控和日志
#
# 适用场景: 100-1000规模容器网络仿真
# ==============================================================================

# --- 颜色定义 ---
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# --- 配置变量 ---
CONTAINER_NAME_PREFIX="frr-router"
NETWORK_NAME="frr-simulation"
FRR_IMAGE="frrouting/frr:latest"
CONTAINER_COUNT=10  # 示例：启动10个容器

# 容器资源限制 (中规模优化)
MEMORY_LIMIT="512m"
CPU_LIMIT="1.0"
SHM_SIZE="128m"

# 网络配置
SUBNET_BASE="172.20"
BRIDGE_NAME="br-frr-sim"

echo -e "${GREEN}=======================================================${NC}"
echo -e "${GREEN}    FRR容器网络仿真启动脚本 (中规模优化)             ${NC}"
echo -e "${GREEN}=======================================================${NC}"

# --- 检查依赖 ---
echo -e "\n${YELLOW}>>> 检查系统依赖...${NC}"

if ! command -v docker &> /dev/null; then
    echo -e "${RED}错误: Docker未安装${NC}"
    exit 1
fi

if ! docker network ls | grep -q "$NETWORK_NAME"; then
    echo -e "[-] 创建Docker网络: $NETWORK_NAME"
    docker network create \
        --driver bridge \
        --subnet=${SUBNET_BASE}.0.0/16 \
        --opt com.docker.network.bridge.name=$BRIDGE_NAME \
        --opt com.docker.network.driver.mtu=1500 \
        $NETWORK_NAME
fi

# --- 创建容器启动函数 ---
start_frr_container() {
    local container_id=$1
    local container_name="${CONTAINER_NAME_PREFIX}-${container_id}"
    local ip_addr="${SUBNET_BASE}.$((container_id/254)).$((container_id%254+1))"
    
    echo -e "[-] 启动容器: $container_name (IP: $ip_addr)"
    
    # 停止并删除已存在的容器
    docker stop $container_name 2>/dev/null || true
    docker rm $container_name 2>/dev/null || true
    
    # 启动优化的FRR容器
    docker run -d \
        --name $container_name \
        --hostname $container_name \
        --network $NETWORK_NAME \
        --ip $ip_addr \
        --cap-add NET_ADMIN \
        --cap-add SYS_ADMIN \
        --cap-add NET_RAW \
        --memory $MEMORY_LIMIT \
        --cpus $CPU_LIMIT \
        --shm-size $SHM_SIZE \
        --ulimit nofile=65536:65536 \
        --ulimit nproc=8192:8192 \
        --sysctl net.ipv4.ip_forward=1 \
        --sysctl net.ipv6.conf.all.forwarding=1 \
        --sysctl net.ipv4.tcp_tw_reuse=1 \
        --sysctl net.ipv4.tcp_fin_timeout=30 \
        --sysctl net.core.rmem_default=262144 \
        --sysctl net.core.wmem_default=262144 \
        --volume $(pwd)/frr_container_optimize.sh:/opt/frr_optimize.sh:ro \
        --restart unless-stopped \
        $FRR_IMAGE \
        /bin/bash -c "
            # 运行容器内核优化脚本
            chmod +x /opt/frr_optimize.sh
            /opt/frr_optimize.sh

            # 启动FRR服务 (使用默认配置)
            /usr/lib/frr/frrinit.sh start || systemctl start frr

            # 保持容器运行
            tail -f /dev/null
        "
    
    if [ $? -eq 0 ]; then
        echo -e "    ${GREEN}✓ 容器 $container_name 启动成功${NC}"
    else
        echo -e "    ${RED}✗ 容器 $container_name 启动失败${NC}"
    fi
}

# --- 批量启动容器 ---
echo -e "\n${YELLOW}>>> 启动FRR容器 (数量: $CONTAINER_COUNT)...${NC}"

for i in $(seq 1 $CONTAINER_COUNT); do
    start_frr_container $i
    sleep 1  # 避免同时启动过多容器
done

# --- 等待容器就绪 ---
echo -e "\n${YELLOW}>>> 等待容器就绪...${NC}"
sleep 10

# --- 验证容器状态 ---
echo -e "\n${YELLOW}>>> 验证容器状态...${NC}"
echo -e "容器运行状态:"
docker ps --filter "name=${CONTAINER_NAME_PREFIX}" --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo -e "\n网络连通性测试:"
for i in $(seq 1 3); do  # 测试前3个容器
    container_name="${CONTAINER_NAME_PREFIX}-${i}"
    target_ip="${SUBNET_BASE}.0.$((i%254+2))"
    if [ $i -lt $CONTAINER_COUNT ]; then
        echo -e "[-] 测试 $container_name -> $target_ip"
        docker exec $container_name ping -c 2 -W 1 $target_ip >/dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo -e "    ${GREEN}✓ 连通性正常${NC}"
        else
            echo -e "    ${RED}✗ 连通性异常${NC}"
        fi
    fi
done

# --- 创建管理脚本 ---
echo -e "\n${YELLOW}>>> 创建管理脚本...${NC}"

# 容器监控脚本
cat > monitor_containers.sh << 'EOF'
#!/bin/bash
# 容器监控脚本

echo "=== FRR容器状态监控 ==="
echo "时间: $(date)"

echo -e "\n1. 容器运行状态:"
docker ps --filter "name=frr-router" --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"

echo -e "\n2. 容器资源使用:"
docker stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.NetIO}}" $(docker ps --filter "name=frr-router" -q)

echo -e "\n3. 网络连接统计:"
for container in $(docker ps --filter "name=frr-router" --format "{{.Names}}" | head -5); do
    echo "[$container]"
    docker exec $container netstat -i 2>/dev/null | grep -E "(eth|lo)" || echo "  网络接口信息获取失败"
done

echo -e "\n4. FRR进程状态 (前3个容器):"
for container in $(docker ps --filter "name=frr-router" --format "{{.Names}}" | head -3); do
    echo "[$container]"
    docker exec $container ps aux | grep -E "(zebra|bgpd|ospfd)" | grep -v grep || echo "  FRR进程未运行"
done
EOF

chmod +x monitor_containers.sh

# 容器清理脚本
cat > cleanup_containers.sh << 'EOF'
#!/bin/bash
# 容器清理脚本

echo "清理FRR仿真容器..."

# 停止所有FRR容器
echo "停止容器..."
docker stop $(docker ps -q --filter "name=frr-router") 2>/dev/null || echo "没有运行的容器"

# 删除所有FRR容器
echo "删除容器..."
docker rm $(docker ps -aq --filter "name=frr-router") 2>/dev/null || echo "没有容器需要删除"

# 删除网络
echo "删除网络..."
docker network rm frr-simulation 2>/dev/null || echo "网络不存在或仍在使用"

echo "清理完成!"
EOF

chmod +x cleanup_containers.sh

# 性能测试脚本
cat > performance_test.sh << 'EOF'
#!/bin/bash
# 性能测试脚本

echo "=== FRR容器性能测试 ==="

# 测试BGP收敛时间
echo "1. BGP收敛测试:"
container1="frr-router-1"
container2="frr-router-2"

if docker ps --format "{{.Names}}" | grep -q "$container1"; then
    echo "  配置BGP邻居关系..."
    docker exec $container1 vtysh -c "conf t" -c "router bgp 65001" -c "neighbor 172.20.0.2 remote-as 65002"
    docker exec $container2 vtysh -c "conf t" -c "router bgp 65002" -c "neighbor 172.20.0.1 remote-as 65001"
    
    echo "  等待BGP邻居建立..."
    sleep 10
    
    echo "  检查BGP状态:"
    docker exec $container1 vtysh -c "show ip bgp summary"
fi

# 测试网络延迟
echo -e "\n2. 网络延迟测试:"
for i in {1..3}; do
    container="frr-router-$i"
    target_ip="172.20.0.$((i+1))"
    if docker ps --format "{{.Names}}" | grep -q "$container"; then
        echo "  [$container] -> $target_ip:"
        docker exec $container ping -c 5 -i 0.2 $target_ip | tail -1
    fi
done

# 测试内存使用
echo -e "\n3. 内存使用测试:"
docker stats --no-stream --format "table {{.Name}}\t{{.MemUsage}}\t{{.MemPerc}}" $(docker ps --filter "name=frr-router" -q | head -5)
EOF

chmod +x performance_test.sh

echo -e "${GREEN}管理脚本已创建:${NC}"
echo -e "  - monitor_containers.sh (监控容器状态)"
echo -e "  - cleanup_containers.sh (清理容器)"
echo -e "  - performance_test.sh (性能测试)"

# --- 完成 ---
echo -e "\n${GREEN}=======================================================${NC}"
echo -e "${GREEN}          🎉 FRR容器仿真环境启动完毕! 🎉              ${NC}"
echo -e "${GREEN}=======================================================${NC}"

echo -e "\n${BLUE}使用说明:${NC}"
echo -e "1. ${YELLOW}监控容器:${NC} ./monitor_containers.sh"
echo -e "2. ${YELLOW}性能测试:${NC} ./performance_test.sh"
echo -e "3. ${YELLOW}清理环境:${NC} ./cleanup_containers.sh"
echo -e "4. ${YELLOW}进入容器:${NC} docker exec -it frr-router-1 bash"
echo -e "5. ${YELLOW}查看FRR:${NC} docker exec -it frr-router-1 vtysh"

echo -e "\n${BLUE}优化特性:${NC}"
echo -e "- 容器内存限制: $MEMORY_LIMIT"
echo -e "- CPU限制: $CPU_LIMIT"
echo -e "- 文件句柄: 65536"
echo -e "- 网络优化: 已启用"
echo -e "- FRR性能调优: 已应用"

echo -e "\n${YELLOW}下一步:${NC}"
echo -e "- 根据需要调整容器数量 (修改 CONTAINER_COUNT)"
echo -e "- 配置具体的路由协议"
echo -e "- 设置网络拓扑"
echo -e "- 进行性能基准测试"
