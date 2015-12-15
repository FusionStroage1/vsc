/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #描述:  vsc 接口文件

********************************************************************/
#include <linux/types.h>

#ifndef _VSC_H_
#define _VSC_H_

/* 默认的命令队列个数 */
/*根据全局参数g_io_depth_per_lun和g_max_lun_per_vbs的最大值决定*/
#define VSC_DEFAULT_MAX_CMD_NUMBER     (2048*2048)

/* 每个lun最大的命令数 */
/*根据全局参数g_io_depth_per_lun的最大值决定*/
#define VSC_DEFAULT_LUN_CMD_NUMBER     2048

/* 最大的CDB数据长度 */
#define VSC_MAX_CDB_LEN                256

/* 请求队列最大超时时间 单位:秒 */
#define VSC_RQ_MAX_TIMEOUT             120

/* 最大的target异常超时时间 单位:秒 */
#define VSC_MAX_TARGET_ABNORMAL_TIMEOUT    6000

/* SCSI命令合并后一次最多传输的命令数量 */
#define MAX_BATCH_SCSI_CMD_COUNT       (4)

/* SCSI响应合并后一次最多传输的响应数量 */
#define MAX_BATCH_SCSI_RSP_COUNT       (4)

/* SCSI CDB最大长度 */
#define MAX_CDB_LENGTH                 (16)

/* SCSI CMD携带数据的最大长度 */
#define SCSI_MAX_DATA_BUF_SIZE         (256)

#define MAX_SCSI_MSG_AND_CDB_LENGTH  \
    ((sizeof(struct vsc_scsi_msg)+MAX_CDB_LENGTH)*MAX_BATCH_SCSI_CMD_COUNT)

#define MAX_SCSI_DATA_AND_RSP_LENGTH  \
    ((SCSI_MAX_DATA_BUF_SIZE+sizeof( struct vsc_scsi_rsp_msg))*MAX_BATCH_SCSI_RSP_COUNT)

#define VSC_IOCTL_NAME                 "vsc"            /* vsc-ioctl 文件名 */

/* ioctl 接口 */
#define VSC_IOCTL_BASE                 (0x4B)           /* 'K' == 0x4B  */

#define VSC_ADD_HOST           _IO(VSC_IOCTL_BASE, 0)   /* 增加虚拟HOST */
struct vsc_ioctl_cmd_add_host {
    __u32        host;          /* HOST编号，通过port计算出的 */
    __u32        sys_host_id;   /* 向系统注册的scsi host id */
    __u32        max_channel;   /* 最大通道数 */
    __u32        max_lun;       /* 最大lun个数 */
    __u32        max_id;        /* 最大target个数 */
    __u32        max_cmd_len;   /* 最大CDB命令长度 */
    __u32        max_nr_cmds;   /* HOST最大的命令深度 */
    __u32        cmd_per_lun;   /* 每个lun的最大命令数 */
    __u32        sg_count;      /* 数据区大小（计算公式为扇区大小*sg_count）*/  
};

#define VSC_RMV_HOST           _IO(VSC_IOCTL_BASE, 1)   /* 删除虚拟HOST */
struct vsc_ioctl_cmd_rmv_host {
    __u32       host;            /* host编号*/
};

#define VSC_ATTACH_HOST        _IO(VSC_IOCTL_BASE, 20)   /* 挂接虚拟控制器 */
struct vsc_ioctl_cmd_attatch_host {
    __u32        host;           /* host编号*/
};

#define VSC_ADD_DISK           _IO(VSC_IOCTL_BASE, 10)   /* 增加磁盘 */
#define VSC_RMV_DISK           _IO(VSC_IOCTL_BASE, 11)   /* 删除磁盘 */
/* 设备标识结构 */
struct vsc_ioctl_disk {
    __u32        host;          /* HOST编号 */
    __u32        channel;       /* 通道号 */
    __u32        id;            /* target编号 */
    __u32        lun;           /* lun编号 */
};

#define VSC_SET_DISK_STAT      _IO(VSC_IOCTL_BASE, 12)   /* 设置单个磁盘状态 */  
#define VSC_GET_DISK_STAT      _IO(VSC_IOCTL_BASE, 13)   /* 获取单个磁盘状态 */

/* 磁盘状态 */
#define VSC_DISK_CREATED         1          /* 已经创建设备，但是没有创建sysfs文件 */
#define VSC_DISK_RUNNING         2          /* SCSI设备状态为运行 （可设置）*/
#define VSC_DISK_CANCEL          3          /* 开始删除设备 */
#define VSC_DISK_DEL             4          /* 设备已经被删除 */
#define VSC_DISK_QUIESCE         5          /* 设备为不活动 */
#define VSC_DISK_OFFLINE         6          /* SCSI设备状态为离线 (可设置) */
#define VSC_DISK_BLOCK           7          /* SCSI设备状态为阻塞 （可设置）*/
#define VSC_DISK_CREATED_BLOCK   8          /* 设备以阻塞方式创建 */
#define VSC_DISK_PREPARE_DELETE  9          /* 设备已经被预删除*/

struct vsc_ioctl_disk_stat {
    __u32         host;          /* HOST编号 */
    __u32         channel;       /* 通道号 */
    __u32         id;            /* target编号 */
    __u32         lun;           /* lun编号 */
    __u32         stat;          /* 磁盘状态 */
};

#define VSC_DISK_RQ_TIMEOUT    _IO(VSC_IOCTL_BASE, 14)        /* 设置磁盘请求队列超时时间 */
struct vsc_ioctl_rq_timeout {
    __u32        host;          /* HOST编号 */
    __u32        channel;       /* 通道号 */
    __u32        id;            /* target编号 */
    __u32        lun;           /* lun编号 */
    __u32        timeout;       /* 超时时间，单位为秒 */
};

#define VSC_SET_TARGET_ABNORMAL_TIMEOUT   _IO(VSC_IOCTL_BASE, 15) /* 设置target进程异常超时时间 */
struct vsc_ioctl_set_tg_abn_timeout {
    __u32        host;          /* HOST编号 */
    __u32        timeout;       /* 超时时间，单位为秒 */
};

/* 如下命令无参数，需要在attach之后执行 */
#define VSC_DETACH_HOST        _IO(VSC_IOCTL_BASE, 21)   /* 释放虚拟控制器 */
#define VSC_SUSPEND_HOST       _IO(VSC_IOCTL_BASE, 30)   /* 挂起虚拟控制器 */
#define VSC_RESUME_HOST        _IO(VSC_IOCTL_BASE, 31)   /* 恢复虚拟控制器 */

#define VSC_ADD_DISK_VOL_NAME  _IO(VSC_IOCTL_BASE, 32)
#define VSC_PREPARE_RMV_VOL    _IO(VSC_IOCTL_BASE, 33)
#define VSC_QUERY_DISK_VOL     _IO(VSC_IOCTL_BASE, 34)

#define VSC_VOL_NAME_LEN       96
#define VSC_MAX_VOL_PER_HOST   128
struct vsc_ioctl_disk_vol_name {
    __u32       host; /* 系统host id，来源vsc_add_device_by_vol_name时sh->host_no;vsc中其他地方未加注明均为内部host id */
    __u32       channel;
    __u32       id;
    __u32       lun;
    __u32       state;      /*ONLINE/DELETE*/
    char        vol_name[VSC_VOL_NAME_LEN];
    __u8        extend[0];  /* 增加扩展字段，不影响原流程 */
};

struct vsc_ioctl_query_vol {
    __u32 host;
    __u32 vol_num;
    struct vsc_ioctl_disk_vol_name volumes[VSC_MAX_VOL_PER_HOST];
};

/* 数据读写的消息类型 */
#define VSC_MSG_TYPE_SCSI_CMD      0       /* scsi命令消息类型 */
#define VSC_MSG_TYPE_EVENT         1       /* 异常处理消息类型 */
#define VSC_MSG_TYPE_SCSI_DATA     2       /* SCSI数据消息类型 */

/* 事件类型 */
#define VSC_EVENT_TYPE_ABORT       1       /* abort消息事件 */
#define VSC_EVENT_TYPE_RESET_DEV   2       /* 复位设备事件 */

/* scsi命令消息数据 */
/* 调整此结构体内容需，需要对应的调整pad成员的大小 */
struct vsc_scsi_msg_data {
    __u32     data_type;                          /* 数据类型 */
    __u32     data_len;                           /* 数据长度 */
    __u8      pad[0];                             /* 保证data对齐到sizeof(此结构体的大小) */
    __u8      data[0];  /*lint !e157*/            /* 数据内容 */
};

/* SCSI命令abort消息 */
struct vsc_event_abort {
    __u32     cmd_sn;                 /* 命令序列号 */
    __u32     host;                   /* host编号 */
    __u32     channel;                /* 通道号 */
    __u32     id;                     /* target编号 */
    __u32     lun;                    /* lun编号 */    
};

/* SCSI命令复位设备消息 */
struct vsc_event_reset_dev {
    __u32     cmd_sn;                 /* 命令序列号 */
    __u32     host;                   /* host编号 */
    __u32     channel;                /* 通道号 */
    __u32     id;                     /* target编号 */
    __u32     lun;                    /* lun编号 */    
};

/* EVENT返回值 */
#define VSC_EVENT_SUCCESS          0       /* 事件成功 */

/* SCSI命令事件结果消息 */
struct vsc_event_result {
    __u32     result;                 /* 消息结果，成功为：VSC_EVENT_SUCCESS， 其他未错误码 */
};

/* SCSI驱动事件消息，整个vsc_scsi_event长度最大为4K*/ 
/* 调整此结构体内容需，需要对应的调整pad成员的大小 */
struct vsc_scsi_event {
    __u32      type;                   /* 命令操作字, 当此字段为VSC_MSG_TYPE_EVENT时，后面消息有效 */  
    __u32      reserved[2];            /* 保留字段 */    
    __u32      event_sn;               /* 事件序列号 */
    __u32      tag;                    /* event事件标志 */
    __u32      data_length;            /* 数据长度，最大4K */
    __u8       pad[4];                 /* 保证data对齐到sizeof(此结构体的大小) */
    __u8       data[0];                /* 命令数据 基于vsc_scsi_msg_data的结构 */
};

/* 错误信息 */
struct vsc_sense_data {
    __u8       scsi_status;            /* SCSI设备状态，默认返回0 */
    __u8       sense_len;              /* 错误码长度 */
    __u8       sense_info[0];          /* 错误信息 */
};

/* DATA数据请求 
 * 当struct vsc_scsi_msg_data的data_type为VSC_MSG_DATA_TYPE_DATA时
 * 其data指向此结构
 */
struct vsc_iovec
{
	__u64      iov_base;               /* 用户态数据地址，当进程为32位时，高位填写0 */
	__u32      iov_len;                /* iov数据长度 */
    __u8       pad[4];                 /* 强制32位与64为大小对齐 */
};

#define VSC_MSG_DATA_TYPE_CDB      1   /* CDB数据类型 */
#define VSC_MSG_DATA_TYPE_DATA     2   /* 读写数据类型 */
#define VSC_MSG_DATA_TYPE_SENSE    3   /* 错误信息类型 */

/* 获取的请求数据信息 */
struct vsc_scsi_data {
    __u32     data_type;               /* 请求的数据类型 */
    __u32     data_len;                /* 请求消息vec的长度 */
    __u32     nr_iovec;                /* iovec的个数 */
    __u32     total_iovec_len;         /* 每个iovec中iov_len的长度总和 */
    __u32     offset;                  /* 请求的偏移地址 */
    __u8      pad[0];                  /* 保证vec对齐到sizeof(此结构体的大小) */
    __u8      vec[0];  /*lint !e157*/  /* DATA数据，此结构指向struct vsc_iovec*/
};


/* SCSI数据交互的消息 */
struct vsc_scsi_data_msg
{
    __u32     type;                    /* 命令操作字, 当此字段为VSC_MSG_TYPE_SCSI_DATA时，后面消息有效 */  
    __u32     reserved[2];             /* 保留字段 */
    __u32     cmd_sn;                  /* 命令序列号 */
    __u32     tag;                     /* scsi命令识别标志 */
    __u32     scsi_data_len;           /* 请求信息的长度 */
    __u8      pad[0];                  /* 保证vec对齐到sizeof(此结构体的大小) */
    __u8      data[0];  /*lint !e157*/ /* 获取的数据类型，指向struct vsc_scsi_data*/
};

/* 数据请求方向 */
#define    VSC_MSG_DATA_DIR_BIDIRECTIONAL   0   /* 双向数据 */
#define    VSC_MSG_DATA_DIR_TO_DEVICE       1   /* 向设备写入数据 */
#define    VSC_MSG_DATA_DIR_FROM_DEVICE     2   /* 从设备获取数据 */
#define    VSC_MSG_DATA_DIR_NONE            3   /* 控制类数据 */

/* SCSI命令消息 */ 
struct vsc_scsi_msg {
    __u32     type;                    /* 命令操作字, 当此字段为VSC_MSG_TYPE_SCSI_CMD时，后面消息有效*/
    __u32     reserved[2];             /* 保留字段 */
    __u32     cmd_sn;                  /* 命令序列号 */
    __u32     tag;                     /* scsi命令识别标志 */
    __u32     host;                    /* host编号 */
    __u32     channel;                 /* 通道号 */
    __u32     id;                      /* target编号 */
    __u32     lun;                     /* lun编号 */
    __u32     direction;               /* 数据方向 */
};

/* SCSI命令操作结果 */
#define CMD_SUCCESS             0x0000               /* 命令执行成功 */
#define CMD_FAILURE             0x0001               /* 错误，错误详细原因在sense数据中，驱动不做任何处理，错误由SCSI中层处理 */
#define CMD_INVALID             0x0002               /* 当寻址不存在的硬盘设备时，返回此错误，通知中层设备未连接 */
#define CMD_CONNECTION_LOST     0x0003               /* 设备连接丢失，通知SCSI中层命令错误 */
#define CMD_TIMEOUT             0x0004               /* SCSI命令超时，通知SCSI中层命令超时 */
#define CMD_PROTOCOL_ERR        0x0005               /* 消息错误，通知中层软件错误，重试对应的SCSI命令 */
#define CMD_NEED_RETRY          0x0006               /* 通知SCSI中层重试对应的SCSI命令 */

/* SAM规范错误码 */
#define GOOD                     0x00                /* 硬盘正常 */
#define CHECK_CONDITION          0x01                /* 检查sense内容 */
#define CONDITION_GOOD           0x02                /* 状态正常 */
#define BUSY                     0x04                /* 忙 */
#define INTERMEDIATE_GOOD        0x08
#define INTERMEDIATE_C_GOOD      0x0a
#define RESERVATION_CONFLICT     0x0c
#define COMMAND_TERMINATED       0x11                /* 命令被中断 */
#define QUEUE_FULL               0x14                /* 队列满 */


/* scsi命令回应消息 */
struct vsc_scsi_rsp_msg {
    __u32     type;                   /* 命令操作字，此字段为VSC_MSG_TYPE_SCSI_CMD，后面消息有效 */ 
    __u32     reserved[2];            /* 保留字段 */    
    __u32     cmd_sn;                 /* 命令序列号 */
    __u32     tag;                    /* scsi命令识别标志 */
    __u32     command_status;         /* SCSI命令状态，与驱动的接口，对应上面描述的宏 */
    __u32     scsi_status;            /* target的状态，上报到SCSI中层，对应SAM-3规范的错误码*/
};

#endif // END _VSC_H_
