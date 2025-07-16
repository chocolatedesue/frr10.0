/*
 * struct in6_addr 赋值方法示例
 * 
 * 这个文件展示了在 FRR 代码中如何给 struct in6_addr 类型的变量赋值
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

void in6_addr_assignment_examples() {
    struct in6_addr addr;
    
    /* 方法1: 使用 inet_pton() 从字符串转换 */
    // 解析 IPv6 地址字符串到 struct in6_addr
    if (inet_pton(AF_INET6, "2001:db8::1", &addr) == 1) {
        printf("成功解析 IPv6 地址\n");
    }
    
    /* 方法2: 使用预定义常量 */
    addr = in6addr_any;        // 全零地址 ::
    addr = in6addr_loopback;   // 回环地址 ::1
    
    /* 方法3: 使用 memcpy() 从另一个 in6_addr 复制 */
    struct in6_addr source_addr;
    inet_pton(AF_INET6, "fe80::1", &source_addr);
    memcpy(&addr, &source_addr, sizeof(struct in6_addr));
    
    /* 方法4: 直接结构体赋值 */
    addr = source_addr;  // 直接赋值整个结构体
    
    /* 方法5: 使用初始化宏 (在某些系统中可用) */
    struct in6_addr init_addr = in6addr_any;  // 初始化为全零地址
    
    /* 方法6: 手动设置字节 (不推荐，但有时需要) */
    memset(&addr, 0, sizeof(addr));
    addr.s6_addr[15] = 1;  // 设置为 ::1
    
    /* 方法7: 从 sockaddr_in6 中提取 */
    struct sockaddr_in6 sin6;
    inet_pton(AF_INET6, "2001:db8::2", &sin6.sin6_addr);
    addr = sin6.sin6_addr;
    
    /* 方法8: 使用联合体字段直接赋值 (在某些场景下) */
    // addr.s6_addr32[0] = htonl(0x20010db8);  // 设置前32位
    // addr.s6_addr32[1] = 0;
    // addr.s6_addr32[2] = 0; 
    // addr.s6_addr32[3] = htonl(1);  // 最后32位设为1
}

/* 在 tvr_link_nlri 结构体中的实际使用示例 */
void tvr_link_nlri_assignment_example() {
    struct tvr_link_nlri {
        uint64_t local_node;
        uint64_t remote_node;
        struct in6_addr link_addr;  // 这就是你要赋值的字段
        uint64_t time_stamp;
        // ... 其他字段
    } nlri;
    
    /* 给 link_addr 赋值的几种方法: */
    
    // 方法1: 从字符串解析
    inet_pton(AF_INET6, "fe80::1:2:3:4", &nlri.link_addr);
    
    // 方法2: 从另一个地址复制
    struct in6_addr source;
    inet_pton(AF_INET6, "2001:db8::abcd", &source);
    nlri.link_addr = source;
    
    // 方法3: 使用预定义值
    nlri.link_addr = in6addr_any;
    
    // 方法4: 使用 memcpy
    memcpy(&nlri.link_addr, &source, sizeof(struct in6_addr));
    
    // 方法5: 清零
    memset(&nlri.link_addr, 0, sizeof(nlri.link_addr));
}

/* 比较 in6_addr 的方法 */
int compare_in6_addr_examples() {
    struct in6_addr addr1, addr2;
    
    inet_pton(AF_INET6, "2001:db8::1", &addr1);
    inet_pton(AF_INET6, "2001:db8::2", &addr2);
    
    // 方法1: 使用 memcmp (最常用)
    int result = memcmp(&addr1, &addr2, sizeof(struct in6_addr));
    if (result == 0) {
        printf("地址相同\n");
    } else if (result < 0) {
        printf("addr1 < addr2\n");
    } else {
        printf("addr1 > addr2\n");
    }
    
    // 方法2: 使用 IN6_ARE_ADDR_EQUAL 宏 (如果可用)
    #ifdef IN6_ARE_ADDR_EQUAL
    if (IN6_ARE_ADDR_EQUAL(&addr1, &addr2)) {
        printf("地址相同\n");
    }
    #endif
    
    return result;
}

/* 转换为字符串的方法 */
void in6_addr_to_string_examples() {
    struct in6_addr addr;
    char addr_str[INET6_ADDRSTRLEN];
    
    inet_pton(AF_INET6, "2001:db8::1", &addr);
    
    // 使用 inet_ntop 转换为字符串
    if (inet_ntop(AF_INET6, &addr, addr_str, INET6_ADDRSTRLEN)) {
        printf("IPv6 地址: %s\n", addr_str);
    }
}
