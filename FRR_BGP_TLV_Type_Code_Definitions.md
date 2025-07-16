# FRR BGP代码中TLV Type Code定义位置分析

## 概述

在FRR BGP模块中，涉及到多种TLV（Type-Length-Value）结构的定义，主要包括BGP Capability Code、BGP Path Attribute Type Code、BGP Notify Code等。这些定义分布在不同的头文件中，本文档详细梳理这些定义的位置。

## 1. BGP Capability Code定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgp_open.h`

### 1.1 Capability Code定义

```c
/* Capability Code */
#define CAPABILITY_CODE_MP              1 /* Multiprotocol Extensions */
#define CAPABILITY_CODE_REFRESH         2 /* Route Refresh Capability */
#define CAPABILITY_CODE_ORF             3 /* Cooperative Route Filtering Capability */
#define CAPABILITY_CODE_RESTART        64 /* Graceful Restart Capability */
#define CAPABILITY_CODE_AS4            65 /* 4-octet AS number Capability */
#define CAPABILITY_CODE_DYNAMIC        67 /* Dynamic Capability */
#define CAPABILITY_CODE_ADDPATH        69 /* Addpath Capability */
#define CAPABILITY_CODE_ENHANCED_RR    70 /* Enhanced Route Refresh capability */
#define CAPABILITY_CODE_LLGR           71 /* Long-lived Graceful Restart */
#define CAPABILITY_CODE_FQDN           73 /* Advertise hostname capability */
#define CAPABILITY_CODE_SOFT_VERSION   75 /* Software Version capability */
#define CAPABILITY_CODE_ENHE            5 /* Extended Next Hop Encoding */
#define CAPABILITY_CODE_EXT_MESSAGE     6 /* Extended Message Support */
#define CAPABILITY_CODE_ROLE            9 /* Role Capability */
```

### 1.2 Capability Length定义

```c
/* Capability Length */
#define CAPABILITY_CODE_MP_LEN          4
#define CAPABILITY_CODE_REFRESH_LEN     0
#define CAPABILITY_CODE_DYNAMIC_LEN     0
#define CAPABILITY_CODE_RESTART_LEN     2 /* Receiving only case */
#define CAPABILITY_CODE_AS4_LEN         4
#define CAPABILITY_CODE_ADDPATH_LEN     4
#define CAPABILITY_CODE_ENHE_LEN        6 /* NRLI AFI = 2, SAFI = 2, Nexthop AFI = 2 */
#define CAPABILITY_CODE_MIN_FQDN_LEN    2
#define CAPABILITY_CODE_ENHANCED_LEN    0
#define CAPABILITY_CODE_LLGR_LEN        0
#define CAPABILITY_CODE_ORF_LEN         5
#define CAPABILITY_CODE_EXT_MESSAGE_LEN 0 /* Extended Message Support */
#define CAPABILITY_CODE_ROLE_LEN        1
#define CAPABILITY_CODE_SOFT_VERSION_LEN 1
```

### 1.3 Capability相关的TLV结构

```c
/* Standard header for capability TLV */
struct capability_header {
    uint8_t code;
    uint8_t length;
};

/* Generic MP capability data */
struct capability_mp_data {
    uint16_t afi; /* iana_afi_t */
    uint8_t reserved;
    uint8_t safi; /* iana_safi_t */
};

struct graceful_restart_af {
    uint16_t afi;
    uint8_t safi;
    uint8_t flag;
};
```

## 2. BGP Path Attribute Type Code定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgpd.h`

### 2.1 BGP Path Attribute定义

```c
/* BGP message type */
#define BGP_MSG_OPEN                             1
#define BGP_MSG_UPDATE                           2
#define BGP_MSG_NOTIFY                           3
#define BGP_MSG_KEEPALIVE                        4
#define BGP_MSG_ROUTE_REFRESH_NEW                5
#define BGP_MSG_CAPABILITY                       6
#define BGP_MSG_ROUTE_REFRESH_OLD              128

/* BGP Path Attribute Type Codes */
#define BGP_ATTR_ORIGIN                          1
#define BGP_ATTR_AS_PATH                         2
#define BGP_ATTR_NEXT_HOP                        3
#define BGP_ATTR_MULTI_EXIT_DISC                 4
#define BGP_ATTR_LOCAL_PREF                      5
#define BGP_ATTR_ATOMIC_AGGREGATE                6
#define BGP_ATTR_AGGREGATOR                      7
#define BGP_ATTR_COMMUNITIES                     8
#define BGP_ATTR_ORIGINATOR_ID                   9
#define BGP_ATTR_CLUSTER_LIST                   10
#define BGP_ATTR_MP_REACH_NLRI                  14
#define BGP_ATTR_MP_UNREACH_NLRI                15
#define BGP_ATTR_EXT_COMMUNITIES                16
#define BGP_ATTR_AS4_PATH                       17
#define BGP_ATTR_AS4_AGGREGATOR                 18
#define BGP_ATTR_PMSI_TUNNEL                    22
#define BGP_ATTR_ENCAP                          23
#define BGP_ATTR_IPV6_EXT_COMMUNITIES           25
#define BGP_ATTR_AIGP                           26
#define BGP_ATTR_LARGE_COMMUNITIES              32
#define BGP_ATTR_OTC                            35
#define BGP_ATTR_PREFIX_SID                     40
#define BGP_ATTR_SRTE_COLOR                     51
#define BGP_ATTR_LINK_STATE_FLIP                56
#ifdef ENABLE_BGP_VNC_ATTR
#define BGP_ATTR_VNC                           255
#endif
```

### 2.2 Path Attribute字符串映射

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgp_attr.c`

```c
static const struct message attr_str[] = {
    {BGP_ATTR_ORIGIN, "ORIGIN"},
    {BGP_ATTR_AS_PATH, "AS_PATH"},
    {BGP_ATTR_NEXT_HOP, "NEXT_HOP"},
    {BGP_ATTR_MULTI_EXIT_DISC, "MULTI_EXIT_DISC"},
    {BGP_ATTR_LOCAL_PREF, "LOCAL_PREF"},
    {BGP_ATTR_ATOMIC_AGGREGATE, "ATOMIC_AGGREGATE"},
    {BGP_ATTR_AGGREGATOR, "AGGREGATOR"},
    {BGP_ATTR_COMMUNITIES, "COMMUNITY"},
    {BGP_ATTR_ORIGINATOR_ID, "ORIGINATOR_ID"},
    {BGP_ATTR_CLUSTER_LIST, "CLUSTER_LIST"},
    {BGP_ATTR_MP_REACH_NLRI, "MP_REACH_NLRI"},
    {BGP_ATTR_MP_UNREACH_NLRI, "MP_UNREACH_NLRI"},
    {BGP_ATTR_EXT_COMMUNITIES, "EXT_COMMUNITIES"},
    {BGP_ATTR_AS4_PATH, "AS4_PATH"},
    {BGP_ATTR_AS4_AGGREGATOR, "AS4_AGGREGATOR"},
    {BGP_ATTR_PMSI_TUNNEL, "PMSI_TUNNEL_ATTRIBUTE"},
    {BGP_ATTR_ENCAP, "ENCAP"},
    {BGP_ATTR_OTC, "OTC"},
#ifdef ENABLE_BGP_VNC_ATTR
    {BGP_ATTR_VNC, "VNC"},
#endif
    {BGP_ATTR_LARGE_COMMUNITIES, "LARGE_COMMUNITY"},
    {BGP_ATTR_PREFIX_SID, "PREFIX_SID"},
    {0}
};
```

## 3. BGP Notify Code定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgpd.h`

### 3.1 BGP Notify消息类型

```c
/* BGP notify message codes */
#define BGP_NOTIFY_HEADER_ERR                    1
#define BGP_NOTIFY_OPEN_ERR                      2
#define BGP_NOTIFY_UPDATE_ERR                    3
#define BGP_NOTIFY_HOLD_ERR                      4
#define BGP_NOTIFY_FSM_ERR                       5
#define BGP_NOTIFY_CEASE                         6
#define BGP_NOTIFY_ROUTE_REFRESH_ERR             7
```

### 3.2 BGP Header Error Sub-codes

```c
/* BGP_NOTIFY_HEADER_ERR sub codes */
#define BGP_NOTIFY_HEADER_NOT_SYNC               1
#define BGP_NOTIFY_HEADER_BAD_MESLEN             2
#define BGP_NOTIFY_HEADER_BAD_MESTYPE            3
```

### 3.3 BGP Open Error Sub-codes

```c
/* BGP_NOTIFY_OPEN_ERR sub codes */
#define BGP_NOTIFY_OPEN_MALFORMED_ATTR           0
#define BGP_NOTIFY_OPEN_UNSUP_VERSION            1
#define BGP_NOTIFY_OPEN_BAD_PEER_AS              2
#define BGP_NOTIFY_OPEN_BAD_BGP_IDENT            3
#define BGP_NOTIFY_OPEN_UNSUP_PARAM              4
#define BGP_NOTIFY_OPEN_UNACEP_HOLDTIME          6
#define BGP_NOTIFY_OPEN_UNSUP_CAPBL              7
#define BGP_NOTIFY_OPEN_ROLE_MISMATCH           11
```

### 3.4 BGP Update Error Sub-codes

```c
/* BGP_NOTIFY_UPDATE_ERR sub codes */
#define BGP_NOTIFY_UPDATE_MAL_ATTR               1
#define BGP_NOTIFY_UPDATE_UNREC_ATTR             2
#define BGP_NOTIFY_UPDATE_MISS_ATTR              3
#define BGP_NOTIFY_UPDATE_ATTR_FLAG_ERR          4
#define BGP_NOTIFY_UPDATE_ATTR_LENG_ERR          5
#define BGP_NOTIFY_UPDATE_INVAL_ORIGIN           6
#define BGP_NOTIFY_UPDATE_INVAL_NEXT_HOP         8
#define BGP_NOTIFY_UPDATE_OPT_ATTR_ERR           9
#define BGP_NOTIFY_UPDATE_INVAL_NETWORK         10
#define BGP_NOTIFY_UPDATE_MAL_AS_PATH           11
```

### 3.5 BGP Cease Sub-codes

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgpd.h`

```c
/* BGP_NOTIFY_CEASE sub codes */
#define BGP_NOTIFY_CEASE_MAX_PREFIX              1
#define BGP_NOTIFY_CEASE_ADMIN_SHUTDOWN          2
#define BGP_NOTIFY_CEASE_PEER_UNCONFIG           3
#define BGP_NOTIFY_CEASE_ADMIN_RESET             4
#define BGP_NOTIFY_CEASE_COLLISION_RESOLUTION    5
#define BGP_NOTIFY_CEASE_OUT_OF_RESOURCE         6
#define BGP_NOTIFY_CEASE_HARD_RESET              7
#define BGP_NOTIFY_CEASE_BFD_DOWN                8
```

## 4. BGP Origin定义

```c
/* BGP update origin */
#define BGP_ORIGIN_IGP                           0
#define BGP_ORIGIN_EGP                           1
#define BGP_ORIGIN_INCOMPLETE                    2
#define BGP_ORIGIN_UNSPECIFIED                 255
```

## 5. ORF (Outbound Route Filtering) 相关定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgp_open.h`

```c
/* Cooperative Route Filtering Capability */

/* ORF Type */
#define ORF_TYPE_RESERVED               0
#define ORF_TYPE_PREFIX                64

/* ORF Mode */
#define ORF_MODE_RECEIVE                1
#define ORF_MODE_SEND                   2
#define ORF_MODE_BOTH                   3
```

## 6. Graceful Restart相关定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgp_open.h`

```c
/* Graceful Restart */
#define GRACEFUL_RESTART_R_BIT 0x8000
#define GRACEFUL_RESTART_N_BIT 0x4000
#define GRACEFUL_RESTART_F_BIT 0x80

/* Long-lived Graceful Restart */
#define LLGR_F_BIT 0x80
```

## 7. ADD-PATH相关定义

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgpd.h`

```c
/* BGP addpath values */
#define BGP_ADDPATH_RX     1
#define BGP_ADDPATH_TX     2
#define BGP_ADDPATH_ID_LEN 4

#define BGP_ADDPATH_TX_ID_FOR_DEFAULT_ORIGINATE 1
```

## 8. Capability字符串映射

**文件位置：** `/home/cnic/work/own/git/frr-tvr-master/bgpd/bgp_open.c`

```c
const struct message capcode_str[] = {
    { CAPABILITY_CODE_MP, "MultiProtocol Extensions" },
    { CAPABILITY_CODE_REFRESH, "Route Refresh" },
    { CAPABILITY_CODE_ORF, "Cooperative Route Filtering" },
    { CAPABILITY_CODE_RESTART, "Graceful Restart" },
    { CAPABILITY_CODE_AS4, "4-octet AS number" },
    { CAPABILITY_CODE_ADDPATH, "AddPath" },
    { CAPABILITY_CODE_DYNAMIC, "Dynamic" },
    { CAPABILITY_CODE_ENHE, "Extended NextHop Encoding" },
    { CAPABILITY_CODE_ENHANCED_RR, "Enhanced Route Refresh" },
    { CAPABILITY_CODE_LLGR, "Long-lived Graceful Restart" },
    { CAPABILITY_CODE_ROLE, "Role" },
    { CAPABILITY_CODE_SOFT_VERSION, "Software Version" },
    { CAPABILITY_CODE_FQDN, "FQDN" },
    { CAPABILITY_CODE_EXT_MESSAGE, "Extended Message" },
    {0}
};
```

## 总结

FRR BGP代码中的TLV Type Code定义主要分布在以下文件中：

1. **`bgpd/bgp_open.h`**: BGP Capability相关的Code和Length定义
2. **`bgpd/bgpd.h`**: BGP Path Attribute、Notify Code、消息类型等核心定义
3. **`bgpd/bgp_attr.c`**: Path Attribute的字符串映射表
4. **`bgpd/bgp_open.c`**: Capability Code的字符串映射表

这种分布式的定义方式使得不同功能模块的Type Code定义相对独立，便于维护和扩展。开发者在需要添加新的BGP扩展功能时，可以根据功能类型在相应的文件中添加新的Type Code定义。

## 文档参考

- RFC 4271: A Border Gateway Protocol 4 (BGP-4)
- RFC 4760: Multiprotocol Extensions for BGP-4
- RFC 4724: Graceful Restart Mechanism for BGP
- RFC 6793: BGP Support for Four-Octet Autonomous System (AS) Numbers
- RFC 7911: Advertisement of Multiple Paths in BGP
