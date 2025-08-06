#!/bin/bash

# =============================================================================
# FRR优化构建脚本
# 使用优化的Dockerfile进行快速构建
# =============================================================================

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

# 默认配置
BUILD_TYPE="minimal"
ENABLE_PROTOCOLS="bgpd zebra staticd bfdd"
PARALLEL_JOBS=$(nproc)
CACHE_SIZE="2G"
REGISTRY=""
PUSH_IMAGES=false

# 显示使用说明
show_usage() {
    echo -e "${BLUE}FRR优化构建脚本${NC}"
    echo -e "\n${BLUE}使用方法:${NC}"
    echo -e "$0 [选项]"
    echo -e "\n${BLUE}选项:${NC}"
    echo -e "  -t, --type TYPE        构建类型 (minimal|full|dev) [默认: minimal]"
    echo -e "  -p, --protocols LIST   启用的协议列表 [默认: bgpd,zebra,staticd,bfdd]"
    echo -e "  -j, --jobs NUM         并行构建任务数 [默认: $(nproc)]"
    echo -e "  -c, --cache-size SIZE  ccache大小 [默认: 2G]"
    echo -e "  -r, --registry URL     Docker镜像仓库"
    echo -e "  --push                 构建后推送镜像"
    echo -e "  --clean                清理构建缓存"
    echo -e "  -h, --help             显示帮助信息"
    echo -e "\n${BLUE}示例:${NC}"
    echo -e "  $0 -t minimal -p 'bgpd,zebra,bfdd'"
    echo -e "  $0 -t full --push -r myregistry.com"
    echo -e "  $0 --clean"
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -p|--protocols)
            ENABLE_PROTOCOLS="$2"
            shift 2
            ;;
        -j|--jobs)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        -c|--cache-size)
            CACHE_SIZE="$2"
            shift 2
            ;;
        -r|--registry)
            REGISTRY="$2"
            shift 2
            ;;
        --push)
            PUSH_IMAGES=true
            shift
            ;;
        --clean)
            echo -e "${YELLOW}清理构建缓存...${NC}"
            docker builder prune -f
            docker system prune -f
            echo -e "${GREEN}缓存清理完成!${NC}"
            exit 0
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

# 验证参数
if ! [[ "$BUILD_TYPE" =~ ^(minimal|full|dev)$ ]]; then
    echo -e "${RED}错误: 无效的构建类型: $BUILD_TYPE${NC}"
    exit 1
fi

# 设置镜像标签
BASE_TAG="frr-buildbase-optimized"
RUNTIME_TAG="frr-optimized"

if [ -n "$REGISTRY" ]; then
    BASE_TAG="$REGISTRY/$BASE_TAG"
    RUNTIME_TAG="$REGISTRY/$RUNTIME_TAG"
fi

case $BUILD_TYPE in
    "minimal")
        FINAL_TAG="$RUNTIME_TAG:latest"
        TARGET_STAGE="frr-runtime-minimal"
        ;;
    "full")
        FINAL_TAG="$RUNTIME_TAG:full"
        TARGET_STAGE="frr-runtime-full"
        ;;
    "dev")
        FINAL_TAG="$RUNTIME_TAG:dev"
        TARGET_STAGE="frr-development"
        ;;
esac

echo -e "${GREEN}=======================================================${NC}"
echo -e "${GREEN}    FRR优化构建开始                                   ${NC}"
echo -e "${GREEN}=======================================================${NC}"
echo -e "构建类型: $BUILD_TYPE"
echo -e "启用协议: $ENABLE_PROTOCOLS"
echo -e "并行任务: $PARALLEL_JOBS"
echo -e "缓存大小: $CACHE_SIZE"
echo -e "最终标签: $FINAL_TAG"

# 获取版本信息
PKGVER=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
echo -e "Git版本: $PKGVER"

# 第一阶段: 构建基础镜像
echo -e "\n${YELLOW}>>> [1/2] 构建优化基础镜像...${NC}"
docker build \
    --file docker/alpine/Dockerfile.buildbase.optimized \
    --target frr-buildbase-optimized \
    --tag "$BASE_TAG:latest" \
    --build-arg MAKEFLAGS="-j$PARALLEL_JOBS" \
    --progress=plain \
    .

if [ $? -ne 0 ]; then
    echo -e "${RED}基础镜像构建失败!${NC}"
    exit 1
fi

echo -e "${GREEN}基础镜像构建完成: $BASE_TAG:latest${NC}"

# 第二阶段: 构建运行时镜像
echo -e "\n${YELLOW}>>> [2/2] 构建FRR运行时镜像...${NC}"
docker build \
    --file docker/alpine/Dockerfile.fast.optimized \
    --target "$TARGET_STAGE" \
    --tag "$FINAL_TAG" \
    --build-arg BASE_BUILD_IMAGE="$BASE_TAG:latest" \
    --build-arg PKGVER="$PKGVER" \
    --build-arg ENABLE_PROTOCOLS="$ENABLE_PROTOCOLS" \
    --build-arg MAKEFLAGS="-j$PARALLEL_JOBS" \
    --progress=plain \
    .

if [ $? -ne 0 ]; then
    echo -e "${RED}运行时镜像构建失败!${NC}"
    exit 1
fi

echo -e "${GREEN}运行时镜像构建完成: $FINAL_TAG${NC}"

# 显示镜像信息
echo -e "\n${BLUE}构建的镜像:${NC}"
docker images | grep -E "(frr-buildbase-optimized|frr-optimized)" | head -10

# 推送镜像 (如果需要)
if [ "$PUSH_IMAGES" = true ] && [ -n "$REGISTRY" ]; then
    echo -e "\n${YELLOW}>>> 推送镜像到仓库...${NC}"
    docker push "$BASE_TAG:latest"
    docker push "$FINAL_TAG"
    echo -e "${GREEN}镜像推送完成!${NC}"
fi

# 运行基本测试
echo -e "\n${YELLOW}>>> 运行基本测试...${NC}"
CONTAINER_ID=$(docker run -d --name frr-test-$$ "$FINAL_TAG")
sleep 5

if docker exec "$CONTAINER_ID" vtysh -c "show version" >/dev/null 2>&1; then
    echo -e "${GREEN}✓ FRR启动测试通过${NC}"
else
    echo -e "${RED}✗ FRR启动测试失败${NC}"
fi

# 清理测试容器
docker stop "$CONTAINER_ID" >/dev/null 2>&1
docker rm "$CONTAINER_ID" >/dev/null 2>&1

# 显示使用说明
echo -e "\n${GREEN}=======================================================${NC}"
echo -e "${GREEN}          🎉 FRR优化构建完成! 🎉                      ${NC}"
echo -e "${GREEN}=======================================================${NC}"

echo -e "\n${BLUE}使用方法:${NC}"
echo -e "1. ${YELLOW}运行容器:${NC} docker run -d --name frr $FINAL_TAG"
echo -e "2. ${YELLOW}进入容器:${NC} docker exec -it frr bash"
echo -e "3. ${YELLOW}查看FRR:${NC} docker exec frr vtysh -c 'show version'"

echo -e "\n${BLUE}优化特性:${NC}"
echo -e "- ✓ 使用ccache加速重复构建"
echo -e "- ✓ 并行构建 ($PARALLEL_JOBS 任务)"
echo -e "- ✓ BuildKit缓存挂载"
echo -e "- ✓ 最小化运行时镜像"
echo -e "- ✓ 智能协议选择"

echo -e "\n${BLUE}镜像大小对比:${NC}"
echo -e "基础镜像: $(docker images --format 'table {{.Repository}}:{{.Tag}}\t{{.Size}}' | grep frr-buildbase-optimized | head -1)"
echo -e "运行镜像: $(docker images --format 'table {{.Repository}}:{{.Tag}}\t{{.Size}}' | grep frr-optimized | head -1)"

echo -e "\n${YELLOW}提示:${NC}"
echo -e "- 首次构建会较慢，后续构建将显著加速"
echo -e "- 使用 --clean 选项清理缓存"
echo -e "- 根据需要调整协议列表以减少构建时间"
