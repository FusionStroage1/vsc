/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #作者：Peng Ruilin (pengruilin@huawei.com)
  #描述: sym 主模块
         此模块从/proc/kallsyms文件中找到符号kallsyms_lookup_name的地址，
         并调用此函数，查找其他内核函数对应的地址。

********************************************************************/
#include "vsc_common.h"
#include "vsc_sym.h"

/**************************************************************************
 功能描述  : 数据结构是否对其检查
          
 参    数  : 
 返 回 值  : 初始化结果
**************************************************************************/
static int vsc_sym_data_align_chk(void) 
{
    if (sizeof(struct vsc_scsi_data_msg) != offsetof(struct vsc_scsi_data_msg, data)) {
        vsc_err("struct vsc_scsi_data_msg is not align.");
        return -EFAULT;
    }

    if (sizeof(struct vsc_scsi_data) != offsetof(struct vsc_scsi_data, vec)) {
        vsc_err("struct vsc_scsi_data is not align.");
        return -EFAULT;
    }

    if (sizeof(struct vsc_scsi_event) != offsetof(struct vsc_scsi_event, data)) {
        vsc_err("struct vsc_scsi_event is not align.");
        return -EFAULT;
    }

    if (sizeof(struct vsc_scsi_msg_data) != offsetof(struct vsc_scsi_msg_data, data)) {
        vsc_err("struct vsc_scsi_msg_data is not align.");
        return -EFAULT;
    }

    return 0;
}

/**************************************************************************
 功能描述  : 函数符号初始化
          
 参    数  : 
 返 回 值  : 初始化结果
**************************************************************************/
int __init vsc_sym_init(void)
{
    int retval = 0;

    /* 检查消息结构中的data是否对齐 */
    retval = vsc_sym_data_align_chk();
    if (retval ) {
        vsc_err("struct align check failed.\n");
    }
    return retval;
}

/**************************************************************************
 功能描述  : sym退出函数,sym仅对变量赋值，无需反初始化。
          
 参    数  : 
 返 回 值  : 
**************************************************************************/
int vsc_sym_exit(void)
{
    return 0;
}

