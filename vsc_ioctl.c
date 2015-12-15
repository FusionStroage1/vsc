/********************************************************************
            Copyright (C) Huawei Technologies, 2012
  #描述:  

********************************************************************/
#include "vsc_common.h"

static int vsc_ioctl_majorno;
static struct class *vsc_class;
/**************************************************************************
 功能描述  : 增加磁盘
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_add_disk(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk disk;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk, p_user, sizeof(disk))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(disk.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_add_device(h, disk.channel, disk.id, disk.lun);
}

static int vsc_dev_add_disk_by_vol_name(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk_vol_name disk_vol;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk_vol, p_user, sizeof(disk_vol))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(disk_vol.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_add_device_by_vol_name(h, disk_vol.channel, disk_vol.id,
                                         disk_vol.lun, disk_vol.vol_name);
}

/**************************************************************************
 功能描述  : 删除
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_rmv_disk(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk disk;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk, p_user, sizeof(disk))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(disk.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_rmv_device(h, disk.channel, disk.id, disk.lun);
}
static int vsc_dev_rmv_disk_by_vol_name(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk_vol_name disk_vol;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk_vol, p_user, sizeof(disk_vol))) {
        return -EFAULT;
    }

    h = vsc_get_ctlr_by_host(disk_vol.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_set_delete_by_vol_name(h, disk_vol.vol_name);
}

static int vsc_dev_query_vol(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_query_vol *query_vol;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    query_vol = kzalloc(sizeof(*query_vol), GFP_KERNEL);
    if (NULL == query_vol)
    {
        return -ENOMEM;
    }

    if (copy_from_user(query_vol, p_user, sizeof(*query_vol))) {
        kfree(query_vol);
        return -EFAULT;
    }

    h = vsc_get_ctlr_by_host(query_vol->host);
    if (!h) {
        kfree(query_vol);
        return -ENODEV;
    }

    vsc_query_vol(h, query_vol);

    if (copy_to_user(p_user, query_vol, sizeof(*query_vol))) {
        kfree(query_vol);
        return -EFAULT;
    }

    kfree(query_vol);
    return 0;
}
/**************************************************************************
 功能描述  : 设置设备状态
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_set_disk_stat(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk_stat disk_stat;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk_stat, p_user, sizeof(disk_stat))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(disk_stat.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_set_device_stat(h, disk_stat.channel, disk_stat.id, disk_stat.lun, (enum scsi_device_state)disk_stat.stat);
}

/**************************************************************************
 功能描述  : 获取设备状态
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_get_disk_stat(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_disk_stat disk_stat;
    struct vsc_ctrl_info *h = NULL;
    enum scsi_device_state sdev_stat = 0;
    int retval = 0;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&disk_stat, p_user, sizeof(disk_stat))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(disk_stat.host);
    if (!h) {
        vsc_err("get ctlr for host failed when get disk[%u:%u:%u:%u] stat, will return -ENODEV\n", 
            disk_stat.host, disk_stat.channel, disk_stat.id, disk_stat.lun);
        return -ENODEV;
    }

    retval =  vsc_get_device_stat(h, disk_stat.channel, disk_stat.id, disk_stat.lun, &sdev_stat);
    if (retval) {
        return retval;
    }

    disk_stat.stat = (__u32)sdev_stat;

    if (copy_to_user(p_user, &disk_stat, sizeof(disk_stat))) {
        return -EFAULT;
    }

    return 0;
}

/**************************************************************************
 功能描述  : 设置磁盘请求队列超时时间
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_set_disk_rq_timeout(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_rq_timeout rq_timeout;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&rq_timeout, p_user, sizeof(rq_timeout))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(rq_timeout.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_set_device_rq_timeout(h, rq_timeout.channel, rq_timeout.id, rq_timeout.lun, rq_timeout.timeout);
}

/**************************************************************************
 功能描述  : 设置target进程异常超时时间
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_set_tg_abn_timeout(char __user *p_user)
{
    struct vsc_ioctl_set_tg_abn_timeout timeout;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&timeout, p_user, sizeof(timeout))) {
        return -EFAULT;
    }

    if (timeout.timeout > VSC_MAX_TARGET_ABNORMAL_TIMEOUT){
        return -EINVAL;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(timeout.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_set_tg_abn_timeout(h, timeout.timeout);
}


/**************************************************************************
 功能描述  : 删除HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_rmv_host(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_cmd_rmv_host host;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&host, p_user, sizeof(host))) {
        return -EFAULT;
    }

    /* 查找host控制句柄 */
    h = vsc_get_ctlr_by_host(host.host);
    if (!h) {
        return -ENODEV;
    }

    return vsc_remove_host(h); 
}

/**************************************************************************
 功能描述  : 增加HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_add_host(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_cmd_add_host host_info;
    struct vsc_ctrl_info *h = NULL;

    if (!p_user || !file) {
        return -EINVAL;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    if (copy_from_user(&host_info, p_user, sizeof(host_info))) {
        vsc_dbg("add host: copy from user failed\n");
        return -EFAULT;
    }

    h = vsc_register_host(&host_info); 

    /* 将新申请的host id回传给vbs */
    if (copy_to_user(p_user, &host_info, sizeof(host_info))) {
        vsc_dbg("add host: copy to user failed\n");
        return -EFAULT;
    }
    
    if (IS_ERR(h)) {
        vsc_dbg("add host: register host failed, ret = %ld\n", PTR_ERR(h));
        return PTR_ERR(h);
    }

    return 0;
}

/**************************************************************************
 功能描述  : 接管HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_attach_host(struct file *file, char __user *p_user)
{
    struct vsc_ioctl_cmd_attatch_host cmd_attach;
    struct vsc_ctrl_info *h = NULL;
    int retval = 0;

    /* 检查此文件是否已经接管其他host */
    h = file->private_data;
    if (h) {
        /* 如果已经接管，直接返回错误 */
        return -EALREADY;
    }

    /* 复制命令数据 */
    if (copy_from_user(&cmd_attach, p_user, sizeof(cmd_attach))) {
        return -EFAULT;
    }

    if (!capable(CAP_SYS_ADMIN)) {
        return -EPERM;
    }

    h = vsc_get_ctlr_by_host(cmd_attach.host);
    if (!h) {
        return -ENODEV;
    }

    /* 检查host接管状态 */
    retval = vsc_scsi_attach(h, file, current); 
    if (retval) {
        return retval;
    }

    /* 接管host */
    file->private_data = h;

    vsc_info("Host %d is attached by %s(%d)\n", vsc_ctrl_get_host_no(h), current->comm, current->pid);

    return 0;
}

/**************************************************************************
 功能描述  : 释放HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_detach_host(struct file *file)
{
    struct vsc_ctrl_info *h = NULL;
    /* 设置host处于非接管状态 */
    h = file->private_data;
    if (!h) {
        /* 如果没有接管，直接返回错误 */
        return -EBADF;
    }

    vsc_info("Host %d is detached from %s(%d)\n", vsc_ctrl_get_host_no(h), current->comm, current->pid);
    /* 取消关联 */
    file->private_data = NULL;
    return vsc_scsi_detach(h, 0);
}

/**************************************************************************
 功能描述  : 挂起HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_suspend_host(struct file *file, char __user *p_user)
{
    struct vsc_ctrl_info *h = NULL;

    h = file->private_data;
    if (!h) {
        /* 如果没有接管，直接返回错误 */
        return -EBADF;
    }

    /* 挂起host请求 */
    return vsc_suspend_host(h);
}

/**************************************************************************
 功能描述  : 恢复HOST
 参    数  : p_user          命令参数
 返 回 值  : 0:成功  其他：失败
**************************************************************************/
static int vsc_dev_resume_host(struct file *file, char __user *p_user)
{
    struct vsc_ctrl_info *h = NULL;

    h = file->private_data;
    if (!h) {
        /* 如果没有接管，直接返回错误 */
        return -EBADF;
    }

    /* 恢复host数据请求 */
    return vsc_resume_host(h);
}

/**************************************************************************
 功能描述  : ioctl接口
 参    数  : vsc_cmnd_list *c       scsi命令控制句柄
 返 回 值  : 无
**************************************************************************/
static long vsc_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    long ret = 0;
    char __user *p_user = (char __user *)arg;  

    if ( VSC_TRACE_IOCTL_EVENT & trace_switch) {
        printk("IOCTL cmd %X from %d(%s)\n", cmd, current->pid, current->comm);
    }
    switch (cmd)  {     
        case VSC_ADD_HOST:
            ret = vsc_dev_add_host(file, p_user);
            break;
        case VSC_RMV_HOST:
            ret = vsc_dev_rmv_host(file, p_user);
            break;
        case VSC_ATTACH_HOST:
            ret = vsc_dev_attach_host(file, p_user);
            break;
        case VSC_DETACH_HOST:
            ret = vsc_dev_detach_host(file);
            break;
        case VSC_SUSPEND_HOST:
            ret = vsc_dev_suspend_host(file, p_user);
            break;
        case VSC_RESUME_HOST:
            ret = vsc_dev_resume_host(file, p_user);
            break;
        case VSC_ADD_DISK:  
            ret = vsc_dev_add_disk(file, p_user);
            break;
        case VSC_RMV_DISK:
            ret = vsc_dev_rmv_disk(file, p_user);
            break;
        case VSC_ADD_DISK_VOL_NAME:
            ret = vsc_dev_add_disk_by_vol_name(file, p_user);
            break;
        case VSC_PREPARE_RMV_VOL:
            ret = vsc_dev_rmv_disk_by_vol_name(file, p_user);
            break;
        case VSC_QUERY_DISK_VOL:
            ret = vsc_dev_query_vol(file, p_user);
            break;
        case VSC_SET_DISK_STAT:
            ret = vsc_dev_set_disk_stat(file, p_user);
            break;
        case VSC_GET_DISK_STAT:
            ret = vsc_dev_get_disk_stat(file, p_user);
            break;
        case VSC_DISK_RQ_TIMEOUT:
            ret = vsc_dev_set_disk_rq_timeout(file, p_user);
            break;
        case VSC_SET_TARGET_ABNORMAL_TIMEOUT:
            ret = vsc_dev_set_tg_abn_timeout(p_user);
            break;            
        default:
            ret = -ENOTTY;
            break;
    }
    
    return ret;
}

static long vsc_dev_compat_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
    return vsc_dev_ioctl(file, ioctl, arg);
}

/**************************************************************************
 功能描述  : 打开设备文件接口
 参    数  : inode:         节点指针
             file:          文件指针
 返 回 值  : 无
**************************************************************************/
static int vsc_dev_open(struct inode *inode, struct file *file)
{   
    file->private_data = NULL;
    file->f_flags = file->f_flags;
    return 0;
}

/**************************************************************************
 功能描述  : 文件poll处理函数
 参    数  : file:          文件指针
 返 回 值  : 无
**************************************************************************/
static unsigned int vsc_dev_poll(struct file *file, poll_table *wait)
{
//    struct vsc_ctlr_info *h = NULL;

//    h = file->private_data;
//    if (!h) {
//        return 0;
//    } /* [review] duplicate  */

    return vsc_poll_wait(file, wait);
}

/**************************************************************************
 功能描述  : 文件读取函数
 参    数  : file:          文件指针
             buf            数据缓存
             len            长度
             ppos           偏移
 返 回 值  : 无
**************************************************************************/
static ssize_t vsc_dev_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    struct vsc_ctrl_info *h = file->private_data;

    if (unlikely(!h)) {
        return -EBADF;   
    }

    return vsc_get_msg(h, file, user_buf, count);
}

/**************************************************************************
 功能描述  : 文件写入函数
 参    数  : file:          文件指针
             buf            数据缓存
             len            长度
             ppos           偏移
 返 回 值  : 无
**************************************************************************/
static ssize_t vsc_dev_write(struct file *file,
                  const char __user *user_buf,
                  size_t count, loff_t *ppos)
    
{
    struct vsc_ctrl_info *h = file->private_data;

    if (unlikely(!h)) {
        return -EBADF;   
    }

    return vsc_response_msg(h, user_buf, count);
}

/**************************************************************************
 功能描述  : 数据刷新，主要用于close句柄的时候，退出
 参    数  : file:          文件指针
 返 回 值  : 无
**************************************************************************/
static int vsc_dev_f_flush(struct file *file, fl_owner_t ownid)
{
    struct vsc_ctrl_info *h = file->private_data;

    if (unlikely(!h)) {
        return 0;   
    }

    /* 如果没有接管任何host，则直接返回 */
    if (!file->private_data) {
        return 0;
    }

    /* 非阻塞时，直接返回 */
    if (file->f_flags & O_NONBLOCK) {
        return 0;
    }

    /* 唤醒阻塞的进程 */
    vsc_interrupt_sleep_on_read(h);
    return 0;
}

/**************************************************************************
 功能描述  : 关闭文件，退出
 参    数  : inode:         节点指针
             file:          文件指针
 返 回 值  : 无
**************************************************************************/
static int vsc_dev_release(struct inode *inode, struct file *file)
{
    struct vsc_ctrl_info *h = file->private_data;

    if (unlikely(!h)) {
        return 0;   
    }

    /* 如果没有接管任何host，则直接返回 */
    if (!file->private_data) {
        return 0;
    }

    h = file->private_data;
    file->private_data = NULL;

    vsc_info("Host %d unexpected detached from %s(%d) as file closed.\n", vsc_ctrl_get_host_no(h), current->comm, current->pid);

    /* 设置HOST处于离线状态 */
    return vsc_scsi_detach(h, 1);
}


static const struct file_operations vsc_ioctl_fops = {
    .owner             = THIS_MODULE,
    .open              = vsc_dev_open,
    .flush             = vsc_dev_f_flush,
    .release           = vsc_dev_release,
    .read              = vsc_dev_read,
    .write             = vsc_dev_write,
    .poll              = vsc_dev_poll,
    .unlocked_ioctl    = vsc_dev_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl      = vsc_dev_compat_ioctl,
#endif
};

int vsc_ioctl_init(void)
{
    int majorno = -1;
    int ret     = -1;
    struct device *vsc_dev = NULL;
    
    majorno = register_chrdev(0, VSC_IOCTL_NAME, &vsc_ioctl_fops);
    if (majorno < 0) {
        vsc_err("register_chrdev %s failed, majorno:%d\n", VSC_IOCTL_NAME, majorno);
        ret = majorno;
        goto err_ret;
    }

    vsc_ioctl_majorno = majorno;
    
    vsc_class = vsc_class_create(THIS_MODULE, VSC_IOCTL_NAME);    
    if (IS_ERR(vsc_class)) {
        vsc_err("class_create[%p] failed\n", vsc_class);
        ret = PTR_ERR(vsc_class);
        goto err_ret;
    }

#if (LINUX_VERSION_CODE == KERNEL_VERSION(2, 6, 18))
    vsc_dev = vsc_device_create(vsc_class, NULL, MKDEV(vsc_ioctl_majorno, 0), 
                                  VSC_IOCTL_NAME);
#else
    vsc_dev = vsc_device_create(vsc_class, NULL, MKDEV(vsc_ioctl_majorno, 0), 
                            NULL, VSC_IOCTL_NAME);
#endif
    if (IS_ERR(vsc_dev)) {
        vsc_err("device_create[%p] failed\n", vsc_dev);
        ret = PTR_ERR(vsc_dev);
        goto err_ret;
    }

    return 0;
    
err_ret:
    if (!IS_ERR(vsc_dev)) {
        vsc_device_destroy(vsc_class, MKDEV(vsc_ioctl_majorno, 0));
    }
    
    if (!IS_ERR(vsc_class)) {
        vsc_class_destroy(vsc_class);
        vsc_class = NULL;
    }
    
    if ( majorno >= 0) {
       unregister_chrdev(vsc_ioctl_majorno, VSC_IOCTL_NAME); 
    }
    
    return ret;
}

void vsc_ioctl_exit(void)
{
    vsc_device_destroy(vsc_class, MKDEV(vsc_ioctl_majorno, 0));
    vsc_class_destroy(vsc_class);
    vsc_class = NULL;
    unregister_chrdev(vsc_ioctl_majorno, VSC_IOCTL_NAME);
    return;
}
