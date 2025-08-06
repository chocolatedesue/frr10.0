#!/bin/bash

# ==============================================================================
# FRR配置模板生成器 - 中规模网络仿真
#
# 功能:
# 1. 生成不同拓扑的FRR配置
# 2. 支持BGP、OSPF、ISIS协议
# 3. 优化的性能参数
# 4. 自动化邻居关系配置
#
# 使用场景: 100-1000容器规模网络仿真
# ==============================================================================

# --- 颜色定义 ---
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# --- 配置参数 ---
TOPOLOGY_TYPE="mesh"  # mesh, ring, star, tree
PROTOCOL="bgp"        # bgp, ospf, isis, mixed
NODE_COUNT=10
AS_BASE=65000
SUBNET_BASE="172.20"

echo -e "${GREEN}=======================================================${NC}"
echo -e "${GREEN}    FRR配置模板生成器 (中规模仿真优化)               ${NC}"
echo -e "${GREEN}=======================================================${NC}"

# --- 显示使用说明 ---
show_usage() {
    echo -e "\n${BLUE}使用方法:${NC}"
    echo -e "$0 [选项]"
    echo -e "\n${BLUE}选项:${NC}"
    echo -e "  -t, --topology TYPE    拓扑类型 (mesh|ring|star|tree) [默认: mesh]"
    echo -e "  -p, --protocol PROTO   路由协议 (bgp|ospf|isis|mixed) [默认: bgp]"
    echo -e "  -n, --nodes COUNT      节点数量 [默认: 10]"
    echo -e "  -a, --as-base AS       BGP AS号基数 [默认: 65000]"
    echo -e "  -s, --subnet BASE      子网基址 [默认: 172.20]"
    echo -e "  -h, --help             显示帮助信息"
    echo -e "\n${BLUE}示例:${NC}"
    echo -e "  $0 -t mesh -p bgp -n 20"
    echo -e "  $0 -t ring -p ospf -n 15"
    echo -e "  $0 -t star -p mixed -n 30"
}

# --- 解析命令行参数 ---
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--topology)
            TOPOLOGY_TYPE="$2"
            shift 2
            ;;
        -p|--protocol)
            PROTOCOL="$2"
            shift 2
            ;;
        -n|--nodes)
            NODE_COUNT="$2"
            shift 2
            ;;
        -a|--as-base)
            AS_BASE="$2"
            shift 2
            ;;
        -s|--subnet)
            SUBNET_BASE="$2"
            shift 2
            ;;
        -h|--help)
            show_usage
            exit 0
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            show_usage
            exit 1
            ;;
    esac
done

# --- 验证参数 ---
if ! [[ "$TOPOLOGY_TYPE" =~ ^(mesh|ring|star|tree)$ ]]; then
    echo -e "${RED}错误: 无效的拓扑类型: $TOPOLOGY_TYPE${NC}"
    exit 1
fi

if ! [[ "$PROTOCOL" =~ ^(bgp|ospf|isis|mixed)$ ]]; then
    echo -e "${RED}错误: 无效的协议类型: $PROTOCOL${NC}"
    exit 1
fi

if ! [[ "$NODE_COUNT" =~ ^[0-9]+$ ]] || [ "$NODE_COUNT" -lt 2 ]; then
    echo -e "${RED}错误: 节点数量必须是大于1的数字${NC}"
    exit 1
fi

echo -e "\n${YELLOW}配置参数:${NC}"
echo -e "  拓扑类型: $TOPOLOGY_TYPE"
echo -e "  路由协议: $PROTOCOL"
echo -e "  节点数量: $NODE_COUNT"
echo -e "  AS基数: $AS_BASE"
echo -e "  子网基址: $SUBNET_BASE"

# --- 创建配置目录 ---
CONFIG_DIR="frr_configs_${TOPOLOGY_TYPE}_${PROTOCOL}_${NODE_COUNT}nodes"
mkdir -p "$CONFIG_DIR"

echo -e "\n${YELLOW}>>> 生成FRR配置文件...${NC}"

# --- 生成基础配置模板 ---
generate_base_config() {
    local node_id=$1
    local router_id="${SUBNET_BASE}.0.${node_id}"
    local as_number=$((AS_BASE + node_id))
    
    cat << EOF
! FRR配置 - 节点 $node_id
! 拓扑: $TOPOLOGY_TYPE, 协议: $PROTOCOL
! 生成时间: $(date)
!
frr version 8.0
frr defaults traditional
!
hostname router-$node_id
!
! === 全局优化配置 ===
log syslog informational
log facility local0
no log timestamp
!
! === 接口配置 ===
interface lo
 ip address $router_id/32
 description "Loopback interface"
!
interface eth0
 description "Container network interface"
 ip address ${SUBNET_BASE}.0.${node_id}/16
EOF

    # 根据协议类型添加接口配置
    case $PROTOCOL in
        "ospf"|"mixed")
            cat << EOF
 ip ospf hello-interval 1
 ip ospf dead-interval 3
 ip ospf retransmit-interval 3
 ip ospf transmit-delay 1
 ip ospf bfd
EOF
            ;;
        "isis"|"mixed")
            cat << EOF
 isis hello-interval 1
 isis hello-multiplier 3
 isis metric 10
 isis bfd
EOF
            ;;
    esac
    
    echo "!"
}

# --- 生成BGP配置 ---
generate_bgp_config() {
    local node_id=$1
    local as_number=$((AS_BASE + node_id))
    local router_id="${SUBNET_BASE}.0.${node_id}"
    
    cat << EOF
! === BGP配置 ===
router bgp $as_number
 bgp router-id $router_id
 !
 ! BGP性能优化
 write-quanta 32
 read-quanta 5
 coalesce-time 500
 !
 ! BGP定时器优化
 bgp graceful-restart stalepath-time 300
 bgp graceful-restart restart-time 120
 !
 ! 禁用不必要的日志
 no bgp log-neighbor-changes
 !
 address-family ipv4 unicast
  no bgp network import-check
  network $router_id/32
EOF

    # 根据拓扑生成邻居关系
    case $TOPOLOGY_TYPE in
        "mesh")
            # 全连接拓扑
            for ((i=1; i<=NODE_COUNT; i++)); do
                if [ $i -ne $node_id ]; then
                    local neighbor_ip="${SUBNET_BASE}.0.${i}"
                    local neighbor_as=$((AS_BASE + i))
                    echo "  neighbor $neighbor_ip remote-as $neighbor_as"
                    echo "  neighbor $neighbor_ip bfd"
                fi
            done
            ;;
        "ring")
            # 环形拓扑
            local prev_node=$(( (node_id - 2 + NODE_COUNT) % NODE_COUNT + 1 ))
            local next_node=$(( node_id % NODE_COUNT + 1 ))
            
            for neighbor_id in $prev_node $next_node; do
                if [ $neighbor_id -ne $node_id ]; then
                    local neighbor_ip="${SUBNET_BASE}.0.${neighbor_id}"
                    local neighbor_as=$((AS_BASE + neighbor_id))
                    echo "  neighbor $neighbor_ip remote-as $neighbor_as"
                    echo "  neighbor $neighbor_ip bfd"
                fi
            done
            ;;
        "star")
            # 星形拓扑 (节点1为中心)
            if [ $node_id -eq 1 ]; then
                # 中心节点连接所有其他节点
                for ((i=2; i<=NODE_COUNT; i++)); do
                    local neighbor_ip="${SUBNET_BASE}.0.${i}"
                    local neighbor_as=$((AS_BASE + i))
                    echo "  neighbor $neighbor_ip remote-as $neighbor_as"
                    echo "  neighbor $neighbor_ip bfd"
                done
            else
                # 边缘节点只连接中心节点
                local center_ip="${SUBNET_BASE}.0.1"
                local center_as=$((AS_BASE + 1))
                echo "  neighbor $center_ip remote-as $center_as"
                echo "  neighbor $center_ip bfd"
            fi
            ;;
        "tree")
            # 树形拓扑 (二叉树)
            local parent=$(( (node_id + 1) / 2 ))
            local left_child=$(( node_id * 2 ))
            local right_child=$(( node_id * 2 + 1 ))
            
            # 连接父节点
            if [ $node_id -gt 1 ] && [ $parent -le $NODE_COUNT ]; then
                local parent_ip="${SUBNET_BASE}.0.${parent}"
                local parent_as=$((AS_BASE + parent))
                echo "  neighbor $parent_ip remote-as $parent_as"
                echo "  neighbor $parent_ip bfd"
            fi
            
            # 连接子节点
            for child in $left_child $right_child; do
                if [ $child -le $NODE_COUNT ]; then
                    local child_ip="${SUBNET_BASE}.0.${child}"
                    local child_as=$((AS_BASE + child))
                    echo "  neighbor $child_ip remote-as $child_as"
                    echo "  neighbor $child_ip bfd"
                fi
            done
            ;;
    esac
    
    cat << EOF
 exit-address-family
!
EOF
}

# --- 生成OSPF配置 ---
generate_ospf_config() {
    local node_id=$1
    local router_id="${SUBNET_BASE}.0.${node_id}"
    
    cat << EOF
! === OSPF配置 ===
router ospf
 ospf router-id $router_id
 !
 ! OSPF性能优化
 timers throttle spf 50 200 1000
 timers lsa min-arrival 100
 !
 ! 网络声明
 network ${SUBNET_BASE}.0.0/16 area 0.0.0.0
 network $router_id/32 area 0.0.0.0
!
EOF
}

# --- 生成ISIS配置 ---
generate_isis_config() {
    local node_id=$1
    local net_id=$(printf "49.0001.%04d.%04d.%04d.00" $((node_id/10000)) $(((node_id%10000)/100)) $((node_id%100)))
    
    cat << EOF
! === ISIS配置 ===
router isis 1
 net $net_id
 is-type level-2-only
 !
 ! ISIS性能优化
 spf-delay-ietf init-delay 50 short-delay 200 long-delay 1000 holddown 5000 time-to-learn 500
 lsp-gen-interval 1
 lsp-refresh-interval 600
 max-lsp-lifetime 1000
!
EOF
}

# --- 生成每个节点的配置 ---
for ((node_id=1; node_id<=NODE_COUNT; node_id++)); do
    config_file="$CONFIG_DIR/router-${node_id}.conf"
    echo -e "[-] 生成配置: router-${node_id}.conf"
    
    {
        generate_base_config $node_id
        
        case $PROTOCOL in
            "bgp")
                generate_bgp_config $node_id
                ;;
            "ospf")
                generate_ospf_config $node_id
                ;;
            "isis")
                generate_isis_config $node_id
                ;;
            "mixed")
                generate_bgp_config $node_id
                generate_ospf_config $node_id
                generate_isis_config $node_id
                ;;
        esac
        
        cat << EOF
! === BFD配置 ===
bfd
!
line vty
 exec-timeout 0 0
!
end
EOF
    } > "$config_file"
done

# --- 生成拓扑信息文件 ---
cat > "$CONFIG_DIR/topology_info.txt" << EOF
网络拓扑信息
============

拓扑类型: $TOPOLOGY_TYPE
路由协议: $PROTOCOL
节点数量: $NODE_COUNT
AS基数: $AS_BASE
子网基址: $SUBNET_BASE

节点列表:
EOF

for ((i=1; i<=NODE_COUNT; i++)); do
    echo "  router-$i: ${SUBNET_BASE}.0.${i} (AS: $((AS_BASE + i)))" >> "$CONFIG_DIR/topology_info.txt"
done

# --- 生成部署脚本 ---
cat > "$CONFIG_DIR/deploy_configs.sh" << 'EOF'
#!/bin/bash
# 配置部署脚本

echo "部署FRR配置到容器..."

for config_file in router-*.conf; do
    if [ -f "$config_file" ]; then
        router_name=$(basename "$config_file" .conf)
        container_name="frr-${router_name}"
        
        echo "部署配置到容器: $container_name"
        
        # 复制配置文件到容器
        docker cp "$config_file" "$container_name:/etc/frr/frr.conf" 2>/dev/null
        
        # 重新加载配置
        docker exec "$container_name" vtysh -c "conf t" -c "do write memory" 2>/dev/null
        docker exec "$container_name" systemctl reload frr 2>/dev/null || \
        docker exec "$container_name" /usr/lib/frr/frrinit.sh reload 2>/dev/null
        
        if [ $? -eq 0 ]; then
            echo "  ✓ $container_name 配置部署成功"
        else
            echo "  ✗ $container_name 配置部署失败"
        fi
    fi
done

echo "配置部署完成!"
EOF

chmod +x "$CONFIG_DIR/deploy_configs.sh"

echo -e "\n${GREEN}=======================================================${NC}"
echo -e "${GREEN}          🎉 FRR配置生成完毕! 🎉                      ${NC}"
echo -e "${GREEN}=======================================================${NC}"

echo -e "\n${BLUE}生成的文件:${NC}"
echo -e "  配置目录: $CONFIG_DIR/"
echo -e "  配置文件: router-1.conf ~ router-${NODE_COUNT}.conf"
echo -e "  拓扑信息: topology_info.txt"
echo -e "  部署脚本: deploy_configs.sh"

echo -e "\n${BLUE}使用方法:${NC}"
echo -e "1. ${YELLOW}查看拓扑:${NC} cat $CONFIG_DIR/topology_info.txt"
echo -e "2. ${YELLOW}部署配置:${NC} cd $CONFIG_DIR && ./deploy_configs.sh"
echo -e "3. ${YELLOW}验证配置:${NC} docker exec frr-router-1 vtysh -c 'show running-config'"

echo -e "\n${YELLOW}注意事项:${NC}"
echo -e "- 确保容器已启动并命名为 frr-router-X 格式"
echo -e "- 部署前请备份现有配置"
echo -e "- 根据实际网络调整IP地址和AS号"
