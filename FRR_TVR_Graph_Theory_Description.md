# TVR SPF最终存储结构的图论描述

## 概述

TVR SPF的`build`函数构建的最终存储结构本质上是一个**时变有向加权图 (Time-Variant Directed Weighted Graph)**，具有特殊的节点状态和边状态属性。

## 图论定义

### 基本图结构
设TVR SPF构建的图为 **G = (V, E, P, T)**，其中：

- **V**: 顶点集合 (Vertex Set) - 网络节点
- **E**: 边集合 (Edge Set) - 网络链路  
- **P**: 前缀集合 (Prefix Set) - 路由前缀
- **T**: 时间区间 (Time Interval) - [t₁, t₂]

### 详细数学描述

#### 1. 顶点集合 V (节点集合)

```
V = {v₁, v₂, ..., vₙ}

每个顶点 vᵢ ∈ V 具有属性：
- id: uint64_t (节点标识符)
- status: {DEFAULT(0), UNREACH(1), NOTRANS(2)} (SPF状态)
- dist: uint64_t ∈ [0, ∞] (到源节点的最短距离)
- visited: boolean (SPF算法访问标记)
- next_hop: IPv6地址 (下一跳地址)
```

**图论表示**：
```
V = {vᵢ | vᵢ.id = node_id, vᵢ.status ∈ {0,1,2}, 
     ∃ node_nlri ∈ DB 使得 node_nlri.time_stamp ≤ t₁}
```

#### 2. 边集合 E (链路集合)

```
E ⊆ V × V

每条边 eᵢⱼ = (vᵢ, vⱼ) 具有属性：
- weight: uint32_t (IGP度量值，边权重)
- status: {DEFAULT(0), UNREACH(1), NOTRANS(2)} (边的SPF状态)
- link_addr: IPv6地址 (链路地址，用于下一跳计算)
- bidirectional: boolean (双向连通性验证)
```

**图论表示**：
```
E = {(vᵢ, vⱼ) | vᵢ, vⱼ ∈ V, 
     ∃ link_nlri ∈ DB 使得 
     link_nlri.local_node = vᵢ.id ∧ 
     link_nlri.remote_node = vⱼ.id ∧
     link_nlri.time_stamp ≤ t₁ ∧
     bidirectional_check(vᵢ, vⱼ) = true}
```

#### 3. 前缀集合 P (路由前缀)

```
P = {p₁, p₂, ..., pₘ}

每个前缀 pᵢ ∈ P 具有属性：
- prefix: IPv6地址/前缀长度 (网络前缀)
- announcing_nodes: V' ⊆ V (宣告该前缀的节点集合)
- status: {DEFAULT(0), UNREACH(1)} (前缀的SPF状态)
```

**关联关系**：
```
宣告关系 A ⊆ V × P
A = {(vᵢ, pⱼ) | vᵢ ∈ V, pⱼ ∈ P,
     ∃ prefix_nlri ∈ DB 使得
     prefix_nlri.local_node = vᵢ.id ∧
     prefix_nlri.prefix = pⱼ.prefix ∧
     prefix_nlri.time_stamp ≤ t₁}
```

## 存储结构的具体实现

### 1. 顶点存储 (struct tvr_node)

```c
struct tvr_node {
    uint64_t local_node;           // 顶点ID
    uint8_t spf_status;            // 顶点状态属性
    bool visited;                  // SPF算法状态
    uint64_t dist;                 // 距离属性 d(s, v)
    struct in6_addr next_hop;      // 路径属性
    
    struct list *links;            // 出边列表 δ⁺(v)
    struct list *prefixes;         // 宣告前缀列表 A(v)
    
    struct node_rb_item entry;     // 红黑树索引
};
```

**图论对应**：
- `local_node` → 顶点标识符
- `links` → 出边邻接表 δ⁺(v) = {u ∈ V | (v,u) ∈ E}
- `prefixes` → 前缀宣告关系 A(v) = {p ∈ P | (v,p) ∈ A}

### 2. 边存储 (struct tvr_nlink)

```c
struct tvr_nlink {
    uint64_t remote_node;          // 目标顶点ID
    struct in6_addr link_addr;     // 边的标识符/权重计算用
    uint32_t igp_metric;           // 边权重 w(e)
    uint8_t spf_status;            // 边状态属性
};
```

**图论对应**：
- `remote_node` → 边的终点
- `igp_metric` → 边权重函数 w: E → ℝ⁺
- `link_addr` → 边的物理标识

### 3. 路由表存储 (struct tvr_route)

```c
struct tvr_route {
    uint8_t prefixlen;             // 前缀长度
    struct in6_addr prefix;        // 前缀地址
    uint64_t dist;                 // 到前缀的最短距离
    struct in6_addr next_hop;      // 最短路径的下一跳
    
    struct route_rb_item entry;    // 红黑树索引
};
```

**图论对应**：
- `prefix` → 前缀标识符
- `dist` → 最短路径距离 d(s, p)
- `next_hop` → 最短路径上的下一个顶点

## 图的类型和性质

### 1. 图的类型

**有向图 (Directed Graph)**：
```
∀ (u,v) ∈ E, 不一定有 (v,u) ∈ E
```
虽然要求双向连通性验证，但边是有方向的，每个节点存储自己的出边。

**加权图 (Weighted Graph)**：
```
w: E → ℝ⁺
w((u,v)) = link.igp_metric
```

**多重图属性**：
```
可能存在 (u,v) ∈ E 且 (u,v)' ∈ E，其中 link_addr 不同
```

### 2. 图的状态属性

**顶点状态函数**：
```
σᵥ: V → {DEFAULT, UNREACH, NOTRANS}
```

**边状态函数**：
```
σₑ: E → {DEFAULT, UNREACH, NOTRANS}
```

**前缀状态函数**：
```
σᵨ: P → {DEFAULT, UNREACH}
```

### 3. 时变性质

图的结构和属性都是时间的函数：
```
G(t) = (V(t), E(t), P(t), σᵥ(t), σₑ(t), σᵨ(t))
```

对于时间区间 [t₁, t₂]，构建的图表示该区间内的**最保守拓扑**：
```
G[t₁,t₂] = (V[t₁,t₂], E[t₁,t₂], P[t₁,t₂])

其中：
σᵥ[t₁,t₂](v) = max{σᵥ(v,t) | t ∈ [t₁,t₂]} (取最严格状态)
σₑ[t₁,t₂](e) = max{σₑ(e,t) | t ∈ [t₁,t₂]}
σᵨ[t₁,t₂](p) = max{σᵨ(p,t) | t ∈ [t₁,t₂]}
```

## SPF算法的图论描述

### 1. 算法输入

- **图**: G = (V, E, P)
- **源顶点**: s ∈ V
- **权重函数**: w: E → ℝ⁺

### 2. 可达性约束

**顶点可达性**：
```
R(v) = {
    false,  if σᵥ(v) = UNREACH
    true,   otherwise
}
```

**边可用性**：
```
U(e) = {
    false,  if σₑ(e) = UNREACH
    true,   otherwise
}
```

**转发能力**：
```
F(v) = {
    false,  if σᵥ(v) = NOTRANS
    true,   otherwise
}
```

### 3. 修改的Dijkstra算法

```
算法：TVR-Dijkstra(G, s, w)
输入：图G=(V,E,P), 源顶点s, 权重函数w
输出：最短路径树和路由表

1. ∀v ∈ V: dist[v] ← ∞, visited[v] ← false
2. dist[s] ← 0, next_hop[s] ← loopback
3. PQ ← {s}  // 优先队列

4. while PQ ≠ ∅:
5.     u ← extract_min(PQ)
6.     if visited[u]: continue
7.     visited[u] ← true
8.     
9.     if R(u) = false: continue  // 跳过不可达顶点
10.    
11.    // 处理该顶点宣告的前缀
12.    ∀p ∈ A(u): 
13.        if σᵨ(p) ≠ UNREACH and dist[u] < route_dist[p]:
14.            route_dist[p] ← dist[u]
15.            route_next_hop[p] ← next_hop[u]
16.    
17.    if F(u) = false: continue  // 跳过不能转发的顶点
18.    
19.    // 松弛出边
20.    ∀(u,v) ∈ δ⁺(u):
21.        if U((u,v)) = false: continue  // 跳过不可用边
22.        if bidirectional_check(u,v) = false: continue
23.        if dist[u] + w((u,v)) < dist[v]:
24.            dist[v] ← dist[u] + w((u,v))
25.            if u = s:
26.                next_hop[v] ← link_addr((u,v))
27.            else:
28.                next_hop[v] ← next_hop[u]
29.            PQ ← PQ ∪ {v}
```

### 4. 双向连通性验证

```
bidirectional_check(u, v) = 
    ∃ (v,u) ∈ δ⁺(v) 使得 
    link_addr((u,v)) = link_addr((v,u)) ∧ 
    U((v,u)) = true
```

## 最终存储结构的图论总结

### 数据结构对应关系

| 图论概念 | TVR实现 | 存储方式 |
|---------|---------|----------|
| 顶点集合 V | `node_rb_root` | 红黑树，按node_id索引 |
| 边集合 E | `node.links` | 邻接表，每个顶点存储出边 |
| 前缀集合 P | `route_rb_root` | 红黑树，按prefix索引 |
| 宣告关系 A | `node.prefixes` | 每个顶点存储宣告的前缀 |
| 权重函数 w | `link.igp_metric` | 边的权重属性 |
| 距离函数 d | `node.dist`, `route.dist` | 最短路径距离 |

### 图的特殊性质

1. **状态约束图**: 顶点和边都有状态属性，影响算法的可达性
2. **双向验证图**: 边的有效性需要双向验证
3. **多层关联图**: 顶点-边-前缀三层结构
4. **时变投影图**: 从时序数据投影到特定时间区间的静态图

### 复杂度分析

- **空间复杂度**: O(|V| + |E| + |P|)
- **查找复杂度**: O(log |V|) (顶点查找), O(log |P|) (前缀查找)
- **SPF复杂度**: O((|V| + |E|) log |V|) (修改的Dijkstra算法)

这种图结构设计巧妙地结合了传统网络图的拓扑信息和时变路由的特殊需求，为动态网络环境提供了高效的路由计算基础。
