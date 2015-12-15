/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #作者：Peng Ruilin (pengruilin@huawei.com)
  #描述: vsc驱动公共头文件
********************************************************************/

#ifndef __VSC_MAIN_H_
#define __VSC_MAIN_H_

struct vsc_ctrl_info;

/* 获取host编号  */
extern unsigned int vsc_ctrl_get_host_no(struct vsc_ctrl_info *h);

/* 根据host no查找控制块 */
extern struct vsc_ctrl_info * vsc_get_ctlr_by_host( unsigned int host_no );

/*  添加host到系统中 */
extern struct vsc_ctrl_info *vsc_register_host( struct vsc_ioctl_cmd_add_host *host_info );

/* 删除指定的host */
extern int vsc_remove_host(struct vsc_ctrl_info *h);

/* 释放所有控制句柄 */
extern void vsc_remove_all_host( void ) ;

/* 取消接管host*/
extern int vsc_scsi_detach(struct vsc_ctrl_info *h, int is_timeout);

/* 挂起host */
extern int vsc_suspend_host(struct vsc_ctrl_info *h);

/* 恢复host */
extern int vsc_resume_host(struct vsc_ctrl_info *h);

/* 接管host */
extern int vsc_scsi_attach(struct vsc_ctrl_info *h, struct file *file,  struct task_struct *task);

/* ioctl等待 */
extern unsigned int vsc_poll_wait(struct file *file, poll_table *wait);

/* 唤醒阻塞读取的进程 */
extern void vsc_interrupt_sleep_on_read(struct vsc_ctrl_info *h);

/* 添加磁盘 */
extern int vsc_add_device(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun);
extern int vsc_add_device_by_vol_name(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun, char *vol_name);

/* 删除磁盘 */
extern int vsc_rmv_device(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun);
extern int vsc_set_delete_by_vol_name(struct vsc_ctrl_info *h, char *vol_name);
extern void vsc_query_vol(struct vsc_ctrl_info *h, struct vsc_ioctl_query_vol *query_vol);

/* 设置磁盘状态 */
extern int vsc_set_device_stat(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun, enum scsi_device_state stat);

/* 获取磁盘状态 */
extern int vsc_get_device_stat(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun,  enum scsi_device_state *stat);

/* 设置磁盘队列超时时间 */
extern int vsc_set_device_rq_timeout(struct vsc_ctrl_info *h, unsigned int channel, unsigned int id, unsigned int lun, int timeout);

/* 设置target进程异常超时时间 */
extern int vsc_set_tg_abn_timeout(struct vsc_ctrl_info *h, __u32 timeout);

/* 获取命令 */
extern ssize_t vsc_get_msg(struct vsc_ctrl_info *h, struct file *file, char __user *  user_ptr, int len);

/* 回应命令 */
extern ssize_t vsc_response_msg(struct vsc_ctrl_info *h, const char __user *  user_ptr, int len);

/* 检查设备头没有在fs层被挂载 */
extern int vsc_check_if_dev_mounted(const char* name);

/* 从scsi_device获取gendisk */
extern void * vsc_get_gendisk_from_scsi_device(struct scsi_device* sdev);


#endif


