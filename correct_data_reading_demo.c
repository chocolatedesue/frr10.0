/*
 * BGP Notify 消息数据读取的正确实现示例
 * 修复原代码中 inner.data 读取方式的问题
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

// 模拟 BGP notify 结构体
typedef uint16_t bgp_size_t;

struct bgp_notify {
    uint8_t code;
    uint8_t subcode;
    bgp_size_t length;
    char *data;           // 十六进制字符串（用于调试显示）
    uint8_t *raw_data;    // 原始字节数据
};

// 模拟内存分配
#define XMALLOC(type, size) malloc(size)
#define XFREE(type, ptr) free(ptr)

/* ================ 原代码的问题实现 ================ */

void buggy_data_reading(struct bgp_notify *inner, void *stream)
{
    printf("=== 错误的实现方式 ===\n");
    
    // 这是原代码中有问题的实现
    int i, first = 0;
    char c[4];
    
    if (inner->length) {
        inner->data = XMALLOC(0, inner->length * 3);
        
        for (i = 0; i < inner->length; i++) {
            if (first) {
                // ❌ 错误：从流中重复读取会移动指针
                snprintf(c, sizeof(c), " %02x", 0x42); // 模拟 stream_getc
                strlcat(inner->data, c, inner->length * 3);
            } else {
                first = 1;
                // ❌ 错误：从流中读取而不是从 raw_data 读取
                snprintf(c, sizeof(c), "%02x", 0x42); // 模拟 stream_getc
                strlcpy(inner->data, c, inner->length * 3);
            }
        }
    }
    
    printf("问题：从流中重复读取，而不是从已保存的 raw_data 读取\n");
}

/* ================ 正确的实现方式 ================ */

void correct_data_reading(struct bgp_notify *inner)
{
    printf("\n=== 正确的实现方式 ===\n");
    
    int i, first = 0;
    char c[4];
    
    if (inner->length && inner->raw_data) {
        // 为十六进制字符串分配足够的内存
        inner->data = XMALLOC(0, inner->length * 3);
        
        for (i = 0; i < inner->length; i++) {
            if (first) {
                // ✅ 正确：从 raw_data 数组读取
                snprintf(c, sizeof(c), " %02x", inner->raw_data[i]);
                strlcat(inner->data, c, inner->length * 3);
            } else {
                first = 1;
                // ✅ 正确：从 raw_data 数组读取
                snprintf(c, sizeof(c), "%02x", inner->raw_data[i]);
                strlcpy(inner->data, c, inner->length * 3);
            }
        }
    }
    
    printf("正确：从 raw_data 字段读取已保存的字节数据\n");
}

/* ================ 更好的实现方式 ================ */

char *create_hex_string(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        return NULL;
    }
    
    // 计算需要的内存：每个字节需要2个字符，字节间需要空格，最后需要null终止符
    size_t hex_str_len = length * 3; // "xx " 每个字节占3个字符
    char *hex_str = malloc(hex_str_len);
    if (!hex_str) {
        return NULL;
    }
    
    hex_str[0] = '\0'; // 初始化为空字符串
    
    for (size_t i = 0; i < length; i++) {
        char temp[4];
        if (i == 0) {
            snprintf(temp, sizeof(temp), "%02x", data[i]);
        } else {
            snprintf(temp, sizeof(temp), " %02x", data[i]);
        }
        strcat(hex_str, temp);
    }
    
    return hex_str;
}

void improved_data_reading(struct bgp_notify *inner)
{
    printf("\n=== 改进的实现方式 ===\n");
    
    if (inner->length && inner->raw_data) {
        inner->data = create_hex_string(inner->raw_data, inner->length);
        if (!inner->data) {
            printf("内存分配失败\n");
            return;
        }
    }
    
    printf("改进：使用专门的函数创建十六进制字符串\n");
}

/* ================ 如何正确获取字节数组 ================ */

// 方法1：直接访问 raw_data
void method1_direct_access(struct bgp_notify *notify)
{
    printf("\n=== 方法1：直接访问 raw_data ===\n");
    
    if (!notify || !notify->raw_data || notify->length == 0) {
        printf("没有可用的数据\n");
        return;
    }
    
    printf("通知数据长度: %u 字节\n", notify->length);
    printf("原始字节数据: ");
    
    for (int i = 0; i < notify->length; i++) {
        printf("%02x ", notify->raw_data[i]);
    }
    printf("\n");
    
    // 直接使用字节数据
    uint8_t *bytes = notify->raw_data;
    size_t len = notify->length;
    
    printf("可以直接使用 bytes[0] = 0x%02x\n", bytes[0]);
}

// 方法2：复制到自己的缓冲区
int method2_copy_to_buffer(struct bgp_notify *notify, uint8_t *buffer, size_t buffer_size)
{
    printf("\n=== 方法2：复制到自己的缓冲区 ===\n");
    
    if (!notify || !notify->raw_data || !buffer || notify->length == 0) {
        printf("参数无效\n");
        return -1;
    }
    
    if (buffer_size < notify->length) {
        printf("缓冲区太小: 需要 %u 字节，提供 %zu 字节\n", 
               notify->length, buffer_size);
        return -1;
    }
    
    memcpy(buffer, notify->raw_data, notify->length);
    printf("成功复制 %u 字节到缓冲区\n", notify->length);
    
    return notify->length;
}

// 方法3：解析特定格式的数据
void method3_parse_specific_data(struct bgp_notify *notify)
{
    printf("\n=== 方法3：解析特定格式数据 ===\n");
    
    if (!notify || !notify->raw_data || notify->length == 0) {
        printf("没有数据可解析\n");
        return;
    }
    
    printf("错误代码: %u\n", notify->code);
    printf("错误子代码: %u\n", notify->subcode);
    
    uint8_t *data = notify->raw_data;
    size_t len = notify->length;
    
    // 根据不同的错误类型解析数据
    printf("数据解析:\n");
    
    if (len >= 2) {
        printf("  前两个字节: 0x%02x 0x%02x\n", data[0], data[1]);
    }
    
    // 显示为可打印字符
    printf("  作为字符串: \"");
    for (size_t i = 0; i < len; i++) {
        if (isprint(data[i])) {
            printf("%c", data[i]);
        } else {
            printf("\\x%02x", data[i]);
        }
    }
    printf("\"\n");
}

/* ================ 演示和测试 ================ */

int main()
{
    printf("BGP Notify 消息数据读取方式演示\n");
    printf("==============================\n");
    
    // 创建测试数据
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
    
    struct bgp_notify notify = {
        .code = 3,
        .subcode = 5,
        .length = sizeof(test_data),
        .data = NULL,
        .raw_data = test_data
    };
    
    printf("测试数据: ");
    for (size_t i = 0; i < sizeof(test_data); i++) {
        printf("%02x ", test_data[i]);
    }
    printf("\n");
    
    // 演示不同的读取方法
    method1_direct_access(&notify);
    
    uint8_t my_buffer[256];
    int copied = method2_copy_to_buffer(&notify, my_buffer, sizeof(my_buffer));
    if (copied > 0) {
        printf("复制的数据: ");
        for (int i = 0; i < copied; i++) {
            printf("%02x ", my_buffer[i]);
        }
        printf("\n");
    }
    
    method3_parse_specific_data(&notify);
    
    // 演示正确的十六进制字符串生成
    correct_data_reading(&notify);
    if (notify.data) {
        printf("生成的十六进制字符串: \"%s\"\n", notify.data);
        XFREE(0, notify.data);
    }
    
    improved_data_reading(&notify);
    if (notify.data) {
        printf("改进版本的十六进制字符串: \"%s\"\n", notify.data);
        free(notify.data);
    }
    
    return 0;
}

/*
 * 编译和运行:
 * gcc -o data_reading_demo data_reading_demo.c
 * ./data_reading_demo
 * 
 * 预期输出:
 * BGP Notify 消息数据读取方式演示
 * ==============================
 * 测试数据: 01 02 03 48 65 6c 6c 6f 
 * 
 * === 方法1：直接访问 raw_data ===
 * 通知数据长度: 8 字节
 * 原始字节数据: 01 02 03 48 65 6c 6c 6f 
 * 可以直接使用 bytes[0] = 0x01
 * 
 * === 方法2：复制到自己的缓冲区 ===
 * 成功复制 8 字节到缓冲区
 * 复制的数据: 01 02 03 48 65 6c 6c 6f 
 * 
 * === 方法3：解析特定格式数据 ===
 * 错误代码: 3
 * 错误子代码: 5
 * 数据解析:
 *   前两个字节: 0x01 0x02
 *   作为字符串: "\x01\x02\x03Hello"
 * 
 * === 正确的实现方式 ===
 * 正确：从 raw_data 字段读取已保存的字节数据
 * 生成的十六进制字符串: "01 02 03 48 65 6c 6c 6f"
 * 
 * === 改进的实现方式 ===
 * 改进：使用专门的函数创建十六进制字符串
 * 改进版本的十六进制字符串: "01 02 03 48 65 6c 6c 6f"
 */
