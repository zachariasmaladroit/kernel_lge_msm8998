/*
 * Copyright(c) 2017, LG Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)     "[Display] %s: " fmt, __func__

#include <linux/delay.h>
#include "lge_display_control.h"
#include "../mdss_fb.h"

extern struct msm_fb_data_type *mfd_primary_base;
extern struct mdss_panel_data *pdata_base;
extern int mdss_dsi_parse_dcs_cmds(struct device_node *np,
		struct dsi_panel_cmds *pcmds, char *cmd_key, char *link_key);
extern void mdss_dsi_panel_cmds_send(struct mdss_dsi_ctrl_pdata *ctrl,
		struct dsi_panel_cmds *pcmds, u32 flags);

#if defined(CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING)
extern struct led_classdev backlight_led;
extern void mdss_fb_set_bl_brightness(struct led_classdev *led_cdev,
				      enum led_brightness value);
#endif /* CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING */

void mdss_dsi_parse_display_control_dcs_cmds(struct device_node *np,
				struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	lge_ctrl_pdata = ctrl_pdata->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	if (lge_ctrl_pdata->display_control_parse_dt)
		lge_ctrl_pdata->display_control_parse_dt(np, ctrl_pdata);

	mdss_dsi_parse_dcs_cmds(np, &lge_ctrl_pdata->white_d65_cmds,
		"lge,mdss-dsi-d65-command", "lge,mdss-dsi-hs-command-state");
#if defined(CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING)
	mdss_dsi_parse_dcs_cmds(np, &lge_ctrl_pdata->bc_dim_cmds,
		"lge,mdss-dsi-bc-dim-command", "lge,mdss-dsi-hs-command-state");
	mdss_dsi_parse_dcs_cmds(np, &lge_ctrl_pdata->bc_default_cmds,
		"lge,mdss-dsi-bc-default_command", "lge,mdss-dsi-hs-command-state");
#endif /* CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING */
#if defined(CONFIG_LGE_DISPLAY_COLOR_MANAGER)
	lge_color_manager_parse_dt(np, ctrl_pdata);
#endif /* CONFIG_LGE_DISPLAY_COLOR_MANAGER */
#if defined(CONFIG_LGE_DISPLAY_COMFORT_MODE)
	lge_mdss_comfort_view_parse_dt(np, ctrl_pdata);
#endif /* CONFIG_LGE_DISPLAY_COMFORT_MODE */
}

void lge_display_control_store(struct mdss_dsi_ctrl_pdata *ctrl_pdata, bool cmd_send)
{
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	if (ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	lge_ctrl_pdata = ctrl_pdata->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	if (lge_ctrl_pdata->display_control_store)
		lge_ctrl_pdata->display_control_store(ctrl_pdata, cmd_send);
}

static ssize_t dgc_status_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->dgc_status);
}

static ssize_t dgc_status_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore sharpness_status set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->dgc_status = input & 0x01;
	lge_display_control_store(ctrl, true);

	pr_debug("dgc_status %d \n", lge_ctrl_pdata->dgc_status);
	return ret;
}
static DEVICE_ATTR(dgc_status, S_IWUSR|S_IRUGO, dgc_status_get, dgc_status_set);

static ssize_t sharpness_status_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->sharpness_status);
}

static ssize_t sharpness_status_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore sharpness_status set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->sharpness_status = input & 0x01;
	lge_display_control_store(ctrl, true);

	pr_debug("sharpness_status %d \n", lge_ctrl_pdata->sharpness_status);
	return ret;
}
static DEVICE_ATTR(sharpness_status, S_IWUSR|S_IRUGO,
					sharpness_status_get, sharpness_status_set);

static ssize_t boost_status_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->boost_status);
}

static ssize_t boost_status_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore boost_status set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->boost_status = input & 0x01;
	lge_display_control_store(ctrl, true);

	pr_debug("boost status %d \n", lge_ctrl_pdata->boost_status);
	return ret;
}
static DEVICE_ATTR(boost_status, S_IRUGO | S_IWUSR | S_IWGRP,
					boost_status_get, boost_status_set);

static ssize_t contrast_status_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->contrast_status);
}

static ssize_t contrast_status_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore contrast_status set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->contrast_status = input & 0x01;
	lge_display_control_store(ctrl, true);

	pr_debug("contrast_status %d \n", lge_ctrl_pdata->contrast_status);
	return ret;
}
static DEVICE_ATTR(contrast_status, S_IWUSR|S_IRUGO,
					contrast_status_get, contrast_status_set);

#if defined(CONFIG_LGE_DISPLAY_VR_MODE)
int lge_vr_low_persist_reg_backup(struct mdss_dsi_ctrl_pdata *ctrl)
{
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (ctrl == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	if (lge_ctrl_pdata->display_control_vr_reg_backup) {
		lge_ctrl_pdata->display_control_vr_reg_backup(ctrl);
	}

	return 0;
}

void mdss_dsi_panel_apply_settings(struct mdss_dsi_ctrl_pdata *ctrl,
	struct dsi_panel_cmds *pcmds)
{
	int vr_enable;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	struct dsi_panel_cmds *pre_lp_cmds;
	struct dsi_panel_cmds *post_lp_cmds;

	if (ctrl == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	vr_enable = (pcmds->cmds[0].payload[1] & 0x08);
	pr_debug("vr persist %s\n", (vr_enable ? "enabled" : "disabled"));

	if (vr_enable) {
		lge_ctrl_pdata->vr_status = 0x01;
		pre_lp_cmds = &ctrl->pre_lp_on_cmds;
		post_lp_cmds = &ctrl->post_lp_on_cmds;

		if (pre_lp_cmds->cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, pre_lp_cmds, CMD_REQ_COMMIT);
		lge_display_control_store(ctrl, true);
		if (post_lp_cmds->cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, post_lp_cmds, CMD_REQ_COMMIT);
	} else {
		lge_ctrl_pdata->vr_status = 0x00;
		pre_lp_cmds = &ctrl->pre_lp_off_cmds;
		post_lp_cmds = &ctrl->post_lp_off_cmds;

		if (pre_lp_cmds->cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, pre_lp_cmds, CMD_REQ_COMMIT);
		lge_display_control_store(ctrl, true);
		if (post_lp_cmds->cmd_cnt)
			mdss_dsi_panel_cmds_send(ctrl, post_lp_cmds, CMD_REQ_COMMIT);
	}
}
#endif /* CONFIG_LGE_DISPLAY_VR_MODE */

static ssize_t hdr_hbm_lut_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl =  container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->hdr_hbm_lut);
}

static ssize_t hdr_hbm_lut_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore hdr_hbm_lut_set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->hdr_hbm_lut = input & 0x03;
	lge_display_control_store(ctrl, true);

	pr_debug("hdr_hbm_lut status %d \n", lge_ctrl_pdata->hdr_hbm_lut);
	return ret;
}
static DEVICE_ATTR(hdr_hbm_lut, S_IWUSR|S_IRUGO,
					hdr_hbm_lut_get, hdr_hbm_lut_set);

static ssize_t hdr_mode_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->hdr_mode);
}

static ssize_t hdr_mode_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore hdr_mode set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->hdr_mode = input & 0x03;
	lge_display_control_store(ctrl, true);

	pr_debug("hdr_mode status %d \n", lge_ctrl_pdata->hdr_mode);
	return ret;
}
static DEVICE_ATTR(hdr_mode, S_IWUSR|S_IRUGO, hdr_mode_get, hdr_mode_set);

static ssize_t acl_mode_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->acl_mode);
}

static ssize_t acl_mode_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore acl_mode set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->acl_mode = input & 0x03;
	lge_display_control_store(ctrl, true);

	pr_debug("acl_mode status %d \n", lge_ctrl_pdata->acl_mode);
	return ret;
}
static DEVICE_ATTR(acl_mode, S_IWUSR|S_IRUGO, acl_mode_get, acl_mode_set);

static ssize_t white_point_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", (lge_ctrl_pdata->white_target ? "D65" : "7500K"));
}

static ssize_t white_point_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}
	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore white point set cmd\n");
		return -EINVAL;
	}
	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	lge_ctrl_pdata->white_target = input & 0x01;
	lge_ctrl_pdata->dgc_status = lge_ctrl_pdata->white_target;

	mdss_dsi_panel_cmds_send(ctrl, &lge_ctrl_pdata->white_d65_cmds, CMD_REQ_COMMIT);
	lge_display_control_store(ctrl, true);

	pr_debug("white_target %s \n", lge_ctrl_pdata->white_target ? "D65" : "7500K");
	return ret;
}
static DEVICE_ATTR(white_target, S_IWUSR|S_IRUGO,
					white_point_get, white_point_set);

#if defined(CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING)
int lge_bc_dim_reg_backup(struct mdss_dsi_ctrl_pdata *ctrl)
{
	char *data;
	int ret = 0;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (ctrl == NULL) {
		pr_err("Invalid ctrl\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid lge_ctrl_pdata\n");
		return -EINVAL;
	}

	data = &lge_ctrl_pdata->bc_default_cmds.cmds[0].payload[1];
	ret = lge_mdss_dsi_panel_cmd_read(BC_CTRL_REG, BC_CTRL_REG_NUM, data);
	if(ret < 0) {
		pr_err("BC CTRL Register Backup Failed\n");
		return -EAGAIN;
	}

	return ret;
}

void lge_bc_dim_set(struct mdss_dsi_ctrl_pdata *ctrl,
		int dim_en, int f_cnt)
{
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	if (ctrl == NULL) {
		pr_err("Invalid ctrl\n");
		return;
	}

	if (pdata_base == NULL) {
		pr_err("Invalid input pdata_base\n");
		return;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid lge_ctrl_pdata\n");
		return;
	}

	if (!mdss_panel_is_power_on_interactive(pdata_base->panel_info.panel_power_state)) {
		pr_debug("skip brightness dimming control\n");
		return;
	}

	if(dim_en == BC_DIM_ON) {
		lge_ctrl_pdata->bc_default_cmds.cmds[0].payload[22] = f_cnt;
		mdss_dsi_panel_cmds_send(ctrl,
				&lge_ctrl_pdata->bc_default_cmds, CMD_REQ_COMMIT);

		lge_ctrl_pdata->bc_dim_cmds.cmds[0].payload[1] = BC_DIM_ON;
		mdss_dsi_panel_cmds_send(ctrl,
				&lge_ctrl_pdata->bc_dim_cmds, CMD_REQ_COMMIT);
		mdelay(15);
	} else {
		lge_ctrl_pdata->bc_dim_cmds.cmds[0].payload[1] = BC_DIM_OFF;
		mdss_dsi_panel_cmds_send(ctrl,
				&lge_ctrl_pdata->bc_dim_cmds, CMD_REQ_COMMIT);
		mdelay(15);
	}

	pr_debug("DIM : %s, FRAME : %d \n", (dim_en == BC_DIM_ON) ? "ON" : "OFF",
			lge_ctrl_pdata->bc_default_cmds.cmds[0].payload[22]);
	return;
}

void lge_mdss_dsi_bc_dim_work(struct work_struct *work)
{
	struct delayed_work *dw = to_delayed_work(work);
	struct mdss_dsi_ctrl_pdata *ctrl_pdata = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	struct mdss_panel_data *pdata;

	lge_ctrl_pdata = container_of(dw, struct lge_mdss_dsi_ctrl_pdata, bc_dim_work);
	if (lge_ctrl_pdata == NULL || pdata_base == NULL) {
		pr_err("Invalid input\n");
		return;
	}

	ctrl_pdata = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	if(ctrl_pdata == NULL) {
		pr_err("invalid ctrl_pdata\n");
		return;
	}

	pdata = &ctrl_pdata->panel_data;
	if (!pdata) {
		pr_err("invalid pdata\n");
		return;
	}

	if (!mdss_panel_is_power_on_interactive(pdata->panel_info.panel_power_state)) {
		pr_debug("skip brightness dimming control\n");
		return;
	} else {
		lge_bc_dim_set(ctrl_pdata, BC_DIM_ON, BC_DIM_FRAMES_NORMAL);
		pr_debug("Set Normal Dim Mode\n");
	}
	return;
}

static ssize_t therm_dim_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	int bc_dim_en = 0;
	int bc_dim_f_cnt = 0;

	if (pdata_base == NULL) {
		pr_err("Invalid pdata_base\n");
		return -EINVAL;
	}

	mutex_lock(&mfd_primary_base->mdss_sysfs_lock);

	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore therm_dim_set cmd\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	if(backlight_led.brightness < BC_DIM_BRIGHTNESS_THERM) {
		pr_err("Normal Mode. Skip therm dim. Current Brightness %d\n",
				backlight_led.brightness);
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,	panel_data);
	if (ctrl == NULL) {
		pr_err("Invalid ctrl\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid lge_ctrl_pdata\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);

	if(input) {
		bc_dim_en = BC_DIM_ON;
		bc_dim_f_cnt = BC_DIM_FRAMES_THERM;
	} else {
		bc_dim_en = BC_DIM_ON;
		bc_dim_f_cnt = BC_DIM_FRAMES_NORMAL;
	}

	cancel_delayed_work_sync(&lge_ctrl_pdata->bc_dim_work);
	lge_bc_dim_set(ctrl, bc_dim_en, bc_dim_f_cnt);
	mdss_fb_set_bl_brightness(&backlight_led, BC_DIM_BRIGHTNESS_THERM);
	queue_delayed_work(system_power_efficient_wq,
			&lge_ctrl_pdata->bc_dim_work, BC_DIM_TIME);

	mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);

	pr_debug("Thermal BC DIM: %d, FRAME : %d\n", bc_dim_en, bc_dim_f_cnt);
	return ret;
}
static DEVICE_ATTR(therm_dim, S_IWUSR, NULL, therm_dim_set);

static ssize_t brightness_dim_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	int bc_dim_en = 0;
	int bc_dim_f_cnt = 0;

	if (pdata_base == NULL) {
		pr_err("Invalid pdata_base\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata, panel_data);
	if (ctrl == NULL) {
		pr_err("Invalid ctrl\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;

	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid lge_ctrl_pdata\n");
		return -EINVAL;
	}

	bc_dim_en = lge_ctrl_pdata->bc_dim_cmds.cmds[0].payload[1];
	bc_dim_f_cnt = lge_ctrl_pdata->bc_default_cmds.cmds[0].payload[22];

	pr_debug("BC DIM EN : %d , FRAMES : %d \n", bc_dim_en, bc_dim_f_cnt);
	return sprintf(buf, "%d\n", bc_dim_f_cnt);
}

static ssize_t brightness_dim_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	int input;
	int bc_dim_en = 0;
	int bc_dim_f_cnt = 0;

	if (pdata_base == NULL) {
		pr_err("Invalid pdata_base\n");
		return -EINVAL;
	}

	if (!mdss_panel_is_power_on_interactive(pdata_base->panel_info.panel_power_state)) {
		pr_debug("skip brightness dimming control\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,	panel_data);
	if (ctrl == NULL) {
		pr_err("Invalid ctrl\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid lge_ctrl_pdata\n");
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	if(input == BC_DIM_OFF) {
		bc_dim_en = BC_DIM_OFF;
		bc_dim_f_cnt = BC_DIM_FRAMES_NORMAL;
	} else {
		bc_dim_en = BC_DIM_ON;
		if(input < BC_DIM_MIN_FRAMES)
			bc_dim_f_cnt = BC_DIM_MIN_FRAMES;
		else if(input > BC_DIM_MAX_FRAMES)
			bc_dim_f_cnt = BC_DIM_MAX_FRAMES;
		else
			bc_dim_f_cnt = input;
	}

	lge_bc_dim_set(ctrl, bc_dim_en, bc_dim_f_cnt);

	pr_debug("BC DIM : %d\n", bc_dim_f_cnt);
	return ret;
}

static DEVICE_ATTR(brightness_dim, S_IWUSR|S_IRUGO,
		brightness_dim_get, brightness_dim_set);
#endif /* CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING */

#if defined(CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT)
static ssize_t video_enhancement_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	ctrl = container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	if(ctrl == NULL) {
		pr_err("invalid ctrl\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", lge_ctrl_pdata->video_enhancement);
}

static ssize_t video_enhancement_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	ssize_t ret = strnlen(buf, PAGE_SIZE);
	int input;
	struct mdss_dsi_ctrl_pdata *ctrl = NULL;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;

	if (pdata_base == NULL || mfd_primary_base == NULL) {
		pr_err("Invalid input data\n");
		return -EINVAL;
	}

	mutex_lock(&mfd_primary_base->mdss_sysfs_lock);

	if (pdata_base->panel_info.panel_power_state == 0) {
		pr_err("Panel off state. Ignore video_enhancement_set cmd\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	ctrl =  container_of(pdata_base, struct mdss_dsi_ctrl_pdata,
			panel_data);
	if(ctrl == NULL) {
		pr_err("invalid ctrl\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl->lge_ctrl_pdata;
	if (lge_ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);
		return -EINVAL;
	}

	sscanf(buf, "%d", &input);
	lge_ctrl_pdata->video_enhancement = input;

	pdata_base->panel_info.ve_mode_on = input;

	lge_ctrl_pdata->hdr_mode = input & 0x03;
	lge_display_control_store(ctrl, true);
	pr_debug("send cmds to %s the video enhancer \n",
	        (input == true) ? "enable" : "disable");

#if defined(CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING)
	cancel_delayed_work_sync(&lge_ctrl_pdata->bc_dim_work);
	lge_bc_dim_set(ctrl, BC_DIM_ON, BC_DIM_FRAMES_VE);
	mdss_fb_set_bl_brightness(&backlight_led, backlight_led.brightness);
	queue_delayed_work(system_power_efficient_wq,
			&lge_ctrl_pdata->bc_dim_work, BC_DIM_TIME);

	pr_debug("VE Mode bl_lvl : %d, BC DIM: 0x%x, BC : 0x%x\n",
			backlight_led.brightness,
			lge_ctrl_pdata->bc_dim_cmds.cmds[0].payload[1],
			lge_ctrl_pdata->bc_default_cmds.cmds[0].payload[22]);

#endif /* CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING */
	mutex_unlock(&mfd_primary_base->mdss_sysfs_lock);

	return ret;
}
static DEVICE_ATTR(video_enhancement, S_IRUGO | S_IWUSR | S_IWGRP,
					video_enhancement_get, video_enhancement_set);
#endif /* CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT */

static ssize_t dolby_mode_get(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", 0);
}

static ssize_t dolby_mode_set(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(dolby_mode, S_IRUGO | S_IWUSR | S_IWGRP,
				dolby_mode_get, dolby_mode_set);

int lge_display_control_create_sysfs(struct class *panel)
{
	int rc = 0;
	static struct device *panel_sysfs_dev = NULL;

	if (!panel) {
		pr_err("Invalid input panel class\n");
		return -EINVAL;
	}

	if (!panel_sysfs_dev) {
		panel_sysfs_dev = device_create(panel, NULL, 0, NULL, "img_tune");
		if (IS_ERR(panel_sysfs_dev)) {
			pr_err("Failed to create dev(panel_sysfs_dev)!");
		} else {
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_dgc_status)) < 0)
				pr_err("add dgc_status set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_sharpness_status)) < 0)
				pr_err("add sharpness set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_boost_status)) < 0)
				pr_err("add boost set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_contrast_status)) < 0)
				pr_err("add contrast_status set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_hdr_hbm_lut)) < 0)
				pr_err("add hdr_hbm_lut set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_hdr_mode)) < 0)
				pr_err("add hdr_mode set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_acl_mode)) < 0)
				pr_err("add scl_mode set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_white_target)) < 0)
				pr_err("add white_target set node fail!");
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_dolby_mode)) < 0)
				pr_err("add dolby mode node fail!");
#if defined(CONFIG_LGE_DISPLAY_COLOR_MANAGER)
			if ((rc = lge_color_manager_create_sysfs(panel_sysfs_dev)) < 0)
				pr_err("failed creating color manager sysfs\n");
#endif /* CONFIG_LGE_DISPLAY_COLOR_MANAGER */
#if defined(CONFIG_LGE_DISPLAY_COMFORT_MODE)
			if ((rc = lge_mdss_comfort_view_create_sysfs(panel_sysfs_dev)) < 0)
				pr_err("failed creating comfort view sysfs\n");
#endif /* CONFIG_LGE_DISPLAY_COMFORT_MODE */
#if defined(CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT)
			if ((rc = device_create_file(panel_sysfs_dev, &dev_attr_video_enhancement)) < 0)
				pr_err("add video enhancement_mode set node fail!");
#endif /* CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT */
#if defined(CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING)
			if((rc = device_create_file(panel_sysfs_dev, &dev_attr_brightness_dim)) < 0)
				pr_err("add brightness dim sysfs\n");
			if((rc = device_create_file(panel_sysfs_dev, &dev_attr_therm_dim)) < 0)
				pr_err("add thermal dim sysfs\n");
#endif /* CONFIG_LGE_DISPLAY_BRIGHTNESS_DIMMING */
		}
	}
	return rc;
}

int lge_display_control_init(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	int rc = 0;
	struct lge_mdss_dsi_ctrl_pdata *lge_ctrl_pdata = NULL;
	if (ctrl_pdata == NULL) {
		pr_err("Invalid input\n");
		return -EINVAL;
	}

	lge_ctrl_pdata = ctrl_pdata->lge_ctrl_pdata;

	/* This function should be defined under each model directory */
	lge_display_control_init_sub(lge_ctrl_pdata);

#if defined(CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT)
	INIT_DELAYED_WORK(&lge_ctrl_pdata->bc_dim_work, lge_mdss_dsi_bc_dim_work);
#endif /*CONFIG_LGE_DISPLAY_VIDEO_ENHANCEMENT*/
#if defined(CONFIG_LGE_DISPLAY_COMFORT_MODE)
	rc = lge_mdss_comfort_view_init(ctrl_pdata);
	if (rc) {
		pr_err("[Comfort View] fail to init (rc:%d)\n", rc);
 	}
#endif /* CONFIG_LGE_DISPLAY_COMFORT_MODE */
	return rc;
}
