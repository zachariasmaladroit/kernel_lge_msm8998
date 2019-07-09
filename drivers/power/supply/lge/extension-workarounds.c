#include <linux/delay.h>
#include <linux/pmic-voter.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>

#include "../qcom/smb-reg.h"
#include "../qcom/smb-lib.h"


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Helper functions
////////////////////////////////////////////////////////////////////////////

static void workaround_helper_regdump(int interesting) {
#ifdef CONFIG_LGE_PM_VENEER_PSY
	enum {	TOTAL	= -1,
		POLL	=  0,
		CHGR	=  1,
		OTG	=  2,
		BATIF	=  3,
		USB	=  4,
		DC	=  5,
		QNOVO	=  6,
		MISC	=  7,
		USBPD	=  8,
		MBG	=  9,
	} command = interesting;

	struct power_supply* psy = power_supply_get_by_name("battery");
	if (psy) {
		union power_supply_propval regdump = { .intval = command, };
		power_supply_set_property(psy,
			POWER_SUPPLY_PROP_DEBUG_BATTERY, &regdump);
		power_supply_put(psy);
	}
#endif
	return;
}

static struct smb_charger* workaround_helper_chg(void) {
	// getting smb_charger from air
	struct power_supply*	psy
		= power_supply_get_by_name("battery");
	struct smb_charger*	chg
		= psy ? power_supply_get_drvdata(psy) : NULL;
	if (psy)
		power_supply_put(psy);

	return chg;
}

static bool workaround_command_chgtype(enum power_supply_type pst) {
#ifdef CONFIG_LGE_PM_VENEER_PSY
	struct power_supply* veneer = power_supply_get_by_name("veneer");

	if (veneer) {
		union power_supply_propval chgtype = { .intval = pst, };
		power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &chgtype);
		power_supply_changed(veneer);
		power_supply_put(veneer);
		pr_info("[W/A] PSD-!) Setting charger type to %d by force\n", pst);

		return true;
	}
#endif
	return false;
}

static bool workaround_command_apsd(/*@Nonnull*/ struct smb_charger* chg) {
	#define APSD_RERUN_DELAY_MS 4000
	u8	buffer;
	u8	command;
	u8	oncc;
	bool	ret = false;

	static DEFINE_MUTEX(lock);
	mutex_lock(&lock);

	if (smblib_read(chg, APSD_RESULT_STATUS_REG, &buffer) >= 0) {
		// Check the ICL_OVERRIDE_LATCH_BIT
		bool override = !!(buffer & ICL_OVERRIDE_LATCH_BIT);
		command = APSD_RERUN_BIT | (!override ? ICL_OVERRIDE_BIT : 0);
	}
	else {	pr_info("[W/A] PSD-1) Couldn't read APSD_RESULT_STATUS_REG\n");
		goto failed;
	}

	oncc = chg->typec_mode == POWER_SUPPLY_TYPEC_NONE ? 0 : APSD_START_ON_CC_BIT;
	if (smblib_masked_write(chg, TYPE_C_CFG_REG, APSD_START_ON_CC_BIT, oncc) < 0) {
		pr_info("[W/A] PSD-2) Couldn't set APSD_START_ON_CC to %d\n", !!oncc);
		goto failed;
	}
	if (smblib_masked_write(chg, USBIN_SOURCE_CHANGE_INTRPT_ENB_REG, AUTH_IRQ_EN_CFG_BIT, AUTH_IRQ_EN_CFG_BIT) < 0) {
		pr_info("[W/A] PSD-3) Couldn't enable HVDCP auth IRQ\n");
		goto failed;
	}
	if (smblib_masked_write(chg, CMD_APSD_REG, ICL_OVERRIDE_BIT | APSD_RERUN_BIT, command) < 0) {
		pr_info("[W/A] PSD-4) Couldn't re-run APSD\n");
		goto failed;
	}

	msleep(APSD_RERUN_DELAY_MS);

	if (smblib_masked_write(chg, TYPE_C_CFG_REG, APSD_START_ON_CC_BIT, 0) < 0) {
		pr_info("[W/A] PSD-5) Couldn't set APSD_START_ON_CC to 0\n");
		goto failed;
	}

	if (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_FLOAT
		&& !workaround_command_chgtype(POWER_SUPPLY_TYPE_USB_FLOAT)) {
		pr_info("[W/A] PSD-6) Couldn't toast slowchg popup\n");
	}

	pr_info("[W/A] PSD-#) Success to detect charger type to %d\n", chg->real_charger_type);
	ret = true;

failed:	mutex_unlock(&lock);
	return ret;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Resuming Suspended USBIN
////////////////////////////////////////////////////////////////////////////

static bool workaround_resuming_suspended_usbin_required(void) {
	// Checking condition in prior to recover usbin suspending
	struct smb_charger*  chg = workaround_helper_chg();

	u8 reg_status_aicl, reg_status_powerpath, reg_status_rt;
	if (chg && (get_effective_result_locked(chg->usb_icl_votable) != 0)
/*0x160A*/	&& (smblib_read(chg, AICL_STATUS_REG, &reg_status_aicl) >= 0)
/*0x160B*/	&& (smblib_read(chg, POWER_PATH_STATUS_REG, &reg_status_powerpath) >= 0)
/*0x1310*/	&& (smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &reg_status_rt) >= 0) ) {
		pr_info("[W/A] RSU-?) AICL_STATUS_REG(0x%04x):0x%02x,"
			" POWER_PATH_STATUS_REG(0x%04x):0x%02x"
			" USB_INT_RT_STS(0x%04x):0x%02x\n",
			AICL_STATUS_REG, reg_status_aicl,
			POWER_PATH_STATUS_REG, reg_status_powerpath,
			USBIN_BASE + INT_RT_STS_OFFSET, reg_status_rt);

		if (reg_status_rt & USBIN_PLUGIN_RT_STS_BIT) {
			if ((reg_status_aicl & AICL_FAIL_BIT)
				|| (reg_status_powerpath & USBIN_SUSPEND_STS_BIT)) {
				pr_info("[W/A] RSU-?) AICL_FAIL:%d, USBIN_SUSPEND:%d\n",
					!!(reg_status_aicl      & AICL_FAIL_BIT),
					!!(reg_status_powerpath & USBIN_SUSPEND_STS_BIT));
				return true;
			}
		}
		else
			pr_info("[W/A] RSU-?) Skip because USB is not present\n");
	}

	return false;
}

static void workaround_resuming_suspended_usbin_func(struct work_struct *unused) {
// 0. Local variables
	// References for charger driver
	struct smb_charger* chg = workaround_helper_chg();
	int 		    irq = (chg && chg->irq_info) ? chg->irq_info[USBIN_ICL_CHANGE_IRQ].irq : 0;
	// Buffer to R/W PMI register
	int		    ret;
	u8		    buf;
	// Previous values to be restored
	int previous_suspend_on_collapse_usbin;
	int previous_aicl_rerun_time;

	if (!chg || !workaround_resuming_suspended_usbin_required()) {
		pr_info("[W/A] RSU-#) Exiting recovery for USBIN-suspend. (%p)\n", chg);
		return;
	}
	else {
		pr_info("[W/A] RSU-0) Start resuming suspended usbin\n");
		workaround_helper_regdump(-1);
	}

// 1. W/A to prevent the IRQ 'usbin-icl-change' storm (CN#03165535)
	// : Before recovery USBIN-suspend, be sure that IRQ 'usbin-icl-change' is enabled.
	//   If not, this recovery will not work well due to the disabled AICL notification.
	// : To prevent IRQ 'usbin-icl-change' storm, it might be disabled in its own ISR.
	//   Refer to the disabling IRQ condition in 'smblib_handle_icl_change()'
	ret = smblib_read(chg, POWER_PATH_STATUS_REG, &buf);
	if (irq && ret >= 0 && (buf & USBIN_SUSPEND_STS_BIT)
		&& !chg->usb_icl_change_irq_enabled) {

		enable_irq(irq);
		chg->usb_icl_change_irq_enabled = true;
		pr_info("[W/A] RSU-1) USBIN_SUSPEND_STS_BIT = High, Enable ICL-CHANGE IRQ\n");
	}
	else {
		pr_info("[W/A] RSU-1) irq_number=%d, irq_enabled=%d, read_return=%d, read_register=%d\n",
			irq, chg->usb_icl_change_irq_enabled, ret, buf);
	}

// 2. Toggling USBIN_CMD_IL_REG
	pr_info("[W/A] RSU-2) Toggling USBIN_CMD_IL_REG(0x1340[0]) := 1\n");
	if (smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT,
		USBIN_SUSPEND_BIT) < 0) {
		pr_info("[W/A] RSU-2) Couldn't write suspend to USBIN_SUSPEND_BIT\n");
		goto failed;
	}
	pr_info("[W/A] RSU-2) Toggling USBIN_CMD_IL_REG(0x1340[0]) := 0\n");
	if (smblib_masked_write(chg, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT,
		0) < 0) {
		pr_info("[W/A] RSU-2) Couldn't write resume to USBIN_SUSPEND_BIT\n");
		goto failed;
	}

// 3. Save origial AICL configurations
	if (smblib_read(chg, USBIN_AICL_OPTIONS_CFG_REG /*0x1380*/, &buf) >= 0) {
		previous_suspend_on_collapse_usbin = buf & SUSPEND_ON_COLLAPSE_USBIN_BIT;
		pr_info("[W/A] RSU-3) USBIN_AICL_OPTIONS_CFG_REG=0x%02x, SUSPEND_ON_COLLAPSE_USBIN_BIT=0x%02x\n",
			buf, previous_suspend_on_collapse_usbin);
	}
	else {
		pr_info("[W/A] RSU-3) Couldn't read USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	if (smblib_read(chg, AICL_RERUN_TIME_CFG_REG /*0x1661*/, &buf) >= 0) {
		previous_aicl_rerun_time = buf & AICL_RERUN_TIME_MASK;
		pr_info("[W/A] RSU-3) AICL_RERUN_TIME_CFG_REG=0x%02x, AICL_RERUN_TIME_MASK=0x%02x\n",
			buf, previous_aicl_rerun_time);
	}
	else {
		pr_info("[W/A] RSU-3) Couldn't read AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 4. Set 0s to AICL configurationss
	pr_info("[W/A] RSU-4) Setting USBIN_AICL_OPTIONS(0x1380[7]) := 0x00\n");
	if (smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG, SUSPEND_ON_COLLAPSE_USBIN_BIT,
		0) < 0) {
		pr_info("[W/A] RSU-4) Couldn't write USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	pr_info("[W/A] RSU-4) Setting AICL_RERUN_TIME_CFG_REG(0x1661[1:0]) := 0x00\n");
	if (smblib_masked_write(chg, AICL_RERUN_TIME_CFG_REG, AICL_RERUN_TIME_MASK,
		0) < 0) {
		pr_info("[W/A] RSU-4) Couldn't write AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 5. Marginal delaying for AICL rerun
	#define AICL_RERUN_DELAY_MS 3500
	pr_info("[W/A] RSU-5) Waiting more 3 secs . . .\n");
	msleep(AICL_RERUN_DELAY_MS);

// 6. Restore AICL configurations
	pr_info("[W/A] RSU-6) Restoring USBIN_AICL_OPTIONS(0x1380[7]) := 0x%02x\n", previous_suspend_on_collapse_usbin);
	if (smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG, SUSPEND_ON_COLLAPSE_USBIN_BIT,
		previous_suspend_on_collapse_usbin) < 0) {
		pr_info("[W/A] RSU-6) Couldn't write USBIN_AICL_OPTIONS_CFG_REG\n");
		goto failed;
	}

	pr_info("[W/A] RSU-6) Restoring AICL_RERUN_TIME_CFG_REG(0x1661[1:0]) := 0x%02x\n", previous_aicl_rerun_time);
	if (smblib_masked_write(chg, AICL_RERUN_TIME_CFG_REG, AICL_RERUN_TIME_MASK,
		previous_aicl_rerun_time) < 0) {
		pr_info("[W/A] RSU-6) Couldn't write AICL_RERUN_TIME_CFG_REG\n");
		goto failed;
	}

// 7. If USBIN suspend is not resumed even with rerunning AICL, recover it from APSD.
	msleep(APSD_RERUN_DELAY_MS);
	if (workaround_resuming_suspended_usbin_required()) {
		pr_info("[W/A] RSU-7) Recover USBIN from APSD\n");
		workaround_command_apsd(chg);
	}
	else
		pr_info("[W/A] RSU-#) Success resuming suspended usbin\n");

	return;
failed:
	pr_info("[W/A] RSU-!) Error on resuming suspended usbin\n");
}

static DECLARE_DELAYED_WORK(workaround_resuming_suspended_usbin_dwork, workaround_resuming_suspended_usbin_func);

void workaround_resuming_suspended_usbin_trigger(struct smb_charger* chg) {

	if (!workaround_resuming_suspended_usbin_required()) {
		pr_info("[W/A] RSU-#) Exiting recovery for USBIN-suspend.\n");
		return;
	}

	// Considering burst aicl-fail IRQs, previous workaround works will be removed,
	// to make this trigger routine handle the latest aicl-fail
	if (delayed_work_pending(&workaround_resuming_suspended_usbin_dwork)) {
		pr_info("[W/A] RSU-0) Cancel the pending resuming work . . .\n");
		cancel_delayed_work(&workaround_resuming_suspended_usbin_dwork);
	}

	#define INITIAL_DELAY_MS 5000
	schedule_delayed_work(&workaround_resuming_suspended_usbin_dwork,
		round_jiffies_relative(msecs_to_jiffies(INITIAL_DELAY_MS)));
}

void workaround_resuming_suspended_usbin_clear(struct smb_charger* chg) {
	cancel_delayed_work(&workaround_resuming_suspended_usbin_dwork);
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Rerun apsd for abnormality type
////////////////////////////////////////////////////////////////////////////

static bool workaround_rerun_apsd_done_at_dcp = false;
static bool workaround_rerun_apsd_done_at_phr = false;
static bool workaround_raa_pd_resetdone = false;
static bool workaround_raa_pd_disabled = false;

static void workaround_recovering_abnormal_apsd_main(struct work_struct *unused) {
	struct smb_charger* chg = workaround_helper_chg();
	union power_supply_propval val = {.intval = 0, };

	if (!(workaround_rerun_apsd_done_at_phr || workaround_rerun_apsd_done_at_dcp)
		|| !chg || chg->pd_active) {
		pr_info("[W/A] RAA) stop apsd done. phr(%d), dcp(%d), pd(%d)\n",
			workaround_rerun_apsd_done_at_phr,
			workaround_rerun_apsd_done_at_dcp,
			chg ? chg->pd_active : -1);
		return;
	}

	pr_info("[W/A] RAA) Rerun apsd\n");
	workaround_command_apsd(chg);

	if (workaround_raa_pd_disabled && !chg->typec_legacy_valid && !chg->pd_active
		&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP)
		smblib_set_prop_pd_active(chg, &val);
}

static DECLARE_DELAYED_WORK(workaround_recovering_abnormal_apsd_dwork, workaround_recovering_abnormal_apsd_main);

void workaround_recovering_abnormal_apsd_dcp(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool usb_type_dcp = chg->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP;
	bool usb_vbus_high
		= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	u8 stat;

	int rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_info("[W/A] RAA) Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}
	pr_debug("[W/A] RAA) workaround_recovering_abnormal_apsd_dcp phr(%d), done(%d), TO(%d), DCP(%d), Vbus(%d)\n",
		workaround_raa_pd_resetdone, workaround_rerun_apsd_done_at_dcp, stat, usb_type_dcp, usb_vbus_high);

	if (workaround_raa_pd_resetdone && !workaround_rerun_apsd_done_at_dcp
		&& (stat & HVDCP_CHECK_TIMEOUT_BIT) && usb_type_dcp && usb_vbus_high) {
		workaround_rerun_apsd_done_at_dcp = true;
		schedule_delayed_work(&workaround_recovering_abnormal_apsd_dwork,
			round_jiffies_relative(msecs_to_jiffies(APSD_RERUN_DELAY_MS)));
	}
}

void workaround_recovering_abnormal_apsd_pdreset(struct smb_charger *chg) {
	static int pre_pd_hard_reset = 0;

	if (!chg->pd_hard_reset && pre_pd_hard_reset) {
		union power_supply_propval val
			= { 0, };
		bool usb_vbus_high
			= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
		bool ufp_mode
			= !(chg->typec_status[3] & UFP_DFP_MODE_STATUS_BIT);
		bool usb_type_unknown
			= chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN;
		pr_debug("[W/A] RAA) workaround_rerun_apsd_at_phr_func VBUS(%d), unknown(%d), ufp_mode(%d)\n",
			usb_vbus_high, usb_type_unknown, ufp_mode);

		if (usb_vbus_high && ufp_mode && usb_type_unknown) {
			workaround_rerun_apsd_done_at_phr = true;
			schedule_delayed_work(&workaround_recovering_abnormal_apsd_dwork,
				round_jiffies_relative(msecs_to_jiffies(0)));
		}
	}

	if (!chg->pd_hard_reset)
		workaround_raa_pd_resetdone = true;

	pre_pd_hard_reset = chg->pd_hard_reset;
	workaround_recovering_abnormal_apsd_dcp(chg);
}

void workaround_recovering_abnormal_apsd_clear(void) {
	workaround_rerun_apsd_done_at_phr = false;
	workaround_rerun_apsd_done_at_dcp = false;
	workaround_raa_pd_resetdone = false;
	workaround_raa_pd_disabled = false;
}

void workaround_recovering_abnormal_apsd_pdactive(struct smb_charger *chg, bool pd_active) {
	if (!pd_active)
		workaround_raa_pd_disabled = true;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Charging without CC
////////////////////////////////////////////////////////////////////////////

/* CWC has two works for APSD and HVDCP, and this implementation handles the
 * works independently with different delay.
 * but you can see that retrying HVDCP detection work is depends on rerunning
 * APSD. i.e, APSD work derives HVDCP work.
 */
#define CHARGING_WITHOUT_CC_DELAY_APSD  1000
#define CHARGING_WITHOUT_CC_DELAY_HVDCP 7000
static bool workaround_charging_without_cc_processed = false;

static inline bool workaround_charging_without_cc_required(struct smb_charger *chg) {

	if (chg) {
		union power_supply_propval val = { 0, };

		bool	pd_hard_reset	= chg->pd_hard_reset;
		bool	usb_vbus_high	= !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
		bool	typec_mode_none	= chg->typec_mode == POWER_SUPPLY_TYPEC_NONE;
		bool usb_type_unknown	= chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN;

		bool	workaround_required = !pd_hard_reset && usb_vbus_high && typec_mode_none && usb_type_unknown;
		if (!workaround_required)
			pr_info("[W/A] CWC) Don't need CWC in %s "
				"(pd_hard_reset:%d, usb_vbus_high:%d, typec_mode_none:%d, usb_type_unknown:%d)\n",
				__func__, pd_hard_reset, usb_vbus_high, typec_mode_none, usb_type_unknown);

		return workaround_required;
	}
	else {
		pr_info("[W/A] CWC) 'chg' is not ready\n");
		return false;
	}
}

static void workaround_charging_without_cc_apsd(struct work_struct *unused) {
// Retriving smb_charger from air
	struct smb_charger*  chg = workaround_helper_chg();

// Check the recovery condition one more time
	if (workaround_charging_without_cc_required(chg)) {
		// Even after 'CHARGING_WITHOUT_CC_TRIGGER_DELAY_MS' interval,
		// if the below condition is still remained,
		//	[ !pd_hard_reset && usb_vbus_high && typec_mode_none ]
		// then start to charge without waiting for CC update
		// (by 'cc-state-change' IRQ).

		pr_info("[W/A] CWC) CC line is not recovered until now, Start W/A\n");
		workaround_charging_without_cc_processed = true;

		if (!workaround_command_apsd(chg))
			pr_info("[W/A] CWC) Error on trying APSD\n");
	}

	return;
}
static DECLARE_DELAYED_WORK(workaround_cwc_try_apsd, workaround_charging_without_cc_apsd);

static void workaround_charging_without_cc_hvdcp(struct work_struct *unused) {
	struct smb_charger* chg = workaround_helper_chg();

	if (workaround_charging_without_cc_processed) {
		if (chg && chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP
			&& chg->typec_mode == POWER_SUPPLY_TYPEC_NONE) {
			pr_info("[W/A] CWC) Enable HVDCP\n");
			vote(chg->hvdcp_disable_votable_indirect, VBUS_CC_SHORT_VOTER, false, 0);
			vote(chg->hvdcp_disable_votable_indirect, PD_INACTIVE_VOTER, false, 0);
		}
		else
			pr_info("[W/A] CWC) Skip to try HVDCP\n");
	}
}
static DECLARE_DELAYED_WORK(workaround_cwc_try_hvdcp, workaround_charging_without_cc_hvdcp);

void workaround_charging_without_cc_reserve(struct smb_charger* chg,
	int (*smblib_request_dpdm)(struct smb_charger *chg, bool enable)) {
	// This may be triggered in the IRQ context of the USBIN rising.
	// So main function to start 'charging without cc', is deferred via delayed_work of kernel.
	// Just check and register (if needed) the work in this call.

	if (workaround_charging_without_cc_required(chg)) {
		int rc = smblib_request_dpdm(chg, true);
		if (rc < 0)
			pr_info("[W/A] CWC) Couldn't to enable DPDM rc=%d\n", rc);

		if (delayed_work_pending(&workaround_cwc_try_apsd)) {
			pr_info("[W/A] CWC) Cancel the pended trying apsd . . .\n");
			cancel_delayed_work(&workaround_cwc_try_apsd);
		}
		if (delayed_work_pending(&workaround_cwc_try_hvdcp)) {
			pr_info("[W/A] CWC) Cancel the pended trying hvdcp . . .\n");
			cancel_delayed_work(&workaround_cwc_try_hvdcp);
		}

		schedule_delayed_work(&workaround_cwc_try_apsd,  round_jiffies_relative(
			msecs_to_jiffies(CHARGING_WITHOUT_CC_DELAY_APSD)));
		schedule_delayed_work(&workaround_cwc_try_hvdcp, round_jiffies_relative(
			msecs_to_jiffies(CHARGING_WITHOUT_CC_DELAY_HVDCP)));
	}
}

void workaround_charging_without_cc_withdraw(struct smb_charger* chg,
	void (*smblib_handle_typec_removal)(struct smb_charger *chg)) {
	cancel_delayed_work(&workaround_cwc_try_apsd);
	cancel_delayed_work(&workaround_cwc_try_hvdcp);
	workaround_charging_without_cc_processed = false;

	if (chg->typec_mode == POWER_SUPPLY_TYPEC_NONE
		&& chg->real_charger_type != POWER_SUPPLY_TYPE_UNKNOWN) {

	       /* Condition 1 : chg->typec_mode == POWER_SUPPLY_TYPEC_NONE
		*	If typec_mode is POWER_SUPPLY_TYPEC_NONE, real_charger_type
		*	will not be updated because smblib_handle_typec_removal()
		*	is never called
		* Condition 2 : chg->real_charger_type != POWER_SUPPLY_TYPE_UNKNOWN
		*	Moreover, if real_charger_type is NOT POWER_SUPPLY_TYPE_UNKNOWN
		*	at this time of lowered USBIN, total system should be updated
		*	to valid(==UNKNONW) type for next charger insertion.
		*/

		pr_info("[W/A] CWC) Call typec_removal by force\n");
		smblib_handle_typec_removal(chg);
		power_supply_changed(chg->usb_psy);
	} else {
		if (smblib_masked_write(chg, TYPE_C_CFG_REG, APSD_START_ON_CC_BIT,
			APSD_START_ON_CC_BIT) < 0)
			pr_info("[W/A] CWC) Couldn't enable APSD_START_ON_CC\n");
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Charging With rd cable
////////////////////////////////////////////////////////////////////////////

static bool workaround_charging_with_rd_backup  = true;
static bool workaround_charging_with_rd_running = false;

void workaround_charging_with_rd_tigger(struct smb_charger *chg) {
	union power_supply_propval val = { 0, };
	bool vbus = !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	bool sink = (chg->typec_mode == POWER_SUPPLY_TYPEC_SINK);

	if (vbus && sink) {
		workaround_charging_with_rd_running = true;
		workaround_charging_with_rd_backup = !!*chg->try_sink_enabled;
		*chg->try_sink_enabled = 0;
		pr_info("[W/A] CWR) Skip try sink for charging with rd only\n");
	}
}

void workaround_charging_with_rd_func(struct smb_charger *chg) {
	if (workaround_charging_with_rd_running) {
		u8 reg = 0;

		vote(chg->usb_icl_votable, OTG_VOTER, false, 0);
		// Check and set the ICL_OVERRIDE_LATCH_BIT
		if (smblib_read(chg, APSD_RESULT_STATUS_REG, &reg) >= 0
			&& !(reg & ICL_OVERRIDE_LATCH_BIT)
			&& smblib_masked_write(chg, CMD_APSD_REG, ICL_OVERRIDE_BIT, ICL_OVERRIDE_BIT) >= 0)
			pr_info("[W/A] CWR) start charging with rd only\n");
	}
}

void workaround_charging_with_rd_clear(struct smb_charger *chg) {
	if (workaround_charging_with_rd_running) {
		workaround_charging_with_rd_running = false;
		*chg->try_sink_enabled = workaround_charging_with_rd_backup;
	}
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Support for weak battery pack
////////////////////////////////////////////////////////////////////////////

#define WEAK_SUPPLY_VOTER "WEAK_SUPPLY_VOTER"

static int  workaround_support_weak_supply_count = 0;
static bool workaround_support_weak_supply_running = false;

static void workaround_support_weak_supply_func(struct work_struct *unused) {
	struct smb_charger* chg = workaround_helper_chg();
	u8 stat;

	if (!workaround_support_weak_supply_running)
		return;

	if (chg && !smblib_read(chg, POWER_PATH_STATUS_REG, &stat)) {
		#define POWER_PATH_USB		BIT(2)
		#define WEAK_DETECTION_COUNT	3
		#define DEFAULT_WEAK_ICL_MA	1000

		if ((stat & POWER_PATH_MASK) == POWER_PATH_USB) {
			workaround_support_weak_supply_count++;
			pr_info("[W/A] SWS) workaround_support_weak_supply_count = %d\n",
				workaround_support_weak_supply_count);
			if (workaround_support_weak_supply_count >= WEAK_DETECTION_COUNT) {
				pr_info("[W/A] SWS) Weak battery is detected, set ICL to 1A\n");
				vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER,
					true, DEFAULT_WEAK_ICL_MA*1000);
			}
		}
	}
	workaround_support_weak_supply_running = false;
}

static DECLARE_DELAYED_WORK(workaround_support_weak_supply_dwork, workaround_support_weak_supply_func);

void workaround_support_weak_supply_trigger(struct smb_charger* chg, bool trigger) {
	#define WEAK_DELAY_MS		500

	if (trigger) {
		if (!delayed_work_pending(&workaround_support_weak_supply_dwork))
			schedule_delayed_work(&workaround_support_weak_supply_dwork,
				round_jiffies_relative(msecs_to_jiffies(WEAK_DELAY_MS)));
	}
	else if (!!workaround_support_weak_supply_count) {
		pr_info("[W/A] SWS) Clear workaround_support_weak_supply_count\n");
		workaround_support_weak_supply_count = 0;
		vote(chg->usb_icl_votable, WEAK_SUPPLY_VOTER, false, 0);
	}
	else
		; /* Do nothing */
}

void workaround_support_weak_supply_check(void) {
	if (delayed_work_pending(&workaround_support_weak_supply_dwork))
		workaround_support_weak_supply_running = true;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Supporting USB Compliance test
////////////////////////////////////////////////////////////////////////////

static bool* workaround_usb_compliance_mode_flag;

bool workaround_usb_compliance_mode_enabled(void) {
	bool ucm = workaround_usb_compliance_mode_flag
		? *workaround_usb_compliance_mode_flag
		: false;
	if (ucm)
		pr_info("[W/A] UCM) Enabled\n");

	return ucm;
}

void workaround_usb_compliance_mode_register(bool* reference) {
	if (workaround_usb_compliance_mode_flag)
		pr_info("[W/A] UCM) Reference is replaced\n");
	workaround_usb_compliance_mode_flag = reference;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Force incompatible HVDCP charger
////////////////////////////////////////////////////////////////////////////

#define FORCE9V_HVDCP_VOTER	"FORCE9V_HVDCP_VOTER"
#define DISABLE_HVDCP_VOTER	"DISABLE_HVDCP_VOTER"

static bool workaround_force_incompatible_hvdcp_running = false;
static bool workaround_force_incompatible_hvdcp_required = false;

void workaround_force_incompatible_hvdcp_clear(struct smb_charger* chg) {
	workaround_force_incompatible_hvdcp_running = false;
	workaround_force_incompatible_hvdcp_required = false;

	vote(chg->hvdcp_hw_inov_dis_votable, FORCE9V_HVDCP_VOTER, false, 0);
	vote(chg->hvdcp_hw_inov_dis_votable, DISABLE_HVDCP_VOTER, false, 0);
}

static void workaround_force_incompatible_hvdcp_func(struct work_struct *unused) {
	struct smb_charger* chg = workaround_helper_chg();
	int rc = 0;
	u8 stat;

	if (!chg) {
		pr_info("[W/A] FIH) 'chg' is not ready\n");
		return;
	}

	pr_info("[W/A] FIH) Start force 9V HVDCP.\n");
	workaround_force_incompatible_hvdcp_required = false;

// 1. Disable HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT
	vote(chg->hvdcp_hw_inov_dis_votable, FORCE9V_HVDCP_VOTER, true, 0);

// 2. Force to 9V
	rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, FORCE_9V_BIT, FORCE_9V_BIT);
	if (rc < 0)
		pr_info("[W/A] FIH) Couldn't force 9V HVDCP rc=%d\n", rc);

// 3. delay 2s
	msleep(2000);

// 4. Read USB present
	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		pr_info("[W/A] FIH) Couldn't read USBIN_RT_STS rc=%d\n", rc);
		return;
	}

// 4.5 Disable HVDCP if aicl-fail is occurred.
	if (workaround_force_incompatible_hvdcp_required && (stat & USBIN_PLUGIN_RT_STS_BIT)) {
		pr_info("[W/A] FIH) Disable HVDCP.\n");
		vote(chg->hvdcp_hw_inov_dis_votable, DISABLE_HVDCP_VOTER, true, 0);
		workaround_command_chgtype(POWER_SUPPLY_TYPE_USB_DCP);
	}

// 5. Force to 5V
	rc = smblib_masked_write(chg, CMD_HVDCP_2_REG, FORCE_5V_BIT, FORCE_5V_BIT);
	if (rc < 0)
		pr_info("[W/A] FIH) Couldn't force 5V HVDCP rc=%d\n", rc);

// 6. Enable HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT
	vote(chg->hvdcp_hw_inov_dis_votable, FORCE9V_HVDCP_VOTER, false, 0);
}

static DECLARE_WORK(workaround_force_incompatible_hvdcp_dwork, workaround_force_incompatible_hvdcp_func);

void workaround_force_incompatible_hvdcp_trigger(struct smb_charger* chg) {
	int rc = 0;
	u8 stat;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_info("[W/A] FIH) Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	if (!workaround_force_incompatible_hvdcp_running
		&& (stat & QC_AUTH_DONE_STATUS_BIT)
		&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_HVDCP) {
		workaround_force_incompatible_hvdcp_running = true;
		schedule_work(&workaround_force_incompatible_hvdcp_dwork);
	}
}

void workaround_force_incompatible_hvdcp_require(void) {
       /* The condition for disabling HVDCP is the aicl-fail during QC detection.
	* Place this function in the ISR of 'aicl-fail'
	*/
	workaround_force_incompatible_hvdcp_required = true;
}

bool workaround_force_incompatible_hvdcp_is_running(void) {
	return workaround_force_incompatible_hvdcp_running;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Skip uevent for supplementary battery
////////////////////////////////////////////////////////////////////////////

static bool workaround_skipping_vusb_delay_running = false;

static void workaround_skipping_vusb_delay_func(struct work_struct *unused) {
	workaround_skipping_vusb_delay_running = false;
	pr_info("[W/A] SVD) Stop skip uevnt for supplementary battery\n");
}
static DECLARE_DELAYED_WORK(workaround_skipping_vusb_delay_dwork, workaround_skipping_vusb_delay_func);

void workaround_skipping_vusb_delay_trigger(struct smb_charger* chg) {
	int rc = 0;
	u8 stat;

	rc = smblib_read(chg, APSD_STATUS_REG, &stat);
	if (rc < 0) {
		pr_info("[W/A] SVD) Couldn't read APSD_STATUS_REG rc=%d\n", rc);
		return;
	}

	if (!(stat & QC_AUTH_DONE_STATUS_BIT) && (stat & QC_CHARGER_BIT)) {
		#define SKIP_UEVENT_DELAY_MS	100
		workaround_skipping_vusb_delay_running = true;
		pr_info("[W/A] SVD) Skip uevnt for supplementary battery\n");
		schedule_delayed_work(&workaround_skipping_vusb_delay_dwork,
			msecs_to_jiffies(SKIP_UEVENT_DELAY_MS));
	}
}

bool workaround_skipping_vusb_delay_enabled(void) {
	return workaround_skipping_vusb_delay_running;
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Avoiding MBG fault on SBU pin
////////////////////////////////////////////////////////////////////////////

#ifdef CONFIG_LGE_USB_SWITCH_FUSB252
#include <linux/usb/fusb252.h>
// Rather than accessing pointer directly, Referring it as a singleton instance
static struct fusb252_instance* workaround_avoiding_mbg_fault_singleton(void) {

	static struct fusb252_desc 	workaround_amf_description = {
		.flags  = FUSB252_FLAG_SBU_AUX
			| FUSB252_FLAG_SBU_USBID
			| FUSB252_FLAG_SBU_FACTORY_ID,
	};
	static struct fusb252_instance*	workaround_amf_instance;
	static DEFINE_MUTEX(workaround_amf_mutex);
	struct smb_charger* chg;

	if (IS_ERR_OR_NULL(workaround_amf_instance)) {
		mutex_lock(&workaround_amf_mutex);
		chg = workaround_helper_chg();
		if (IS_ERR_OR_NULL(workaround_amf_instance) && chg) {
			workaround_amf_instance
				= devm_fusb252_instance_register(chg->dev, &workaround_amf_description);
			if (IS_ERR_OR_NULL(workaround_amf_instance))
				devm_fusb252_instance_unregister(chg->dev, workaround_amf_instance);
		}
		mutex_unlock(&workaround_amf_mutex);
	}

	return IS_ERR_OR_NULL(workaround_amf_instance) ? NULL : workaround_amf_instance;
}

bool workaround_avoiding_mbg_fault_uart(bool enable) {
	// Preparing instance and checking validation of it.
	struct fusb252_instance* instance
		= workaround_avoiding_mbg_fault_singleton();
	if (!instance)
		return false;

	if (enable) {
		if (fusb252_get_current_flag(instance) != FUSB252_FLAG_SBU_AUX)
			fusb252_get(instance, FUSB252_FLAG_SBU_AUX);
	}
	else
		fusb252_put(instance, FUSB252_FLAG_SBU_AUX);

	return true;
}

bool workaround_avoiding_mbg_fault_usbid(bool enable) {
	// Preparing instance and checking validation of it.
	struct fusb252_instance* instance
		= workaround_avoiding_mbg_fault_singleton();
	if (!instance)
		return false;

	if (enable)
		fusb252_get(instance, FUSB252_FLAG_SBU_FACTORY_ID);
	else
		fusb252_put(instance, FUSB252_FLAG_SBU_FACTORY_ID);

	return true;
}
#else
bool workaround_avoiding_mbg_fault_uart(bool enable) {
	return true;
}
bool workaround_avoiding_mbg_fault_usbid(bool enable) {
	return true;
}
#endif


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Floating type detected over rerunning APSD
////////////////////////////////////////////////////////////////////////////

static atomic_t workaround_fdr_running = ATOMIC_INIT(0);
static atomic_t workaround_fdr_ready = ATOMIC_INIT(0);

static void workaround_floating_during_rerun_work(struct work_struct *unused) {
// 1. Getting smb_charger
	struct smb_charger* chg = workaround_helper_chg();
	if (!chg) {
		pr_info("[W/A] FDR) charger driver is not ready\n");
		return;
	}

// 2. Rerunning APSD
	workaround_command_apsd(chg);

// 3. Resetting this running flag
	atomic_set(&workaround_fdr_running, 0);
}
static DECLARE_WORK(workaround_fdr_dwork, workaround_floating_during_rerun_work);

void workaround_floating_during_rerun_apsd(enum power_supply_type pst) {
	bool charger_unknown = (pst == POWER_SUPPLY_TYPE_UNKNOWN);
	bool charger_float = (pst == POWER_SUPPLY_TYPE_USB_FLOAT);
	bool fdr_running = workaround_floating_during_rerun_working();
	bool fdr_ready = !!atomic_read(&workaround_fdr_ready);
	pr_info("[W/A] FDR) pstype=%d, running=%d, ready=%d\n",
		pst, fdr_running, fdr_ready);

	if (!charger_unknown && !charger_float && !fdr_ready) {
		pr_info("[W/A] FDR) Being ready to check runtime floating\n");
		atomic_set(&workaround_fdr_ready, 1);
		return;
	}

	if (fdr_ready && !fdr_running && charger_float) {
		pr_info("[W/A] FDR) Rerunning APSD\n");
		atomic_set(&workaround_fdr_running, 1);
		schedule_work(&workaround_fdr_dwork);
	}
}

void workaround_floating_during_rerun_reset(void) {
	atomic_set(&workaround_fdr_ready, 0);
}

bool workaround_floating_during_rerun_working(void) {
	return !!atomic_read(&workaround_fdr_running);
}


////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Block Vddmin on chargerlogo
////////////////////////////////////////////////////////////////////////////
#ifdef CONFIG_LGE_PM_VENEER_PSY
void workaround_blocking_vddmin_chargerlogo(struct smb_charger *chg) {
	extern bool unified_bootmode_chargerlogo(void);

	static struct regulator* vddcx_supply = NULL;
	static int		 vddcx_minlv = 0;
	static int		 vddcx_maxlv = 0;

	int rc = 0;
	// Parsing DT nodes
	if (vddcx_supply == NULL) {
		vddcx_supply = devm_regulator_get(chg->dev, "bvc-vddcx");
		if (IS_ERR(vddcx_supply)) {
			pr_info("[W/A] BVC) unable get bvc-vddcx %ld\n", PTR_ERR(vddcx_supply));
			vddcx_supply = NULL;
		}
	}
	if (vddcx_minlv == 0) {
		rc = of_property_read_u32(chg->dev->of_node, "bvc-vddcx-minlv", &vddcx_minlv);
		if (rc < 0) {
			pr_info("[W/A] BVC) Fail to read bvc-vddcx-minlv %d.\n", rc);
			vddcx_minlv = 0;
		}
	}
	if (vddcx_maxlv == 0) {
		rc = of_property_read_u32(chg->dev->of_node, "bvc-vddcx-maxlv", &vddcx_maxlv);
		if (rc < 0) {
			pr_info("[W/A] BVC) Fail to read bvc-vddcx-maxlv %d.\n", rc);
			vddcx_maxlv = 0;
		}
	}

	if (unified_bootmode_chargerlogo() && vddcx_supply && vddcx_minlv && vddcx_maxlv) {
	// Controlling vddcx
		rc = regulator_set_voltage(vddcx_supply, vddcx_minlv, vddcx_maxlv);
		if (rc) {
			pr_info("[W/A] BVC) Fail to set voltage of vddcx by %d.\n", rc);
			return;
		}
		rc = regulator_enable(vddcx_supply);
		if (rc) {
			pr_info("[W/A] BVC) Fail to enable vddcx by %d.\n", rc);
			return;
		}
	// Done.
		pr_info("[W/A] BVC) enable vddcx_supply to MIN_SVS(%d) ~ MAX(%d) on chargerlogo\n",
			vddcx_minlv, vddcx_maxlv);
	}
}
#else
void workaround_blocking_vddmin_chargerlogo(struct smb_charger *chg) { return; }
#endif

////////////////////////////////////////////////////////////////////////////
// LGE Workaround : Check if cable type is unknown or not
////////////////////////////////////////////////////////////////////////////

#define WA_CUC_VOTER	"WA_CUC_VOTER"
static void workaround_check_unknown_cable_main(struct work_struct *unused) {
	struct smb_charger*  chg = workaround_helper_chg();
	int rc = 0;
	bool	vbus_valid = false;
	bool	vbus_present = false;
	union power_supply_propval val = { 0, };

	if (!chg) {
		pr_info("[W/A] CUC) 'chg' is not ready\n");
		return;
	}
	rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, USBIN_ADAPTER_ALLOW_5V);
	if (rc < 0) {
		pr_info("[W/A] CUC) Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
			USBIN_ADAPTER_ALLOW_5V, rc);
		return;
	}
	vbus_valid = !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_PRESENT, &val) ? !!val.intval : false;
	vbus_present = !smblib_get_prop_usb_present(chg, &val) ? !!val.intval : false;

	pr_info("[W/A] CUC) workaround_check_unknown_cable is called vbus_valid(%d), vbus_present(%d)\n", vbus_valid, vbus_present);

	if (vbus_valid) {
		struct power_supply* veneer = power_supply_get_by_name("veneer");
		union power_supply_propval floated
			= { .intval = POWER_SUPPLY_TYPE_USB_FLOAT, };

		if (veneer) {
			power_supply_set_property(veneer,
					POWER_SUPPLY_PROP_REAL_TYPE, &floated);
			power_supply_changed(veneer);
			power_supply_put(veneer);
		}
	} else if (vbus_present) {
		vote(chg->apsd_disable_votable, WA_CUC_VOTER, true, 0);
	}
}

static DECLARE_DELAYED_WORK (workaround_check_unknown_cable_dwork, workaround_check_unknown_cable_main);

void workaround_check_unknown_cable(struct smb_charger *chg) {
	union power_supply_propval val	= { 0, };
	u8 stat;

	if (!chg->pd_hard_reset
		&& !smblib_get_prop_usb_present(chg, &val)
		&& (val.intval != false)
		&& !smblib_read(chg, APSD_STATUS_REG, &stat)
		&& (stat & APSD_DTC_STATUS_DONE_BIT)
		&& (chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
		&& !(chg->typec_status[3] & UFP_DFP_MODE_STATUS_BIT)
		&& !power_supply_get_property(chg->usb_psy, POWER_SUPPLY_PROP_MOISTURE_DETECTION, &val)
		&& (val.intval != true)) {
		pr_debug("[W/A] CUC) APSD: 0x%02x, real_charger_type: %d\n",
				stat, chg->real_charger_type);
		schedule_delayed_work(&workaround_check_unknown_cable_dwork,
				round_jiffies_relative(0));
	}
	else {
		pr_debug("[W/A] CUC) workaround_check_unknown_cable skip\n");
	}
}

void workaround_check_unknown_cable_clear(struct smb_charger *chg) {
	vote(chg->apsd_disable_votable, WA_CUC_VOTER, false, 0);
}

