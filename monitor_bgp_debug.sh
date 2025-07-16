#!/bin/bash

# BGP 调试日志监控和分析脚本
# 用于实时查看和分析 BGP 调试信息
# 使用方法: ./monitor_bgp_debug.sh [选项]

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 显示使用说明
show_usage() {
    echo "BGP 调试日志监控脚本"
    echo ""
    echo "使用方法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -f, --follow         实时跟踪日志文件"
    echo "  -n, --neighbor IP    只显示特定邻居的日志"
    echo "  -u, --updates        只显示 UPDATE 消息"
    echo "  -k, --keepalives     只显示 KEEPALIVE 消息"
    echo "  -e, --events         只显示邻居事件"
    echo "  -b, --bestpath       只显示最佳路径选择"
    echo "  -z, --zebra          只显示与 Zebra 的通信"
    echo "  -s, --status         显示当前调试状态"
    echo "  -h, --help           显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 -f                # 实时跟踪所有 BGP 日志"
    echo "  $0 -f -n 192.168.1.1 # 实时跟踪特定邻居的日志"
    echo "  $0 -u                # 显示最近的 UPDATE 消息"
    echo "  $0 -s                # 显示当前调试状态"
}

# 显示调试状态
show_debug_status() {
    echo -e "${GREEN}=== BGP 调试状态 ===${NC}"
    vtysh -c "show debug" | grep -E "(bgp|BGP)" || echo "未启用 BGP 调试"
    echo ""
    
    echo -e "${GREEN}=== BGP 邻居状态 ===${NC}"
    vtysh -c "show bgp summary" 2>/dev/null || echo "BGP 服务未运行或无邻居配置"
    echo ""
    
    echo -e "${GREEN}=== 日志文件状态 ===${NC}"
    ls -lh /var/log/frr/bgp* 2>/dev/null || echo "未找到 BGP 日志文件"
}

# 实时跟踪日志
follow_logs() {
    local filter="$1"
    local logfile="/var/log/frr/bgpd.log"
    
    if [ ! -f "$logfile" ]; then
        echo -e "${RED}错误: 日志文件 $logfile 不存在${NC}"
        echo "请确保 BGP 调试已启用并且日志配置正确"
        exit 1
    fi
    
    echo -e "${GREEN}正在实时跟踪 BGP 日志... (按 Ctrl+C 退出)${NC}"
    echo ""
    
    if [ -n "$filter" ]; then
        tail -f "$logfile" | grep --line-buffered -E "$filter" | while read line; do
            # 根据日志类型添加颜色
            if echo "$line" | grep -q "UPDATE"; then
                echo -e "${BLUE}$line${NC}"
            elif echo "$line" | grep -q "KEEPALIVE"; then
                echo -e "${GREEN}$line${NC}"
            elif echo "$line" | grep -q "OPEN\|CLOSE\|Established\|Connect"; then
                echo -e "${YELLOW}$line${NC}"
            elif echo "$line" | grep -q "ERROR\|error\|Error"; then
                echo -e "${RED}$line${NC}"
            elif echo "$line" | grep -q "bestpath"; then
                echo -e "${MAGENTA}$line${NC}"
            else
                echo "$line"
            fi
        done
    else
        tail -f "$logfile" | while read line; do
            # 根据日志类型添加颜色
            if echo "$line" | grep -q "UPDATE"; then
                echo -e "${BLUE}$line${NC}"
            elif echo "$line" | grep -q "KEEPALIVE"; then
                echo -e "${GREEN}$line${NC}"
            elif echo "$line" | grep -q "OPEN\|CLOSE\|Established\|Connect"; then
                echo -e "${YELLOW}$line${NC}"
            elif echo "$line" | grep -q "ERROR\|error\|Error"; then
                echo -e "${RED}$line${NC}"
            elif echo "$line" | grep -q "bestpath"; then
                echo -e "${MAGENTA}$line${NC}"
            else
                echo "$line"
            fi
        done
    fi
}

# 显示特定类型的日志
show_logs() {
    local filter="$1"
    local logfile="/var/log/frr/bgpd.log"
    local lines=50
    
    if [ ! -f "$logfile" ]; then
        echo -e "${RED}错误: 日志文件 $logfile 不存在${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}显示最近 $lines 行相关日志:${NC}"
    echo ""
    
    tail -n $lines "$logfile" | grep -E "$filter" | while read line; do
        # 根据日志类型添加颜色
        if echo "$line" | grep -q "UPDATE"; then
            echo -e "${BLUE}$line${NC}"
        elif echo "$line" | grep -q "KEEPALIVE"; then
            echo -e "${GREEN}$line${NC}"
        elif echo "$line" | grep -q "OPEN\|CLOSE\|Established\|Connect"; then
            echo -e "${YELLOW}$line${NC}"
        elif echo "$line" | grep -q "ERROR\|error\|Error"; then
            echo -e "${RED}$line${NC}"
        elif echo "$line" | grep -q "bestpath"; then
            echo -e "${MAGENTA}$line${NC}"
        else
            echo "$line"
        fi
    done
}

# 主程序
main() {
    case "$1" in
        -f|--follow)
            if [ -n "$2" ] && [ "$2" != "-n" ]; then
                follow_logs
            else
                shift
                case "$1" in
                    -n|--neighbor)
                        if [ -n "$2" ]; then
                            follow_logs "$2"
                        else
                            echo -e "${RED}错误: 请指定邻居 IP 地址${NC}"
                            exit 1
                        fi
                        ;;
                    *)
                        follow_logs
                        ;;
                esac
            fi
            ;;
        -n|--neighbor)
            if [ -n "$2" ]; then
                show_logs "$2"
            else
                echo -e "${RED}错误: 请指定邻居 IP 地址${NC}"
                exit 1
            fi
            ;;
        -u|--updates)
            show_logs "UPDATE"
            ;;
        -k|--keepalives)
            show_logs "KEEPALIVE"
            ;;
        -e|--events)
            show_logs "neighbor.*event\|Established\|Connect\|OPEN\|CLOSE"
            ;;
        -b|--bestpath)
            show_logs "bestpath"
            ;;
        -z|--zebra)
            show_logs "zebra"
            ;;
        -s|--status)
            show_debug_status
            ;;
        -h|--help)
            show_usage
            ;;
        "")
            show_usage
            ;;
        *)
            echo -e "${RED}错误: 未知选项 $1${NC}"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

# 检查是否有 root 权限（某些操作可能需要）
if [ "$EUID" -ne 0 ] && [ "$1" = "-f" -o "$1" = "--follow" ]; then
    echo -e "${YELLOW}警告: 建议使用 sudo 运行此脚本以确保访问所有日志文件${NC}"
    echo ""
fi

main "$@"
