/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #作者：Peng Ruilin (pengruilin@huawei.com)
  #描述: virtual storage controller驱动scsi命令处理模块
********************************************************************/
#include "vsc_common.h"
#include <linux/proc_fs.h>
#if LINUX_VERSION_CODE == KERNEL_VERSION(3, 10, 0)
#include <linux/internal.h>
#endif
#include <scsi/scsi_transport.h>


#include <linux/jiffies.h>
#include <linux/time.h>

//#define __VSC_LATENCY_DEBUG__

/* 每个vbs最多使用的host */
#define MAX_HOST_NR_PER_VBS      (8)

/* 每个server最多支持的vbs */
#define MAX_VBS_NR_PER_SERVER    (8)

/* scsi host id的起始，host id=offset+vbs_id*max_host_per_vbs */
#define SCSI_HOST_BASE_OFFSET    0x10

/* 最大proc文件名 */
#define VSC_PROC_FILE_NAME_LEN    64

/* 默认的事件个数 */
#define VSC_DEFAULT_EVENT_NUMBER  8

/* abort消息等待超时时间 */
#define VSC_ABORT_EVENT_WAIT_TIME 15

/* 设备复位的事件等待时间 */
#define VSC_RESET_EVENT_WAIT_TIME 30

/* 获取数据 */
#define VSC_MODE_READ             1

/* 写入数据 */
#define VSC_MODE_WRITE            2

/* 最大的sg_count */
#define VSC_MAX_SG_COUNT          8192

/* 每个host能缓存的最大cmd个数 */
/* 每个vbs卷IODEPTH 256, 每个host下最多32个卷 */
#define VSC_MAX_CMD_NUM           (256 * 32)

/* 事件触发类型 */
enum event_shoot_type {
    EVENT_SHOOT_NOSHOOT        = 0,
    EVENT_SHOOT_SHOOT          = 1,
    EVENT_SHOOT_ABORT          = 2,
};

/* 事件队列 */ 
/* [zr] 命令队列类型 */
enum queue_type {
    VSC_LIST_REQQ              = 1,
    VSC_LIST_COMQ              = 2,
    VSC_LIST_RSPQ              = 3,
};

/* 事件所在的队列 */
enum event_list_stat {
    EVENT_LIST_STAT_INIT               = 0,
    EVENT_LIST_STAT_REQUEST            = 1,
    EVENT_LIST_STAT_READ_COMP             ,
    EVENT_LIST_STAT_RESP                  ,
};

/* 事件触发类型 */
enum event_type {
    EVENT_TIMEOUT                 = 0,
    EVENT_SHOOT                   = 1,
    EVENT_ABORT                   = 2,
};

/* 命令状态 */
enum cmd_stat {
    CMD_STAT_INIT                 = 0,
    CMD_STAT_REQUEST              = 1,
    CMD_STAT_READ_COMP            = 2,
    CMD_STAT_RESP,
};

struct vsc_event_list;
typedef int (*event_callback)(struct vsc_ctrl_info *h, enum event_type type, struct vsc_event_list *e,
                                int data_type, void *data, int data_len);
struct vsc_event_list {
    struct list_head              list;
    __u32                         event_index;           /* 事件索引 */
    __u32                         event_sn;              /* 事件序列号 */
    enum event_list_stat          stat;                  /* 事件处理状态 */
    struct vsc_ctrl_info          *h;
    int                           event_data_len;        /* 消息数据长度 */
    unsigned char                 event_data[PAGE_SIZE]; /* 事件数据 */
    unsigned long                 event_priv;            /* event私有事件 */
    event_callback                event_callback;        /* event事件回调函数 */
    wait_queue_head_t             wait;                  /* 等待处理事件 */ 
    int                           shoot;                 /* 是否触发消息 */
    atomic_t                      refcnt;                /* 结构体引用计数 */
};

struct vsc_cmnd_list {
    struct list_head              list;
    __u32                         cmd_index;             /* 命令索引 */
    enum cmd_stat                 stat;                  /* 命令状态 */
    void                          *scsi_cmd;
    __u32                         cmd_sn;                /* 命令序列号 */
    struct vsc_ctrl_info          *h;
    atomic_t                      refcnt;                /* 结构体引用计数 */

#ifdef __VSC_LATENCY_DEBUG__
    // for read: start_queue->get_scmd->back_data->back_rsp
    // for write: start_queue->get_scmd->get_data->back_rsp
    __u64 start_queue_time;
    __u64 get_scmd_time;
    __u64 get_data_time;
    __u64 back_data_time;
    __u64 back_rsp_time;
#endif
};

struct vsc_ctlr_statics {
    unsigned int                  reset_dev_count;       /* 复位请求的计数 */
    unsigned int                  abort_cmd_count;       /* 放弃命令的请求计数 */
    unsigned int                  read_fail_count;       /* vsc文件读取失败次数 */
    unsigned int                  write_fail_count;      /* vsc文件写入失败次数 */
};

struct vsc_host_mgr;

struct vsc_ctrl_info {
    spinlock_t                    lock;
    //struct list_head              list;
    struct Scsi_Host              *scsi_host;            /* scsi_host结构体指针 */
    int                           vbs_host_id;           /* vbs通过port算出的host id，和往os注册的host id不一样 */
    struct vsc_host_mgr           *silbing_hosts;        /* 和当前host属于同一个vbs的host链表 */

    int                           nr_cmds;               /* 缓冲区scsi命令个数 */

    unsigned int                  io_running;            /* host中正在处理的所有命令数 */

    struct list_head              reqQ;                  /* 请求命令队列 */
    struct list_head              cmpQ;                  /* 读取完成命令队列 */
    struct list_head              rspQ;                  /* 等待回应队列 */
    unsigned int                  Qdepth;
    __u64                         cmd_sn;                /* 命令序列号 */
    struct vsc_cmnd_list          **cmd_pool_tbl;        /* 队列缓冲池 */
    unsigned long                 *cmd_pool_bits;        /* 命令缓冲池使用映射表 */

    struct list_head              event_reqQ;            /* 事件请求队列 */
    struct list_head              event_rspQ;            /* 事件回应队列 */
    int                           nr_events;             /* 事件个数 */
    __u64                         event_sn;              /* 事件序列号 */
    struct vsc_event_list         **event_pool_tbl;      /* 事件队列 */
    unsigned long                 *event_pool_bits;      /* 事件缓冲池使用映射表 */

    wait_queue_head_t             queue_wait;            /* 处理事件 */ 

    unsigned int                  suspend;               /* 数据发送挂起 */         

    struct file                   *file;                 /* 打开的文件信息 */
    int                           wakeup_pid;            /* 退出时是否需要唤醒进程 */
    struct vsc_ctlr_statics       stat;

    struct timer_list             target_abnormal_timer; /* 进程异常定时器 */
    __u32                         target_abnormal_time;  /* 进程异常容忍时间 */
    atomic_t                      target_abnormal_lock;  /* 是否正在进行attach或timer */

    unsigned int get_cmd_times;  /*取cmd次数*/
    unsigned int get_cmd_total;  /*取cmd总数*/
    unsigned int get_cdb_times;  /*取cdb次数*/
    unsigned int get_cdb_total;  /*取cdb总数*/
    unsigned int put_rsp_times;  /*响应次数*/
    unsigned int put_rsp_total;  /*响应命令总数*/

#ifdef __VSC_LATENCY_DEBUG__
    __u64 write_cmd_count;
    __u64 read_cmd_count;

    __u64 write_stage1_total;
    __u64 write_stage2_total;
    __u64 write_stage3_total;
    __u64 write_total;

    __u64 read_stage1_total;
    __u64 read_stage2_total;
    __u64 read_stage3_total;
    __u64 read_total;
#endif
};

/* 分vbs记录所有的host */
struct vsc_host_mgr {
    struct list_head node;
    __u32 vbs_id; // 通过vbs注册的host id生成
    __u32 host_count; // 该vbs注册的host个数
    struct vsc_ctrl_info* host_list[MAX_HOST_NR_PER_VBS];
};

/* 将每个vbs的管理结构串起来 */
static struct list_head g_host_mgr_list;

/* scsi多命令合并后，需要大块内存，使用栈内存不合适，引入slab */
static struct kmem_cache *scsi_buf_slab = NULL;
static mempool_t *scsi_buf_pool = NULL;
#define MIN_SCSI_CMD_RESERVED    2048

static DEFINE_MUTEX(ctrl_info_list_lock);
static DEFINE_MUTEX(ctrl_host_reg_lock);

/* proc目录 */
static struct proc_dir_entry *proc_vsc_root = NULL;

#ifdef DRIVER_VER
static char banner[] __initdata = KERN_INFO "Huawei Cloudstorage virtual storage controller driver "DRIVER_VER" initialized.\n";
#else
static char banner[] __initdata = KERN_INFO "Huawei Cloudstorage virtual storage controller driver initialized.\n";
#endif

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18)) 
    #define QUEUE_FLAG_BIDI        7    /* queue supports bidi requests */
#endif

static uint io_per_host = 4;
module_param(io_per_host, uint, 0644);
MODULE_PARM_DESC(io_per_host, "IOs'll be send to one host when enable multi-fd,0-disable,0|1|2|4");

uint io_per_host_shift = 2;

/**************************************************************************
 功能描述  : host_info参数检查，在register host的时候调用
 参    数  : struct hlist_head *list  hash链表
             vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline int check_host_info(struct vsc_ioctl_cmd_add_host *host_info)
{
    if (unlikely(!host_info)) {
        return -EFAULT;
    }

    if (host_info->max_cmd_len > VSC_MAX_CDB_LEN
        || host_info->max_cmd_len <= 0
        || host_info->sg_count > VSC_MAX_SG_COUNT 
        || host_info->sg_count <= 0
        || host_info->max_nr_cmds > VSC_DEFAULT_MAX_CMD_NUMBER
        || host_info->max_nr_cmds <= 0
        || host_info->cmd_per_lun > VSC_DEFAULT_LUN_CMD_NUMBER 
        || host_info->cmd_per_lun <= 0
        || host_info->cmd_per_lun > host_info->max_nr_cmds
        || host_info->max_channel <= 0
        || host_info->max_id <= 0
        || host_info->max_lun <= 0) {
        return -EINVAL;
    }
    return 0;
}


/**************************************************************************
 功能描述  : 将事件添加到队列 
 参    数  : struct hlist_head *list  hash链表
             vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void add_eventQ(struct list_head *list, struct vsc_event_list *c)
{
    list_add_tail(&c->list, list);
}

/**************************************************************************
 功能描述  : 将事件从队列中删除
 参    数  : vsc_cmnd_list *c       scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void remove_eventQ(struct vsc_event_list *c)
{
    list_del_init(&c->list);
}

/**************************************************************************
 功能描述  : 将命令加入到队列中  
 参    数  : struct list_head *list   hash链表
             vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void addQ(struct list_head *list, struct vsc_cmnd_list *c)
{
    list_add_tail(&c->list, list);
}

/**************************************************************************
 功能描述  : 从队列中删除命令 
 参    数  : vsc_cmnd_list *c       scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void removeQ(struct vsc_cmnd_list *c)
{
    list_del_init(&c->list);
}

static inline int list_size(struct list_head *list)
{
    int count = 0;
    struct list_head *temp;

    list_for_each(temp, list)
    {
        count++;
    }

    return count;
}

/* VBS传过来的host id，bit 15~8 vbs id(port)，bit 7~0 host index within vbs */
static inline __u32 vsc_get_vbs_id_by_host_id(__u32 host_id)
{
    //return (host_id - SCSI_HOST_BASE_OFFSET) / MAX_HOST_NR_PER_VBS - 1;
    return (host_id >> 8) & 0xFFFF;
}

static inline __u32 vsc_get_host_index_in_vbs(__u32 host_id)
{
    //return host_id % MAX_HOST_NR_PER_VBS;
    return host_id & 0xFF;
}

static inline __u64 vsc_get_usec(void)
{
    struct timeval tv;
    do_gettimeofday(&tv);

    return tv.tv_sec * 1000000 + tv.tv_usec;
}

/*static void vsc_debug_buf(char *buffer, int len)
{    
    char temp[128];// 单行输出16个字节，128个字节用于组装输出结果    
    int temp_len;    
    int out_len = len/16*16;    
    int i,j;     

    // 按16个字节一行，循环输出 
    for (i=0;i<out_len;i+=16){        
        temp_len=0;        
        for (j=0;j<16;j++){            
            temp_len += sprintf(temp+temp_len, "%02x ", buffer[i+j]&0xFF);        
        }        
        vsc_err("%s\n", temp);    
    }     

    // 输出最后不足16个字节的一行   
    temp_len=0; 
    for (i=out_len;i<len;i++){       
        temp_len += sprintf(temp+temp_len, "%02x ", buffer[i]&0xFF);    
    }   

    vsc_err("%s\n", temp);
} */  

/**************************************************************************
 功能描述  : 申请可用的事件缓冲区
 参    数  : struct list_head *list  链表
 返 回 值  : vsc_event_list*         可用的事件缓冲区
**************************************************************************/
static struct vsc_event_list *vsc_event_alloc(struct vsc_ctrl_info *h)
{
    int i;
    struct vsc_event_list *e = NULL;

    if (unlikely(!h)) {
        return NULL;
    }

    /* 按照bit位查找可用的缓冲区 */
    do {
        i = find_first_zero_bit(h->event_pool_bits, h->nr_events);
        /*
          * [zr]
          * x86 下没有找到0 bit 时返回size; 
          * arm 下没有找到0 bit 时返回size+1
          * 所以比较通用的方式应该判断(i >= h->nr_cmds)
          */
        if (i == h->nr_events)
            return NULL;
    } while (test_and_set_bit
         (i & (BITS_PER_LONG - 1),
          h->event_pool_bits + (i / BITS_PER_LONG)) != 0);

    /* 按空闲的bit位，获取可用的命令缓冲区 */
    e = h->event_pool_tbl[i];
    memset(e, 0, sizeof(*e));

    /* 初始化数据 */
    INIT_LIST_HEAD(&e->list);
    e->event_index = i;
    e->stat = EVENT_LIST_STAT_INIT;
    e->h = h;
    h->event_sn++;
    e->event_sn = (__u32)h->event_sn;
    e->event_data_len = 0;
    e->shoot = EVENT_SHOOT_NOSHOOT;
    atomic_set(&e->refcnt, 1);    
    init_waitqueue_head(&e->wait);
    
    return e;
}

/**************************************************************************
 功能描述  : 获取引用计数 
 参    数  : struct list_head *list  链表
             vsc_event_list *e       事件控制句柄
 返 回 值  : 无
**************************************************************************/
static void vsc_event_get(struct vsc_event_list *e)
{
    if (unlikely(!e)) {
        return ;
    }

    atomic_inc(&e->refcnt);
}

/**************************************************************************
 功能描述  : 释放缓冲区 
 参    数  : struct list_head *list  链表
             vsc_event_list *e       事件控制句柄
 返 回 值  : 无
**************************************************************************/
static void vsc_event_put(struct vsc_event_list *e)
{
    int i;
    struct vsc_ctrl_info *h = NULL;

    if (unlikely(!e) ) {
        return ;
    }

    h = e->h;
    /* 引用计数递减 */
    if (atomic_dec_and_test(&e->refcnt)) {
        if (e->event_index >= h->nr_events) {
            return;
        }

        /* 计算出命令的位置，并清除对应的位表*/
        i = e->event_index;

        clear_bit(i & (BITS_PER_LONG - 1), h->event_pool_bits + (i / BITS_PER_LONG));
    }
}


/**************************************************************************
 功能描述  : 添加事件
 参    数  : struct vsc_event_list *e 事件指针
             callback                 事件回调函数
             event_data               事件数据
             event_data_len           事件数据长度
             event_priv               事件私有数据
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
static int vsc_event_add(struct vsc_event_list *e, event_callback callback, int event_type, 
                            void *event_data, int event_data_len, unsigned long event_priv)
{
    struct vsc_ctrl_info *h;
    struct vsc_scsi_msg_data *msg_data;

    if (unlikely(!e) || unlikely(!event_data) || !callback) {
        return -EFAULT;
    }

    /* 检查有效性 */
    if (event_data_len >= sizeof(e->event_data) - sizeof(*msg_data)) {
        return -EINVAL;
    }

    h = e->h;

    msg_data = (struct vsc_scsi_msg_data *)(e->event_data);
    /* 复制事件数据 */
    memcpy(msg_data->data, event_data, event_data_len);
    msg_data->data_len = event_data_len;
    msg_data->data_type = event_type;
    e->event_data_len = event_data_len + sizeof(*msg_data);

    /*注册事件参数 */
    e->event_callback = callback;
    e->event_priv = event_priv;

    /* 唤醒用户态程序获取事件 */
    spin_lock_bh(&h->lock);
    add_eventQ(&e->h->event_reqQ, e);
    spin_unlock_bh(&h->lock);
    wake_up_interruptible(&e->h->queue_wait);  

    return 0;
}

/**************************************************************************
 功能描述  : 事件唤醒函数
 参    数  : struct vsc_event_list *e 事件指针
             time                     超时时间
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
static int vsc_event_wait_timeout(struct vsc_event_list *e, unsigned long time)
{
    int retval;
    
    vsc_event_get(e);
    retval = wait_event_timeout(e->wait, e->shoot, time);
    if (0 == retval) {
        /* 等待超时，处理函数*/
        if (e->event_callback) {
            e->event_callback(e->h, EVENT_TIMEOUT, e, 0, NULL, 0);
        }
    }

    /* 如果是命令ABORT，则返回ABORT */
    if (EVENT_SHOOT_ABORT == e->shoot) {
        if (e->event_callback) {
            e->event_callback(e->h, EVENT_ABORT, e, 0, NULL, 0);
        }
    }

    vsc_event_put(e);

    return retval;
}

/**************************************************************************
 功能描述  : 事件删除
 参    数  : struct vsc_event_list *e 事件指针
 返 回 值  : 
**************************************************************************/
static int vsc_event_del(struct vsc_event_list *e)
{
    struct vsc_ctrl_info *h = NULL;

    if (unlikely(!e)) {
        return -EFAULT;
    }

    /* 唤醒事件 */
    if (waitqueue_active(&e->wait)) {
        e->shoot = EVENT_SHOOT_ABORT;
        wake_up(&e->wait);
    }

    h = e->h;
    spin_lock_bh(&h->lock);
    if (!list_empty(&e->list)) {
        remove_eventQ(e);
    }
    spin_unlock_bh(&h->lock);

    vsc_event_put(e);

    return 0;
}

/**************************************************************************
 功能描述  : 退出所有event事件
 参    数  : struct vsc_ctlr_info *h  host控制句柄
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_abort_all_event(struct vsc_ctrl_info *h)
{
    struct vsc_event_list *e;

    if (unlikely(!h) ) {
        return -EINVAL;
    }
    
    spin_lock_bh(&h->lock);
    while (!list_empty(&h->event_reqQ)) {
        e = list_first_entry(&h->event_reqQ, struct vsc_event_list, list);
        remove_eventQ(e);
        spin_unlock_bh(&h->lock);
        vsc_event_del(e);
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);

    spin_lock_bh(&h->lock);
    while (!list_empty(&h->event_rspQ)) {
        e = list_first_entry(&h->event_rspQ, struct vsc_event_list, list);
        remove_eventQ(e);
        spin_unlock_bh(&h->lock);
        vsc_event_del(e);
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);

    return 0;
}


/**************************************************************************
 功能描述  : 申请可用的缓冲区
 参    数  : struct list_head *list  链表
 返 回 值  : vsc_cmd_list*            可用的命令缓冲区
**************************************************************************/
static struct vsc_cmnd_list *vsc_cmd_alloc(struct vsc_ctrl_info *h)
{
    int i;
    struct vsc_cmnd_list *c = NULL;

    if (unlikely(!h)) {
        return NULL;
    }

    /* 按照bit位查找可用的缓冲区 */
    do {
        i = find_first_zero_bit(h->cmd_pool_bits, h->nr_cmds);
        /*
                * [zr]
                * x86 下没有找到0 bit 时返回size; 
                * arm下没有找到0 bit 时返回size+1  //implemented in arch/arm/lib/findbit.S
                * 所以比较通用的方式应该判断(i >= h->nr_cmds)
                */
        if (i == h->nr_cmds)
            return NULL;
    } while (test_and_set_bit
         (i & (BITS_PER_LONG - 1),
          h->cmd_pool_bits + (i / BITS_PER_LONG)) != 0);

    /* 按空闲的bit位，获取可用的命令缓冲区 */
    c = h->cmd_pool_tbl[i];

    /* 初始化数据 */
    INIT_LIST_HEAD(&c->list);
    c->cmd_index = i;
    c->stat = CMD_STAT_INIT;
    c->h = h;
    c->scsi_cmd = NULL;
    h->cmd_sn++;
    c->cmd_sn = (__u32)h->cmd_sn;
    atomic_set(&c->refcnt, 1);  
    
    return c;
}

/**************************************************************************
 功能描述  : 获取命令结构引用计数 
 参    数  : struct list_head *list  链表
             vsc_event_list *e       事件控制句柄
 返 回 值  : 无
**************************************************************************/
static void vsc_cmd_get(struct vsc_cmnd_list *c)
{
    if (unlikely(!c)) {
        return ;
    }

    atomic_inc(&c->refcnt);
}

/**************************************************************************
 功能描述  : 释放缓冲区 
 参    数  : vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static void vsc_cmd_put(struct vsc_cmnd_list *c)
{
    int i;
    struct vsc_ctrl_info *h = NULL;

    if (unlikely(!c) ) {
        return ;
    }

    h = c->h;
    /* 引用计数递减 */
    if (atomic_dec_and_test(&c->refcnt)) {
        if (c->cmd_index >= h->nr_cmds) {
            return;
        }

        /* 计算出命令的位置，并清除对应的位表*/
        i = c->cmd_index;
        
        clear_bit(i & (BITS_PER_LONG - 1), h->cmd_pool_bits + (i / BITS_PER_LONG));
    }
}

void vsc_debug_host(struct vsc_ctrl_info *h)
{
    struct vsc_cmnd_list *c = NULL;
    struct list_head *temp = NULL;

    if (!h)
        return;

    vsc_err("*************************Req Queue*************************\n");
    spin_lock_bh(&h->lock);
    list_for_each (temp, &h->reqQ) {
        c = list_entry(temp, struct vsc_cmnd_list, list);

        vsc_err("host = %u, cmd_sn = %d, tag = %d cmd_stat = %d\n",
            vsc_ctrl_get_host_no(h), c->cmd_sn, c->cmd_index, c->stat);

    }
    spin_unlock_bh(&h->lock);

    vsc_err("*************************Cmp Queue*************************\n");
    spin_lock_bh(&h->lock);
    list_for_each (temp, &h->cmpQ) {
        c = list_entry(temp, struct vsc_cmnd_list, list);

        vsc_err("host = %u, cmd_sn = %d, tag = %d cmd_stat = %d\n",
            vsc_ctrl_get_host_no(h), c->cmd_sn, c->cmd_index, c->stat);

    }
    spin_unlock_bh(&h->lock);

    vsc_err("*************************Rsp Queue*************************\n");
    spin_lock_bh(&h->lock);
    list_for_each (temp, &h->rspQ) {
        c = list_entry(temp, struct vsc_cmnd_list, list);

        vsc_err("host = %u, cmd_sn = %d, tag = %d cmd_stat = %d\n",
            vsc_ctrl_get_host_no(h), c->cmd_sn, c->cmd_index, c->stat);

    }
    spin_unlock_bh(&h->lock);
}

void vsc_debug_cmd(int host_no)
{
    __u32 i;
    struct vsc_host_mgr *mgr = NULL;
    __u32 vbs_id = vsc_get_vbs_id_by_host_id(host_no);
    __u32 host_index = vsc_get_host_index_in_vbs(host_no);

    if (host_index >= MAX_HOST_NR_PER_VBS) {
        return;
    }

    /* 输出跟指定host同一个vbs的host的cmd信息 */
    mutex_lock(&ctrl_info_list_lock);
    list_for_each_entry(mgr, &g_host_mgr_list, node) {
        if (mgr->vbs_id == vbs_id && mgr->host_count <= MAX_HOST_NR_PER_VBS) {
            for (i=0;i<mgr->host_count;i++){
                vsc_debug_host(mgr->host_list[i]);
            }

            mutex_unlock(&ctrl_info_list_lock);
            return;
        }
    }
    mutex_unlock(&ctrl_info_list_lock);
}

/**************************************************************************
 功能描述  : 获取ctrl_info指针
 参    数  : struct scsi_device *sdev       scsi设备指针
 返 回 值  : struct vsc_ctlr_info *         host控制句柄
**************************************************************************/
static inline struct vsc_ctrl_info *sdev_to_ctrl_info(struct scsi_device *sdev)
{
    return (struct vsc_ctrl_info *) (shost_priv(sdev->host));
}

/**************************************************************************
 功能描述  : 查询scsi命令对应的struct vsc_cmnd_list *c指针
 参    数  : struct scsi_cmnd *sc   sc命令指针
 返 回 值  : struct vsc_cmnd_list *c指针
**************************************************************************/
static inline struct vsc_cmnd_list *sc_to_cmnd_list(struct scsi_cmnd *sc)
{
    return (struct vsc_cmnd_list *)sc->host_scribble;
}

/**************************************************************************
 功能描述  : 挂起host
 参    数  : svsc_ctlr_info *h:         host控制句柄
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
int vsc_suspend_host(struct vsc_ctrl_info *h)
{
    if (!h) {
        return -EBADF;
    }

    scsi_block_requests(h->scsi_host);
    h->suspend = 1;

    return 0;
}

/**************************************************************************
 功能描述  : 恢复host
 参    数  : svsc_ctlr_info *h:         host控制句柄
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
int vsc_resume_host(struct vsc_ctrl_info *h)
{
    if (!h) {
        return -EBADF;
    }

    scsi_unblock_requests(h->scsi_host);
    h->suspend = 0;

    return 0;
}

/**************************************************************************
 功能描述  : poll_wait接口
 参    数  : file *file:                file 文件句柄
             svsc_ctlr_info *h:         host控制句柄
             poll_table *wait          poll wait参数
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
unsigned int vsc_poll_wait(struct file *file, poll_table *wait)
{
    unsigned int             mask = 0;
    struct vsc_ctrl_info *h = NULL;

    if (!file ) {
        /* Here should return 0 on error [zr] */
        //return -EFAULT;  
        return 0;
    }

    h = file->private_data;
    if (!h) {
        return 0;
    }

    /* 将select进程加入监控队列 */
    poll_wait(file, &h->queue_wait, wait);
    
    /* 如果有数据可以读，则设置可读标志 */
    spin_lock_bh(&h->lock);
    if (!list_empty(&h->cmpQ) || !list_empty(&h->event_reqQ)) {
        mask |= (POLLIN | POLLRDNORM); 
    }
    spin_unlock_bh(&h->lock);

    return mask;
}

/**************************************************************************
 功能描述  : 将scsi命令加入队列
 参    数  : svsc_ctlr_info *h:     host控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void vsc_start_io(struct vsc_ctrl_info *h)
{
    struct vsc_cmnd_list *c;

    spin_lock_bh(&h->lock);
    while (!list_empty(&h->reqQ)) {
        /* 从请求队列中获取第一个请求 */
        c = list_first_entry(&h->reqQ, struct vsc_cmnd_list, list);

        /* 从请求队列中删除数据 */
        removeQ(c);
        h->Qdepth--;

        /* 通知scsi处理任务执行scsi命令 */
        c->stat = CMD_STAT_REQUEST;
        addQ(&h->cmpQ, c);
        spin_unlock_bh(&h->lock);

        if (waitqueue_active(&h->queue_wait)) {
            wake_up_interruptible(&h->queue_wait);
        }
        /* [zr] 这里每挂一个IO到cmpQ都要重新获取锁,读取线程每从cmpQ上摘下一个IO挂到rspQ上也需要获取锁,
                 这样会不会导致queuecommand耗时较长? */
        /* --SMP下每次queuecommand进来可能多个scsi_cmnd, 这样处理可以尽可能减少锁持有的时间, 会提高性能 */
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);
}

static inline void vsc_start_io_new(struct vsc_ctrl_info *h)
{
    struct vsc_cmnd_list *c;
    struct list_head temp;
    struct list_head *node = NULL;

    INIT_LIST_HEAD(&temp);

    spin_lock_bh(&h->lock);
    list_replace_init(&h->reqQ, &temp);
    h->Qdepth = 0;
    spin_unlock_bh(&h->lock);

    list_for_each(node, &temp) {
        /* 从请求队列中获取第一个请求 */
        c = list_entry(node, struct vsc_cmnd_list, list);

        /* 通知scsi处理任务执行scsi命令 */
        c->stat = CMD_STAT_REQUEST;
    }

    spin_lock_bh(&h->lock);
    list_splice_tail(&temp, &h->cmpQ);
    if (waitqueue_active(&h->queue_wait)) {
        wake_up_interruptible(&h->queue_wait);
    }
    spin_unlock_bh(&h->lock);
}

static inline int vsc_is_readwrite_cmd(unsigned char cmd_type)
{
    switch (cmd_type) {
        case WRITE_6:
        case WRITE_10:
        case WRITE_12:
        case WRITE_16:
        case READ_6:
        case READ_10:
        case READ_12:
        case READ_16:
            return 1;
        default:
            return 0;
    }
}

static inline int vsc_is_read_cmd(unsigned char cmd_type)
{
    switch (cmd_type) {
        case READ_6:
        case READ_10:
        case READ_12:
        case READ_16:
            return 1;
        default:
            return 0;
    }
}

static inline int vsc_is_write_cmd(unsigned char cmd_type)
{
    switch (cmd_type) {
        case WRITE_6:
        case WRITE_10:
        case WRITE_12:
        case WRITE_16:
            return 1;
        default:
            return 0;
    }
}

/**************************************************************************
 功能描述  : 将scsi命令加入队列
 参    数  : svsc_ctlr_info *h:     host控制句柄
             vsc_cmnd_list *c       scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static inline void vsc_enqueue_cmd_and_start_io(struct vsc_ctrl_info *h, struct vsc_cmnd_list *c)
{
    spin_lock_bh(&h->lock);
    addQ(&h->reqQ, c);
    h->Qdepth++;
    h->io_running++;
    spin_unlock_bh(&h->lock);
    /* [zr] 这中间会不会有其他的线程往h->reqQ上挂I/O请求? */
    /* --SMP, 会有多个CPU同时挂 */
    vsc_start_io(h);
}

/**************************************************************************
 功能描述  : vsc scsi 命令处理函数
 参    数  : struct scsi_cmnd *sc        scsi命令数据指针
             done                         scsi操作结果中层通知回调函数
 返 回 值  : 0: 成功，其他：scsi中层错误码
**************************************************************************/

#ifdef DEF_SCSI_QCMD
static int vsc_scsi_queue_command_lck(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *)) 
#else
static int vsc_scsi_queue_command(struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *)) 
#endif
{
    struct vsc_ctrl_info *orig_h = NULL;
    struct vsc_ctrl_info *h = NULL;
    struct vsc_cmnd_list *c = NULL;
    __u64 sn = 0;
    __u32 host_index = 0;
    struct vsc_host_mgr *silbing_hosts = NULL;
    struct vsc_ioctl_disk_vol_name *vol_name = NULL;
    struct scsi_device *sdev = sc->device;
    
    orig_h = sdev_to_ctrl_info(sdev);
    if (unlikely(!orig_h)) {
        return FAILED;
    }

    if (unlikely(VSC_DISK_PREPARE_DELETE ==
                 ((struct vsc_ioctl_disk_vol_name *)(&sdev->sdev_data[0]))->state)) {
        sc->result = DID_NO_CONNECT << 16;
        done(sc);
        return 0;
    }

    /* [zr] 这里为什么要释放host_lock? */
    /* --为了减少关中断的时间, 中层在调queuecommand之间已经关了中断 */
    spin_unlock_irq(orig_h->scsi_host->host_lock);

    /* io_per_host = 0: 单fd模式
       io_per_host > 0: 多fd模式，io被轮流发送给host_per_vbs个host，每个host发送io_per_host个io
       如果io running非0，使用多fd；如果io running是0，使用单fd，时延更好
    */
    if (likely(io_per_host > 0)) {
        silbing_hosts = orig_h->silbing_hosts;
        vol_name = (struct vsc_ioctl_disk_vol_name *)(&sdev->sdev_data[0]);
            
        if (unlikely(!silbing_hosts || !vol_name)) {
            vsc_err("silbing_hosts=%p, vol_name=%p", silbing_hosts, vol_name);
            /* 前面已经解锁，这里退到scsi层之前必须加锁 */
            spin_lock_irq(orig_h->scsi_host->host_lock);
            return FAILED;
        }

        /* 非读写命令默认在0号线程处理，和vbs保持一致 */
        if (likely(vsc_is_readwrite_cmd(sc->cmnd[0]))) {
            /* 每个卷维护一个计数器，保证每个卷的io在各个线程上均衡 */
            sn = atomic64_add_return(1, (atomic64_t*)&vol_name->extend);

            /* io轮流落在同一个vbs的不同host上，每个host上发一批 */
            host_index = ((__u32)sn >> io_per_host_shift) % silbing_hosts->host_count;
            if (unlikely(host_index >= MAX_HOST_NR_PER_VBS)) {
                vsc_err("invalid host_index=%d, sn=%llu, host_count=%d", host_index, sn, silbing_hosts->host_count);
                /* 前面已经解锁，这里退到scsi层之前必须加锁 */
                spin_lock_irq(orig_h->scsi_host->host_lock);
                return FAILED;
            }
        }
        else {
            host_index = 0;
        }
        h = silbing_hosts->host_list[host_index];
    }
    else {
        h = orig_h;
    }
    
    if (unlikely(!h)) {
        /* 前面已经解锁，这里退到scsi层之前必须加锁 */
        spin_lock_irq(orig_h->scsi_host->host_lock);
        return FAILED;
    }

    /* 获取可用的缓冲区 */
    spin_lock_bh(&h->lock);
    c = vsc_cmd_alloc(h);
    spin_unlock_bh(&h->lock);
    if (unlikely(c == NULL)) {
        spin_lock_irq(orig_h->scsi_host->host_lock);
        return SCSI_MLQUEUE_HOST_BUSY;
    }
   
    /* 保存命令完成的回调通知函数地址 */
    sc->scsi_done = done;    

    /* 保存命令地址，以便命令ABORT的时候使用 */
    sc->host_scribble = (unsigned char *) c;
    c->scsi_cmd = sc;
#ifdef __VSC_LATENCY_DEBUG__
    c->start_queue_time = vsc_get_usec();
#endif

    /* 将scsi命令加入处理队列中 */
    vsc_enqueue_cmd_and_start_io(h, c);

    spin_lock_irq(orig_h->scsi_host->host_lock);

    return 0;
}
#ifdef DEF_SCSI_QCMD
static DEF_SCSI_QCMD(vsc_scsi_queue_command)
#endif
/**************************************************************************
 功能描述  : scsi设备扫描开始回调函数
 参    数  : Scsi_Host *sh:     scsi设备扫描结束回调函数
             elapsed_time       耗费时间
 返 回 值  : 1： 成功，0：继续等待
**************************************************************************/
static int vsc_scan_finished(struct Scsi_Host *sh, unsigned long elapsed_time)
{
    return 1; 
}

/**************************************************************************
 功能描述  scsi设备扫描开始回调函数
 参    数  : Scsi_Host *sh:     scsi host控制句柄
 返 回 值  : 无
**************************************************************************/
static void vsc_scan_start(struct Scsi_Host *sh)
{
    return;
}

/**************************************************************************
 功能描述  : 遍历队列中的所有命令，并从队列中删除
 参    数  : vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_each_queue_remove(struct vsc_ctrl_info *h, void (*callback)
            (struct vsc_ctrl_info *, struct vsc_cmnd_list *, enum queue_type) )
{
    struct vsc_cmnd_list *c = NULL;

    /* 循环处理reqQ队列中的scsi命令 */
    spin_lock_bh(&h->lock);
    while (!list_empty(&h->reqQ)) {
        c = list_first_entry(&h->reqQ, struct vsc_cmnd_list, list);
        removeQ(c);
        h->io_running--;
        spin_unlock_bh(&h->lock);
        vsc_cmd_get(c);
        callback(h, c, VSC_LIST_REQQ);
        vsc_cmd_put(c);
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);
    

    /* 循环处理cmpQ队列中的scsi命令 */
    spin_lock_bh(&h->lock);
    while (!list_empty(&h->cmpQ)) {
        c = list_first_entry(&h->cmpQ, struct vsc_cmnd_list, list);
        removeQ(c);
        h->io_running--;
        spin_unlock_bh(&h->lock);
        vsc_cmd_get(c);
        callback(h, c, VSC_LIST_COMQ);
        vsc_cmd_put(c);
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);

    /* 循环处理rspQ队列中的scsi命令 */
    spin_lock_bh(&h->lock);
    while (!list_empty(&h->rspQ)) {
        c = list_first_entry(&h->rspQ, struct vsc_cmnd_list, list);
        removeQ(c);
        h->io_running--;
        spin_unlock_bh(&h->lock);
        vsc_cmd_get(c);
        callback(h, c, VSC_LIST_RSPQ);
        vsc_cmd_put(c);
        spin_lock_bh(&h->lock);
    }
    spin_unlock_bh(&h->lock);

    return 0;
}

/**************************************************************************
 功能描述  : abort命令队列的回调函数
 参    数  : struct vsc_ctlr_info *h  host控制句柄
             vsc_cmnd_list *c         scsi命令控制句柄
             list_type                命令所属链表类型
 返 回 值  : 无
**************************************************************************/
static void vsc_abort_cmd_callback(struct vsc_ctrl_info *h, struct vsc_cmnd_list *c, enum queue_type list_type)
{
    struct scsi_cmnd *sc = NULL;

    if (unlikely(!c) || unlikely(!h) ) {
        return;
    }
    
    //vsc_err("Abort command, ");
    switch (list_type) {
    case VSC_LIST_REQQ:
        vsc_err("Abort host:%u reqQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        h->Qdepth--;
        break;
    case VSC_LIST_COMQ:
        vsc_err("Abort host:%u cmpQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        break;
    case VSC_LIST_RSPQ:
        vsc_err("Abort host:%u rspQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        break;
    default:
        break;
    }
    sc = c->scsi_cmd;
    if (!sc)
    {
        vsc_err("fatal error, host:%u sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        vsc_cmd_put(c);
        return;
    }

    sc->result = DID_SOFT_ERROR << 16;
    /* 通知上层软件错误，重试 */
    sc->scsi_done(sc);

    /* 释放所有命令 */
    vsc_cmd_put(c);

    return;
}


/**************************************************************************
 功能描述  : 退出所有scsi命令的处理
 参    数  : struct vsc_ctlr_info *h  host控制句柄
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_abort_all_cmd(struct vsc_ctrl_info *h)
{
    if (unlikely(!h) ) {
        return -EINVAL;
    }

    return vsc_each_queue_remove(h, vsc_abort_cmd_callback);
}

/**************************************************************************
 功能描述  : abort命令队列的回调函数
 参    数  : struct vsc_ctlr_info *h  host控制句柄
             vsc_cmnd_list *c         scsi命令控制句柄
             list_type                命令所属链表类型
 返 回 值  : 无
**************************************************************************/
static void vsc_requeue_cmd_callback(struct vsc_ctrl_info *h, struct vsc_cmnd_list *c, enum queue_type list_type)
{
    struct scsi_cmnd *sc = NULL;

    //printk("Requeue command, ");
    switch (list_type) {
    case VSC_LIST_REQQ:
        vsc_err("Requeue host:%u reqQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        h->Qdepth--;
        break;
    case VSC_LIST_COMQ:
        vsc_err("Requeue host:%u cmpQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        break;
    case VSC_LIST_RSPQ:
        vsc_err("Requeue host:%u rspQ: sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        break;
    default:
        break;
    }

    sc = c->scsi_cmd;
    if (!sc)
    {
        vsc_err("fatal error, host:%u sn:%u, stat = %u\n", vsc_ctrl_get_host_no(h), c->cmd_sn, c->stat);
        vsc_cmd_put(c);
        return;
    }

    sc->result = DID_REQUEUE << 16;
    /* 回应命令处理方式为重新入队列 */
    sc->scsi_done(sc);

    /* 释放所有命令 */
    vsc_cmd_put(c);

    return;
}

/**************************************************************************
 功能描述  : 重新将所有命令加入队列 
 参    数  : vsc_cmnd_list *c         scsi命令控制句柄
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_requeue_all_cmd(struct vsc_ctrl_info *h)
{
    if (unlikely(!h) ) {
        return -EINVAL;
    }

    return vsc_each_queue_remove(h, vsc_requeue_cmd_callback);
}

/**************************************************************************
 功能描述  : abort事件回调函数
 参    数  : vsc_ctlr_info *h            host控制句柄
             enum event_type type        事件响应类型
             vsc_event_list *e           事件指针
             data_type                   事件数据类型
             data                        事数据指针
             data_len                    事件数据长度
 返 回 值  : 0: 成功, 其他: 错误码
**************************************************************************/
static int vsc_abort_event_callback(struct vsc_ctrl_info *h, enum event_type type,
                    struct vsc_event_list *e, int data_type, void *data, int data_len)
{
    struct vsc_event_result *abort_rsp;
    /* 如果超时，或者主动释放事件，则返回SUCCESS*/
    if (EVENT_TIMEOUT == type || EVENT_ABORT == type) {
        spin_lock_bh(&h->lock);
        if (!h->file) {
            /* 如果host没有被接管，则返回SUCCESS，通知SCSI上层重试 */
            e->event_priv = SUCCESS;
        }
        /* <zr> 正常情况下host是被接管了的, 这时候直接给ML回abort失败? */
        spin_unlock_bh(&h->lock);
        return 0;
    }

    // [zr] EVENT_SHOOT == type, when go here, that means triggered by user write event rsp.
    if (VSC_EVENT_TYPE_ABORT != data_type) {
        vsc_err("abort event data type is invalid, host is %u, type is %d\n", 
            vsc_ctrl_get_host_no(h), data_type);
        return -EINVAL;
    }

    abort_rsp = data;
    
    /* 检查数据有效性 */
    if (data_len != sizeof(*abort_rsp)) {
        return -EINVAL;
    }

    if (abort_rsp->result) {
        vsc_err("abort event failed, host is %u, result is %u\n", 
            vsc_ctrl_get_host_no(h), abort_rsp->result);
        e->event_priv = FAILED;
    } else {
        e->event_priv = SUCCESS;
    }

    return 0;
}

/**************************************************************************
 功能描述  : scsi中层abort回调函数
 参    数  : struct scsi_cmnd *scsicmd  scsi中层命令
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_eh_abort(struct scsi_cmnd *sc)
{
    struct vsc_ctrl_info *h = NULL;
    struct vsc_cmnd_list *c = NULL;
    struct vsc_event_list *e = NULL;
    int result = 0;
    struct vsc_event_abort event_abort;

    c = sc_to_cmnd_list(sc);
    if (!c) {
        vsc_err("vsc abort: Get cmnd list failed.\n");
        scsi_print_command(sc);
        return FAILED;
    }

    /* 获取命令引用计数 */
    vsc_cmd_get(c);
    h = c->h;
    h->stat.abort_cmd_count++;
    spin_lock_bh(&h->lock);
    e = vsc_event_alloc(h);
    spin_unlock_bh(&h->lock);
    if (!e) {
        /* 立即重试命令 */
        vsc_cmd_put(c);  
        /* [zr] 这里不需要将该命令释放掉吗? 并且这时候target并没有abort这个命令 */
        /* --如果直接返回SUCCESS, 需要将命令释放, 并且这里也不应该返回SUCCESS, 后续改成返回FAILED */
        return SUCCESS;
    }

    event_abort.cmd_sn = c->cmd_sn;
    /* io有可能被分发非归属host，这里event需要分发到实际的host，即当前host */
    //event_abort.host = sc->device->host->host_no;
    event_abort.host = vsc_ctrl_get_host_no(h);
    event_abort.channel = sc->device->channel;
    event_abort.id = sc->device->id;
    event_abort.lun = sc->device->lun;

    vsc_err("add abort_command event host:%u/%u channel:%u id:%u lun:%u cmd_sn:%d event_sn:%d tag:%d\n",
            vsc_ctrl_get_host_no(h), event_abort.host, event_abort.channel,
            event_abort.id, event_abort.lun, event_abort.cmd_sn, e->event_sn, e->event_index);

    /* 添加事件，借用event私有数据作为返回值*/
    vsc_event_add(e, vsc_abort_event_callback, VSC_EVENT_TYPE_ABORT, 
                    &event_abort, sizeof(event_abort), FAILED);

    /* 等待15s */
    vsc_event_wait_timeout(e, HZ * VSC_ABORT_EVENT_WAIT_TIME);

    /* 若成功ABORT，则释放对应的消息 */
    if (SUCCESS == e->event_priv) {
        
        /* 将命令重新加入队列重试，若重试失败，则直接返回错误 */
        spin_lock_bh(&h->lock);
        if (unlikely(!list_empty(&c->list))) { /* <zr> 为什么队列非空是unlikely? */
            
            /* 从请求队列中删除数据 */
            removeQ(c);
            h->io_running--;
            spin_unlock_bh(&h->lock);
            
            /* 释放所有命令 */
            vsc_cmd_put(c);
            
            /* <zr> 后续中层如何处理再分析, 需要再看代码 */
            sc->result = DID_ABORT << 16;
            sc->scsi_done(sc);
        } else {
            spin_unlock_bh(&h->lock);
        }
    } else {
        vsc_err("abort command sn:%u failed, host:%u\n", event_abort.cmd_sn, 
            vsc_ctrl_get_host_no(h));
    }
    result = e->event_priv;
    vsc_event_del(e);
    vsc_cmd_put(c);
    
    /* [zr] 如果result是SUCCESS，中层会重试该命令,
        如果非SUCCESS，那么中层会对此命令做何处理? 留在驱动队列里的这个命令怎么处理? 
        --等待后续中层的错误处理 */
    return result;
}

/**************************************************************************
 功能描述  : reset dev事件回调函数
 参    数  : vsc_ctlr_info *h            host控制句柄
             enum event_type type        事件响应类型
             vsc_event_list *e           事件指针
             data_type                   事件数据类型
             data                        事数据指针
             data_len                    事件数据长度
 返 回 值  : 0: 成功, 其他: 错误码
**************************************************************************/
static int vsc_reset_dev_event_callback(struct vsc_ctrl_info *h, enum event_type type,
                     struct vsc_event_list *e, int data_type, void *data, int data_len)
{
    struct vsc_event_result *reset_rsp;
    /* 如果超时，或者主动释放事件，则返回SUCCESS*/
    if (EVENT_TIMEOUT == type || EVENT_ABORT == type) {
        spin_lock_bh(&h->lock);
        if (!h->file) {
            /* 如果host没有被接管，则返回SUCESS，通知SCSI上层重试 */
            e->event_priv = SUCCESS;
        }
        spin_unlock_bh(&h->lock);
        return 0;
    }

    if (VSC_EVENT_TYPE_RESET_DEV != data_type) {
        vsc_err("reset device data type is invalid, host is %u, type is %d\n", 
                vsc_ctrl_get_host_no(h), data_type);
        return -EINVAL;
    }

    reset_rsp = data;
    
    /* 检查数据有效性 */
    if (data_len != sizeof(*reset_rsp)) {
        return -EINVAL;
    }

    if (reset_rsp->result) {
        vsc_err("reset device failed, host is %u, result is %u\n", 
            vsc_ctrl_get_host_no(h), reset_rsp->result);
        e->event_priv = FAILED;
    } else {
        e->event_priv = SUCCESS;
    }

    return 0;       
}

/**************************************************************************
 功能描述  : scsi中层复位回调函数
 参    数  : struct scsi_cmnd *scsicmd  scsi中层命令
 返 回 值  : 0: 成功, 其他: scsi中层错误码
**************************************************************************/
static int vsc_eh_device_reset_handler(struct scsi_cmnd *sc)
{
    struct vsc_ctrl_info *h = NULL;
    struct vsc_cmnd_list *c = NULL;
    struct vsc_event_list *e = NULL;
    int result = 0;
    struct vsc_event_reset_dev event_reset;

    c = sc_to_cmnd_list(sc);
    if (!c) {
        vsc_err("vsc reset dev: Get cmnd list failed.\n");
        scsi_print_command(sc);
        return FAILED;  /* interal retval, 0x2003 */
    }
    /* 获取命令引用计数 */
    vsc_cmd_get(c);

    h = c->h;
    h->stat.reset_dev_count++;
    spin_lock_bh(&h->lock);
    e = vsc_event_alloc(h);
    spin_unlock_bh(&h->lock);
    if (!e) {
        /* 立即重试命令 */
        vsc_cmd_put(c);
        /* [zr] 不需要释放该命令吗? */
        /* --如果直接返回SUCCESS, 需要将命令释放, 并且这里也不应该返回SUCCESS, 后续改成返回FAILED */
        return SUCCESS;
    }

    event_reset.cmd_sn = c->cmd_sn;
    /* io有可能被分发非归属host，这里event需要分发到实际的host，即当前host */
    //event_reset.host = sc->device->host->host_no;
    event_reset.host = vsc_ctrl_get_host_no(h);
    event_reset.channel = sc->device->channel;
    event_reset.id = sc->device->id;
    event_reset.lun = sc->device->lun;

    vsc_err("add reset_host event host:%u/%u channel:%u id:%u lun:%u cmd_sn:%d event_sn:%d\n",
            vsc_ctrl_get_host_no(h), event_reset.host, event_reset.channel,
            event_reset.id, event_reset.lun, event_reset.cmd_sn, e->event_sn);

    /* 添加事件，借用event私有数据作为返回值，默认返回成功*/
    vsc_event_add(e, vsc_reset_dev_event_callback, VSC_EVENT_TYPE_RESET_DEV, 
                    &event_reset, sizeof(event_reset), FAILED);

    /* 等待30s */
    vsc_event_wait_timeout(e, HZ * VSC_RESET_EVENT_WAIT_TIME);
    result = e->event_priv;

    h = c->h;
    vsc_err("device reset, host is %u, result is %X\n", vsc_ctrl_get_host_no(h), result);

    /* <zr> 这里只是device reset handler, 为什么把整个host下的cmd和event全都给释放了, 
        而不是只释放跟这个lun device相关的资源?? */
    /* 无论成功与否，都必须释放所有的资源 */
    /* <liaodf> 改成多线程后，io被分发多个host，如果reset只释放当前host的io，返成功后，
       中层把所有scmd释放，其他host的残留io就会出问题；所以必须清理本vbs的所有host*/
    if (SUCCESS == e->event_priv) {
        /* 所有命令重新入队，释放对应的资源 */
        struct vsc_host_mgr *silbing_hosts = h->silbing_hosts;
        int i = 0;
        
        for (i=0;i<silbing_hosts->host_count;i++) {
            vsc_requeue_all_cmd(silbing_hosts->host_list[i]);
        }
    } else {
        /* 释放所有命令，释放对应的资源 */
        struct vsc_host_mgr *silbing_hosts = h->silbing_hosts;
        int i = 0;
        
        for (i=0;i<silbing_hosts->host_count;i++) {
            vsc_abort_all_cmd(silbing_hosts->host_list[i]);
        }
    }

    vsc_event_del(e);
    vsc_cmd_put(c);  /* [zr] 这里会该cmd会被释放掉 */ 

    /* 取消所有事件 */
    vsc_abort_all_event(h);

    return result;   
}

/**************************************************************************
 功能描述  : 接管scsi target
 参    数  : svsc_ctlr_info *h:     host控制句柄
             struct file *file      所属文件
             task                   进程信息
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
int vsc_scsi_attach(struct vsc_ctrl_info *h, struct file *file,  struct task_struct *task)
{
    if (!h) {
        return -EFAULT;
    }

    if (!file) {
        return -EFAULT;
    }

    spin_lock_bh(&h->lock);
    if (h->file) {
        spin_unlock_bh(&h->lock);
        return -EBUSY;
    }

    /* 如果定时器正在运行 返回重试*/ 
    /* [zr] vsc_target_abnormal() is running */
    if (atomic_inc_return(&h->target_abnormal_lock) > 1) {
        atomic_dec(&h->target_abnormal_lock);
        spin_unlock_bh(&h->lock);
        return -EAGAIN;
    } 
    
    del_timer_sync(&h->target_abnormal_timer);
    
    h->file = file;
    spin_unlock_bh(&h->lock);

    if (!h->suspend) {
        vsc_resume_host(h);
    }
    
    atomic_dec(&h->target_abnormal_lock);

    return 0;
}

/**************************************************************************
 功能描述  : 离线监控函数
 参    数  : unsigned long arg  监控函数参数                    
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
void vsc_target_abnormal(unsigned long arg)
{
    struct vsc_ctrl_info *h = (struct vsc_ctrl_info *)arg;
    struct scsi_device *sdev = NULL;

    if (unlikely(!h)) {
        return;
    }    
        
    if (atomic_inc_return(&h->target_abnormal_lock) > 1) {
        atomic_dec(&h->target_abnormal_lock);
        vsc_info("host[%u] may be attaching\n", vsc_ctrl_get_host_no(h));
        return;
    }

    vsc_info("offline abnormal host[%u], timeout:%us\n", vsc_ctrl_get_host_no(h), h->target_abnormal_time);

    /* 防止删除时死锁，唤醒设备 */
    vsc_resume_host(h);

    /* 丢弃所有在处理中的事件 */
    vsc_abort_all_event(h);
  
    /* 丢弃所有命令 */
    vsc_abort_all_cmd(h);

    /* 把所有host下磁盘都设置为离线 */
    shost_for_each_device(sdev, h->scsi_host) {
        scsi_device_set_state(sdev, SDEV_OFFLINE);
    }

    atomic_dec(&h->target_abnormal_lock);
   
    return;
}

/**************************************************************************
 功能描述  : 取消接管scsi target
 参    数  : struct vsc_ctlr_info *h:         host控制句柄
             int is_timeout                   是否启用定时器 0 表示不启用 1 表示启用
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
int vsc_scsi_detach(struct vsc_ctrl_info *h, int is_timeout)
{
    int retval = 0;

    if (!h) {
        return -EFAULT;
    }

    spin_lock_bh(&h->lock);
    if (!h->file) {
        spin_unlock_bh(&h->lock);
        return -EPERM;
    }

    h->file = NULL;
    spin_unlock_bh(&h->lock);

    /* 阻塞后续IO的下发 */
    scsi_block_requests(h->scsi_host);
    
    /* 释放所有的事件处理 */
    retval = vsc_abort_all_event(h);
    if (retval < 0) {
        return retval;
    }

    /* 将队列中的命令重新加入到scsi中层重试 */
    retval = vsc_requeue_all_cmd(h);
    if (retval < 0) {
        return retval;
    }

    /* 如果用户态进程阻塞在read函数，则唤醒通知退出 */
    if (waitqueue_active(&h->queue_wait)) {
        wake_up_interruptible(&h->queue_wait);
    }

    // Modified by z00108977  for DTS2012091802520
    if (is_timeout) {
        /* 如果是target异常导致的detach, 就设置suspend标志, 表明后续用户层要自己负责resume */
        h->suspend = 1;

        if (h->target_abnormal_time > 0) {
            /* 启动秒定时器 */
            mod_timer(&h->target_abnormal_timer, h->target_abnormal_time * HZ + jiffies);
            vsc_dbg("Startup timer, timeout:%us, host_no:%u\n", h->target_abnormal_time, vsc_ctrl_get_host_no(h));
        }
    }

    return 0;
}

/**************************************************************************
 功能描述  : 改变队列深度
 参    数  : struct scsi_device *sdev      scsi设备句柄
             int qdepth                    深度
 返 回 值  : 0: 成功，其他：错误码
**************************************************************************/
/* [zr] 这个回调函数是在什么时候被调用的?  
  --在scsi_add_lun的时候被注册进device_attribute, 在echo 255 > /sys/block/sdx/device/queue_depth的时候调用
  也就是在访问struct sysfs_ops的store方法时被调用 */

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
static int vsc_change_queue_depth(struct scsi_device *sdev, int qdepth)
#else
static int vsc_change_queue_depth(struct scsi_device *sdev, int qdepth, int reason)
#endif
{
    struct vsc_ctrl_info *h = sdev_to_ctrl_info(sdev);
#if (LINUX_VERSION_CODE != KERNEL_VERSION(2, 6, 18))
    if (reason != SCSI_QDEPTH_DEFAULT) {
        return -ENOTSUPP;
    }
#endif

    if (qdepth < 1) {
        qdepth = 1;
    } else {
        if (qdepth > h->nr_cmds) {
            qdepth = h->nr_cmds;
        }
    }

    scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
    return (int)(sdev->queue_depth);
}

/* [zr] slave_alloc <- scsi_alloc_sdev <- scsi_probe_and_add_lun <- 
   __scsi_add_device <- scsi_add_device */
static int vsc_slave_alloc(struct scsi_device *sdev)
{
    set_bit(QUEUE_FLAG_BIDI, &sdev->request_queue->queue_flags);
    return 0;
}

/* [zr] slave_configure <- scsi_add_lun <- scsi_probe_and_add_lun <- 
   __scsi_add_device <- scsi_add_device */
static int vsc_slave_configure(struct scsi_device *sdev)
{
    blk_queue_bounce_limit(sdev->request_queue, BLK_BOUNCE_ANY);
    blk_queue_dma_alignment(sdev->request_queue, 0);

    return 0;
}

/**
 * vsch_bios_param - fetch head, sector, cylinder info for a disk
 * @sdev: scsi device struct
 * @bdev: pointer to block device context
 * @capacity: device size (in 512 byte sectors)
 * @params: three element array to place output:
 *              params[0] number of heads (max 255)
 *              params[1] number of sectors (max 63)
 *              params[2] number of cylinders
 *
 * Return nothing.
 */
static int
vsch_bios_param(struct scsi_device *sdev, struct block_device *bdev,
                sector_t capacity, int params[])
{
    int        heads;
    int        sectors;
    sector_t    cylinders;
    ulong         dummy;

    heads = 64;
    sectors = 32;

    dummy = heads * sectors;
    cylinders = capacity;
    sector_div(cylinders, dummy);

    /*
     * Handle extended translation size for logical drives
     * > 1Gb
     */
    if ((ulong)capacity >= 0x200000) {
        heads = 255;
        sectors = 63;
        dummy = heads * sectors;
        cylinders = capacity;
        sector_div(cylinders, dummy);
    }

    /* return result */
    params[0] = heads;
    params[1] = sectors;
    params[2] = cylinders;

    return 0;
}

/**
 *    vsc scsi host template
 **/
static struct scsi_host_template vsc_driver_template = {
    .module                   = THIS_MODULE,
    .name                     = "vsc",
    .proc_name                = "vsc",
    .queuecommand             = vsc_scsi_queue_command,
    .this_id                  = -1,
    .can_queue                = 1,
    .max_sectors              = 8192,
    .sg_tablesize             = 128,
    .use_clustering            = ENABLE_CLUSTERING,
    .bios_param               = vsch_bios_param,
    .eh_abort_handler         = vsc_eh_abort,
    .eh_device_reset_handler  = vsc_eh_device_reset_handler,
    .change_queue_depth       = vsc_change_queue_depth,
    .slave_alloc              = vsc_slave_alloc,
    .slave_configure          = vsc_slave_configure,
    .scan_finished            = vsc_scan_finished,
    .scan_start               = vsc_scan_start,
};

static struct scsi_device *__vsc_scsi_device_lookup(struct Scsi_Host *shost,
        uint channel, uint id, uint lun)
{
    struct scsi_device *sdev;

    list_for_each_entry(sdev, &shost->__devices, siblings) {
        if (sdev->channel == channel && sdev->id == id &&
                sdev->lun ==lun && sdev->sdev_state != SDEV_DEL)
            return sdev;
    }

    return NULL;
}

static struct scsi_device *__vsc_scsi_device_lookup_by_vol_name(struct Scsi_Host *shost,
                                                                char *vol_name)
{
    struct scsi_device *sdev;
    struct vsc_ioctl_disk_vol_name *disk_vol;
    
    list_for_each_entry(sdev, &shost->__devices, siblings) {
       disk_vol = (struct vsc_ioctl_disk_vol_name *) &(sdev->sdev_data[0]); 
       if (NULL == disk_vol) {
           continue;
       }
       if (!strncmp(disk_vol->vol_name, vol_name, VSC_VOL_NAME_LEN)) {
            return sdev;
       }
    }
    return NULL;
}

static struct scsi_device *vsc_scsi_device_lookup(struct Scsi_Host *shost,
        uint channel, uint id, uint lun)
{
    struct scsi_device *sdev;
    unsigned long flags;

    spin_lock_irqsave(shost->host_lock, flags);
    sdev = __vsc_scsi_device_lookup(shost, channel, id, lun);
    if (sdev && scsi_device_get(sdev))
        sdev = NULL;
    spin_unlock_irqrestore(shost->host_lock, flags);

    return sdev;
}

static struct scsi_device *vsc_scsi_device_lookup_by_vol_name(struct Scsi_Host *shost, char *vol_name)
{
    struct scsi_device *sdev;
    unsigned long flags;

    spin_lock_irqsave(shost->host_lock, flags);
    sdev = __vsc_scsi_device_lookup_by_vol_name(shost, vol_name);
    if (sdev && scsi_device_get(sdev))
        sdev = NULL;
    spin_unlock_irqrestore(shost->host_lock, flags);

    return sdev;
}

/**************************************************************************
 功能描述  : 向host中添加一个设备
 参    数  : vsc_ctlr_info *h:     host控制句柄
             channel:  通道号
             id :      target id编号
             lun :      lun编号
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_add_device(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;
    int error = 0;

    if (!h) {
        return -EINVAL;
    }

    /* 检查对应的HOST是否已经接管，如果没有接管，返回禁止访问*/
    if (!(h->file)) {
        return -EACCES;
    }

    sh = h->scsi_host;
    /* VBS 的流程需要在host 为suspend 状态时，进行add device. */
//    /* 如果host 处于阻塞状态，则返回失败 */
//    if (sh->host_self_blocked == 1) {
//        return -EBUSY;
//    }

    /* 检查对应的设备是否存在 */
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (sdev) {
        scsi_device_put(sdev);
        return -EEXIST;
    }

    /* 向scsi中层注册设备 */
    error = scsi_add_device(sh, channel, id, lun); 
    if (error) {
        vsc_err("scsi_add_device() failed, device to be added: [%u %u %u %u], err=%d\n", 
            vsc_ctrl_get_host_no(h), channel, id, lun, error);
        return error;
    }
    
    vsc_info("Disk [%u %u %u %u] added success!\n", vsc_ctrl_get_host_no(h), channel, id, lun);
    return 0;
}

int vsc_add_device_by_vol_name(struct vsc_ctrl_info *h, unsigned int channel,
                               unsigned int id, unsigned int lun, char *vol_name)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;
    struct vsc_ioctl_disk_vol_name *disk_vol = NULL;
    int error = 0;

    if (!h || !vol_name) {
        return -EINVAL;
    }

    /* 检查对应的HOST是否已经接管，如果没有接管，返回禁止访问*/
    if (!(h->file)) {
        return -EACCES;
    }

    sh = h->scsi_host;
    /* VBS 的流程需要在host 为suspend 状态时，进行add device. */
//    /* 如果host 处于阻塞状态，则返回失败 */
//    if (sh->host_self_blocked == 1) {
//        return -EBUSY;
//    }

    /* 检查对应的设备是否存在 */
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (sdev) {
        scsi_device_put(sdev);
        return -EEXIST;
    }

    /* 向scsi中层注册设备 */
    error = scsi_add_device(sh, channel, id, lun); 
    if (error) {
        vsc_err("scsi_add_device() failed, device to be added: [%u %u %u %u], err=%d\n", 
            vsc_ctrl_get_host_no(h), channel, id, lun, error);
        return error;
    }
    
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (!sdev) {
        return -ENODEV;
    }
    disk_vol = (struct vsc_ioctl_disk_vol_name *) &(sdev->sdev_data[0]);
    memcpy(disk_vol->vol_name, vol_name, VSC_VOL_NAME_LEN);
    disk_vol->state = VSC_DISK_RUNNING;
    disk_vol->host = sh->host_no;
    disk_vol->channel = sdev->channel;
    disk_vol->id = sdev->id;
    disk_vol->lun = sdev->lun;
    
    /* 初始化每个卷的io计数器，用于多线程间分发 */
    atomic64_set((atomic64_t*)disk_vol->extend, 1);
    scsi_device_put(sdev);

    vsc_info("Disk [%u %u %u %u] volume(%s) added success!\n",
                vsc_ctrl_get_host_no(h), channel, id, lun, vol_name);
    return 0;
}

/**************************************************************************
 功能描述  : 从host中删除一个设备
 参    数  : vsc_ctlr_info *h:     host控制句柄
             channel:  通道号
             id :      target id编号
             lun :      lun编号
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_rmv_device(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;

    if ((!h) || (!h->scsi_host)) {
        vsc_err("input para is null [%u %u %u]\n",channel, id, lun);
        return -EINVAL;
    }

    sh = h->scsi_host;
    /* 如果host 处于阻塞状态，则返回失败 */
    if (sh->host_self_blocked == 1) {
        return -EBUSY;
    }

    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (!sdev) {
        vsc_err("lookup scsi device sdev failed [%u %u %u %u] \n",
            vsc_ctrl_get_host_no(h), channel, id, lun);
        return -ENODEV;
    }

    scsi_remove_device(sdev);
    vsc_info("disk [%u %u %u %u] removed!\n", vsc_ctrl_get_host_no(h), channel, id, lun);

    scsi_device_put(sdev);
    return 0;
}

int vsc_set_delete_by_vol_name(struct vsc_ctrl_info *h, char *vol_name)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;
    struct vsc_ioctl_disk_vol_name *disk_vol = NULL;

    if ((!vol_name) || (!h) || (!h->scsi_host)) {
        vsc_err("para is null, vol_name: %p, vsc_host: %p, scsi_host: %p\n",
                vol_name, h, h?h->scsi_host:NULL);
        return -EINVAL;
    }
    sh = h->scsi_host;

    sdev = vsc_scsi_device_lookup_by_vol_name(sh, vol_name);
    if (!sdev) {
        vsc_err("lookup scsi device failed, host_no: %u, vol_name: %s \n",
                vsc_ctrl_get_host_no(h), vol_name);
        return -ENODEV;
    }
    disk_vol = (struct vsc_ioctl_disk_vol_name *) &sdev->sdev_data[0];

    disk_vol->state = VSC_DISK_PREPARE_DELETE;

    vsc_info("disk [host_no: %u volume(%s)] set to delete flag!\n",
                vsc_ctrl_get_host_no(h), vol_name);
    
    scsi_device_put(sdev);
    return 0;
}

void vsc_query_vol(struct vsc_ctrl_info *h, struct vsc_ioctl_query_vol *query_vol)
{
    struct Scsi_Host *shost = h->scsi_host;
    struct vsc_ioctl_disk_vol_name *des = &query_vol->volumes[0];
    struct vsc_ioctl_disk_vol_name *src = NULL;
    struct scsi_device *sdev;
    unsigned long flags;

    query_vol->vol_num = 0;
    spin_lock_irqsave(shost->host_lock, flags);
    list_for_each_entry(sdev, &shost->__devices, siblings) {
       if (query_vol->vol_num >= VSC_MAX_VOL_PER_HOST) {
           break;
       }
       src = (struct vsc_ioctl_disk_vol_name *) &(sdev->sdev_data[0]); 
       if (sdev->sdev_state == SDEV_DEL)
       {
           vsc_info("maybe disk(%u,%u,%u,%u) state: %u have deleted but it's "
                   "refcount is not zero\n", src->host, src->channel,
                    src->id, src->lun, src->state);
           continue;
       }
       memcpy(des, src, sizeof(*des));
       query_vol->vol_num++;
       des++;
    }
    spin_unlock_irqrestore(shost->host_lock, flags);
}

/**************************************************************************
 功能描述  : 设置磁盘状态
 参    数  : vsc_ctlr_info *h:     host控制句柄
             channel:  通道号
             id :      target id编号
             lun :     lun编号
             stat :    设备状态
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_set_device_stat(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id,
                            unsigned int lun, enum scsi_device_state stat)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;
    enum scsi_device_state oldstate;
    int retval = 0;

    if (!h) {
        return -EINVAL;
    }

    sh = h->scsi_host;
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (!sdev) {
        return -ENODEV;
    }

    
    /* 状态检查 */
    oldstate = sdev->sdev_state;
    switch (stat) {
    case SDEV_RUNNING:
        switch (oldstate) {
        case SDEV_CREATED:
        case SDEV_OFFLINE:
        case SDEV_QUIESCE:
        case SDEV_BLOCK:
            break;
        default:
            retval = -EINVAL;
            goto out;
        }
        break;
    case SDEV_OFFLINE:
        switch (oldstate) {
        case SDEV_CREATED:
        case SDEV_RUNNING:
        case SDEV_QUIESCE:
        case SDEV_BLOCK:
            break;
        default:
            retval = -EINVAL;
            goto out;
        }
        break;
    case SDEV_BLOCK:
        switch (oldstate) {
        case SDEV_RUNNING:
#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
        case SDEV_CREATED:
#else
        case SDEV_CREATED_BLOCK:
#endif
            break;
        default:
            retval = -EINVAL;
            goto out;
        }
        break;
    default:
        retval = -EINVAL;
        goto out;
    }

    retval = scsi_device_set_state(sdev, stat);
out:    
    scsi_device_put(sdev);

    return retval;
}

/**************************************************************************
 功能描述  : 获取磁盘状态 
 参    数  : vsc_ctlr_info *h:     host控制句柄
             channel:  通道号
             id :      target id编号
             lun :     lun编号
             stat :    设备状态  
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_get_device_stat(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun,  enum scsi_device_state *stat)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;

    if (!h || !stat) {
        return -EINVAL;
    }

    sh = h->scsi_host;
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (!sdev) {
        vsc_err("scsi lookup dev [%u:%u:%u:%u] failed when get disk stat, return -ENODEV\n", 
            vsc_ctrl_get_host_no(h), channel, id, lun);
        return -ENODEV;
    }

    *stat = sdev->sdev_state;
    
    scsi_device_put(sdev);

    return 0;
}

/**************************************************************************
 功能描述  : 设置请求队列超时时间
 参    数  : sdev:    所要设置的scsi device
             timeout : 超时时间  
 返 回 值  : 无
**************************************************************************/
static void vsc_blk_queue_rq_timeout(struct scsi_device *sdev, unsigned int timeout)
{
#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
    sdev->timeout = timeout;
#else
    sdev->request_queue->rq_timeout = timeout;
#endif
}

/**************************************************************************
 功能描述  : 设置请求队列超时时间
 参    数  : vsc_ctlr_info *h:     host控制句柄
             channel:  通道号
             id :      target id编号
             lun :     lun编号
             timeout : 超时时间  
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_set_device_rq_timeout(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun, int timeout)
{
    struct Scsi_Host *sh = NULL;
    struct scsi_device *sdev = NULL;

    if (!h) {
        return -EINVAL;
    }

    if (timeout <= 0 || timeout > VSC_RQ_MAX_TIMEOUT) {
        return -EINVAL;
    }

    sh = h->scsi_host;
    sdev = vsc_scsi_device_lookup(sh, channel, id, lun);
    if (!sdev) {
        return -ENODEV;
    }

    vsc_blk_queue_rq_timeout(sdev, timeout * HZ);
    
    scsi_device_put(sdev);

    return 0;
}

/**************************************************************************
 功能描述  : 设置target进程异常超时时间
 参    数  : vsc_ctlr_info *h:     host控制句柄
             timeout : 超时时间  
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_set_tg_abn_timeout(struct vsc_ctrl_info *h, __u32 timeout)
{
    if (!h) {
        return -EINVAL;
    }

    h->target_abnormal_time = timeout;

    return 0;
}


/**************************************************************************
 功能描述  : 从用户态复制数据到内核
 参    数  : vsc_cmnd_list *c:     命令指针
             command_status        命令执行结果
 返 回 值  : 无
**************************************************************************/
inline void vsc_sense_check(struct vsc_cmnd_list *c, __u32 command_status) 
{
    struct scsi_cmnd *sc = NULL;
    sc = c->scsi_cmd;

    /* <zr> 下面的错误分支里把result的msg_byte和status_byte都给覆盖掉了, 这些信息在返回
        DID_NO_CONNECT/DID_ERROR/DID_TIME_OUT/DID_SOFT_ERROR/DID_ERROR之后，后续SCSI中层
        或sd层就不再需要检查了吗? */
    switch (command_status) {
    case CMD_SUCCESS:
        break;
    case CMD_FAILURE:
        break;
    case CMD_INVALID:
        vsc_err_limit("sense, invalid command.\n");
        sc->result = DID_NO_CONNECT << 16;
        break;
    case CMD_CONNECTION_LOST:
        sc->result = DID_ERROR << 16;
        vsc_err_limit("connection lost error.\n");
        break;
    case CMD_TIMEOUT:
        sc->result = DID_TIME_OUT << 16;
        vsc_err_limit("timedout\n");
        break;
    case CMD_PROTOCOL_ERR:
        sc->result = DID_SOFT_ERROR << 16;
        vsc_err_limit("unknown error: (CMD_PROTOCOL_ERR).\n");
        break;
    case CMD_NEED_RETRY:
        sc->result = DID_REQUEUE << 16;
        break;
    default:
        sc->result = DID_ERROR << 16;
        vsc_err_limit("unknown command status\n");
        break;
    }
}

/**************************************************************************
 功能描述  : 用户态向vsc获取多个scsi cmd和cdb数据，最多16个
 参    数  : vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/

#define MAX_COMMAND_SIZE        16
struct vsc_msg_cdb
{
    struct vsc_scsi_msg smsg;
    char cdb[MAX_COMMAND_SIZE];
};

static ssize_t vsc_get_multi_scsi_cmd_and_cdb(struct vsc_ctrl_info *h, char  __user *  user_ptr, int len)
{
    struct vsc_cmnd_list *c = NULL;
    struct vsc_cmnd_list *c_arr[MAX_BATCH_SCSI_CMD_COUNT] = {NULL};
    struct vsc_msg_cdb smsg_cdb[MAX_BATCH_SCSI_CMD_COUNT];
    struct scsi_cmnd *sc[MAX_BATCH_SCSI_CMD_COUNT] = {NULL};
    int count = 0;
    int index = 0;
    int left_count = 0;
    int retval = 0;

    if (unlikely(!h)) {
        return -EFAULT;
    }

    if (unlikely(len != MAX_SCSI_MSG_AND_CDB_LENGTH)) {
        vsc_err("%s():%d expect_len %d actual_len %u \n", __FUNCTION__, __LINE__,
            (int)MAX_SCSI_MSG_AND_CDB_LENGTH, len);
        return -EINVAL;
    }

    spin_lock_bh(&h->lock);
    if (unlikely(list_empty(&h->cmpQ))) {
        spin_unlock_bh(&h->lock);
        retval = -ENODATA;
        goto errout;
    }

    while (!list_empty(&h->cmpQ)) {
        c = list_first_entry(&h->cmpQ, struct vsc_cmnd_list, list);

        /* 从读取完成命令队列中删除命令 */
        removeQ(c);
        
        /* 添加到等待完成队列 */
        addQ(&h->rspQ, c);
        
        /* 通过数据把c记录下来，以链表的方式会存在DTS2015101408787问题 */
        c_arr[count] = c;

        count++;
        if (count >= MAX_BATCH_SCSI_CMD_COUNT) {
            /* 一次最多传输MAX_BATCH_SCSI_CMD_COUNT个命令 */
            break;
        }
    }

    h->get_cmd_times++;
    h->get_cmd_total += count;

    left_count = list_size(&h->cmpQ);
    spin_unlock_bh(&h->lock);

    /* 
       将命令暂存在数组中，也有可能同时发生device reset，仍然把命令发给vbs，但是这时不会有更严重的后果;
       因为命令已经从三条队列中移除，后续处理会找不到此命令而异常终止；多命令合并前的版本也存在类似问题 
     */
    for (index = 0; index < count; index++) {
        c = c_arr[index];
        sc[index] = c->scsi_cmd;

        /* 修改状态为等待完成，直接置状态，减少遍历队列操作，如果后续操作失败做错误处理 */
        c->stat = CMD_STAT_READ_COMP;

    #ifdef __VSC_LATENCY_DEBUG__
        if (vsc_is_write_cmd(sc[index]->cmnd[0])) {
            h->write_cmd_count++;
            c->get_scmd_time = vsc_get_usec();
            h->write_stage1_total += c->get_scmd_time - c->start_queue_time;
        }
        else if (vsc_is_read_cmd(sc[index]->cmnd[0])) {
            h->read_cmd_count++;
            c->get_scmd_time = vsc_get_usec();
            h->read_stage1_total += c->get_scmd_time - c->start_queue_time;
        }
    
        if (vsc_is_readwrite_cmd(sc[index]->cmnd[0])) {
            if (c->get_scmd_time > c->start_queue_time && c->get_scmd_time - c->start_queue_time > 2000000) {
                vsc_err("too long in queue, host(%d) cmd_sn(%d) tag(%d) start_queue(%llu) get_scmd(%llu)",
                        vsc_ctrl_get_host_no(h), c->cmd_sn, c->cmd_index,
                        c->start_queue_time, c->get_scmd_time);
            }
        }
    #endif

        /* 复制数据到用户态 */
        smsg_cdb[index].smsg.type = VSC_MSG_TYPE_SCSI_CMD;
        smsg_cdb[index].smsg.cmd_sn = c->cmd_sn;
        smsg_cdb[index].smsg.tag = c->cmd_index;
        //smsg_cdb[index].smsg.host = sc[index]->device->host->host_no;
        smsg_cdb[index].smsg.host = vsc_ctrl_get_host_no(h);
        smsg_cdb[index].smsg.channel = sc[index]->device->channel;
        smsg_cdb[index].smsg.id = sc[index]->device->id;
        smsg_cdb[index].smsg.lun = sc[index]->device->lun;
        smsg_cdb[index].smsg.direction = sc[index]->sc_data_direction;
    #if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
        memcpy(smsg_cdb[index].cdb, sc[index]->cmnd, COMMAND_SIZE(sc[index]->cmnd[0]));
    #else
        memcpy(smsg_cdb[index].cdb, sc[index]->cmnd, scsi_command_size(sc[index]->cmnd));
    #endif
    }

    /* 在第一个消息中标示实际返回cmd个数 */
    smsg_cdb[0].smsg.reserved[0] = count;
    smsg_cdb[0].smsg.reserved[1] = left_count;

    /* 复制消息头 */  /* 40 Bytes */
    if (copy_to_user(user_ptr, smsg_cdb, sizeof(struct vsc_msg_cdb) * count)) {
        vsc_err("Get cmd: copy msg head failed, host is %u, count = %d.\n", vsc_ctrl_get_host_no(h), count);
        retval = -EFAULT;
        goto errout;
    }

    /* 跟踪消息 */
    if (unlikely(VSC_TRACE_REQ_SCSI_CMD & trace_switch)) {
        for (index = 0; index < count; index++) {
            vsc_err("[%d:%d:%d:%d] sn:%u tag:%d  direct:%u  sg_count:%u  sg_len:%u",
                    smsg_cdb[index].smsg.host, smsg_cdb[index].smsg.channel, smsg_cdb[index].smsg.id, smsg_cdb[index].smsg.lun,
                    smsg_cdb[index].smsg.cmd_sn, smsg_cdb[index].smsg.tag, smsg_cdb[index].smsg.direction,
                    scsi_sg_count(sc[index]), scsi_bufflen(sc[index]));
            scsi_print_command(sc[index]);
        }
    }

    return sizeof(struct vsc_msg_cdb) * count;

errout:
    /* 消息未被取走，重新加入队列，尽量按照原顺序放回cmpQ，无法完全保证，因为新的io还是往cmpQ上放 */
    if (count > 0) {
        spin_lock_bh(&h->lock);
        for (index = 0; index < count; index++) {
            c = c_arr[index];
            c->stat = CMD_STAT_REQUEST;
            /* 从读取完成命令队列中删除命令 */
            removeQ(c);
            /* 放到头部，因为是从头部取出，尽量保证原顺序 */
            list_add(&c->list, &h->cmpQ); 
        }
        spin_unlock_bh(&h->lock);
    }

    return retval;
}

/**************************************************************************
 功能描述  : 读取事件函数
 参    数  : vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t：数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_get_event(struct vsc_ctrl_info *h, char  __user *  user_ptr, int len)
{
    struct vsc_event_list *e = NULL;
    struct vsc_scsi_event event;
    char __user *user = user_ptr;
    int retval = 0;

    event.reserved[0] = 1;
    event.reserved[1] = 0;

    if (!h) {
        return -EFAULT;
    }

    /* <zr> 这里应该判断入参len的有效性 */
    if (len < (sizeof(event) + sizeof(struct vsc_scsi_msg_data) 
                + sizeof(struct vsc_event_abort))) {
        return -EINVAL;
    }

    spin_lock_bh(&h->lock);
    if (list_empty(&h->event_reqQ)) {
        spin_unlock_bh(&h->lock);
        retval = -ENODATA;
        goto errout;
    }
    e = list_first_entry(&h->event_reqQ, struct vsc_event_list, list);
    /* 从读取完成命令队列中删除命令 */
    remove_eventQ(e);
    add_eventQ(&h->event_rspQ, e);
    if (!list_empty(&h->cmpQ) || !list_empty(&h->event_reqQ)) {
        event.reserved[1] = 1;
    }

    spin_unlock_bh(&h->lock);

    /* 跳过事件消息头 */
    user += sizeof(event);
    
    /* 复制事件消息 */  /* sizeof(struct vsc_scsi_msg_data) + sizeof(struct vsc_event_abort) or 
                            sizeof(struct vsc_scsi_msg_data) + sizeof(struct vsc_event_reset_dev) 
                            = 8+20 = 28 Bytes */
    if (copy_to_user(user, e->event_data, e->event_data_len)) {
        vsc_err("Get event: copy event head failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }

    /* 填充事件消息头 */
    event.type = VSC_MSG_TYPE_EVENT;
    event.event_sn = e->event_sn;
    event.tag = e->event_index;
    event.data_length = e->event_data_len;

    /* 复制事件头 */   /* 28 Bytes */
    if (copy_to_user(user_ptr, &event, sizeof(event))) {
        vsc_err("Get event: copy event failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }

    /* 跟踪消息 */
    if ( unlikely(VSC_TRACE_REQ_EVENT & trace_switch )) {
        struct vsc_scsi_msg_data *event_msg = NULL;
        
        event_msg = (struct vsc_scsi_msg_data *)(e->event_data);
        vsc_err("event req: host:%u sn:%u   tag: %u   len: %u  type:%u\n", vsc_ctrl_get_host_no(h), 
                event.event_sn, event.tag, event.data_length, event_msg->data_type);
    }

    /* 设置状态为等待回应 */
    e->stat = EVENT_LIST_STAT_READ_COMP;

    return sizeof(event) + event.data_length;

errout:
    /* 事件未被取走，重新加入队列 */
    if (e) {
        spin_lock_bh(&h->lock);
        remove_eventQ(e);
        add_eventQ(&h->event_reqQ, e);
        spin_unlock_bh(&h->lock);
    }

    return retval;
}

/**************************************************************************
 功能描述  : scsi命令读取函数
 参    数  : vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_get_scsi_cmd_msg(struct vsc_ctrl_info *h, struct file *file,
                                        char  __user *  user_ptr, int len)
{
    ssize_t retval = 0;
    int do_get_event = 0;

    if (!h || !user_ptr) {
        return -EFAULT;
    }
   
    /* 循环处理cmpQ队列中的scsi命令 */
    spin_lock_bh(&h->lock);
    while (list_empty(&h->cmpQ) && list_empty(&h->event_reqQ)) {
        spin_unlock_bh(&h->lock);
        if (file->f_flags & O_NONBLOCK) {
            retval = -EAGAIN;
            goto errout_no_count;
        }
        
        /* 阻塞读取的情况下，等待通知事件 */
        retval = wait_event_interruptible(h->queue_wait, 
            !list_empty(&h->cmpQ) || !list_empty(&h->event_reqQ) || h->wakeup_pid);
        if (retval < 0) {
            goto errout;    // -ERESTARTSYS
        }

        /* 检查是否因为detach退出 */
        if (unlikely(h->wakeup_pid)) {
            h->wakeup_pid = 0;
            retval = -EINTR;
            goto errout_no_count;
        }

        spin_lock_bh(&h->lock);
    }

    /* 优先处理事件消息 */
    if (unlikely(!list_empty(&h->event_reqQ))) {
        do_get_event = 1;
    }
    spin_unlock_bh(&h->lock);

    /* <zr> 释放h->lock之后消息可能被别的线程取走,这时队列可能重新变为空, 在下面的
       vsc_get_event()/vsc_get_scsi_cmd()中就会返回ENODATA失败,这次read也会返回失败, 
       这样合理吗? */
    if (unlikely(do_get_event)) {
        retval = vsc_get_event(h, user_ptr, len);
    } else {
        retval = vsc_get_multi_scsi_cmd_and_cdb(h, user_ptr, len);
    }
    
    if (unlikely(retval < 0)) {
        goto errout;
    }

    return retval;
errout:
    h->stat.read_fail_count++;
errout_no_count:

    return retval;
}

/**************************************************************************
 功能描述  : 向用户态iovec中复制数据
 参    数  : vsc_ctlr_info *h:     host控制句柄
             struct vsc_scsi_data  用户态数据信息
             char  __user *  user_ptr 用户态iovec起始地址
             unsigned char         内核态数据
             unsigned int len      内核态数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_copy_data_to_iovec(struct vsc_ctrl_info *h, struct vsc_scsi_data *scsi_data,
                            const char *user, unsigned char *from_ptr, unsigned int len)
{
    struct vsc_iovec *iovec;
    int i = 0;
    ssize_t retval = 0;
    unsigned char *from = from_ptr;
    unsigned int left_len = len;
    unsigned int copy_len = 0;
    ssize_t total_len = 0;

    /* 获取指定偏移的数据 */
    left_len -= scsi_data->offset;
    from += scsi_data->offset;

    for ( i = 0; i < scsi_data->nr_iovec && left_len > 0; i++) {
        iovec = (struct vsc_iovec *)user;

        /* 复制数据到用户态 */
        copy_len = min(iovec->iov_len, left_len);
        if (copy_to_user((void __user *)iovec->iov_base, from, copy_len)) {
            vsc_err("copy data to user failed, host is %u.\n", vsc_ctrl_get_host_no(h));
            retval = -EFAULT;
            goto errout;
        }
        from += copy_len;
        left_len -= copy_len;
        total_len += copy_len;

        user += sizeof(struct vsc_iovec);
    }
    return total_len;

errout:
    return retval;
}

/**************************************************************************
 功能描述  : 从用户态iovec中复制数据到内核
 参    数  : vsc_ctlr_info *h:     host控制句柄
             struct vsc_scsi_data *scsi_data iovec数据头指针
             user_ptr              iovec指针
             unsigned to_ptr       内核态数据
             unsigned int len      内核态数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_copy_data_from_iovec(struct vsc_ctrl_info *h, struct vsc_scsi_data *scsi_data,
                                const char *user, unsigned char *to_ptr, unsigned int len)
{
    struct vsc_iovec *iovec;
    int i = 0;
    ssize_t retval = 0;
    unsigned char *to = to_ptr;
    unsigned int left_len = len;
    unsigned int copy_len = 0;
    ssize_t total_len = 0;

    /* 获取指定偏移的数据 */
    left_len -= scsi_data->offset;
    to += scsi_data->offset;

    for ( i = 0; i < scsi_data->nr_iovec && left_len > 0; i++) {
        iovec = (struct vsc_iovec *)user;

        /* 复制数据from 用户态 */
        copy_len = min(iovec->iov_len, left_len);
        if (copy_from_user(to, (void __user *)iovec->iov_base, copy_len)) {
            vsc_err("copy data from user failed, host is %u.\n", vsc_ctrl_get_host_no(h));
            retval = -EFAULT;
            goto errout;
        }
        to += copy_len;
        left_len -= copy_len;
        total_len += copy_len;

        user += sizeof(struct vsc_iovec);
    }
    return total_len;

errout:
    return retval;
}

#ifndef for_each_sg
#define for_each_sg(sglist, sg, nr, __i)    \
    for (__i = 0, sg = (sglist); __i < (nr); __i++, sg++ )
#endif

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
static inline void *sg_virt(struct scatterlist *sg)
{
    return page_address(sg->page) + sg->offset;
}

static inline struct scatterlist *sg_next(struct scatterlist *sg)
{
    return ++sg;
}
#endif

void vsc_dump_scsi_data(struct vsc_scsi_data *scsi_data)
{
    int count = 0;
    struct vsc_iovec *iovec = (struct vsc_iovec *)scsi_data->vec;
    
    vsc_err("scsi_data={%d %d %d %d %d}\n", scsi_data->data_type, scsi_data->data_len, scsi_data->nr_iovec,
            scsi_data->total_iovec_len, scsi_data->offset);

    if (scsi_data->nr_iovec > (SCSI_MAX_DATA_BUF_SIZE-sizeof(struct vsc_scsi_msg_data)-sizeof(struct vsc_scsi_data))/sizeof(struct vsc_iovec)) {
        return;
    }
    
    for (count=0;count<scsi_data->nr_iovec;count++) {
        vsc_err("%d/%d iovec_base=0x%llx iovec_len=%d", count, scsi_data->nr_iovec,
                iovec[count].iov_base, iovec[count].iov_len);
    }
}

void vsc_dump_scsi_sgl(struct scsi_cmnd *sc)
{
    struct scatterlist *sl = NULL;
    struct scatterlist *sl_first = NULL;
    int j = 0;
    
    if (!sc) {
        return;
    }
    
    scsi_print_command(sc);
    
    sl = scsi_sglist(sc);
    sl_first = scsi_sglist(sc);
    
    for_each_sg(sl_first, sl, scsi_sg_count(sc), j) {
        vsc_err("%d/%d addr=0x%p len=%d", j, scsi_sg_count(sc), sg_virt(sl), sl->length);
    }    
}

void vsc_dump_scsi_rsp(struct vsc_scsi_rsp_msg *rsp_msg)
{
    if (!rsp_msg) {
        return;
    }

    vsc_err("rsp_msg={%d %d %d %d %d %d %d}", rsp_msg->type, rsp_msg->reserved[0], rsp_msg->reserved[1],
            rsp_msg->cmd_sn, rsp_msg->tag, rsp_msg->command_status, rsp_msg->scsi_status);
}

/**************************************************************************
 功能描述  : 复制scsi数据到用户态空间 
 参    数  : vsc_cmnd_list *c:     命令指针
             struct vsc_scsi_data *scsi_data iovec数据头指针
             user_ptr              用户数据指针
 返 回 值  : 0： 复制数据的长度，其他：错误码
**************************************************************************/
inline int vsc_copy_sg_buff_to_iovec(struct scsi_cmnd *sc, struct vsc_scsi_data *scsi_data,
        const char *user)
{
    int i = 0;
    int j = 0;
    int sl_offset = 0;  /* sl的偏移位置 */
    int sl_seek_offset = 0;  /* 寻址时的总的偏移量 */
    struct vsc_iovec *iovec;
    //struct scsi_cmnd *sc = NULL;
    struct scatterlist *sl = NULL;
    struct scatterlist *sl_first = NULL;
    int copy_len = 0;
    int iov_base_offset = 0;
    int sl_data_offset = 0;
    int left_len = 0;
    int totallen = 0;

    //sc = c->scsi_cmd;
    sl_first = scsi_sglist(sc);

    /* 偏移到指定的位置 */
    for_each_sg(sl_first, sl, scsi_sg_count(sc), j) {
        sl_seek_offset += sl->length;
        if (sl_seek_offset > scsi_data->offset) {
            left_len = sl_seek_offset - scsi_data->offset;
            sl_offset = j;
            break;
        }
    }

    sl_first = sl;
    if (unlikely(!sl_first)) {
        vsc_err("fatal error, invalid sgl, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EINVAL;
    }

    if (unlikely(scsi_data->nr_iovec == 0)) {
        vsc_err("fatal error, invalid iovec, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EINVAL;
    }

    /* 读写命令时判断，用户态读写的数据长度和实际拷贝的数据长度必须一致 */
    if (unlikely(vsc_is_readwrite_cmd(sc->cmnd[0]) && scsi_bufflen(sc) != scsi_data->total_iovec_len)) {
        vsc_err("fatal error, mismatch buflen, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EIO;
    }

    for ( i = 0; i < scsi_data->nr_iovec && sl_first; i++) {
        iovec = (struct vsc_iovec *)user;

        iov_base_offset = 0;
        /* 从上一次位置开始 */
        for_each_sg (sl_first, sl, scsi_sg_count(sc) - sl_offset, j) {
            /* 检查是否需要偏移复制sg_list */
            if (left_len > 0 && left_len <= sl->length) {
                /* 计算出对应的偏移量 */
                sl_data_offset = sl->length - left_len;
            } else {
                sl_data_offset = 0;
            }

            /* 按较小的长度复制数据 */
            copy_len = min(sl->length - sl_data_offset, iovec->iov_len - iov_base_offset);

            if (copy_to_user((void __user *)iovec->iov_base + iov_base_offset, sg_virt(sl) + sl_data_offset, copy_len)) {
                vsc_err("copy to user failed, sg_buflen=%d sg_count=%d copylen=%d.\n",
                        scsi_bufflen(sc), scsi_sg_count(sc),totallen);
                vsc_dump_scsi_sgl(sc);
                vsc_dump_scsi_data(scsi_data);
                return -EFAULT;
            }
            
            iov_base_offset += copy_len;
            sl_data_offset += copy_len;
            totallen += copy_len;

            left_len = sl->length - sl_data_offset - (iovec->iov_len - iov_base_offset);
            /* 如果iovec小于sl->length，则说明数据还没有复制完成，还需要复制数据 */
            if (left_len > 0) {
                /* 当前sl没有复制完成，需要再次复制 */
                sl_first = sl;
                break;
            } else {
                left_len = 0;
            }

            if (iov_base_offset >= iovec->iov_len) {
                /* 将sg头，指向下一个，同时数值j++ */
                sl_first = sg_next(sl);
                j++;
                break;
            }
        }
        /* 加上已经处理的个数 */
        sl_offset += j;

        user += sizeof(struct vsc_iovec);
    }

    /* 读写命令时判断，用户态读写的数据长度和实际拷贝的数据长度必须一致 */
    if (unlikely(vsc_is_readwrite_cmd(sc->cmnd[0]) && totallen != scsi_data->total_iovec_len)) {
        vsc_err("fatal error, mismatch copylen, sg_buflen=%d sg_count=%d copylen=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc),totallen);
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EIO;
    }
    
    return totallen;
}

/**************************************************************************
 功能描述  : 从用户态复制数据到内核
 参    数  : vsc_cmnd_list *c:     命令指针
             struct vsc_scsi_data *scsi_data iovec数据头指针
             user_ptr              用户数据指针
             user_len              用户数据长度
 返 回 值  : 0： 复制数据的长度，其他：错误码
**************************************************************************/
inline int vsc_copy_sg_buff_from_iovec(struct scsi_cmnd *sc, struct vsc_scsi_data *scsi_data,
        const char *  user)
{
    int i = 0;
    int j = 0;
    int sl_offset = 0;  /* sl的偏移位置 */
    int sl_seek_offset = 0;  /* 寻址时的总的偏移量 */
    struct vsc_iovec *iovec;
    //struct scsi_cmnd *sc = NULL;
    struct scatterlist *sl = NULL;
    struct scatterlist *sl_first = NULL;
    int copy_len = 0;
    int iov_base_offset = 0;
    int sl_data_offset = 0;
    int left_len = 0;
    int totallen = 0;

    //sc = c->scsi_cmd;
    sl_first = scsi_sglist(sc);

    /* 偏移到指定的位置 */
    for_each_sg (sl_first, sl, scsi_sg_count(sc), j) {
        sl_seek_offset += sl->length;
        if (sl_seek_offset > scsi_data->offset) {
            left_len = sl_seek_offset - scsi_data->offset;
            sl_offset = j;
            break;
        }
    }

    sl_first = sl;
    if (unlikely(!sl_first)) {
        vsc_err("fatal error, invalid sgl, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EINVAL;
    }

    if (unlikely(scsi_data->nr_iovec == 0)) {
        vsc_err("fatal error, invalid iovec, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EINVAL;
    }

    /* 读写命令时判断，用户态读写的数据长度和实际拷贝的数据长度必须一致 */
    if (unlikely(vsc_is_readwrite_cmd(sc->cmnd[0]) && scsi_bufflen(sc) != scsi_data->total_iovec_len)) {
        vsc_err("fatal error, mismatch buflen, sg_buflen=%d sg_count=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc));
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EIO;
    }
    
    for ( i = 0; i < scsi_data->nr_iovec && sl_first; i++) {
        iovec = (struct vsc_iovec *)user;

        iov_base_offset = 0;
        /* 从上一次位置开始 */
        for_each_sg (sl_first, sl, scsi_sg_count(sc) - sl_offset, j) {
            /* 检查是否需要偏移复制sg_list */
            if (left_len > 0 && left_len <= sl->length) {
                /* 计算出对应的偏移量 */
                sl_data_offset = sl->length - left_len;
            } else {
                sl_data_offset = 0;
            }

            /* 按较小的长度复制数据 */
            copy_len = min(sl->length - sl_data_offset, iovec->iov_len - iov_base_offset);

            if (copy_from_user(sg_virt(sl) + sl_data_offset, (void __user *)iovec->iov_base + iov_base_offset, copy_len)) {
                vsc_err("copy from user failed, sg_buflen=%d sg_count=%d copylen=%d.\n",
                        scsi_bufflen(sc), scsi_sg_count(sc),totallen);
                vsc_dump_scsi_sgl(sc);
                vsc_dump_scsi_data(scsi_data);
                return -EFAULT;
            }

            iov_base_offset += copy_len;
            sl_data_offset += copy_len;
            totallen += copy_len;

            left_len = sl->length - sl_data_offset - (iovec->iov_len - iov_base_offset);
            /* 如果iovec小于sl->length，则说明数据还没有复制完成，还需要复制数据 */
            if (left_len > 0) {
                /* 当前sl没有复制完成，需要再次复制 */
                sl_first = sl;
                break;
            } else {
                left_len = 0;
            }

            if (iov_base_offset >= iovec->iov_len) {
                /* 将sg头，指向下一个，同时数值j++ */
                sl_first = sg_next(sl);
                j++;
                break;
            }
        }
        /* 加上已经处理的个数 */
        sl_offset += j;

        user += sizeof(struct vsc_iovec);
    }

    /* 读写命令时判断，用户态读写的数据长度和实际拷贝的数据长度必须一致 */
    if (unlikely(vsc_is_readwrite_cmd(sc->cmnd[0]) && totallen != scsi_data->total_iovec_len)) {
        vsc_err("fatal error, mismatch copylen, sg_buflen=%d sg_count=%d copylen=%d.\n",
                scsi_bufflen(sc), scsi_sg_count(sc),totallen);
        vsc_dump_scsi_sgl(sc);
        vsc_dump_scsi_data(scsi_data);
        return -EIO;
    }

    return totallen;
}

/**************************************************************************
 功能描述  : 根据不同的scsi数据类型在用户态和核态拷贝数据
 参    数  : scsi_data             请求的数据信息
             mode                  读写模式
             vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_xfer_data(struct vsc_scsi_data* scsi_data, unsigned int mode, struct vsc_ctrl_info *h,
                              const char *user, struct scsi_cmnd* scsi_cmnd)
{
    struct scsi_cmnd* sc = scsi_cmnd;
    ssize_t retval = 0;
    
    switch (scsi_data->data_type) {
    case VSC_MSG_DATA_TYPE_CDB:
        if (VSC_MODE_READ != mode) {
            vsc_err("request cmd: write CDB is not permited, host is %u.\n", vsc_ctrl_get_host_no(h));    
            retval = -EACCES;
            goto errout;
        }
        /* 复制CDB到用户态 */

    h->get_cdb_total++;

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
    retval = vsc_copy_data_to_iovec(h, scsi_data, user, sc->cmnd, COMMAND_SIZE(sc->cmnd[0]));
#else
    retval = vsc_copy_data_to_iovec(h, scsi_data, user, sc->cmnd, scsi_command_size(sc->cmnd));
#endif
        if (retval < 0) {
            vsc_err("request cmd: copy CDB failed, host = %u, retval = %ld\n", vsc_ctrl_get_host_no(h), retval);
            goto errout;
        }
        break;
        
    case VSC_MSG_DATA_TYPE_DATA:
        if (VSC_MODE_READ == mode) {
            /* 复制数据到用户态 */
            retval = vsc_copy_sg_buff_to_iovec(sc, scsi_data, user);
            if (retval < 0) {
                vsc_err("request cmd: copy sg buff to iovec failed, host is %u.\n", vsc_ctrl_get_host_no(h));  
                goto errout;
            }
        } else if (VSC_MODE_WRITE == mode) {
            /* 写入数据到sglist */
            retval = vsc_copy_sg_buff_from_iovec(sc, scsi_data, user);
            if (retval < 0) {
                vsc_err("request cmd: copy sg buff from iovec failed, host is %u.\n", vsc_ctrl_get_host_no(h));  
                goto errout;
            }
        } else {
            retval = -EFAULT;
            vsc_err("invalid xfer_mode %d.\n", mode);
            goto errout;
        }
        break;
        
    case VSC_MSG_DATA_TYPE_SENSE:
        if (VSC_MODE_WRITE != mode) {
            vsc_err("request cmd: read sense is not permited, host is %u.\n", vsc_ctrl_get_host_no(h));    
            retval = -EACCES;
            goto errout;
        }
        /* 复制错误信息到sc命令 */
        retval = vsc_copy_data_from_iovec(h, scsi_data, user, sc->sense_buffer, SCSI_SENSE_BUFFERSIZE);
        if (retval < 0) {
            vsc_err("request cmd: copy sense data failed, host = %u, retval = %lu\n", vsc_ctrl_get_host_no(h), retval); 
            goto errout;
        }
        break;
        
    default:
        vsc_err("request msg error: vsc_scsi_data type is invalid, host = %u, type = %d\n", 
            vsc_ctrl_get_host_no(h), scsi_data->data_type); 
        retval = -EIO;
        goto errout;
        break;
    }
    
errout:
    return retval;
}


/**************************************************************************
 功能描述  : scsi命令数据处理
 参    数  : mode                  读写模式
             vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_scsi_cmd_data(unsigned int mode, struct vsc_ctrl_info *h,
        char *user, int len, int *data_len)
{
    struct vsc_scsi_data_msg *scsi_data_msg;
    struct vsc_scsi_data *scsi_data;
    char *iovec;
    //const char __user *user = user_ptr;
    ssize_t retval = 0;
    struct vsc_cmnd_list *c = NULL;
    //int scsi_data_len = 0;
    struct scsi_cmnd *sc = NULL;
    int totallen = 0;

    scsi_data_msg = (struct vsc_scsi_data_msg *)user;
    scsi_data = (struct vsc_scsi_data *)(user + sizeof(struct vsc_scsi_data_msg));
    iovec = user + sizeof(struct vsc_scsi_data_msg) + sizeof(struct vsc_scsi_data);

    /* 检查命令是否有效 */
    if (unlikely(scsi_data_msg->type != VSC_MSG_TYPE_SCSI_DATA
            || scsi_data_msg->tag >= h->nr_cmds
            || scsi_data_msg->scsi_data_len > len)) {

        vsc_err("request msg error: host = %u, type = %u, tag = %u, len = %u\n",
            vsc_ctrl_get_host_no(h), scsi_data_msg->type, scsi_data_msg->tag, scsi_data_msg->scsi_data_len);
        retval = -EIO;
        goto errout;
    }

    /* 按照tag查找command指针 */
    spin_lock_bh(&h->lock);
    c = h->cmd_pool_tbl[scsi_data_msg->tag];
    if (unlikely(c->stat != CMD_STAT_READ_COMP || c->cmd_sn != scsi_data_msg->cmd_sn)) {
        spin_unlock_bh(&h->lock);
        //vsc_debug_cmd(vsc_ctrl_get_host_no(h));
        vsc_err("request msg error: host = %u, msg.sn = %u, msg.tag = %u, cmd.stat = %d, cmd.tag = %u, cmd.sn = %u\n",
                vsc_ctrl_get_host_no(h),
                scsi_data_msg->cmd_sn, scsi_data_msg->tag, c->stat, c->cmd_index, c->cmd_sn);
        c = NULL;
        retval = -EIO;
        goto errout;
    }

    /* 判断命令是否已经处理 */
    if (unlikely(list_empty(&c->list))) { 
        spin_unlock_bh(&h->lock);
        vsc_err("request msg error: already removed host = %u, cmd.sn = %u, cmd.tag = %d, cmd.stat = %d\n",
                vsc_ctrl_get_host_no(h), 
                c->cmd_sn, c->cmd_index, c->stat);
        c = NULL;
        retval = -EIO;
        goto errout;
    }
    sc = c->scsi_cmd;

#ifdef __VSC_LATENCY_DEBUG__
    if (vsc_is_write_cmd(sc->cmnd[0])) {
        c->get_data_time = vsc_get_usec();
        h->write_stage2_total += c->get_data_time - c->get_scmd_time;
    }
    else if ((vsc_is_read_cmd(sc->cmnd[0])) {
        c->back_data_time = vsc_get_usec();
        h->read_stage2_total += c->back_data_time - c->get_scmd_time;
    }
#endif

    spin_unlock_bh(&h->lock);

    retval = vsc_xfer_data(scsi_data, mode, h, iovec, sc);
    if (unlikely(retval < 0))
        goto errout;
    totallen += retval;

    /* 将iovec长度传到上层函数，用于跳转至下一个scsi cmd */
    *data_len = scsi_data->data_len;

    return totallen;
errout:
    return retval;
}

#define VSC_SCSI_BUF_HEAD_MAGIC 0x55AA33CC
#define VSC_SCSI_BUF_TAIL_MAGIC 0x11EE22DD

/* 为buffer增加头尾magic校验，提前发现踩内存的问题 */
static inline void* vsc_alloc_scsi_buf(void)
{
    char *buf = mempool_alloc(scsi_buf_pool, GFP_KERNEL);
    if (unlikely(!buf)) {
        return buf;
    }

    *(__u32*)buf = VSC_SCSI_BUF_HEAD_MAGIC;
    *(__u32*)(buf+sizeof(__u32)+MAX_SCSI_DATA_AND_RSP_LENGTH) = VSC_SCSI_BUF_TAIL_MAGIC;
    return buf+sizeof(__u32);
}

static int vsc_free_scsi_buf(void *buffer)
{
    char *buf = (char*)buffer;
    __u32 head_magic = *(__u32*)(buf-sizeof(__u32));
    __u32 tail_magic = *(__u32*)(buf+MAX_SCSI_DATA_AND_RSP_LENGTH);
    
    if (unlikely(head_magic!=VSC_SCSI_BUF_HEAD_MAGIC || tail_magic!=VSC_SCSI_BUF_TAIL_MAGIC)) {
        vsc_err("fatal error, error magic 0x%x tail_magic 0x%x", head_magic, tail_magic);
        mempool_free(buf-sizeof(__u32), scsi_buf_pool);
        return -EFAULT;
    }
    
    mempool_free(buf-sizeof(__u32), scsi_buf_pool);
    return 0;
}

static ssize_t vsc_multi_scsi_cmd_data(unsigned int mode, struct vsc_ctrl_info *h,
    const char  __user *  user_ptr, int len)
{
    char *buf;
    char *temp;
    struct vsc_scsi_data_msg *scsi_data_msg;
    int scsi_data_len = 0;
    int totallen = 0;
    int index = 0;
    int ret_len = 0;
    int count = 0;

    /* buf长度为MAX_SCSI_DATA_AND_RSP_LENGTH */
    buf = vsc_alloc_scsi_buf();
    if(unlikely(!buf)) {
        vsc_err("alloc scsi_data buffer failed.\n");
        return -ENOMEM;
    }

    /* 复制请求数据头 */
    if (copy_from_user(buf, user_ptr, len)) {
        vsc_err("copy scsi data msg head failed.\n");
        vsc_free_scsi_buf(buf);
        return -EFAULT;
    }

    scsi_data_msg = (struct vsc_scsi_data_msg *)buf;
    temp = buf;

    count = scsi_data_msg->reserved[0];
    if (unlikely(count == 0 || count > MAX_BATCH_SCSI_CMD_COUNT)) {
        vsc_err("too many cmd from user_space, %d.\n", count);
        vsc_free_scsi_buf(buf);
        return -EFAULT;
    }
    
    for (index = 0; index < count; index++) {
        ret_len = vsc_scsi_cmd_data(mode, h, temp, len, &scsi_data_len);
        if (unlikely(ret_len < 0)) {
            scsi_data_msg = (struct vsc_scsi_data_msg *)temp;
            vsc_err("%s cmd_data error, %d/%d, ret_len %d 1st type %d len %d reserve1 %d cmd_sn %d tag %d data_len %d host %d\n",
                mode == VSC_MODE_READ ? "Read" : "Write",
                index, count, ret_len, scsi_data_msg->type, len, scsi_data_msg->reserved[1],
                scsi_data_msg->cmd_sn, scsi_data_msg->tag, scsi_data_msg->scsi_data_len, vsc_ctrl_get_host_no(h));
            vsc_free_scsi_buf(buf);
            return -EIO;
        }
        totallen += ret_len;
        temp += SCSI_MAX_DATA_BUF_SIZE;
        len -= SCSI_MAX_DATA_BUF_SIZE;
    }
    
    return vsc_free_scsi_buf(buf);
}

static ssize_t vsc_response_multi_scsi_cmd(struct vsc_ctrl_info *h,
    const char *user, int len);

static ssize_t vsc_response_multi_data_and_rsp(unsigned int mode, struct vsc_ctrl_info *h,
        const char  __user *  user_ptr, int len)
{
    char *buf;
    char *temp;
    struct vsc_scsi_data_msg *scsi_data_msg;
    int scsi_data_len = 0;
    int totallen = 0;
    int index = 0;
    int ret_len = 0;
    int cmd_count = 0;
    int read_count = 0;
    int temp_len = 0;

    if (unlikely(len != MAX_SCSI_DATA_AND_RSP_LENGTH)) {
        vsc_err("expect_len %d actual_len %u \n", (int)MAX_SCSI_DATA_AND_RSP_LENGTH, len);
        return -EINVAL;
    }

    buf = vsc_alloc_scsi_buf();
    if(unlikely(!buf)) {
        vsc_err("alloc scsi_data buffer failed.\n");
        return -ENOMEM;
    }

    /* 复制请求数据头 */
    if (unlikely(copy_from_user(buf, user_ptr, len))) {
        vsc_err("copy scsi data msg head failed.\n");
        vsc_free_scsi_buf(buf);
        return -EFAULT;
    }
    scsi_data_msg = (struct vsc_scsi_data_msg *)buf;
    read_count = scsi_data_msg->reserved[0];
    cmd_count = scsi_data_msg->reserved[1];

    temp = buf;
    temp_len = len;

    if (unlikely(read_count > MAX_BATCH_SCSI_RSP_COUNT || cmd_count > MAX_BATCH_SCSI_RSP_COUNT || cmd_count == 0)) {
        vsc_err("invalid read_cnt %d or cmd_count %d.\n", read_count, cmd_count);
        vsc_free_scsi_buf(buf);
        return -EFAULT;
    }
    
    for (index = 0; index < read_count; index++) {
        ret_len = vsc_scsi_cmd_data(mode, h, temp, temp_len, &scsi_data_len);
        if (unlikely(ret_len < 0)) {
            scsi_data_msg = (struct vsc_scsi_data_msg *)temp;
            vsc_err("xfer cmd_data error, %d/%d, ret_len %d 1st type %d len %d cmd_sn %d tag %d data_len %d host %d\n",
                index, read_count, ret_len, scsi_data_msg->type, temp_len,
                scsi_data_msg->cmd_sn, scsi_data_msg->tag, scsi_data_msg->scsi_data_len, vsc_ctrl_get_host_no(h));
            vsc_free_scsi_buf(buf);
            return -EIO;
        }
        totallen += ret_len;
        temp += SCSI_MAX_DATA_BUF_SIZE;
        temp_len -= SCSI_MAX_DATA_BUF_SIZE;
    }

    ret_len = vsc_response_multi_scsi_cmd(h, buf + MAX_BATCH_SCSI_RSP_COUNT * SCSI_MAX_DATA_BUF_SIZE,
        len - MAX_BATCH_SCSI_RSP_COUNT * SCSI_MAX_DATA_BUF_SIZE);
    if (unlikely(ret_len < 0)) {
        vsc_err("response error reserve[0]=%d reserve[1]=%d ret_len=%d len=%d host=%d\n", read_count,
            cmd_count, ret_len, len, vsc_ctrl_get_host_no(h));
        vsc_free_scsi_buf(buf);
        return -EIO;
    }

    return vsc_free_scsi_buf(buf);
}

/**************************************************************************
 功能描述  : 读取scsi信息
 参    数  : vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
ssize_t vsc_get_msg(struct vsc_ctrl_info *h, struct file *file, char  __user *  user_ptr, int len)
{
    ssize_t retval = 0;
    unsigned int type;

    if (unlikely(!h)) {
        retval = -EFAULT;
        goto errout;
    }
   
    /* 读取消息类型 */
    if (unlikely(get_user(type, user_ptr))) {
        vsc_err("Request msg: copy req head data failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }

    if (likely(VSC_MSG_TYPE_SCSI_CMD == type)) {
        retval = vsc_get_scsi_cmd_msg(h, file, user_ptr, len);
    } else if (likely(VSC_MSG_TYPE_SCSI_DATA == type)) {
        retval = vsc_multi_scsi_cmd_data(VSC_MODE_READ, h, user_ptr, len);
    } else {
        retval = -EINVAL;
        goto errout;
    }

    if (unlikely(retval < 0)) {
        goto errout;
    }

    return retval;
errout:
    h->stat.read_fail_count++;
    return retval;
}

/**************************************************************************
 功能描述  : 唤醒阻塞读取的进程
 参    数  : vsc_ctlr_info *h:     host控制句柄
 返 回 值  : 无
**************************************************************************/
void vsc_interrupt_sleep_on_read(struct vsc_ctrl_info *h)
{
    if (!h) {
        return;
    }

    /* 唤醒事件 */
    if (waitqueue_active(&h->queue_wait)) {
        h->wakeup_pid = 1;
        wake_up(&h->queue_wait);
    }

    return;
}

/**************************************************************************
 功能描述  : scsi命令处理任务
 参    数  : vsc_ctlr_info *h:     host控制句柄
             user_ptr              用户态回应数据指针
             len                   数据长度
 返 回 值  : ssize_t 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_response_scsi_cmd_new(struct vsc_ctrl_info *h, struct vsc_scsi_rsp_msg *rsp_msg)
{
    struct vsc_cmnd_list *c = NULL;
    struct scsi_cmnd *sc = NULL;
    //struct vsc_scsi_rsp_msg rsp_msg;
    int retval = 0;
    int total_len = 0;
    int resid = 0;
    int sc_type;

    if (unlikely(!h)) {
        retval = -EFAULT;
        goto errout;
    }

    if (unlikely(rsp_msg->tag >= h->nr_cmds)) {
        vsc_err("Response cmd error: tag is invalid, host = %u, tag = %d\n", vsc_ctrl_get_host_no(h), rsp_msg->tag);
        retval = -EIO;
        goto errout;
    }

    /* 按照tag查找command指针 */
    spin_lock_bh(&h->lock);
    c = h->cmd_pool_tbl[rsp_msg->tag];
    if (unlikely(c->stat != CMD_STAT_READ_COMP || c->cmd_sn != rsp_msg->cmd_sn)) {
        spin_unlock_bh(&h->lock);

        //vsc_debug_cmd(vsc_ctrl_get_host_no(h));
        vsc_err("response cmd error: host = %u, rsp.sn = %u, rsp.tag = %d, cmd.stat = %d, cmd.tag = %d, cmd.sn = %u\n",
                vsc_ctrl_get_host_no(h),
                rsp_msg->cmd_sn, rsp_msg->tag, c->stat, c->cmd_index, c->cmd_sn);
        c = NULL;
        retval = -EIO;
        goto errout;
    }

    /* 判断命令是否已经处理 */
    if (unlikely(list_empty(&c->list))) { 
        spin_unlock_bh(&h->lock);
        vsc_err("response cmd error: already removed host = %u, rsp.sn = %u, rsp.tag = %d, cmd.stat = %d\n",
                vsc_ctrl_get_host_no(h),
                rsp_msg->cmd_sn, rsp_msg->tag, c->stat);
        c = NULL;
        retval = -EIO;
        goto errout;
    }

    /* 从命令队列中删除命令 */
    removeQ(c);
    /* 设置标志为正在处理 */
    c->stat = CMD_STAT_RESP;

    sc = c->scsi_cmd;
    sc_type = sc->cmnd[0];

#ifdef __VSC_LATENCY_DEBUG__
    if ((vsc_is_write_cmd(sc_type)) {
        c->back_rsp_time = vsc_get_usec();
        h->write_stage3_total += c->back_rsp_time - c->get_data_time;
    }
    else if ((vsc_is_read_cmd(sc_type)) {
        c->back_rsp_time = vsc_get_usec();
        h->read_stage3_total += c->back_rsp_time - c->back_data_time;
    }

    if ((vsc_is_readwrite_cmd(sc_type)) {
        if (c->back_rsp_time > c->start_queue_time && c->back_rsp_time - c->start_queue_time > 30000000) {
            vsc_err("too long to rsp(%llu us), host(%d) cmd_sn(%d) tag(%d) start_queue(%llu) get_scmd(%llu) rsp_time(%llu)",
                    vsc_ctrl_get_host_no(h), c->cmd_sn, c->cmd_index, c->back_rsp_time - c->start_queue_time,
                    c->start_queue_time, c->get_scmd_time, c->back_rsp_time);
        }
    }
#endif

    h->io_running--;
    spin_unlock_bh(&h->lock);

    /* 保存错误码 */
    sc->result = (DID_OK << 16);              /* host byte */

     /* 错误检查 */
    vsc_sense_check(c, rsp_msg->command_status);

    /* 这里将msg_byte和status_byte的设置放在最后, 为了保证不丢失这2个错误信息 */
    sc->result |= (COMMAND_COMPLETE << 8);    /* msg byte */
    sc->result |= ((rsp_msg->scsi_status & 0x7F) << 1); /* [zr] status byte set by target,
                                                                  extract by ML using status_byte() */

    /* 跟踪消息 */
    if (unlikely(VSC_TRACE_RSP_SCSI_CMD & trace_switch)) {
        vsc_err("host:%u, sn:%u tag:%u  command_status = %x, scsi_status = %x, result = %x\n",
                vsc_ctrl_get_host_no(h),
                rsp_msg->cmd_sn, rsp_msg->tag, rsp_msg->command_status, rsp_msg->scsi_status, sc->result);
    }

    /* 上报scsi命令结果 */
    /* 设置剩余数据长度为0 */
    scsi_set_resid(sc, resid);
    sc->scsi_done(sc);

#ifdef __VSC_LATENCY_DEBUG__
    if ((vsc_is_write_cmd(sc_type)) {
        h->write_total += vsc_get_usec() - c->start_queue_time;
    }
    else if (vsc_is_read_cmd(sc_type)) {
        h->read_total += vsc_get_usec() - c->start_queue_time;
    }
#endif

    /* 释放命令 */
    vsc_cmd_put(c);

    return total_len;

errout:
    if (c) {
        spin_lock_bh(&h->lock);
        c->stat = CMD_STAT_READ_COMP;
        addQ(&h->cmpQ, c);
        spin_unlock_bh(&h->lock);
    }
    return retval;
}

static ssize_t vsc_response_multi_scsi_cmd(struct vsc_ctrl_info *h,
    const char *user, int len)
{
    struct vsc_scsi_rsp_msg *rsp_msg;
    int retval = 0;
    int total_len = 0;
    int index = 0;
    int cmd_count = 0;
    
    if (unlikely(!h)) {
        retval = -EFAULT;
        goto errout;
    }

    if (unlikely(len != sizeof(struct vsc_scsi_rsp_msg)*MAX_BATCH_SCSI_RSP_COUNT)) {
        vsc_err("expect_len %d actual_len %u \n",
            (int)sizeof(struct vsc_scsi_rsp_msg)*MAX_BATCH_SCSI_RSP_COUNT, len);
        retval = -EFAULT;
        goto errout;
    }

    rsp_msg = (struct vsc_scsi_rsp_msg *)user;
    cmd_count = rsp_msg->reserved[0];

    if (unlikely(cmd_count == 0 || cmd_count > MAX_BATCH_SCSI_RSP_COUNT)) {
        vsc_err("invalid cmd_count %d \n", cmd_count);
        retval = -EINVAL;
        goto errout;
    }
    
    for (index = 0; index < cmd_count; index++) {
        retval = vsc_response_scsi_cmd_new(h, &rsp_msg[index]);
        if(unlikely(retval < 0)) {
            vsc_err("%d/%d response process error, len %d host %d\n", index, cmd_count, len, vsc_ctrl_get_host_no(h));
            return -EIO;
        }

        total_len += retval;
    }
    h->put_rsp_times++;
    h->put_rsp_total += cmd_count;

    return total_len;
errout:
    // TODO: do some error handle
    return retval;
}

/**************************************************************************
 功能描述  : 事件回应消息
 参    数  : vsc_ctlr_info *h:     host控制句柄
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
static ssize_t vsc_response_event(struct vsc_ctrl_info *h, const char __user *  user_ptr, int len)
{
    struct vsc_event_list *e = NULL;
    struct vsc_scsi_event event;
    const char __user *user = user_ptr;
    unsigned total_len = 0;
    int retval = 0;
    struct vsc_scsi_msg_data *event_msg = NULL;

    if (unlikely(!h) || unlikely(!user_ptr)) {
        retval = -EFAULT;
        goto errout;
    }
   
    /* 读取回应头 */
    if (copy_from_user(&event, user, sizeof(event))) {
        vsc_err("Response event: copy resp head data failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }
    user += sizeof(event);
    total_len += sizeof(event);

    if (event.tag >= h->nr_events) {
        vsc_err("response event error: tag is invalid, host = %u, tag = %d\n", 
            vsc_ctrl_get_host_no(h), event.tag);
        retval = -EIO;
        goto errout;
    }

    spin_lock_bh(&h->lock);
    e = h->event_pool_tbl[event.tag];
    if (e->stat != EVENT_LIST_STAT_READ_COMP || e->event_sn != event.event_sn) {
        spin_unlock_bh(&h->lock);
        vsc_err("response event error: host = %u, rsp.sn = %u, rsp.tag = %u, e.stat = %d, e.tag = %u, e.sn = %u\n",
                vsc_ctrl_get_host_no(h), 
                event.event_sn, event.tag, e->stat, e->event_index, e->event_sn);
        e = NULL;
        retval = -EIO;
        goto errout;
    }
    e->stat = EVENT_LIST_STAT_RESP;
    spin_unlock_bh(&h->lock);

    /* 如果数据长度不正确 */
    if (event.data_length > len || event.data_length >= sizeof(e->event_data)) {
        vsc_err("Response event: invalid event data_length, host = %u\n, event.data_lenth=%u, len=%d.\n", 
            vsc_ctrl_get_host_no(h), event.data_length, len);
        retval = -EIO;
        goto errout;
    }

    /* 获取事件内容 */
    e->event_data_len = event.data_length;
    if (copy_from_user(e->event_data, user, event.data_length)) {
        vsc_err("Response event: copy resp data failed, host = %u\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }

    /* 固定获取event事件数据 */
    event_msg = (struct vsc_scsi_msg_data *)(e->event_data);
    if (e->event_callback) {
        if (event_msg->data_len > event.data_length) {
            vsc_err("Response event: event msg data_len invalid. "
                        "host=%u, type=%d, event_msg.data_len=%u, event.data_length=%u.\n", 
                    vsc_ctrl_get_host_no(h), 
                    event_msg->data_type, event_msg->data_len, event.data_length);
            retval = -EFAULT;
            goto errout;    
        }
        retval = e->event_callback(e->h, EVENT_SHOOT, e, event_msg->data_type, event_msg->data, event_msg->data_len);
        /* 唤醒处理事件 */
        e->shoot = EVENT_SHOOT_SHOOT;
        wake_up(&e->wait);
        if (retval) {
            goto errout;
        }
    }

    /* 跟踪消息 */
    if (unlikely(VSC_TRACE_RSP_EVENT & trace_switch)) {
        vsc_err("event resp: host:%u sn:%u tag:%d  len:%d  type = %x\n",
                vsc_ctrl_get_host_no(h), 
                event.event_sn, event.tag, event.data_length, event_msg->data_type);
    }

    return total_len;
errout:
    return retval;
}

/**************************************************************************
 功能描述  : scsi命令处理任务
 参    数  : vsc_ctlr_info *h:     host控制句柄
 返 回 值  : ssize_t： 数据长度，其他：错误码
**************************************************************************/
ssize_t vsc_response_msg(struct vsc_ctrl_info *h, const char __user *  user_ptr, int len)
{
    int retval = 0;
    unsigned int type;

    if (unlikely(!h)) {
        retval = -EFAULT;
        goto errout;
    }
   
    /* 读取消息类型 */
    if (get_user(type, user_ptr)) {
        vsc_err("Response msg: copy resp head data failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        retval = -EFAULT;
        goto errout;
    }

    if (unlikely(VSC_MSG_TYPE_EVENT == type)) {
        retval = vsc_response_event(h, user_ptr, len);
    } else if (likely(VSC_MSG_TYPE_SCSI_DATA == type)) {
        retval = vsc_response_multi_data_and_rsp(VSC_MODE_WRITE, h, user_ptr, len);
    } else {
        retval = -EINVAL;
        vsc_err("invalid msg type %d\n", type);
        goto errout;
    }

    if (unlikely(retval < 0)) {
        goto errout;
    }

    return retval;

errout:
    if (likely(h)) {
        h->stat.write_fail_count++;
    }
    return retval;
}


/**************************************************************************
 功能描述  : 控制结构体释放
 参    数  : vsc_ctlr_info *h:   host控制句柄
 返 回 值  : 无
**************************************************************************/
static void inline vsc_ctlr_info_destroy(struct vsc_ctrl_info *h)
{
    int i = 0;
    struct vsc_cmnd_list *c;
    struct vsc_event_list *e;

    if (h->event_pool_tbl) {
        for (i = 0; i < h->nr_events; i++) {
            e = h->event_pool_tbl[i];
            if (e) {
                kfree(e);
                h->event_pool_tbl[i] = NULL;
            }
        }
        kfree(h->event_pool_tbl);
    }

    if (h->event_pool_bits) {
        kfree(h->event_pool_bits);
    }

    if (h->cmd_pool_tbl) {
        for (i = 0; i < h->nr_cmds; i++) {
            c = h->cmd_pool_tbl[i];
            if (c) {
                kfree(c);
                h->cmd_pool_tbl[i] = NULL;
            }
        }
        kfree(h->cmd_pool_tbl);
    }

    if (h->cmd_pool_bits) {
        kfree(h->cmd_pool_bits);
    }
}

/**************************************************************************
 功能描述  : 控制结构体初始化
 参    数  : vsc_ctlr_info *h:     host控制句柄
             struct Scsi_Host *sh: scsi中层的指针
             int nr_cmds           命令队列个数
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
static int inline vsc_ctrl_init(struct vsc_ctrl_info *h, struct Scsi_Host *sh, int nr_cmds, int vbs_host_id)
{
    int retval = 0;
    struct vsc_cmnd_list *c;
    struct vsc_event_list *e;
    int i = 0;

    if (!h || !sh) {
        return -EINVAL;
    }

    memset(h, 0, sizeof(*h));

    /*初始化hash链表*/
    INIT_LIST_HEAD(&h->cmpQ);
    INIT_LIST_HEAD(&h->reqQ);
    INIT_LIST_HEAD(&h->rspQ);
    INIT_LIST_HEAD(&h->event_reqQ);
    INIT_LIST_HEAD(&h->event_rspQ);
   
    /*初始化锁*/
    spin_lock_init(&h->lock);

    /* 初始化target信息 */
    h->file = NULL;

    /* 初始等待队列 */
    init_waitqueue_head(&h->queue_wait);

    h->wakeup_pid = 0;
    h->scsi_host = sh; 
    h->vbs_host_id = vbs_host_id;
    h->nr_cmds = (nr_cmds) ? nr_cmds : VSC_MAX_CMD_NUM;
    h->nr_events = VSC_DEFAULT_EVENT_NUMBER;
    h->suspend = 0;
    h->target_abnormal_time = 0;
    h->io_running = 0;
    atomic_set(&h->target_abnormal_lock, 0);
    init_timer(&h->target_abnormal_timer);
    setup_timer(&h->target_abnormal_timer, vsc_target_abnormal, (unsigned long)h);

    /* 初始化缓冲区位表信息，并置0 */
    h->cmd_pool_bits = kzalloc(((h->nr_cmds + BITS_PER_LONG - 1) / BITS_PER_LONG) * sizeof(unsigned long), GFP_KERNEL);
    if (!h->cmd_pool_bits) {
        retval = -ENOMEM;
        vsc_err("malloc cmd memory for pool bits failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        goto err_out;
    }

    /* 初始化缓冲区大小 */
    h->cmd_pool_tbl = kzalloc( h->nr_cmds * sizeof(*h->cmd_pool_tbl), GFP_KERNEL);
    if (!h->cmd_pool_tbl) {
        retval = -ENOMEM;
        vsc_err("malloc cmd memory for pool failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        goto err_out;
    }

    /* 申请对应的命令缓冲区 */
    for (i = 0; i < h->nr_cmds; i++) {
        c = kmalloc(sizeof(*c), GFP_KERNEL);
        if (!c) {
            retval = -ENOMEM;
            goto err_out;
        }
        h->cmd_pool_tbl[i] = c;
    }
    
    /* 初始化缓冲区位表信息，并置0 */
    h->event_pool_bits = kzalloc(((h->nr_events + BITS_PER_LONG - 1) / BITS_PER_LONG) * sizeof(unsigned long), GFP_KERNEL);
    if (!h->event_pool_bits) {
        retval = -ENOMEM;
        vsc_err("malloc event memory for pool bits failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        goto err_out;
    }

    /* 初始化缓冲区大小 */
    h->event_pool_tbl = kzalloc( h->nr_events * sizeof(*h->event_pool_tbl), GFP_KERNEL);
    if (!h->event_pool_tbl) {
        retval = -ENOMEM;
        vsc_err("malloc event memory for pool failed, host is %u.\n", vsc_ctrl_get_host_no(h));
        goto err_out;
    }

    /* 申请对应的事件缓冲区 */
    for (i = 0; i < h->nr_events; i++) {
        e = kmalloc(sizeof(*e), GFP_KERNEL);
        if (!e) {
            retval = -ENOMEM;
            goto err_out;
        }
        h->event_pool_tbl[i] = e;
    }

    return 0;

err_out:
    vsc_ctlr_info_destroy(h);

    return retval;
}

/**************************************************************************
 功能描述  : 获取host编号
 参    数  : vsc_ctlr_info *h:   host控制句柄
 返 回 值  : 正数：host_no，错误：全F
**************************************************************************/
unsigned int vsc_ctrl_get_host_no(struct vsc_ctrl_info *h)
{
    if (unlikely(!h)) {
        return -1;
    }

    if (!h->scsi_host) {
        return -1;
    }

    //return h->scsi_host->host_no;
    return h->vbs_host_id;
}

/**************************************************************************
 功能描述  : 根据host no查找控制块
 参    数  : hostno:        控制器编号
 返 回 值  : vsc_ctlr_info *h:   host控制句柄
**************************************************************************/
struct vsc_ctrl_info * vsc_get_ctlr_by_host( unsigned int host_no )
{
    struct vsc_host_mgr *mgr = NULL;
    __u32 vbs_id = vsc_get_vbs_id_by_host_id(host_no);
    __u32 host_index = vsc_get_host_index_in_vbs(host_no);

    if (host_index >= MAX_HOST_NR_PER_VBS) {
        return NULL;
    }
    mutex_lock(&ctrl_info_list_lock);
    list_for_each_entry(mgr, &g_host_mgr_list, node) {
        if (mgr->vbs_id == vbs_id) {
            mutex_unlock(&ctrl_info_list_lock);
            return mgr->host_list[host_index];
        }
    }
    mutex_unlock(&ctrl_info_list_lock);

    return NULL;
}

static int vsc_add_host_to_mgr(struct vsc_ctrl_info *h, __u32 host_id)
{
    struct vsc_host_mgr *mgr = NULL;
    __u32 vbs_id = vsc_get_vbs_id_by_host_id(host_id);
    __u32 host_index = vsc_get_host_index_in_vbs(host_id);

    if (host_index >= MAX_HOST_NR_PER_VBS) {
        vsc_err("fatal error..., host_id=%d, host_index=%d\n", host_id, host_index);
        return FAILED;
    }

    mutex_lock(&ctrl_info_list_lock);
    list_for_each_entry(mgr, &g_host_mgr_list, node) {
        if (mgr->vbs_id == vbs_id) {
            if (mgr->host_count >= MAX_HOST_NR_PER_VBS) {
                vsc_err("fatal error..., host_count=%d\n", mgr->host_count);
                mutex_unlock(&ctrl_info_list_lock);
                return FAILED;
            }

            mgr->host_list[host_index] = h;
            h->silbing_hosts = mgr;
            mgr->host_count++;
            mutex_unlock(&ctrl_info_list_lock);
            return 0;
        }
    }

    /* 没有找到相应的vbs，需要添加 */
    mgr = kmalloc(sizeof(struct vsc_host_mgr), GFP_KERNEL);
    if (!mgr) {
        vsc_err("fatal to alloc vbs_host_mgr\n");
        mutex_unlock(&ctrl_info_list_lock);
        return FAILED;
    }
    memset(mgr, 0, sizeof(struct vsc_host_mgr));

    mgr->vbs_id = vbs_id;
    mgr->host_list[host_index] = h;
    h->silbing_hosts = mgr;
    mgr->host_count++;
    list_add_tail(&mgr->node, &g_host_mgr_list);
    mutex_unlock(&ctrl_info_list_lock);
    return 0;
}

/**************************************************************************
 功能描述  : 读取host信息的proc接口
 参    数  : host_info:   注册信息
 返 回 值  : 返回host控制句柄
**************************************************************************/
static int vsc_hostinfo_proc_read(struct seq_file *v_pstFile, void *unused)
{
    struct vsc_ctrl_info *h = NULL;
    int sdev_count = 0;
    struct scsi_device *sdev = NULL;

    h = (struct vsc_ctrl_info *)v_pstFile->private;
    if (!h) {
        return -EFAULT;
    }

    mutex_lock(&ctrl_info_list_lock);

    /* 获取设备数目 */
    shost_for_each_device(sdev, h->scsi_host) {
        sdev_count ++;
    }
    
    /* 打印输出信息 */
    seq_printf(v_pstFile, "Host No.:\t%u\n", vsc_ctrl_get_host_no(h));
    seq_printf(v_pstFile, "SCSI_Host ID.:\t%u\n", h->scsi_host->host_no);
    seq_printf(v_pstFile, "Max channel:\t%u\n", h->scsi_host->max_channel);
    seq_printf(v_pstFile, "Max id:\t\t%u\n", h->scsi_host->max_id);
#if LINUX_VERSION_CODE < (KERNEL_VERSION(3, 17, 0))
    seq_printf(v_pstFile, "Max lun:\t%u\n", h->scsi_host->max_lun);
#else
    seq_printf(v_pstFile, "Max lun:\t%llu\n", h->scsi_host->max_lun);
#endif
    seq_printf(v_pstFile, "Max sg count: \t%u\n", h->scsi_host->sg_tablesize);
    seq_printf(v_pstFile, "Max cmd count:\t%u\n", h->nr_cmds);
    seq_printf(v_pstFile, "Cmd per lun:\t%u\n", h->scsi_host->cmd_per_lun);
    seq_printf(v_pstFile, "Host status:\t%s\n", (h->suspend) ? "Suspend" : "Running");
    seq_printf(v_pstFile, "Host blocked:\t%s\n", (h->scsi_host->host_self_blocked)? "Blocked" : "Unblock");
    seq_printf(v_pstFile, "Device count:\t%d\n", sdev_count);
    seq_printf(v_pstFile, "Cmd sn: \t%u (Overflow count: %u)\n", (__u32)((h->cmd_sn) & 0xFFFFFFFF), (__u32)((h->cmd_sn)>>32));
    seq_printf(v_pstFile, "Event sn:\t%llu\n", h->event_sn);
    seq_printf(v_pstFile, "Attatched status:\t%s\n", (h->file) ? "Attached" : "Detached");
    seq_printf(v_pstFile, "Abort cmd count:\t%u\n", h->stat.abort_cmd_count);
    seq_printf(v_pstFile, "Reset device count:\t%u\n", h->stat.reset_dev_count);
    seq_printf(v_pstFile, "Vsc read fail count:\t%u\n", h->stat.read_fail_count);
    seq_printf(v_pstFile, "Vsc write fail count:\t%u\n", h->stat.write_fail_count);
    seq_printf(v_pstFile, "Host Running Cmd    :\t%u\n", h->io_running);

    seq_printf(v_pstFile, "\nGet Command Times :\t%u\n", h->get_cmd_times);
    seq_printf(v_pstFile, "Get Command Total :\t%u\n", h->get_cmd_total);
    seq_printf(v_pstFile, "Put Response Times:\t%u\n", h->put_rsp_times);
    seq_printf(v_pstFile, "Put Response Total:\t%u\n", h->put_rsp_total);

#ifdef __VSC_LATENCY_DEBUG__
    seq_printf(v_pstFile, "\nWrite Cmd Count   :\t%llu\n", h->write_cmd_count);
    if (h->write_cmd_count) {
        seq_printf(v_pstFile, "Write Stage1 Lat:\t%llu\n", h->write_stage1_total/h->write_cmd_count);
        seq_printf(v_pstFile, "Write Stage2 Lat:\t%llu\n", h->write_stage2_total/h->write_cmd_count);
        seq_printf(v_pstFile, "Write Stage3 Lat:\t%llu\n", h->write_stage3_total/h->write_cmd_count);
        seq_printf(v_pstFile, "Write Total Lat :\t%llu\n", h->write_total/h->write_cmd_count);
    }
    seq_printf(v_pstFile, "\nRead Cmd Count    :\t%llu\n", h->read_cmd_count);
    if (h->read_cmd_count) {
        seq_printf(v_pstFile, "Read Stage1 Lat:\t%llu\n", h->read_stage1_total/h->read_cmd_count);
        seq_printf(v_pstFile, "Read Stage2 Lat:\t%llu\n", h->read_stage2_total/h->read_cmd_count);
        seq_printf(v_pstFile, "Read Stage3 Lat:\t%llu\n", h->read_stage3_total/h->read_cmd_count);
        seq_printf(v_pstFile, "Read Total Lat :\t%llu\n", h->read_total/h->read_cmd_count);
    }
#endif

    spin_lock_bh(&h->lock);
    seq_printf(v_pstFile, "\nReq Queue Count   :\t%u\n", list_size(&h->reqQ));
    seq_printf(v_pstFile, "Cmp Queue Count   :\t%u\n", list_size(&h->cmpQ));
    seq_printf(v_pstFile, "Rsp Queue Count   :\t%u\n", list_size(&h->rspQ));
    spin_unlock_bh(&h->lock);

    mutex_unlock(&ctrl_info_list_lock);
    return 0;
}
static int vsc_seq_open_dev(struct inode *inode,struct file *file)
{
#if LINUX_VERSION_CODE < (KERNEL_VERSION(3, 10, 0))
    return single_open(file, vsc_hostinfo_proc_read, PDE(inode)->data);
#else
    return single_open(file, vsc_hostinfo_proc_read, PDE_DATA(inode));
#endif
}

static ssize_t vsc_seq_write(struct file *file, const char __user *user_ptr,
        size_t len, loff_t *pos)
{
    char buf[128];
    int retval = 0;
#ifdef __VSC_LATENCY_DEBUG__
    struct vsc_ctrl_info *h = NULL;
    struct vsc_host_mgr *mgr = NULL;
    int j;
#endif

    if (unlikely(!file)) {
        retval = -EFAULT;
        goto errout;
    }

    if (copy_from_user(buf, user_ptr, len)) {
        vsc_err("seq write failed\n");
        retval = -EFAULT;
        goto errout;
    }

    /* 暂不支持其他值 */
    if (!strncmp(buf, "4", 1)) {
        io_per_host=4;
        io_per_host_shift=2;
        vsc_info("io_per_host change to %d, io_per_host_shift=%d\n", io_per_host, io_per_host_shift);
    }
    else if (!strncmp(buf, "2", 1)) {
        io_per_host=2;
        io_per_host_shift=1;
        vsc_info("io_per_host change to %d, io_per_host_shift=%d\n", io_per_host, io_per_host_shift);
    }
    else if (!strncmp(buf, "1", 1)) {
        io_per_host=1;
        io_per_host_shift=0;
        vsc_info("io_per_host change to %d, io_per_host_shift=%d\n", io_per_host, io_per_host_shift);
    }
    else if (!strncmp(buf, "0", 1)) {
        io_per_host=0;
        vsc_info("io_per_host change to %d\n", io_per_host);
    }
    
#ifdef __VSC_LATENCY_DEBUG__
    if (!strncmp(buf, "clear", 5)) {
        /* 清除io统计 */
        mutex_lock(&ctrl_info_list_lock);
        list_for_each_entry(mgr, &g_host_mgr_list, node) {
            if (mgr->host_count > MAX_HOST_NR_PER_VBS) {
                vsc_err("invalid host_count=%d", mgr->host_count);
                continue;
            }
            for (j=0;j<mgr->host_count;j++) {
                if(!mgr->host_list[j]) {
                    continue;
                }

                h = mgr->host_list[j];
                spin_lock_bh(&h->lock);
                h->get_cmd_times = 0;
                h->get_cmd_total = 0;
                h->get_cdb_times = 0;
                h->get_cdb_total = 0;
                h->put_rsp_times = 0;
                h->put_rsp_total = 0;

                h->write_cmd_count = 0;
                h->read_cmd_count = 0;

                h->write_stage1_total = 0;
                h->write_stage2_total = 0;
                h->write_stage3_total = 0;
                h->write_total = 0;

                h->read_stage1_total = 0;
                h->read_stage2_total = 0;
                h->read_stage3_total = 0;
                h->read_total = 0;
                spin_unlock_bh(&h->lock);
            }
        }
    }
    else {
        vsc_err("invalid command, %s\n", buf);
    }
#endif

    return len;
errout:
    return retval;
}

struct file_operations g_proc_host_vsc_ops =
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    .owner = THIS_MODULE,
#endif
    .open = vsc_seq_open_dev,
    .read = seq_read,
    .write = vsc_seq_write,
    .llseek = seq_lseek,
    .release = NULL,
};

#if LINUX_VERSION_CODE < (KERNEL_VERSION(3, 17, 0))
static int vsc_user_scan(struct Scsi_Host *shost, unsigned int channel, unsigned int id, unsigned int lun)
{
    vsc_info("hostno: %d, try to rescan device: channel: %d, id: %d, lun: %d\n",
                shost->host_no, channel, id, lun);

    return 0;
}
#else
static int vsc_user_scan(struct Scsi_Host *shost, unsigned int channel, unsigned int id, unsigned long long lun)
{
    vsc_info("hostno: %d, try to rescan device: channel: %d, id: %d, lun: %llu\n",
                shost->host_no, channel, id, lun);
    return 0;
}
#endif   
                     
static struct scsi_transport_template vsc_transportt =
{
    .user_scan = vsc_user_scan,
    .device_size = sizeof(struct vsc_ioctl_disk_vol_name)+sizeof(atomic64_t),/*扩展单个device的io计数器*/
};

struct Scsi_Host *vsc_scsi_host_lookup(unsigned int host_no)
{
    struct vsc_ctrl_info *h = NULL;

    h = vsc_get_ctlr_by_host(host_no);
    if (NULL == h || NULL == h->scsi_host)
    {
        return NULL;
    }

    return scsi_host_get(h->scsi_host);
}

/**************************************************************************
 功能描述  : 添加host到系统中
 参    数  : host_info:   注册信息
 返 回 值  : 返回host控制句柄
**************************************************************************/
struct vsc_ctrl_info *vsc_register_host( struct vsc_ioctl_cmd_add_host *host_info )
{
    struct vsc_ctrl_info *h = NULL;
    struct Scsi_Host *sh  = NULL;
    long scsi_add_retval = -EFAULT;
    char proc_file_name[VSC_PROC_FILE_NAME_LEN];
    struct proc_dir_entry  *proc_file = NULL;
    int err_retval = 0;
    int sys_host_id = 0;
    
    if (!host_info) {
        return ERR_PTR(-EINVAL);
    }

    if (check_host_info(host_info)) {
        vsc_err("invalid param in host_info when add host!\n");
        return ERR_PTR(-EINVAL);
    }

    /* 防止并发注册相同的HOST，加锁保护 */
    mutex_lock(&ctrl_host_reg_lock);
    /* 检查系统中是否存在对应的host */
    /*scsi_host_lookup函数不能查找host no 超过65535的scsi_host*/
    sh = vsc_scsi_host_lookup(host_info->host);
    if (sh) {
        sys_host_id = sh->host_no;
        scsi_host_put(sh);
        sh = NULL;
        err_retval = -EEXIST;
        
        h = vsc_get_ctlr_by_host(host_info->host);
        if (!h) {
            err_retval = -ENODEV; /* [review] 中层有驱动中没有，返回给用户层错误，让用户层决定如何处理。
                                        所有中层跟驱动中不一致的场景，都说明发生过严重的系统错误或者非法操作。 */
            vsc_err("vsc_register_host(): host %u found in ml, but info not found in driver.\n", host_info->host);
        }
        else {
            /* vbs重启时，把sys host id回传给vbs */
            host_info->sys_host_id = sys_host_id;
        }
        h = NULL;
        goto err_out;
    }

    /* 检查是否有对应的host */
    h = vsc_get_ctlr_by_host(host_info->host);
    if (h) {
        h = NULL;
        //err_retval = -EEXIST;
        err_retval = -ENODEV; 
        vsc_err("vsc_register_host(): host %u info found in driver but host not found in ml.\n", host_info->host);
        goto err_out;
    }

    /* 注册scs host，并申请存放vsc_ctlr_info的内存 */
    sh = scsi_host_alloc(&vsc_driver_template, sizeof(struct vsc_ctrl_info));
    if (!sh) {
        vsc_err("register scsi driver template failed, host is %u.\n", host_info->host);
        err_retval = -ENOMEM;
        goto err_out;
    }

    /* 获取存放vsc_ctlr_info的内存 */
    h = shost_priv(sh);
    if (!h) {
        vsc_err("get shost priv failed, host is %u.\n", host_info->host);
        err_retval = -EFAULT;
        goto err_out;
    }
    
    /* 初始化host控制句柄 */
    if (vsc_ctrl_init(h, sh, host_info->max_nr_cmds, host_info->host)) {
        vsc_err("init ctrl info failed, host is %u.\n", host_info->host);
        h = NULL;
        err_retval = -EFAULT;
        goto err_out;
    }

    //sh->host_no = host_info->host;
    sh->io_port = 0;
    sh->n_io_port = 0;
    sh->this_id = -1;
    sh->max_channel = host_info->max_channel;
    sh->max_cmd_len = host_info->max_cmd_len;
    sh->max_lun = host_info->max_lun;
    sh->max_id = host_info->max_id;
    sh->can_queue = h->nr_cmds;
    sh->cmd_per_lun = host_info->cmd_per_lun;
    sh->shost_gendev.parent = NULL;
    sh->sg_tablesize = host_info->sg_count;
    sh->irq = 0;
    sh->unique_id = sh->irq;
    sh->transportt = &vsc_transportt;
    
    /* 通知SCSI中层添加host */
    scsi_add_retval = scsi_add_host(sh, NULL);
    if (scsi_add_retval) {
        vsc_err("add scsi host failed, host = %u, ret = %ld\n", sh->host_no, scsi_add_retval);
        err_retval = scsi_add_retval;
        goto err_out;
    }

     /* 创建proc目录文件 */
    snprintf(proc_file_name, VSC_PROC_FILE_NAME_LEN, "host%d", sh->host_no);
    //snprintf(proc_file_name, VSC_PROC_FILE_NAME_LEN, "host%d", host_info->host);
    proc_file = proc_create_data(proc_file_name, S_IRUGO, proc_vsc_root, &g_proc_host_vsc_ops, h);
    if (!proc_file) {
        vsc_err("vsc_register_host(): create vsc root proc file error, host is %u.\n", host_info->host);
        err_retval = -ENOMEM;
        goto err_out;
    }

    /* 将host指针加入到链表中 */
    vsc_add_host_to_mgr(h, host_info->host);
    /* 将host no回传给vbs */
    host_info->sys_host_id = sh->host_no;
    
    mutex_unlock(&ctrl_host_reg_lock);
    return h;

err_out:
    if (!scsi_add_retval) {
        scsi_remove_host(sh);
        vsc_err("vsc_register_host(): err_out, host %u removed in ml.", host_info->host);
    }
    if (proc_file) {
        remove_proc_entry(proc_file_name, proc_vsc_root);
        proc_file = NULL;
    }
    if (h) {
        vsc_ctlr_info_destroy(h); 
        vsc_err("vsc_register_host(): err_out, host %u info destroied in driver.", host_info->host);
    }

    if (sh) {
        scsi_host_put(sh);
    }
    mutex_unlock(&ctrl_host_reg_lock);
    return ERR_PTR(err_retval);
}

/**************************************************************************
 功能描述  : 删除指定的host
 参    数  : h:  scsi控制句柄 
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
static int vsc_do_remove_host(struct vsc_ctrl_info *h)
{
    struct Scsi_Host *sh = NULL;
    char proc_file_name[VSC_PROC_FILE_NAME_LEN];

    if (unlikely(!h)) {
        vsc_err("scsi ctlr info is null.\n");
        return -EINVAL;
    }

    sh = h->scsi_host;
    if (unlikely(!sh)) {
        vsc_err("scsi host is null, ctrl info is %p, host is %u.\n", h, vsc_ctrl_get_host_no(h));
        return -EFAULT;
    }

    del_timer_sync(&h->target_abnormal_timer);

    spin_lock_bh(&h->lock);
    h->file = NULL;
    spin_unlock_bh(&h->lock);

    /* 删除创建的proc文件 */
    snprintf(proc_file_name, VSC_PROC_FILE_NAME_LEN, "host%d", sh->host_no);
    remove_proc_entry(proc_file_name, proc_vsc_root);

    /* 从队列中删除数据 */
    //list_del(&h->list);

    /* 防止删除时死锁，唤醒设备 */
    vsc_resume_host(h);

    /* 丢弃所有在处理中的事件 */
    vsc_abort_all_event(h);

    /* 丢弃所有命令 */
    vsc_abort_all_cmd(h);

    /* 释放申请的资源 */
    scsi_remove_host(sh);
    vsc_ctlr_info_destroy(h);
    scsi_host_put(sh);
    
    return 0;
}

/**************************************************************************
 功能描述  : 删除指定的host
 参    数  : vsc_ctlr_info *h:   host控制句柄
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
int vsc_remove_host(struct vsc_ctrl_info *h)
{
    int retval = 0;

    mutex_lock(&ctrl_info_list_lock);
    retval = vsc_do_remove_host(h);
    mutex_unlock(&ctrl_info_list_lock);

    return retval;
}

/**************************************************************************
 功能描述  : 释放所有控制句柄
 参    数  :
 返 回 值  : 无
**************************************************************************/
void vsc_remove_all_host( void ) 
{
    struct vsc_host_mgr *mgr;
    struct vsc_host_mgr *temp;
    __u32 i;

    mutex_lock(&ctrl_info_list_lock);
    list_for_each_entry_safe(mgr, temp, &g_host_mgr_list, node) {
        if (mgr->host_count > MAX_HOST_NR_PER_VBS) {
            vsc_err("invalid host_count=%d", mgr->host_count);
            continue;
        }
        for (i=0;i<mgr->host_count;i++) {
            vsc_do_remove_host(mgr->host_list[i]);
            mgr->host_list[i] = NULL;
        }
        list_del(&mgr->node);
        kfree(mgr);
    }
    mutex_unlock(&ctrl_info_list_lock);
}


/**************************************************************************
 功能描述  : 在某一条系统挂载点信息中查找是否包含有当前设备名字
 参    数  :
 返 回 值  : 0:找到了  非0:没找到
**************************************************************************/
static int vsc_find_dev_in_mnt_info(const char *line, const char * name)
{
    int ret = 0; 
    char *devmane = NULL;
    char *dummy = NULL;

    if (unlikely(!line) || unlikely(!name)) {
        return -EINVAL;
    }
    
    devmane = kmalloc(NAME_MAX, GFP_KERNEL);
    if (NULL == devmane) {
        ret = -ENOMEM;
        goto err_out;
    }
    
    dummy = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (NULL == dummy) {        
        ret = -ENOMEM;
        goto err_out;
    }

    memset(devmane, 0, NAME_MAX);
    memset(dummy, 0, PAGE_SIZE);

    /* /dev/sda6 /var ext3 rw,nosuid,nodev,relatime,errors=panic,barrier=1,data=ordered 0 0 */ 
    if (2 != sscanf(line, "%s %s", devmane, dummy)) {
        ret = -ENOENT;
        goto err_out;
    }

    if (strncmp(devmane, name,NAME_MAX)) {
        ret = -ENOENT;
        goto err_out;
    }

    ret = 0;
    vsc_err("dev (%s) mount,mount info=%s\n",devmane,line);
err_out:
    if (devmane) {
        kfree(devmane);
        devmane = NULL;
    }
    if (dummy) {
        kfree(dummy);
        dummy = NULL;
    }
    return ret;
}


/**************************************************************************
 功能描述  : 检查当前系统中对于某设备是否有其挂载点
          
 参    数  : 
 返 回 值  : 0:找到了挂载点
**************************************************************************/
int vsc_check_if_dev_mounted(const char* name)
{
    int ret = -ENOENT;
    struct file *file = NULL;
    char *buff = NULL;
    char *line = NULL;
    int linelen = 0;
    int readlen = 0;
    int i = 0;
    int is_found = 0;

    if (unlikely(!name)) {
        return -EINVAL;
    }

    buff = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buff) {
        ret = -ENOMEM;
        goto err_out;
    }
    
    line = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!line) {
        ret = -ENOMEM;
        goto err_out;
    }

    file = filp_open("/proc/self/mounts", O_RDONLY, 0);
    if (IS_ERR(file)) {
        vsc_err("open /proc/self/mounts error: %d\n", ret = PTR_ERR(file));
        ret = -EACCES;
        goto err_out;
    }

    if (!S_ISREG(file->f_dentry->d_inode->i_mode)) {
        ret = -EACCES;
        goto err_out;
    }
    if (!file->f_op->read) {
        ret = -EIO;
        goto err_out;
    }
    
    while (!is_found) {
        readlen = kernel_read(file, file->f_pos, (char*)buff, PAGE_SIZE-1);
        if (readlen <= 0) {
            break;  // read end of file
        }
        file->f_pos += readlen;

        for (i = 0; i < readlen; i++) {
            if (linelen >= PAGE_SIZE -1) {
                linelen = 0;
                break;
            }

            line[linelen] = buff[i];
            if (line[linelen] == '\n' ) {
                line[linelen + 1] = 0;
                ret = vsc_find_dev_in_mnt_info(line, name);
                linelen = 0;
                
                if (0 == ret) { /* 返回值为0说明已经被挂载 */
                    is_found = 1;
                    break;
                }
                continue;
            }
            linelen++;
        }
    }

err_out:
    if (buff) {
        kfree(buff);
        buff = NULL;
    }
    if (line) {
        kfree(line);
        line = NULL;
    }
    return ret;    
}


/**************************************************************************
 功能描述  : vsc驱动初始化
 参    数  :
 返 回 值  : 0： 成功，其他：错误码
**************************************************************************/
static int __init vsc_init( void )
{
    int retval;
    
    printk(banner);

    switch(io_per_host){
        case 0:
            break;
        case 1:
            io_per_host_shift = 0;
            break;
        case 2:
            io_per_host_shift = 1;
            break;
        case 4:
            io_per_host_shift = 2;
            break;
        default:
            vsc_info("invalid io_per_host = %d\n", io_per_host);
            return -EINVAL;
    }
    vsc_info("io_per_host = %d io_per_host_shift = %d\n", io_per_host, io_per_host_shift);

    INIT_LIST_HEAD(&g_host_mgr_list);

    proc_vsc_root = proc_mkdir("vsc", NULL);
    if (!proc_vsc_root) {
        vsc_err("create proc directory vsc failed.\n");
        retval = -EFAULT;
        goto errout1;
    }

    /* 初始化GPL的系统函数 */
    retval = vsc_sym_init();
    if (retval) {
        vsc_err("vsc_sym_init failed, ret = %d\n", retval);
        goto errout2;
    }

    /* 初始化驱动管理接口 */
    retval = vsc_ioctl_init();
    if (retval) {
        vsc_err("vsc_ioctl_init failed, ret:%d.\n", retval);
        goto errout3;
    }

    /* 
       用户态有两次合并读数据和一次合并写数据：
       第一次读scsi msg+cdb，
       第二次读vsc_scsi_data_msg+vsc_scsi_data_msg+iovecs，最大SCSI_MAX_DATA_BUF_SIZE字节
       写rsp时，内存大小为SCSI_MAX_DATA_BUF_SIZE+sizeof(struct vsc_scsi_rsp_msg)
       通过mempool预留内存，按照rsp的预留即可
     */
    scsi_buf_slab = kmem_cache_create("vsc_scsi_buf",
            MAX_SCSI_DATA_AND_RSP_LENGTH+sizeof(__u32)*2, 0, 0, NULL);
    if (!scsi_buf_slab) {
        vsc_err("failed to create rsp slab");
        goto errout4;
    }

    /* mempool首先通过mempool_alloc_slab分配，系统分配不到，才使用预留的MIN_SCSI_CMD_RESERVED个结构 */
    scsi_buf_pool = mempool_create(MIN_SCSI_CMD_RESERVED, mempool_alloc_slab,
            mempool_free_slab, scsi_buf_slab);
    if (!scsi_buf_pool) {
        vsc_err("mempool create failed");
        goto errout5;
    }

    return 0;

errout5:
    if (scsi_buf_slab) {
        kmem_cache_destroy(scsi_buf_slab);
    }
errout4:
    vsc_ioctl_exit();
errout3:
    vsc_sym_exit();
errout2:
    proc_vsc_root = NULL;
    remove_proc_entry("vsc", NULL);
errout1:

    vsc_err("Virtual storage controller driver initialize failed, ret = %d\n", retval);
    return retval;
}

/**************************************************************************
 功能描述  : vsc驱动退出
 参    数  :
 返 回 值  : 无
**************************************************************************/
static void __exit vsc_exit( void )
{
    if (scsi_buf_pool) {
        mempool_destroy(scsi_buf_pool);
        scsi_buf_pool = NULL;
    }
    
    if (scsi_buf_slab) {
        kmem_cache_destroy(scsi_buf_slab);
        scsi_buf_slab = NULL;
    }

    vsc_remove_all_host();
    vsc_ioctl_exit();

    if (proc_vsc_root) {
        remove_proc_entry("vsc", NULL);
    }

    printk("Virtual storage controller driver unregistered.\n");
    return ;
}


module_init(vsc_init);
module_exit(vsc_exit);

MODULE_DESCRIPTION("Huawei Cloudstorage virtual storage controller driver. (build date : "__DATE__" "__TIME__")");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Technologies Co., Ltd.");
#ifdef DRIVER_VER
MODULE_VERSION(DRIVER_VER);
#else
MODULE_VERSION("1.0.0");
#endif

