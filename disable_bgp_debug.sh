#!/bin/bash

# BGP 调试关闭脚本
# 用于通过 vtysh 关闭所有 BGP 调试功能
# 使用方法: sudo ./disable_bgp_debug.sh

echo "正在关闭 BGP 所有调试功能..."

# 使用 vtysh 执行关闭调试命令
vtysh << 'EOF'
configure terminal

! 关闭基础调试功能
no debug bgp as4
no debug bgp as4 segment
no debug bgp neighbor-events
no debug bgp nht
no debug bgp keepalives
no debug bgp updates
no debug bgp updates detail
no debug bgp bestpath
no debug bgp zebra
no debug bgp update-groups

! 关闭高级调试功能
no debug bgp vpn
no debug bgp labelpool
no debug bgp graceful-restart
no debug bgp bfd
no debug bgp conditional-advertisement
no debug bgp pbr
no debug bgp evpn mh es
no debug bgp evpn mh route
no debug bgp flowspec

! 保存配置
write memory

! 退出配置模式
exit

! 显示当前启用的调试功能
show debug

exit
EOF

echo "BGP 调试功能已关闭完成！"
