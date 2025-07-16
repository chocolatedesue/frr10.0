#!/bin/bash

# TVR 共享内存环境诊断脚本

echo "=== TVR 共享内存环境诊断 ==="
echo "当前时间: $(date)"
echo "容器 ID: $(hostname)"
echo

echo "=== 1. 系统共享内存支持检查 ==="
echo "检查 /dev/shm 挂载:"
mount | grep shm || echo "  警告: /dev/shm 未找到"

echo
echo "检查 /dev/shm 权限和大小:"
ls -la /dev/shm/ 2>/dev/null && df -h /dev/shm/ || echo "  错误: /dev/shm 不可访问"

echo
echo "检查共享内存系统限制:"
echo "  SHMMAX (最大共享内存段大小): $(cat /proc/sys/kernel/shmmax 2>/dev/null || echo '未知')"
echo "  SHMALL (系统总共享内存页数): $(cat /proc/sys/kernel/shmall 2>/dev/null || echo '未知')"
echo "  SHMMNI (最大共享内存段数量): $(cat /proc/sys/kernel/shmmni 2>/dev/null || echo '未知')"

echo
echo "=== 2. 进程状态检查 ==="
echo "FRR 相关进程:"
ps aux | grep -E "(bgpd|sharpd|zebra)" | grep -v grep || echo "  没有找到 FRR 进程"

echo
echo "=== 3. 共享内存段检查 ==="
echo "当前共享内存段:"
ipcs -m 2>/dev/null || echo "  ipcs 命令不可用"

echo
echo "检查 TVR 专用共享内存:"
ls -la /dev/shm/*tvr* 2>/dev/null || echo "  没有找到 TVR 共享内存文件"

echo
echo "=== 4. FRR 配置检查 ==="
echo "FRR 配置目录:"
ls -la /etc/frr/ 2>/dev/null || echo "  /etc/frr 目录不存在"

echo
echo "FRR 日志文件:"
ls -la /var/log/frr/ 2>/dev/null || echo "  /var/log/frr 目录不存在"

echo
echo "=== 5. Docker 容器特殊检查 ==="
echo "容器共享内存配置:"
if [ -f /.dockerenv ]; then
    echo "  检测到 Docker 环境"
    echo "  /dev/shm 大小: $(df -h /dev/shm 2>/dev/null | tail -1 | awk '{print $2}' || echo '未知')"
    
    # 检查容器是否有 IPC_LOCK 权限
    if capsh --print 2>/dev/null | grep -q "cap_ipc_lock"; then
        echo "  IPC_LOCK 权限: 有"
    else
        echo "  IPC_LOCK 权限: 无 (可能影响共享内存)"
    fi
else
    echo "  非 Docker 环境"
fi

echo
echo "=== 6. TVR 共享内存测试 ==="
echo "尝试创建测试共享内存段:"

# 简单的共享内存测试
TEST_SHM_NAME="/test_tvr_shm_$$"
TEST_SIZE=4096

if command -v python3 >/dev/null 2>&1; then
    python3 << EOF
import mmap
import os
import errno

try:
    # 尝试创建共享内存
    fd = os.open('$TEST_SHM_NAME', os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o666)
    os.ftruncate(fd, $TEST_SIZE)
    
    # 尝试映射内存
    mm = mmap.mmap(fd, $TEST_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
    
    # 写入测试数据
    mm.write(b'TVR_TEST')
    mm.seek(0)
    data = mm.read(8)
    
    if data == b'TVR_TEST':
        print("  ✓ 共享内存读写测试通过")
    else:
        print("  ✗ 共享内存读写测试失败")
    
    mm.close()
    os.close(fd)
    os.unlink('$TEST_SHM_NAME')
    
except OSError as e:
    if e.errno == errno.ENOSYS:
        print("  ✗ 系统不支持共享内存 (ENOSYS)")
    elif e.errno == errno.ENODEV:
        print("  ✗ 设备不支持共享内存 (ENODEV)")
    elif e.errno == errno.EACCES:
        print("  ✗ 权限不足 (EACCES)")
    else:
        print(f"  ✗ 共享内存测试失败: {e}")
except Exception as e:
    print(f"  ✗ 测试异常: {e}")
EOF
else
    echo "  跳过 (python3 不可用)"
fi

echo
echo "=== 7. 建议的解决方案 ==="

if [ ! -d /dev/shm ] || [ ! -w /dev/shm ]; then
    echo "❌ 问题: /dev/shm 不可访问"
    echo "   解决方案: 确保 Docker 容器有 --shm-size 参数或 tmpfs 挂载"
fi

if ! mount | grep -q shm; then
    echo "❌ 问题: 共享内存文件系统未挂载"
    echo "   解决方案: 在 Docker 中添加 --tmpfs /dev/shm:rw,noexec,nosuid,size=128m"
fi

SHM_SIZE=$(df /dev/shm 2>/dev/null | tail -1 | awk '{print $2}' || echo "0")
if [ "$SHM_SIZE" -lt 65536 ]; then  # 64MB
    echo "⚠️  警告: /dev/shm 大小可能不足 (当前: ${SHM_SIZE}K)"
    echo "   建议: 增加共享内存大小到至少 64MB"
fi

echo
echo "=== 诊断完成 ==="
