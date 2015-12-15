/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #描述:  用于记录日志

********************************************************************/

#ifndef __VSC_LOG_H_
#define __VSC_LOG_H_

typedef enum 
{
    VSC_ERR     = 0,
    VSC_INFO    = 1,
    VSC_DBG     = 2,
    VSC_TYPE_COUNT,
}enum_vsc_log_type;

#define VSC_TRACE_IOCTL_EVENT         (1 << 0)
#define VSC_TRACE_REQ_SCSI_CMD        (1 << 1)
#define VSC_TRACE_RSP_SCSI_CMD        (1 << 2)
#define VSC_TRACE_REQ_EVENT           (1 << 3)
#define VSC_TRACE_RSP_EVENT           (1 << 4)

#define PREFIX "%s():%d "

#define vsc_err(fmt, arg...)  \
    do { printk(KERN_ERR PREFIX fmt , __FUNCTION__, __LINE__, ## arg); \
    } while (0)
#define vsc_err_limit(fmt, arg...) \
    do { if (printk_ratelimit()) printk(KERN_ERR PREFIX fmt , __FUNCTION__, __LINE__, ## arg); \
    } while (0)
#define vsc_info(fmt, arg...)  \
    do { printk(KERN_NOTICE PREFIX fmt , __FUNCTION__, __LINE__, ## arg); \
    } while (0)

#define vsc_dbg(fmt, arg...)  \
    do { if ( log_level >= VSC_DBG) \
        printk(KERN_DEBUG PREFIX fmt , __FUNCTION__, __LINE__, ## arg); \
    } while (0)

extern enum_vsc_log_type log_level;
extern unsigned long     trace_switch;

#endif

