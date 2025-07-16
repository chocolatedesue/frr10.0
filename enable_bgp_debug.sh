#!/bin/bash

# BGP 调试启用脚本
# 用于通过 vtysh 启用所有 BGP 调试功能
# 使用方法: sudo ./enable_bgp_debug.sh

echo "正在启用 BGP 所有调试功能..."

# 使用 vtysh 执行调试命令
vtysh << 'EOF'
configure terminal

! 基础调试功能
debug bgp as4
debug bgp as4 segment
debug bgp neighbor-events
debug bgp nht
debug bgp keepalives
debug bgp updates
debug bgp updates detail
debug bgp bestpath
debug bgp zebra
debug bgp update-groups

! 高级调试功能
debug bgp vpn
debug bgp labelpool
debug bgp graceful-restart
debug bgp bfd
debug bgp conditional-advertisement
debug bgp pbr
debug bgp evpn mh es
debug bgp evpn mh route
debug bgp flowspec

! 保存配置
write memory

! 退出配置模式
exit

! 显示当前启用的调试功能
show debug

exit
EOF

echo "BGP 调试功能已启用完成！"
echo ""
echo "查看日志的方法："
echo "1. tail -f /var/log/frr/bgpd.log"
echo "2. journalctl -u frr -f"
echo "3. vtysh -c 'show debug'"
echo ""
echo "关闭调试的方法："
echo "sudo ./disable_bgp_debug.sh"
