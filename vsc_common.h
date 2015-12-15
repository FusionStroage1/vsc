/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #作者：Peng Ruilin (pengruilin@huawei.com)
  #描述: vsc驱动公共头文件
********************************************************************/

#ifndef __VSC_COMMON_H_
#define __VSC_COMMON_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_sas.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_tcq.h>

#include "vsc.h"
#include "vsc_log.h"
#include "vsc_ioctl.h"
#include "vsc_main.h"
#include "vsc_sym.h"
#include "vsc_version.h"

#define TMP_SIZE_8 8
#define TMP_SIZE_16 16
#define TMP_SIZE_32 32
#define TMP_SIZE_64 64

#define UNUSED(p)  (p)=(p);

#endif


