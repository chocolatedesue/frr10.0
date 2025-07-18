#!/bin/bash

# FRR Build Script with Base Image Acceleration
# This script builds FRR using a base image to speed up the process

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BASE_IMAGE_NAME="frr-buildbase"
FINAL_IMAGE_NAME="frr"
DOCKERFILE_BASE="docker/alpine/Dockerfile.buildbase"
DOCKERFILE_FAST="docker/alpine/Dockerfile.fast"

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if base image exists
check_base_image() {
    if docker images --format "table {{.Repository}}" | grep -q "^${BASE_IMAGE_NAME}$"; then
        return 0
    else
        return 1
    fi
}

# Function to build base image
build_base_image() {
    print_status "Building base image: $BASE_IMAGE_NAME"
    docker build -f "$DOCKERFILE_BASE" -t "$BASE_IMAGE_NAME" .
    print_status "Base image built successfully!"
}

# Function to build FRR using base image
build_frr_fast() {
    print_status "Building FRR using base image acceleration"
    
    # Get git commit hash for version
    PKGVER=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    
    docker build \
        -f "$DOCKERFILE_FAST" \
        --build-arg PKGVER="$PKGVER" \
        -t "$FINAL_IMAGE_NAME" \
        .
    print_status "FRR built successfully!"
}

# Function to build FRR without base image (fallback)
build_frr_full() {
    print_status "Building FRR with full build (no base image)"
    
    # Get git commit hash for version
    PKGVER=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
    
    docker build \
        -f "docker/alpine/Dockerfile" \
        --build-arg PKGVER="$PKGVER" \
        -t "$FINAL_IMAGE_NAME" \
        .
    print_status "FRR built successfully!"
}

# Main execution
main() {
    print_status "FRR Fast Build Script"
    print_status "====================="
    
    # Check if we're in the right directory
    if [[ ! -f "$DOCKERFILE_BASE" ]]; then
        print_error "Base Dockerfile not found: $DOCKERFILE_BASE"
        print_error "Make sure you're running this script from the FRR root directory"
        exit 1
    fi
    
    # Parse command line arguments
    case "${1:-auto}" in
        "base-only")
            print_status "Building base image only"
            build_base_image
            ;;
        "fast")
            if check_base_image; then
                print_status "Base image found, using fast build"
                build_frr_fast
            else
                print_warning "Base image not found, building it first"
                build_base_image
                build_frr_fast
            fi
            ;;
        "full")
            print_status "Using full build (ignoring base image)"
            build_frr_full
            ;;
        "auto")
            if check_base_image; then
                print_status "Base image found, using fast build"
                build_frr_fast
            else
                print_warning "Base image not found, building full image"
                build_frr_full
            fi
            ;;
        "help"|"-h"|"--help")
            echo "Usage: $0 [option]"
            echo ""
            echo "Options:"
            echo "  auto       (default) Use base image if available, otherwise full build"
            echo "  fast       Use base image acceleration (build base if needed)"
            echo "  full       Full build without base image"
            echo "  base-only  Build only the base image"
            echo "  help       Show this help message"
            echo ""
            echo "Base image: $BASE_IMAGE_NAME"
            echo "Final image: $FINAL_IMAGE_NAME"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            print_error "Use '$0 help' for usage information"
            exit 1
            ;;
    esac
    
    print_status "Build completed successfully!"
    print_status "Final image: $FINAL_IMAGE_NAME"
}

# Run main function
main "$@"
