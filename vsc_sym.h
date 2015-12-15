/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #作者：Peng Ruilin (pengruilin@huawei.com)
  #描述: vsc sym符号头文件
********************************************************************/
#include <linux/version.h>
#include "vsc_common.h"

#ifndef __VSC_SYM_H_
#define __VSC_SYM_H_

/*sym模块初始*/
int vsc_sym_init(void);
/*sym模块退出*/
int vsc_sym_exit(void);

#define vsc_device_create  device_create
#define vsc_device_destroy device_destroy
#define vsc_class_create   class_create
#define vsc_class_destroy  class_destroy

#endif

