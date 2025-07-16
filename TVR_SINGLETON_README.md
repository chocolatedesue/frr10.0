# TVR Database 单例模式实现说明

## 概述

为 `tvr_db_create` 函数添加了单例模式版本，确保整个系统中只有一个TVR数据库实例。这种设计在大型路由系统中特别有用，可以避免多个模块各自维护数据库实例的复杂性。

## 实现的函数

### 1. 核心单例函数

#### `tvr_db_get_instance()`
- **功能**: 获取单例实例，如果不存在则创建
- **返回值**: TVR数据库单例指针
- **线程安全**: 当前实现不是线程安全的

```c
struct tvr_db *tvr_db_get_instance(void) {
    if (g_tvr_db_instance == NULL) {
        g_tvr_db_instance = tvr_db_create();
    }
    return g_tvr_db_instance;
}
```

#### `tvr_db_instance_exists()`
- **功能**: 检查单例实例是否存在
- **返回值**: true表示存在，false表示不存在

```c
bool tvr_db_instance_exists(void) {
    return (g_tvr_db_instance != NULL);
}
```

#### `tvr_db_destroy_instance()`
- **功能**: 销毁单例实例并设置为NULL
- **用途**: 程序退出时清理资源

```c
void tvr_db_destroy_instance(void) {
    if (g_tvr_db_instance != NULL) {
        tvr_db_destroy(&g_tvr_db_instance);
        g_tvr_db_instance = NULL;
    }
}
```

#### `tvr_db_reset_instance()`
- **功能**: 重置单例实例（销毁旧的，创建新的）
- **返回值**: 新的单例实例指针
- **用途**: 测试或重新初始化时使用

```c
struct tvr_db *tvr_db_reset_instance(void) {
    tvr_db_destroy_instance();
    return tvr_db_get_instance();
}
```

## 使用场景

### 1. 多模块协同场景

在FRR这样的大型路由系统中，多个模块需要访问同一个TVR数据库：

```c
// BGP模块
void bgp_update_topology(void) {
    struct tvr_db *db = tvr_db_get_instance();
    // 更新BGP相关的拓扑信息
}

// OSPF模块  
void ospf_update_links(void) {
    struct tvr_db *db = tvr_db_get_instance();
    // 更新OSPF链路信息
}

// SPF模块
void spf_calculate_paths(void) {
    struct tvr_db *db = tvr_db_get_instance();
    // 基于TVR数据库计算路径
}
```

### 2. 初始化和清理

```c
int main(void) {
    // 程序启动时可选择性初始化
    if (!tvr_db_instance_exists()) {
        tvr_db_get_instance();
    }
    
    // 程序运行...
    
    // 程序退出时清理
    tvr_db_destroy_instance();
    return 0;
}
```

### 3. 配置和测试

```c
// 配置重载
void reload_configuration(void) {
    printf("Reloading TVR configuration...\n");
    struct tvr_db *new_db = tvr_db_reset_instance();
    // 重新加载配置到新实例
}

// 单元测试
void test_tvr_functionality(void) {
    // 确保测试开始时有干净的环境
    tvr_db_reset_instance();
    
    // 执行测试...
    
    // 测试结束后清理
    tvr_db_destroy_instance();
}
```

## 优势

### 1. 简化接口
- 无需在函数间传递数据库指针
- 减少参数复杂度
- 全局统一的数据访问点

### 2. 资源管理
- 保证只有一个数据库实例
- 避免重复创建和内存浪费
- 统一的生命周期管理

### 3. 模块解耦
- 各模块无需知道数据库的创建细节
- 便于不同模块间的数据共享
- 降低模块间的依赖复杂度

## 注意事项

### 1. 线程安全
当前实现**不是线程安全**的。在多线程环境中需要额外保护：

```c
// 推荐做法：在程序启动时初始化
int main(void) {
    tvr_db_get_instance();  // 主线程中初始化
    // 启动其他线程...
}

// 或者添加互斥锁保护
static pthread_mutex_t instance_mutex = PTHREAD_MUTEX_INITIALIZER;

struct tvr_db *tvr_db_get_instance_safe(void) {
    pthread_mutex_lock(&instance_mutex);
    if (g_tvr_db_instance == NULL) {
        g_tvr_db_instance = tvr_db_create();
    }
    pthread_mutex_unlock(&instance_mutex);
    return g_tvr_db_instance;
}
```

### 2. 测试困难
单例模式可能使单元测试变得困难：

```c
// 测试中需要显式重置状态
void test_function(void) {
    tvr_db_reset_instance();  // 确保干净的测试环境
    
    // 执行测试逻辑
    
    tvr_db_destroy_instance(); // 清理测试状态
}
```

### 3. 内存泄漏风险
必须确保程序退出时调用 `tvr_db_destroy_instance()`：

```c
void cleanup_handler(void) {
    tvr_db_destroy_instance();
}

int main(void) {
    atexit(cleanup_handler);  // 注册退出处理函数
    // 程序逻辑...
}
```

## 与Sharp工具集成

可以扩展Sharp工具的VTY命令来支持单例模式：

```c
DEFPY(sharp_tvrdb_create_singleton,
      sharp_tvrdb_create_singleton_cmd,
      "tvrdb create singleton",
      TVR_DB_STR 
      "Creation\n"
      "Use singleton pattern\n")
{
    if(tvr_db_instance_exists()) {
        vty_out(vty, "Singleton instance already exists!\n");
        return CMD_WARNING;
    }
    
    struct tvr_db *db = tvr_db_get_instance();
    if(db == NULL) {
        vty_out(vty, "Failed to create singleton!\n");
        return CMD_WARNING;
    }
    
    vty_out(vty, "Singleton instance created successfully!\n");
    return CMD_SUCCESS;
}

DEFPY(sharp_tvrdb_show_singleton,
      sharp_tvrdb_show_singleton_cmd,
      "tvrdb show singleton",
      TVR_DB_STR 
      "Show\n"
      "Show singleton instance\n")
{
    if(!tvr_db_instance_exists()) {
        vty_out(vty, "Singleton instance does not exist!\n");
        return CMD_WARNING;
    }
    
    struct tvr_db *db = tvr_db_get_instance();
    tvr_db_show(db, vty);
    return CMD_SUCCESS;
}
```

## 最佳实践

1. **初始化时机**: 在程序启动时就创建单例，避免运行时的懒初始化
2. **错误处理**: 检查 `tvr_db_instance_exists()` 来验证实例状态
3. **清理机制**: 使用 `atexit()` 或信号处理来确保程序退出时清理
4. **测试隔离**: 在测试中使用 `tvr_db_reset_instance()` 确保测试独立性
5. **文档说明**: 清楚标明哪些函数依赖单例实例

这个单例模式实现为TVR数据库提供了更简洁的使用接口，特别适合FRR这样的大型路由系统架构。
