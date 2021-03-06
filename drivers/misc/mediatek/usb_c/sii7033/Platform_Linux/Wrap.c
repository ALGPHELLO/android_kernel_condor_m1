/*
*Copyright (C) 2013-2014 Silicon Image, Inc.
*
*This program is free software; you can redistribute it and/or
*modify it under the terms of the GNU General Public License as
*published by the Free Software Foundation version 2.
*This program is distributed AS-IS WITHOUT ANY WARRANTY of any
*kind, whether express or implied; INCLUDING without the implied warranty
*of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE or NON-INFRINGEMENT.
*See the GNU General Public License for more details at
*http://www.gnu.org/licenses/gpl-2.0.html.
*/
#include "Wrap.h"
#include "../Common/si_time.h"
#include "../Common/si_usbpd_main.h"

/* I2C Wrappers */
struct i2c_adapter *i2c_bus;

void usbpd_pf_i2c_init(struct i2c_adapter *adapter)
{
	i2c_bus = adapter;
}

int sii_read_i2c_block(uint8_t offset, uint16_t count, uint8_t *values)
{
	int ret;
	uint8_t device_id = SII_DRV_DEVICE_I2C_ADDR;

	struct i2c_msg msg[2];

	int i;

	msg[0].flags = 0;
	msg[0].addr = device_id >> 1;
	msg[0].buf = &offset;
	msg[0].len = 1;

	msg[1].flags = I2C_M_RD;
	msg[1].addr = device_id >> 1;
	msg[1].buf = values;
	msg[1].len = count;

	for (i = 0; i < I2C_RETRY_MAX; i++) {
		ret = i2c_transfer(i2c_bus, msg, 2);
		if (ret != 2) {
			pr_err("%s:%d I2c read failed, retry 0x%02x:0x%02x\n", __func__, __LINE__,
			       device_id, offset);
			msleep(20);
		} else {
			break;
		}
	}
	return ret;
}

int sii_write_i2c_block(uint8_t offset, uint16_t count, const uint8_t *values)
{
	int ret;
	uint8_t device_id = SII_DRV_DEVICE_I2C_ADDR;

	struct i2c_msg msg;
	u8 *buffer;

	int i;

	buffer = kzalloc(count + 1, GFP_KERNEL);
	if (!buffer) {
		/*pr_debug(KERN_ERR, "%s:%d buffer allocation failed\n",
		   __func__, __LINE__); */
		return -ENOMEM;
	}
	buffer[0] = offset;
	memmove(&buffer[1], values, count);

	msg.flags = 0;
	msg.addr = device_id >> 1;
	msg.buf = buffer;
	msg.len = count + 1;

	for (i = 0; i < I2C_RETRY_MAX; i++) {
		ret = i2c_transfer(i2c_bus, &msg, 1);
		if (ret != 1) {
			pr_err("%s:%d I2c write failed, retry 0x%02x:0x%02x\n", __func__, __LINE__,
			       device_id, offset);
			ret = -EIO;
			msleep(20);
		} else {
			ret = 0;
			break;
		}
	}

	kfree(buffer);
	return ret;
}

#ifdef HR_TIMER_INCLUDE
/*
 * Timer Related APIs
 */
int sii_create_timer(struct usbpd_device *context, void (*callback_handler)
		      (void *callback_param), void *callback_param,
		     struct usbpd_timer *usbpdtmr, uint32_t time_msec, bool periodicity)
{
	struct usbpd_device *drv_context = context;
	/*above line is redundant has to be modified */
	struct timer_obj *new_timer;

	if (!callback_handler) {
		pr_debug("Invalid context\n");
		return -EINVAL;
	}
	if (!context) {
		pr_debug("Invalid context\n");
		return -EINVAL;
	}
	new_timer = kzalloc(sizeof(*new_timer), GFP_KERNEL);
	if (new_timer == NULL)
		return -ENOMEM;

	new_timer->timer_cnt = 0;
	new_timer->timer_callback_handler = callback_handler;
	new_timer->callback_param = callback_param;
	new_timer->flags = 0;
	new_timer->drv_context = drv_context;
	list_add(&new_timer->list_link, &drv_context->timer_list);
	hrtimer_init(&new_timer->hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	new_timer->hr_timer.function = sii_timer_handler;
	/**timer_handle = new_timer;*/

	usbpdtmr->timerhandle = new_timer;
	usbpdtmr->time_msec = time_msec;
	usbpdtmr->periodicity = periodicity;
	usbpdtmr->time_msec = time_msec;
	usbpdtmr->periodicity = periodicity;
	return 0;
}

int sii_start_timer(struct usbpd_device *context, struct usbpd_timer *usbpdtmr)
{
	struct usbpd_device *drv_context = context;
	struct timer_obj *timer;
	ktime_t timer_period;
	int status = 0;

	/*pr_debug("timer start...\n"); */
	status = is_timer_handle_valid(drv_context, usbpdtmr->timerhandle);
	if (status == 0) {
		long secs = 0;

		timer = usbpdtmr->timerhandle;
		timer->delay = usbpdtmr->periodicity;
		secs = usbpdtmr->time_msec / 1000;
		usbpdtmr->time_msec %= 1000;
		timer_period = ktime_set(secs, MSEC_TO_NSEC(usbpdtmr->time_msec));
		hrtimer_start(&timer->hr_timer, timer_period, HRTIMER_MODE_REL);
		/*pr_debug("hrtimer started..\n"); */
	}
	return status;
}

int sii_stop_timer(struct usbpd_device *context, struct usbpd_timer *usbpdtmr)
{
	int status;
	struct timer_obj *timer;
	struct usbpd_device *drv_context = context;

	if (!(usbpdtmr->timerhandle))
		return -EPERM;

	status = is_timer_handle_valid(drv_context, usbpdtmr->timerhandle);
	if (status == 0) {
		timer = usbpdtmr->timerhandle;
		hrtimer_cancel(&timer->hr_timer);
	}
	return status;
}

int sii_delete_timer(struct usbpd_device *context, struct usbpd_timer *usbpdtmr)
{
	int status = 0;
	struct usbpd_device *drv_context = context;
	struct timer_obj *timer;

	status = is_timer_handle_valid(drv_context, usbpdtmr->timerhandle);
	if (status == 0) {
		timer = usbpdtmr->timerhandle;
		list_del(&timer->list_link);
		hrtimer_cancel(&timer->hr_timer);
		if (timer->flags & TIMER_OBJ_FLAG_WORK_IP)
			timer->flags |= TIMER_OBJ_FLAG_DEL_REQ;
		else
			kfree(timer);
	}
	return status;
}

void sii_destroy_timer_support(struct usbpd_device *drv_context)
{
	struct timer_obj *sii_timer;

	/*Make sure all outstanding timer objects are canceled
	 *and the memory allocated for them is freed.
	 */
	while (!list_empty(&drv_context->timer_list)) {
		sii_timer = list_first_entry(&drv_context->timer_list, struct timer_obj, list_link);

		hrtimer_cancel(&sii_timer->hr_timer);
		list_del(&sii_timer->list_link);
		kfree(sii_timer);
	}
}

enum hrtimer_restart sii_timer_handler(struct hrtimer *timer)
{
	struct timer_obj *sii_timer;

	sii_timer = container_of(timer, struct timer_obj, hr_timer);
	sii_timer->flags |= TIMER_OBJ_FLAG_WORK_IP;
	sii_timer->timer_callback_handler(sii_timer->callback_param);
	sii_timer->flags &= ~TIMER_OBJ_FLAG_WORK_IP;

	if (sii_timer->flags & TIMER_OBJ_FLAG_DEL_REQ)
		kfree(sii_timer);
	return HRTIMER_NORESTART;
}

int is_timer_handle_valid(struct usbpd_device *drv_context, void *timer_handle)
{
	struct timer_obj *timer = timer_handle;	/* Set to avoid lint warning. */

	list_for_each_entry(timer, &drv_context->timer_list, list_link) {
		if (timer == timer_handle)
			break;
	}
	if (timer != timer_handle) {
		pr_debug("Invalid timer handle %p received\n", timer_handle);
		return -EINVAL;
	}
	return 0;
}
#else
static int s_timer_thread(void *data)
{
	struct timer_obj *sii_timer = (struct timer_obj *)data;
	int ret;

	if (!sii_timer) {
		pr_err("Invalid Timer Instance\n");
		return -ENODEV;
	}

	do {
		if (sii_timer->timer_cnt == 0)
			ret = wait_event_interruptible(sii_timer->timer_run_wait_queue,
						       sii_timer->wake_flag);
		/*handle timer delete condition */
		if (kthread_should_stop()) {
			break;
			;
		}
		sii_timer->wake_flag = 0;

		/*handle timer stop condition */
		if (!sii_timer->stop_flag && sii_timer->timer_callback_handler)
			sii_timer->timer_callback_handler(sii_timer->callback_param);

		if (sii_timer->timer_cnt > 0)
			sii_timer->timer_cnt--;
	} while (true);
	sii_timer->del_flag = 1;
	wake_up_interruptible(&sii_timer->timer_del_wait_queue);
	return 0;
}

int sii_timer_start(void **timer_handle)
{
	struct timer_obj *sii_timer = (struct timer_obj *)*timer_handle;
	/*uint32_t time_msec = 1; */

	if (!sii_timer) {
		pr_err("Invalid Timer Instance\n");
		return -ENODEV;
	}
	pr_info("timer_check :%lx\n", jiffies);
	/* jiffies + HZ yields 1 sec delay */
	sii_timer->timer.expires = jiffies + ((HZ *
					       ((sii_timer->time_interval >= 10) ?
						(sii_timer->time_interval) : 10)) / 1000) + 1;
	pr_info("\nexpires = %lx\n", sii_timer->timer.expires);
	if (!timer_pending(&sii_timer->timer)) {
		add_timer(&sii_timer->timer);
		;
	} else {
		/*sii_timer->timer_cnt++; */
		mod_timer_pending(&sii_timer->timer, sii_timer->timer.expires);
		pr_debug("Previous timer pending\n");
	}
	sii_timer->stop_flag = 0;
	return 0;
}

int sii_timer_stop(void **timer_handle)
{
	struct timer_obj *sii_timer = (struct timer_obj *)*timer_handle;

	if (!sii_timer) {
		pr_err("Invalid Timer Instance\n");
		return -ENODEV;
	}
	sii_timer->timer.expires = 0;
	sii_timer->stop_flag = 1;
	sii_timer->wake_flag = 0;
	return 0;
}

static void s_timer_handler(unsigned long data_ptr)
{
	struct timer_obj *sii_timer = (struct timer_obj *)data_ptr;

	if (!sii_timer) {
		pr_err("Invalid Timer Instance\n");
		return;
	}

	sii_timer->wake_flag = 1;
	wake_up_interruptible(&sii_timer->timer_run_wait_queue);
}

int sii_timer_create(void (*callback_handler) (void *callback_param),
		     void *callback_param, void **timer_handle, uint32_t time_msec,
		     bool periodicity)
{
	struct timer_obj *new_timer;

	if (!callback_handler) {
		pr_debug("Invalid context\n");
		return -EINVAL;
	}
	new_timer = kzalloc(sizeof(*new_timer), GFP_KERNEL);
	if (new_timer == NULL)
		return -ENOMEM;

	new_timer->timer_cnt = 0;
	new_timer->timer_callback_handler = callback_handler;
	new_timer->callback_param = callback_param;
	new_timer->wake_flag = 0;
	new_timer->del_flag = 0;
	new_timer->stop_flag = 0;
	new_timer->time_interval = time_msec;
	new_timer->periodicity = periodicity;	/*not using */
	init_waitqueue_head(&new_timer->timer_run_wait_queue);
	init_waitqueue_head(&new_timer->timer_del_wait_queue);
	new_timer->timer_task_thread = kthread_create(s_timer_thread, (void *)new_timer, "kthread");
	if (new_timer->timer_task_thread)
		wake_up_process(new_timer->timer_task_thread);
	else
		goto exit;
	new_timer->timer.slack = 0;
	init_timer(&new_timer->timer);
	new_timer->timer.function = s_timer_handler;
	new_timer->timer.data = (unsigned long)new_timer;

	*timer_handle = new_timer;
	return 0;
exit:
	kfree(new_timer);
	return 0;
}

int sii_timer_delete(void **timer_handle)
{
	struct timer_obj *sii_timer = (struct timer_obj *)*timer_handle;

	if (!sii_timer)
		return -ENODEV;

	sii_timer->wake_flag = 1;
	kthread_stop(sii_timer->timer_task_thread);
	wake_up_interruptible(&sii_timer->timer_run_wait_queue);
	wait_event_interruptible(sii_timer->timer_del_wait_queue, sii_timer->del_flag);
	del_timer(&sii_timer->timer);
	kfree(sii_timer);
	return 0;
}
#endif
