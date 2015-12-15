/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #描述:  设置模块参数

********************************************************************/
#include "vsc_common.h"

/* Default log_level to error. */
enum_vsc_log_type log_level = VSC_ERR;
unsigned long     trace_switch = 0;

static int vsc_set_log_level(const char *val, struct kernel_param *kp)
{
    /* echo x > /sys/module/vsc/parameters/log_level */  
    char *endp;
    int  l;
    int  rv = 0;

    if (!val)
        return -EINVAL;
    l = simple_strtoul(val, &endp, 0);
    if (endp == val)
        return -EINVAL;
        
    if (l >= VSC_TYPE_COUNT)
        return -EINVAL;

    *((int *)kp->arg) = l;

    return rv;
}

static int vsc_get_log_level(char *buffer, struct kernel_param *kp)
{
    /* cat /sys/module/vsc/parameters/log_level */ 
    return snprintf(buffer, PAGE_SIZE, "%i", *((int *)kp->arg));
}

module_param_call(log_level, vsc_set_log_level, 
        vsc_get_log_level, &log_level, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(log_level, "Get/Set Log level (0-2). ");

/**************************************************************************
 功能描述  : 获取trace开关
 参    数  : const char *val, struct kernel_param *kp
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_get_trace_switch(char *buffer, struct kernel_param *kp)
{
    /* cat /sys/module/vsc/parameters/trace_switch */
    int len = 0;
    len += snprintf(buffer + len, PAGE_SIZE, "Current value: 0x%lX\n", *((unsigned long *)kp->arg));
    len += snprintf(buffer + len, PAGE_SIZE, "Instrunction for switch bit:\n");
    len += snprintf(buffer + len, PAGE_SIZE, "%X:\t%s file event.\n", VSC_TRACE_IOCTL_EVENT, VSC_IOCTL_NAME);
    len += snprintf(buffer + len, PAGE_SIZE, "%X:\tRequest scsi cmd.\n", VSC_TRACE_REQ_SCSI_CMD);
    len += snprintf(buffer + len, PAGE_SIZE, "%X:\tResponse scsi cmd.\n", VSC_TRACE_RSP_SCSI_CMD);
    len += snprintf(buffer + len, PAGE_SIZE, "%X:\tRequest event.\n", VSC_TRACE_REQ_EVENT);
    len += snprintf(buffer + len, PAGE_SIZE, "%X:\tResponse event.\n", VSC_TRACE_RSP_EVENT);
    return len;
}

/**************************************************************************
 功能描述  : 设置trace开关
 参    数  : const char *val, struct kernel_param *kp
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_set_trace_swtich(const char *val, struct kernel_param *kp)
{
    /* echo x > cat /sys/module/vsc/parameters/trace_switch */
    char *endp;
    unsigned long  l;
    int  rv = 0;

    if (!val)
        return -EINVAL;
    l = simple_strtoul(val, &endp, 0);
    if (endp == val)
        return -EINVAL;

    /* [zr] 最好是加上参数检查 */
    *((unsigned long *)kp->arg) = l;

    return rv;
}

module_param_call(trace_switch, vsc_set_trace_swtich, 
        vsc_get_trace_switch, &trace_switch, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(trace_switch, "Get/Set trace switch. ");


