# FRR容器网络仿真部署检查清单

## 📋 部署前检查清单

### 系统环境检查
- [ ] **操作系统**: Linux内核 4.9+ (推荐 5.4+)
- [ ] **Docker版本**: 20.10+ 且支持BuildKit
- [ ] **硬件资源**: 
  - [ ] CPU: 8核心以上
  - [ ] 内存: 16GB以上  
  - [ ] 存储: 100GB以上 (SSD推荐)
  - [ ] 网络: 1Gbps以上
- [ ] **权限确认**: 当前用户有sudo权限
- [ ] **防火墙**: 确认Docker端口未被阻止

### 文件准备检查
- [ ] **主机优化脚本**: `opt.sh` 存在且可执行
- [ ] **容器优化脚本**: `frr_container_optimize.sh` 存在且可执行
- [ ] **容器启动脚本**: `run_frr_container.sh` 存在且可执行
- [ ] **配置生成脚本**: `generate_frr_configs.sh` 存在且可执行
- [ ] **监控脚本**: `monitor_containers.sh` 存在且可执行

### 网络环境检查
- [ ] **Docker网络**: 确认Docker daemon正常运行
- [ ] **端口冲突**: 检查172.20.0.0/16网段未被占用
- [ ] **网桥配置**: 确认br-netfilter模块可加载
- [ ] **iptables**: 确认iptables服务正常

## 🚀 部署执行清单

### 第一阶段: 主机系统优化
- [ ] **1.1** 备份当前配置
  ```bash
  sudo cp /etc/sysctl.conf /etc/sysctl.conf.backup.$(date +%Y%m%d)
  sudo cp /etc/security/limits.conf /etc/security/limits.conf.backup.$(date +%Y%m%d)
  ```

- [ ] **1.2** 运行主机优化脚本
  ```bash
  sudo ./opt.sh
  ```

- [ ] **1.3** 验证脚本执行成功
  - [ ] 无错误信息输出
  - [ ] sysctl.conf 已更新
  - [ ] limits.conf 已更新
  - [ ] Docker daemon.json 已配置

- [ ] **1.4** 重启系统
  ```bash
  sudo reboot
  ```

- [ ] **1.5** 重启后验证优化效果
  ```bash
  sysctl net.netfilter.nf_conntrack_max  # 应为 1048576
  sysctl net.core.rmem_max               # 应为 33554432
  ulimit -n                              # 应为 524288
  ```

### 第二阶段: 容器环境部署
- [ ] **2.1** 确认Docker服务正常
  ```bash
  sudo systemctl status docker
  docker version
  ```

- [ ] **2.2** 拉取或构建FRR镜像
  ```bash
  docker pull frrouting/frr:latest
  # 或使用优化构建: ./docker/alpine/build-optimized.sh
  ```

- [ ] **2.3** 配置容器启动参数
  - [ ] 检查 `CONTAINER_COUNT` 设置 (默认10)
  - [ ] 检查 `MEMORY_LIMIT` 设置 (默认512m)
  - [ ] 检查 `CPU_LIMIT` 设置 (默认1.0)

- [ ] **2.4** 启动容器集群
  ```bash
  ./run_frr_container.sh
  ```

- [ ] **2.5** 验证容器启动成功
  ```bash
  docker ps --filter "name=frr-router" | wc -l  # 应等于CONTAINER_COUNT
  ```

### 第三阶段: 容器内优化验证
- [ ] **3.1** 验证容器内优化脚本执行
  ```bash
  docker exec frr-router-1 ls -la /usr/local/bin/verify_optimization.sh
  ```

- [ ] **3.2** 运行优化验证
  ```bash
  docker exec frr-router-1 verify
  ```

- [ ] **3.3** 检查关键参数
  - [ ] 文件句柄限制: `docker exec frr-router-1 ulimit -n` (应为65536)
  - [ ] 进程数限制: `docker exec frr-router-1 ulimit -u` (应为8192)
  - [ ] 网络参数: `docker exec frr-router-1 sysctl net.ipv4.tcp_tw_reuse` (应为1)

### 第四阶段: 网络连通性测试
- [ ] **4.1** 容器间基础连通性
  ```bash
  docker exec frr-router-1 ping -c 3 172.20.0.2
  docker exec frr-router-1 ping -c 3 172.20.0.3
  ```

- [ ] **4.2** 网络性能测试
  ```bash
  # 延迟测试
  docker exec frr-router-1 ping -c 100 -i 0.01 172.20.0.2
  
  # 带宽测试 (可选)
  docker exec frr-router-2 iperf3 -s -D
  docker exec frr-router-1 iperf3 -c 172.20.0.2 -t 10
  ```

- [ ] **4.3** FRR服务状态检查
  ```bash
  docker exec frr-router-1 vtysh -c "show version"
  docker exec frr-router-1 vtysh -c "show interface brief"
  ```

### 第五阶段: 监控和日志配置
- [ ] **5.1** 启动监控
  ```bash
  ./monitor_containers.sh
  ```

- [ ] **5.2** 检查监控输出
  - [ ] 容器状态正常
  - [ ] 资源使用在合理范围
  - [ ] 网络接口统计正常

- [ ] **5.3** 配置日志收集 (可选)
  ```bash
  # 设置日志轮转
  docker exec frr-router-1 ls -la /var/log/frr/
  ```

## ✅ 部署后验证清单

### 系统性能验证
- [ ] **CPU使用率** < 70% (空载状态)
- [ ] **内存使用率** < 80%
- [ ] **磁盘使用率** < 80%
- [ ] **网络延迟** < 1ms (容器间)

### 网络功能验证
- [ ] **连接跟踪**: 使用率 < 10% (空载状态)
- [ ] **ARP表**: 正常解析邻居
- [ ] **路由表**: 基础路由正确
- [ ] **网络接口**: 无错误统计

### FRR功能验证
- [ ] **Zebra**: 正常运行
- [ ] **BGP**: 可以配置 (如果启用)
- [ ] **OSPF**: 可以配置 (如果启用)
- [ ] **BFD**: 可以配置 (如果启用)

### 容器管理验证
- [ ] **容器重启**: `docker restart frr-router-1` 正常
- [ ] **容器停止**: `docker stop frr-router-1` 正常
- [ ] **容器启动**: `docker start frr-router-1` 正常
- [ ] **配置持久化**: 重启后配置保持

## 🔧 故障排除检查清单

### 主机优化问题
- [ ] **权限错误**: 确认使用sudo运行opt.sh
- [ ] **参数未生效**: 确认已重启系统
- [ ] **Docker启动失败**: 检查daemon.json语法
- [ ] **网络模块问题**: 手动加载br_netfilter模块

### 容器启动问题
- [ ] **权限不足**: 确认添加了NET_ADMIN权限
- [ ] **资源不足**: 检查内存和CPU限制
- [ ] **网络冲突**: 确认172.20.0.0/16未被占用
- [ ] **镜像问题**: 确认FRR镜像正确

### 网络连通问题
- [ ] **防火墙**: 检查iptables规则
- [ ] **路由**: 检查容器路由表
- [ ] **DNS**: 检查域名解析
- [ ] **MTU**: 检查网络MTU设置

### 性能问题
- [ ] **CPU瓶颈**: 检查进程CPU使用
- [ ] **内存不足**: 检查内存使用和swap
- [ ] **网络瓶颈**: 检查网络接口统计
- [ ] **磁盘IO**: 检查磁盘读写性能

## 📊 性能基准记录

### 部署完成后记录基准数据
```bash
# 系统基准
echo "=== 系统基准数据 ===" > deployment_baseline.txt
echo "部署时间: $(date)" >> deployment_baseline.txt
echo "容器数量: $(docker ps --filter 'name=frr-router' | wc -l)" >> deployment_baseline.txt
echo "CPU使用: $(top -bn1 | grep 'Cpu(s)' | awk '{print $2}')" >> deployment_baseline.txt
echo "内存使用: $(free | grep Mem | awk '{printf "%.1f%%", $3/$2*100}')" >> deployment_baseline.txt

# 网络基准
echo "=== 网络基准数据 ===" >> deployment_baseline.txt
echo "连接跟踪: $(cat /proc/net/nf_conntrack | wc -l)" >> deployment_baseline.txt
echo "网络延迟: $(docker exec frr-router-1 ping -c 10 172.20.0.2 | tail -1)" >> deployment_baseline.txt

# 保存配置
cp /etc/sysctl.conf deployment_sysctl_$(date +%Y%m%d).conf
cp /etc/security/limits.conf deployment_limits_$(date +%Y%m%d).conf
```

## 📝 部署完成确认

### 最终确认清单
- [ ] **所有容器正常运行**
- [ ] **网络连通性正常**
- [ ] **FRR服务正常**
- [ ] **监控脚本正常**
- [ ] **性能指标正常**
- [ ] **基准数据已记录**
- [ ] **备份配置已保存**

### 交付文档
- [ ] **部署报告**: 记录部署过程和结果
- [ ] **配置清单**: 记录所有配置参数
- [ ] **监控指标**: 记录基准性能数据
- [ ] **故障排除**: 记录遇到的问题和解决方案
- [ ] **运维手册**: 提供日常运维指导

---

**✅ 部署完成**: 所有检查项通过后，FRR容器网络仿真环境即可投入使用！

**📞 技术支持**: 如遇问题，请参考 `KERNEL_TUNING_GUIDE.md` 详细文档或 `QUICK_REFERENCE.md` 快速参考。
