# BGP Notify 消息中 inner.data 的读取方式详解

## 1. 数据结构分析

### 1.1 BGP Notify 结构体
```c
struct bgp_notify {
    uint8_t code;           // 错误代码
    uint8_t subcode;        // 错误子代码
    bgp_size_t length;      // 数据长度
    bool hard_reset;        // 是否为硬重置
    char *data;            // 格式化的十六进制字符串（用于调试显示）
    uint8_t *raw_data;     // 原始字节数据
};
```

### 1.2 数据字段说明
- **`raw_data`**: 存储原始的字节数据，这是你需要的 bytes 数组
- **`data`**: 存储格式化后的十六进制字符串，仅用于调试显示

## 2. 数据读取流程分析

### 2.1 从数据流中读取原始数据
```c
// 在 bgp_notify_receive 函数中
outer.code = stream_getc(peer->curr);           // 读取错误代码
outer.subcode = stream_getc(peer->curr);        // 读取错误子代码
outer.length = size - 2;                       // 计算数据长度（总长度 - 代码字段）

// 分配内存并复制原始字节数据
if (outer.length) {
    outer.raw_data = XMALLOC(MTYPE_BGP_NOTIFICATION, outer.length);
    memcpy(outer.raw_data, stream_pnt(peer->curr), outer.length);
}
```

### 2.2 硬重置解封装
```c
// 如果是硬重置通知，需要解封装内部通知
if (hard_reset && outer.length) {
    inner = bgp_notify_decapsulate_hard_reset(&outer);
} else {
    inner = outer;  // 直接使用外层通知
}
```

### 2.3 解封装函数实现
```c
struct bgp_notify bgp_notify_decapsulate_hard_reset(struct bgp_notify *notify)
{
    struct bgp_notify bn = {};
    
    // 从原始数据中提取内部通知的代码和子代码
    bn.code = notify->raw_data[0];      // 第一个字节：错误代码
    bn.subcode = notify->raw_data[1];   // 第二个字节：错误子代码
    bn.length = notify->length - 2;    // 剩余长度作为数据长度
    
    // 分配内存并复制内部通知的数据部分
    if (bn.length > 0) {
        bn.raw_data = XMALLOC(MTYPE_BGP_NOTIFICATION, bn.length);
        memcpy(bn.raw_data, notify->raw_data + 2, bn.length);
    }
    
    return bn;
}
```

## 3. 调试用的十六进制字符串生成

### 3.1 有问题的代码分析
```c
// 当前代码中的问题实现
if (inner.length) {
    inner.data = XMALLOC(MTYPE_BGP_NOTIFICATION, inner.length * 3);
    for (i = 0; i < inner.length; i++)
        if (first) {
            snprintf(c, sizeof(c), " %02x", stream_getc(peer->curr));  // ❌ 错误！
            strlcat(inner.data, c, inner.length * 3);
        } else {
            first = 1;
            snprintf(c, sizeof(c), "%02x", stream_getc(peer->curr));   // ❌ 错误！
            strlcpy(inner.data, c, inner.length * 3);
        }
}
```

**问题分析：**
1. 使用 `stream_getc(peer->curr)` 会移动流指针，导致重复读取
2. 应该从 `inner.raw_data` 读取数据而不是从流中读取

### 3.2 正确的实现方式
```c
// 正确的十六进制字符串生成
if (inner.length && inner.raw_data) {
    inner.data = XMALLOC(MTYPE_BGP_NOTIFICATION, inner.length * 3);
    for (i = 0; i < inner.length; i++) {
        if (first) {
            snprintf(c, sizeof(c), " %02x", inner.raw_data[i]);  // ✅ 正确！
            strlcat(inner.data, c, inner.length * 3);
        } else {
            first = 1;
            snprintf(c, sizeof(c), "%02x", inner.raw_data[i]);   // ✅ 正确！
            strlcpy(inner.data, c, inner.length * 3);
        }
    }
}
```

## 4. 如何正确获取 bytes 数组

### 4.1 获取原始字节数据
```c
// 方法1：直接从 raw_data 获取
uint8_t *get_notification_bytes(struct bgp_notify *notify, size_t *length)
{
    if (!notify || !notify->raw_data || notify->length == 0) {
        *length = 0;
        return NULL;
    }
    
    *length = notify->length;
    return notify->raw_data;  // 返回原始字节数组
}

// 使用示例
size_t data_len;
uint8_t *bytes = get_notification_bytes(&inner, &data_len);
if (bytes) {
    printf("Notification data (%zu bytes):\n", data_len);
    for (size_t i = 0; i < data_len; i++) {
        printf("%02x ", bytes[i]);
    }
    printf("\n");
}
```

### 4.2 复制字节数据
```c
// 方法2：复制字节数据到自己的缓冲区
int copy_notification_bytes(struct bgp_notify *notify, uint8_t *buffer, size_t buffer_size)
{
    if (!notify || !notify->raw_data || !buffer || notify->length == 0) {
        return -1;
    }
    
    if (buffer_size < notify->length) {
        return -1;  // 缓冲区太小
    }
    
    memcpy(buffer, notify->raw_data, notify->length);
    return notify->length;  // 返回复制的字节数
}

// 使用示例
uint8_t my_buffer[1024];
int copied_bytes = copy_notification_bytes(&inner, my_buffer, sizeof(my_buffer));
if (copied_bytes > 0) {
    printf("Copied %d bytes of notification data\n", copied_bytes);
    // 现在可以安全地使用 my_buffer 中的数据
}
```

### 4.3 按字节解析数据
```c
// 方法3：按字节解析特定格式的数据
void parse_notification_data(struct bgp_notify *notify)
{
    if (!notify || !notify->raw_data || notify->length == 0) {
        return;
    }
    
    uint8_t *data = notify->raw_data;
    size_t len = notify->length;
    
    printf("Parsing notification data:\n");
    printf("Error Code: %u\n", notify->code);
    printf("Error Subcode: %u\n", notify->subcode);
    printf("Data length: %zu bytes\n", len);
    
    // 根据错误类型解析数据
    switch (notify->code) {
    case BGP_NOTIFY_HEADER_ERR:
        printf("Header error data: ");
        break;
    case BGP_NOTIFY_OPEN_ERR:
        printf("Open message error data: ");
        break;
    case BGP_NOTIFY_UPDATE_ERR:
        printf("Update message error data: ");
        break;
    case BGP_NOTIFY_CEASE:
        printf("Cease notification data: ");
        break;
    default:
        printf("Unknown error type data: ");
        break;
    }
    
    // 以十六进制显示原始字节
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
    
    // 如果是文本数据，也可以尝试显示为字符串
    printf("As string: ");
    for (size_t i = 0; i < len; i++) {
        if (isprint(data[i])) {
            printf("%c", data[i]);
        } else {
            printf(".");
        }
    }
    printf("\n");
}
```

## 5. 实际应用示例

### 5.1 完整的数据提取函数
```c
/**
 * 从 BGP notify 消息中提取和处理字节数据
 */
typedef struct {
    uint8_t *data;
    size_t length;
    uint8_t code;
    uint8_t subcode;
} notification_info_t;

notification_info_t extract_notification_info(struct bgp_notify *notify)
{
    notification_info_t info = {0};
    
    if (!notify) {
        return info;
    }
    
    info.code = notify->code;
    info.subcode = notify->subcode;
    info.length = notify->length;
    
    if (notify->raw_data && notify->length > 0) {
        // 分配内存并复制数据
        info.data = malloc(notify->length);
        if (info.data) {
            memcpy(info.data, notify->raw_data, notify->length);
        } else {
            info.length = 0;
        }
    }
    
    return info;
}

// 释放资源
void free_notification_info(notification_info_t *info)
{
    if (info && info->data) {
        free(info->data);
        info->data = NULL;
        info->length = 0;
    }
}
```

### 5.2 使用示例
```c
void handle_bgp_notification(struct peer *peer)
{
    // 假设已经接收到通知消息
    struct bgp_notify *notify = &peer->notify;
    
    // 提取通知信息
    notification_info_t info = extract_notification_info(notify);
    
    printf("BGP Notification received:\n");
    printf("Code: %u, Subcode: %u\n", info.code, info.subcode);
    printf("Data length: %zu bytes\n", info.length);
    
    if (info.data && info.length > 0) {
        printf("Raw bytes: ");
        for (size_t i = 0; i < info.length; i++) {
            printf("%02x ", info.data[i]);
        }
        printf("\n");
        
        // 进行特定的数据处理
        process_notification_data(info.data, info.length, info.code, info.subcode);
    }
    
    // 清理资源
    free_notification_info(&info);
}
```

## 6. 总结

### 6.1 关键要点
1. **`raw_data` 字段**：包含原始的字节数组，这是你需要的数据
2. **`data` 字段**：包含格式化的十六进制字符串，仅用于调试显示
3. **数据来源**：从网络流中读取并存储在 `raw_data` 中
4. **硬重置处理**：需要解封装才能获取真正的通知数据

### 6.2 最佳实践
1. 总是检查 `raw_data` 是否为 NULL 和 `length` 是否大于 0
2. 如果需要长期保存数据，应该复制到自己的缓冲区
3. 根据错误代码和子代码来解释数据内容
4. 使用适当的内存管理避免泄漏

### 6.3 修复建议
原代码中的十六进制字符串生成应该修改为从 `inner.raw_data` 读取而不是从流中重复读取。
