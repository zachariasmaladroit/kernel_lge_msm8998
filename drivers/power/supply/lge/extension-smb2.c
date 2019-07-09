/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-smb2.c".
 * 	So "qpnp-smb2.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system may not care the update time of this file.
 */

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/iio/consumer.h>
#include <linux/qpnp/qpnp-adc.h>
#include <soc/qcom/lge/board_lge.h>

#include "veneer-primitives.h"

#define VENEER_VOTER_IUSB 	"VENEER_VOTER_IUSB"
#define VENEER_VOTER_IBAT 	"VENEER_VOTER_IBAT"
#define VENEER_VOTER_IDC 	"VENEER_VOTER_IDC"
#define VENEER_VOTER_VFLOAT 	"VENEER_VOTER_VFLOAT"
#define VENEER_VOTER_HVDCP 	"VENEER_VOTER_HVDCP"

static char* log_raw_status(struct smb_charger* charger) {
	u8 reg;

	smblib_read(charger, BATTERY_CHARGER_STATUS_1_REG, &reg);	// PMI@1006
	reg = reg & BATTERY_CHARGER_STATUS_MASK;			// BIT(2:0)
	switch (reg) {
		case TRICKLE_CHARGE:	return "TRICKLE";
		case PRE_CHARGE:	return "PRE";
		case FAST_CHARGE:	return "FAST";
		case FULLON_CHARGE:	return "FULLON";
		case TAPER_CHARGE:	return "TAPER";
		case TERMINATE_CHARGE:	return "TERMINATE";
		case INHIBIT_CHARGE:	return "INHIBIT";
		case DISABLE_CHARGE:	return "DISABLE";
		default:		break;
	}
	return "UNKNOWN (UNDEFINED CHARGING)";
}

static char* log_psy_status(int status) {
	switch (status) {
		case POWER_SUPPLY_STATUS_UNKNOWN :	return "UNKNOWN";
		case POWER_SUPPLY_STATUS_CHARGING:	return "CHARGING";
		case POWER_SUPPLY_STATUS_DISCHARGING:	return "DISCHARGING";
		case POWER_SUPPLY_STATUS_NOT_CHARGING:	return "NOTCHARGING";
		case POWER_SUPPLY_STATUS_FULL:		return "FULL";
		default :				break;
	}

	return "UNKNOWN (UNDEFINED STATUS)";
}

static char* log_psy_type(int type) {
       /* Refer to 'enum power_supply_type' in power_supply.h
	* and 'static char *type_text[]' in power_supply_sysfs.c
	*/
	switch (type) {
		case POWER_SUPPLY_TYPE_UNKNOWN :	return "UNKNOWN";
		case POWER_SUPPLY_TYPE_BATTERY :	return "BATTERY";
		case POWER_SUPPLY_TYPE_UPS :		return "UPS";
		case POWER_SUPPLY_TYPE_MAINS :		return "MAINS";
		case POWER_SUPPLY_TYPE_USB :		return "USB";
		case POWER_SUPPLY_TYPE_USB_DCP :	return "DCP";
		case POWER_SUPPLY_TYPE_USB_CDP :	return "CDP";
		case POWER_SUPPLY_TYPE_USB_ACA :	return "ACA";
		case POWER_SUPPLY_TYPE_USB_HVDCP :	return "HVDCP";
		case POWER_SUPPLY_TYPE_USB_HVDCP_3:	return "HVDCP3";
		case POWER_SUPPLY_TYPE_USB_PD :		return "PD";
		case POWER_SUPPLY_TYPE_WIRELESS :	return "WIRELESS";
		case POWER_SUPPLY_TYPE_USB_FLOAT :	return "FLOAT";
		case POWER_SUPPLY_TYPE_BMS :		return "BMS";
		case POWER_SUPPLY_TYPE_PARALLEL :	return "PARALLEL";
		case POWER_SUPPLY_TYPE_MAIN :		return "MAIN";
		case POWER_SUPPLY_TYPE_WIPOWER :	return "WIPOWER";
		case POWER_SUPPLY_TYPE_TYPEC :		return "TYPEC";
		case POWER_SUPPLY_TYPE_UFP :		return "UFP";
		case POWER_SUPPLY_TYPE_DFP :		return "DFP";
		default :				break;
	}

	return "UNKNOWN (UNDEFINED TYPE)";
}

static void debug_dump(struct smb_charger* charger, const char* title, u16 start) {
	u16 reg, i;
	u8 val[16];
	for (reg = start; reg < start+0x100; reg += 0x10) {
		for (i=0; i<0x10; i++) {
			val[i] = 0x99;
			smblib_read(charger, reg+i, &val[i]);
		}
		pr_err("REGDUMP: [%s] 0x%X - %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			title, reg, val[0], val[1], val[2], val[3], val[4], val[5], val[6], val[7], val[8],
			val[9], val[10], val[11], val[12], val[13], val[14], val[15]);
	}
}

static void debug_polling(struct smb_charger* charger) {
	union power_supply_propval	val = {0, };
	u8				reg = 0;

	bool disabled_ibat  = !!get_effective_result(charger->chg_disable_votable);
	bool disabled_prll  = !!get_effective_result(charger->pl_disable_votable);

	int  capping_ibat   = disabled_ibat ? 0 : get_effective_result(charger->fcc_votable)/1000;
	int  capping_iusb   = get_effective_result(charger->usb_icl_votable)/1000;
	int  capping_idc    = get_effective_result(charger->dc_icl_votable)/1000;
	int  capping_vfloat = get_effective_result(charger->fv_votable)/1000;

	bool presence_usb = !smblib_get_prop_usb_present(charger, &val) ? !!val.intval : false;
	bool presence_dc  = !smblib_get_prop_dc_present(charger, &val) ? !!val.intval : false;

	#define POLLING_LOGGER_VOTER "POLLING_LOGGER_VOTER"
	vote(charger->awake_votable, POLLING_LOGGER_VOTER, true, 0);
	if (false /* for debug purpose */) {
		static struct power_supply* psy_battery;
		static struct power_supply* psy_bms;
		static struct power_supply* psy_dc;
		static struct power_supply* psy_main;
		static struct power_supply* psy_parallel;
		static struct power_supply* psy_pc_port;
		static struct power_supply* psy_usb;
		static struct power_supply* psy_veneer;
		static struct power_supply* psy_wireless;

		if (!psy_battery)	psy_battery = power_supply_get_by_name("battery");
		if (!psy_bms)		psy_bms = power_supply_get_by_name("bms");
		if (!psy_dc)		psy_dc = power_supply_get_by_name("dc");
		if (!psy_main)		psy_main = power_supply_get_by_name("main");
		if (!psy_parallel)	psy_parallel = power_supply_get_by_name("parallel");
		if (!psy_pc_port)	psy_pc_port = power_supply_get_by_name("pc_port");
		if (!psy_usb)		psy_usb = power_supply_get_by_name("usb");
		if (!psy_veneer)	psy_veneer = power_supply_get_by_name("veneer");
		if (!psy_wireless)	psy_wireless = power_supply_get_by_name("wireless");

		pr_info("PMINFO: [REF] battery:%d, bms:%d, dc:%d, main:%d, "
			"parallel:%d, pc_port:%d, usb:%d, veneer:%d, wireless:%d\n",
			psy_battery ? atomic_read(&psy_battery->use_cnt) : 0,
			psy_bms ? atomic_read(&psy_bms->use_cnt) : 0,
			psy_dc ? atomic_read(&psy_dc->use_cnt) : 0,
			psy_main ? atomic_read(&psy_main->use_cnt) : 0,
			psy_parallel ? atomic_read(&psy_parallel->use_cnt) : 0,
			psy_pc_port ? atomic_read(&psy_pc_port->use_cnt) : 0,
			psy_usb ? atomic_read(&psy_usb->use_cnt) : 0,
			psy_veneer ? atomic_read(&psy_veneer->use_cnt) : 0,
			psy_wireless ? atomic_read(&psy_wireless->use_cnt) : 0);
	}

	#define LOGGING_ON_BMS 1
	val.intval = LOGGING_ON_BMS;
	if (charger->bms_psy)
		power_supply_set_property(charger->bms_psy, POWER_SUPPLY_PROP_UPDATE_NOW, &val);

	pr_info("PMINFO: [VOT] IUSB:%d(%s), IBAT:%d(%s), IDC:%d(%s), FLOAT:%d(%s), CHDIS:%d(%s), PLDIS:%d(%s)\n",
		capping_iusb,	get_effective_client(charger->usb_icl_votable),
		capping_ibat,	get_effective_client(disabled_ibat ? charger->chg_disable_votable : charger->fcc_votable),
		capping_idc,	get_effective_client(charger->dc_icl_votable),
		capping_vfloat,	get_effective_client(charger->fv_votable),

		disabled_ibat,  get_effective_client(charger->chg_disable_votable),
		disabled_prll,  get_effective_client(charger->pl_disable_votable));

	// If not charging, skip the remained logging
	if (!presence_usb && !presence_dc)
		goto out;

	// Basic charging information
	{	int   stat_pwr = smblib_read(charger, POWER_PATH_STATUS_REG, &reg) >= 0
			? reg : -1;
		char* stat_ret = !power_supply_get_property(charger->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val) ? log_psy_status(val.intval) : NULL;
		char* stat_ori = !smblib_get_prop_batt_status(charger, &val)
			? log_psy_status(val.intval) : NULL;
		char* chg_stat
			= log_raw_status(charger);
		char  chg_name [16] = { 0, };

		#define QNOVO_PTTIME_STS		0x1507
		#define QNOVO_PTRAIN_STS		0x1508
		#define QNOVO_ERROR_STS2		0x150A
		#define QNOVO_PE_CTRL			0x1540
		#define QNOVO_PTRAIN_EN			0x1549

		int qnovo_en = smblib_read(charger, QNOVO_PT_ENABLE_CMD_REG, &reg) >= 0
			? reg : -1;
		int qnovo_pt_sts = smblib_read(charger, QNOVO_PTRAIN_STS, &reg) >= 0
			? reg : -1;
		int qnovo_pt_time = smblib_read(charger, QNOVO_PTTIME_STS, &reg) >= 0
			? reg*2 : -1;
		int qnovo_sts = smblib_read(charger, QNOVO_ERROR_STS2, &reg) >= 0
			? reg : -1;
		int qnovo_pe_ctrl = smblib_read(charger, QNOVO_PE_CTRL, &reg) >= 0
			? reg : -1;
		int qnovo_pt_en = smblib_read(charger, QNOVO_PTRAIN_EN, &reg) >= 0
			? reg : -1;

		unified_nodes_show("charger_name", chg_name);

		pr_info("PMINFO: [CHG] NAME:%s, STAT:%s(ret)/%s(ori)/%s(reg), PATH:0x%02x "
				"[QNI] en=%d(sts=0x%X, ctrl=0x%X), pt_en=%d(sts=0x%X), pt_t=%d\n",
			chg_name, stat_ret, stat_ori, chg_stat, stat_pwr,
			qnovo_en, qnovo_sts, qnovo_pe_ctrl, qnovo_pt_en, qnovo_pt_sts, qnovo_pt_time);
	}

	if (presence_usb) { // On Wired charging
		char* usb_real
			= log_psy_type(charger->real_charger_type);
		int   usb_vnow = smblib_get_prop_usb_voltage_now(charger, &val)
			? val.intval/1000 : -1;

		int prll_chgen = !charger->pl.psy ? -2 : (!power_supply_get_property(charger->pl.psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val) ? !!val.intval : -1);
		int prll_pinen = !charger->pl.psy ? -2 : (!power_supply_get_property(charger->pl.psy,
			POWER_SUPPLY_PROP_PIN_ENABLED, &val) ? !!val.intval : -1);
		int prll_suspd = !charger->pl.psy ? -2 : (!power_supply_get_property(charger->pl.psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &val) ? !!val.intval : -1);

		int temp_pmi = smblib_get_prop_charger_temp(charger, &val)
			? val.intval : -1;
		int temp_smb = !charger->pl.psy ? -2 : (!power_supply_get_property(charger->pl.psy,
			POWER_SUPPLY_PROP_CHARGER_TEMP, &val) ? val.intval : -1);
		int iusb_now = smblib_get_prop_usb_current_now(charger, &val)
			? val.intval/1000 : -1;
		int iusb_set = !smblib_get_prop_input_current_settled(charger, &val)
			? val.intval/1000 : -1;
		int ibat_now = !smblib_get_prop_from_bms(charger, POWER_SUPPLY_PROP_CURRENT_NOW, &val)
			? val.intval/1000 : -1;
		int ibat_pmi = !smblib_get_charge_param(charger, &charger->param.fcc, &val.intval)
			? val.intval/1000 : 0;
		int ibat_smb = (prll_chgen <= 0) ? 0 : (!power_supply_get_property(charger->pl.psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &val) ? val.intval/1000 : -1);

		int icl_override_latched = (smblib_read(charger, APSD_RESULT_STATUS_REG, &reg) >= 0)
			? !!(reg & ICL_OVERRIDE_LATCH_BIT) : -1;
		int icl_override_aftapsd = (smblib_read(charger, USBIN_LOAD_CFG_REG, &reg) >= 0)
			? !!(reg & ICL_OVERRIDE_AFTER_APSD_BIT) : -1;
		int icl_override_usbmode = (smblib_read(charger, USBIN_ICL_OPTIONS_REG, &reg) >= 0)
			? reg : -1;

		pr_info("PMINFO: [USB] REAL:%s, VNOW:%d, TPMI:%d, TSMB:%d,"
			" IUSB:%d(now)<=%d(set)<=%d(cap), IBAT:%d(now):=%d(pmi)+%d(smb)<=%d(cap),"
			" PRLL:CHGEN(%d)/PINEN(%d)/SUSPN(%d),"
			" [OVR] LATCHED:%d, AFTAPSD:%d, USBMODE:0x%02x\n",
			usb_real, usb_vnow, temp_pmi, temp_smb,
			iusb_now, iusb_set, capping_iusb, ibat_now, ibat_pmi, ibat_smb, capping_ibat,
			prll_chgen, prll_pinen, prll_suspd,
			icl_override_latched, icl_override_aftapsd, icl_override_usbmode);
	}
	if (presence_dc) { // On DC(Wireless) charging
		extern bool adc_dcin_vnow(struct smb_charger* chg, int* adc);
		extern bool adc_dcin_inow(struct smb_charger* chg, int* adc);
		int adc;

		struct power_supply* psy
			= power_supply_get_by_name("wireless");

		int wlc_vmax = !psy ? -2 : (!power_supply_get_property(psy,
			POWER_SUPPLY_PROP_VOLTAGE_MAX, &val) ? val.intval/1000 : -1);
		int wlc_vnow = adc_dcin_vnow(charger, &adc)
			? adc/1000 : 0;
		int wlc_imax =
			get_client_vote_locked(charger->dc_icl_votable, USER_VOTER)/1000;
		int wlc_inow = adc_dcin_inow(charger, &adc)
			? adc/1000 : 0;
		int wlc_aicl = !smblib_get_prop_input_current_settled(charger, &val)
			? val.intval/1000 : -1;

		int stat_input = smblib_read(charger, DCIN_INPUT_STATUS_REG, &reg) >= 0
			? reg : -1;
		int stat_aicl = smblib_read(charger, DCIN_AICL_OPTIONS_CFG_REG, &reg) >= 0
			? reg : -1;
		int stat_cmd = smblib_read(charger, DCIN_CMD_IL_REG, &reg) >= 0
			? reg : -1;
		if (psy)
			power_supply_put(psy);

		pr_info("PMINFO: [WLC] VMAX:%d, VNOW:%d, IMAX:%d, IWLC:%d(now)<=%d(set)<=%d(cap)"
			" [REG] INPUT:0x%02x, AICL:0x%02x, CMD:0x%02x\n",
			wlc_vmax, wlc_vnow, wlc_imax, wlc_inow, wlc_aicl, capping_idc,
			stat_input, stat_aicl, stat_cmd);
	}
	if (presence_usb && presence_dc) {
		pr_info("PMINFO: [ERR] usbin + dcin charging is not permitted\n");
	}

out:	pr_info("PMINFO: ---------------------------------------------"
			"-----------------------------------------%s-END.\n",
			unified_bootmode_marker());

	vote(charger->awake_votable, POLLING_LOGGER_VOTER, false, 0);
	return;
}

static void debug_battery(struct smb_charger* charger, int func) {

	static const struct base {
		#define PMI_REG_BASE_CHGR	0x1000
		#define PMI_REG_BASE_OTG	0x1100
		#define PMI_REG_BASE_BATIF	0x1200
		#define PMI_REG_BASE_USB	0x1300
		#define PMI_REG_BASE_DC		0x1400
		#define PMI_REG_BASE_QNOVO	0x1500
		#define PMI_REG_BASE_MISC	0x1600
		#define PMI_REG_BASE_USBPD	0x1700
		#define PMI_REG_BASE_MBG	0x2C00

		const char* name;
		int base;
	} bases [] = {
		/* 0: */ { .name = "POLL", 	.base = -1, },	// Dummy for polling logs
		/* 1: */ { .name = "CHGR", 	.base = PMI_REG_BASE_CHGR, },
		/* 2: */ { .name = "OTG", 	.base = PMI_REG_BASE_OTG, },
		/* 3: */ { .name = "BATIF", 	.base = PMI_REG_BASE_BATIF, },
		/* 4: */ { .name = "USB", 	.base = PMI_REG_BASE_USB, },
		/* 5: */ { .name = "DC", 	.base = PMI_REG_BASE_DC, },
		/* 6: */ { .name = "QNOVO", 	.base = PMI_REG_BASE_QNOVO, },
		/* 7: */ { .name = "MISC", 	.base = PMI_REG_BASE_MISC, },
		/* 8: */ { .name = "USBPD", 	.base = PMI_REG_BASE_USBPD, },
		/* 9: */ { .name = "MBG", 	.base = PMI_REG_BASE_MBG, },
	};

	if (func < 0) {
		int i;
		for (i=1; i<ARRAY_SIZE(bases); ++i)
			debug_dump(charger, bases[i].name, bases[i].base);
	}
	else if (func == 0)
		debug_polling(charger);
	else if (func < ARRAY_SIZE(bases))
		debug_dump(charger, bases[func].name, bases[func].base);
	else
		; /* Do nothing */
}

static int restricted_charging_iusb(struct smb_charger* charger, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		if (mvalue != VOTE_TOTALLY_RELEASED) {
			// Releasing undesirable capping on IUSB :

			// In the case of CWC, LEGACY_UNKNOWN_VOTER limits IUSB
			// which is set in the 'previous' TypeC removal
			if (is_client_vote_enabled(charger->usb_icl_votable, LEGACY_UNKNOWN_VOTER)) {
				pr_info("Releasing LEGACY_UNKNOWN_VOTER\n");
				rc |= vote(charger->usb_icl_votable, LEGACY_UNKNOWN_VOTER,
					true, mvalue*1000);
			}

			// In the case of non SDP enumerating by DWC,
			// DWC will not set any value via USB_PSY_VOTER.
			if (is_client_vote_enabled(charger->usb_icl_votable, USB_PSY_VOTER)) {
				pr_info("Releasing USB_PSY_VOTER\n");
				rc |= vote(charger->usb_icl_votable, USB_PSY_VOTER,
					true, mvalue*1000);
			}
		}
	}
	else
		pr_info("USBIN blocked\n");

	rc |= vote(charger->usb_icl_votable, VENEER_VOTER_IUSB,
		mvalue != VOTE_TOTALLY_RELEASED, mvalue*1000);

	return	rc ? -EINVAL : 0;
}

static int restricted_charging_ibat(struct smb_charger* charger, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		pr_info("Restricted IBAT : %duA\n", mvalue*1000);
		rc |= vote(charger->fcc_votable, VENEER_VOTER_IBAT,
			mvalue != VOTE_TOTALLY_RELEASED, mvalue*1000);

		rc |= vote(charger->chg_disable_votable,
			VENEER_VOTER_IBAT, false, 0);
	}
	else {
		pr_info("Stop charging\n");
		rc = vote(charger->chg_disable_votable,
			VENEER_VOTER_IBAT, true, 0);
	}

	return rc ? -EINVAL : 0;
}

static int restricted_charging_idc(struct smb_charger* charger, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		rc |= vote(charger->dc_icl_votable, VENEER_VOTER_IDC,
			mvalue != VOTE_TOTALLY_RELEASED, mvalue*1000);

		rc |= vote(charger->dc_suspend_votable,
			VENEER_VOTER_IDC, false, 0);
	}
	else {
		pr_info("DCIN blocked\n");
		rc = vote(charger->dc_suspend_votable,
			VENEER_VOTER_IDC, true, 0);
	}

	return rc ? -EINVAL : 0;
}

static int restricted_charging_vfloat(struct smb_charger* charger, int mvalue) {
	int rc = 0;

	if (mvalue != VOTE_TOTALLY_BLOCKED) {
		union power_supply_propval val;

		if (mvalue == VOTE_TOTALLY_RELEASED) {
		       /* Clearing related voters :
			* 1. VENEER_VOTER_VFLOAT for BTP and
			* 2. TAPER_STEPPER_VOTER for pl step charging
			*/
			vote(charger->fv_votable, VENEER_VOTER_VFLOAT,
				false, 0);
			vote(charger->fcc_votable, "TAPER_STEPPER_VOTER",
				false, 0);

		       /* If EoC is met with the restricted vfloat,
			* charging is not resumed automatically with restoring vfloat only.
			* Because SoC is not be lowered, so FG(BMS) does not trigger "Recharging".
			* For work-around, do recharging manually here.
			*/
			rc |= vote(charger->chg_disable_votable, VENEER_VOTER_VFLOAT,
				true, 0);
			rc |= vote(charger->chg_disable_votable, VENEER_VOTER_VFLOAT,
				false, 0);
		}
		else {
		       /* At the normal restriction, vfloat is adjusted to "max(vfloat, vnow)",
			* to avoid bat-ov.
			*/
			int uv_float = mvalue*1000, uv_now;
			rc |= power_supply_get_property(charger->bms_psy,
				POWER_SUPPLY_PROP_VOLTAGE_OCV, &val);
			uv_now = val.intval; pr_debug("uv_now : %d\n", uv_now);
			if (uv_now > uv_float
				&& !is_client_vote_enabled(charger->fv_votable, VENEER_VOTER_VFLOAT)) {
				rc |= vote(charger->chg_disable_votable, VENEER_VOTER_VFLOAT,
					true, 0);
			} else {
				rc |= vote(charger->chg_disable_votable, VENEER_VOTER_VFLOAT,
					false, 0);
				rc |= vote(charger->fv_votable, VENEER_VOTER_VFLOAT,
					true, uv_float);
			}
		}
	}
	else {
		pr_info("Non permitted mvalue\n");
		rc = -EINVAL;
	}

	if (rc) {
		pr_err("Failed to restrict vfloat\n");
		vote(charger->fv_votable, VENEER_VOTER_VFLOAT,
			false, 0);
		vote(charger->chg_disable_votable, VENEER_VOTER_VFLOAT,
			false, 0);
		rc = -EINVAL;
	}

	return rc;
}

static int restricted_charging_hvdcp(struct smb_charger* charger, int mvalue) {

	if (VOTE_TOTALLY_BLOCKED < mvalue && mvalue < VOTE_TOTALLY_RELEASED) {
		pr_info("Non permitted mvalue for HVDCP voting %d\n", mvalue);
		return -EINVAL;
	}

	vote(charger->hvdcp_disable_votable_indirect, VENEER_VOTER_HVDCP,
		mvalue == VOTE_TOTALLY_BLOCKED, 0);

	return 0;
}

#define SAFETY_TIMER_ENABLE_CFG_REG	(CHGR_BASE + 0xA0)
#define PRE_CHARGE_SAFETY_TIMER_EN	BIT(1)
#define FAST_CHARGE_SAFETY_TIMER_EN	BIT(0)
static int safety_timer_enabled(struct smb_charger *chg, int* val) {
	u8	reg;
	int	rc = smblib_read(chg, SAFETY_TIMER_ENABLE_CFG_REG, &reg);

	if (rc >= 0)
		*val = !!((reg & PRE_CHARGE_SAFETY_TIMER_EN)
			&& (reg & FAST_CHARGE_SAFETY_TIMER_EN));
	else
		pr_err("Failed to get SAFETY_TIMER_ENABLE_CFG\n");

	return rc;
}

static int safety_timer_enable(struct smb_charger *chg, bool enable) {

	int	val, rc = safety_timer_enabled(chg, &val);
	u8	reg = enable ?
			(PRE_CHARGE_SAFETY_TIMER_EN & FAST_CHARGE_SAFETY_TIMER_EN) : 0;

	if (rc >= 0 && val == !enable)
		return smblib_masked_write(chg, SAFETY_TIMER_ENABLE_CFG_REG,
			PRE_CHARGE_SAFETY_TIMER_EN | FAST_CHARGE_SAFETY_TIMER_EN, reg);

	return rc;
}

static int smblib_set_prop_parallel_batfet_en(struct smb_charger *chg,
				const union power_supply_propval *val) {
	int rc;

	if (val->intval == 0)
		rc = gpiod_direction_output(gpio_to_desc(chg->smb_bat_en), true);
	else
		rc = gpiod_direction_output(gpio_to_desc(chg->smb_bat_en), false);

	if (rc < 0) {
		pr_err("Couldn't set parallel batfet_en rc=%d\n", rc);
		return rc;
	}

	pr_info("PMI: Parallel batfet %s\n", val->intval ? "enabled" : "disabled");
	return rc;
}

static int smblib_get_prop_batt_voltage_max(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	val->intval = get_client_vote(chg->fv_votable, BATT_PROFILE_VOTER);
	if (val->intval == -EINVAL) {
		val->intval = get_client_vote(chg->fv_votable, QNOVO_VOTER);
	}

	return 0;
}

static int smblib_get_prop_fcc_max(struct smb_charger *chg,
				     union power_supply_propval *val)
{
	val->intval = get_client_vote(chg->fcc_votable, BATT_PROFILE_VOTER);
	if (val->intval == -EINVAL) {
		val->intval = get_client_vote(chg->fcc_votable, QNOVO_VOTER);
	}

	return 0;
}


///////////////////////////////////////////////////////////////////////////////

#define PROPERTY_CONSUMED_WITH_SUCCESS	0
#define PROPERTY_CONSUMED_WITH_FAIL	EINVAL
#define PROPERTY_BYPASS_REASON_NOENTRY	ENOENT
#define PROPERTY_BYPASS_REASON_ONEMORE	EAGAIN

static enum power_supply_property extension_battery_appended [] = {
	POWER_SUPPLY_PROP_STATUS_RAW,
	POWER_SUPPLY_PROP_CAPACITY_RAW,
	POWER_SUPPLY_PROP_BATTERY_TYPE,
	POWER_SUPPLY_PROP_DEBUG_BATTERY,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE,
	POWER_SUPPLY_PROP_RESTRICTED_CHARGING,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
};

static int extension_battery_get_property_pre(struct power_supply *psy,
		enum power_supply_property prp, union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smb_charger* charger = power_supply_get_drvdata(psy);
	static struct power_supply* veneer = NULL;

	if (!veneer)
		veneer = power_supply_get_by_name("veneer");

	switch (prp) {
	case POWER_SUPPLY_PROP_STATUS :
		if (!veneer || power_supply_get_property(veneer, POWER_SUPPLY_PROP_STATUS, val))
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
	case POWER_SUPPLY_PROP_HEALTH :
		if (!veneer || power_supply_get_property(veneer, POWER_SUPPLY_PROP_HEALTH, val))
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW :
		if (!veneer || power_supply_get_property(veneer, POWER_SUPPLY_PROP_TIME_TO_FULL_NOW, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW :
		if (!charger->bms_psy || power_supply_get_property(charger->bms_psy, POWER_SUPPLY_PROP_CAPACITY_RAW, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_BATTERY_TYPE :
		if (!charger->bms_psy || power_supply_get_property(charger->bms_psy, POWER_SUPPLY_PROP_BATTERY_TYPE, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN :
		if (!charger->bms_psy || power_supply_get_property(charger->bms_psy, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, val))
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
		break;

	case POWER_SUPPLY_PROP_STATUS_RAW :
		rc = smblib_get_prop_batt_status(charger, val);
		break;
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE :
		rc = safety_timer_enabled(charger, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT :
		val->intval = get_effective_result(charger->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		val->intval = !get_effective_result(charger->chg_disable_votable);
		break;

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING:
		/* Do nothing and just consume getting */
		val->intval = -1;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_MAX :
		rc = smblib_get_prop_batt_voltage_max(charger, val);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX :
		rc = smblib_get_prop_fcc_max(charger, val);
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
		break;
	}

	return rc;
}

static int extension_battery_get_property_post(struct power_supply *psy,
	enum power_supply_property prp, union power_supply_propval *val, int rc) {

	switch (prp) {
	default:
		break;
	}

	return rc;
}

static int extension_battery_set_property_pre(struct power_supply *psy,
	enum power_supply_property prp, const union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct smb_charger* charger = power_supply_get_drvdata(psy);

	switch (prp) {
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING : {
		enum voter_type type = vote_type(val);
		int limit = vote_limit(val); // in mA

		switch (type) {
		case VOTER_TYPE_IUSB:
			if (!workaround_usb_compliance_mode_enabled())
				rc = restricted_charging_iusb(charger, limit);
			break;
		case VOTER_TYPE_IBAT:
			rc = restricted_charging_ibat(charger, limit);
			break;
		case VOTER_TYPE_IDC:
			rc = restricted_charging_idc(charger, limit);
			break;
		case VOTER_TYPE_VFLOAT:
			rc = restricted_charging_vfloat(charger, limit);
			break;
		case VOTER_TYPE_HVDCP:
			rc = restricted_charging_hvdcp(charger, limit);
			break;
		default:
			rc = -PROPERTY_CONSUMED_WITH_FAIL;
			break;
		}
	}	break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE : {
		char buf [2] = { 0, };
		enum charging_step chgstep;

		switch (val->intval) {
		case TRICKLE_CHARGE:
		case PRE_CHARGE:	chgstep = CHARGING_STEP_TRICKLE;
			break;
		case FAST_CHARGE:
		case FULLON_CHARGE:	chgstep = CHARGING_STEP_CC;
			break;
		case TAPER_CHARGE:	chgstep = CHARGING_STEP_CV;
			break;
		case TERMINATE_CHARGE:	chgstep = CHARGING_STEP_TERMINATED;
			break;
		case DISABLE_CHARGE:	chgstep = CHARGING_STEP_NOTCHARGING;
			break;
		default:		chgstep = CHARGING_STEP_DISCHARGING;
			break;
		}

		snprintf(buf, sizeof(buf), "%d", chgstep);
		unified_nodes_store("charging_step", buf, strlen(buf));
	}	break;

	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED :
		vote(charger->chg_disable_votable, USER_VOTER, (bool)!val->intval, 0);
		break;

	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE :
		rc = safety_timer_enable(charger, !!val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(charger->fcc_votable, QNOVO_VOTER,
			(val->intval >= 0), val->intval);
		break;

	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
		debug_battery(charger, val->intval);
		break;

	case POWER_SUPPLY_PROP_PARALLEL_BATFET_EN:
		if (gpio_is_valid(charger->smb_bat_en))
			rc = smblib_set_prop_parallel_batfet_en(charger, val);
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	return rc;
}

static int extension_battery_set_property_post(struct power_supply *psy,
	enum power_supply_property prp, const union power_supply_propval *val, int rc) {

	struct smb_charger* charger = power_supply_get_drvdata(psy);

	switch (prp) {
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE: {
		char buff [16] = { 0, };
		int  test;
		if (!val->intval // Enabling parallel charging
			&& unified_nodes_show("support_fastpl", buff)
			&& sscanf(buff, "%d", &test) && test == 1) {

			vote(charger->pl_enable_votable_indirect, USBIN_I_VOTER, true, 0);
			while (get_effective_result(charger->pl_disable_votable)) {
				const char* client = get_effective_client(charger->pl_disable_votable);
				vote(charger->pl_disable_votable, client, false, 0);
				pr_info("FASTPL: Clearing PL_DISABLE voter %s for test purpose\n", client);
			}
			pr_info("FASTPL: After clearing PL_DISABLE, result:%d, client:%s\n",
				get_effective_result(charger->pl_disable_votable),
				get_effective_client(charger->pl_disable_votable));
		}
	}
	break;

	default:
		break;
	}

	return rc;
}

///////////////////////////////////////////////////////////////////////////////
enum power_supply_property* extension_battery_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb2_batt_props) + ARRAY_SIZE(extension_battery_appended)];
	int size_original = ARRAY_SIZE(smb2_batt_props);
	int size_appended = ARRAY_SIZE(extension_battery_appended);

	memcpy(extended_properties, smb2_batt_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_battery_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb2_batt_props, size_original,
		extension_battery_appended, size_appended);

	return extended_properties;
}

size_t extension_battery_num_properties(void) {
	return ARRAY_SIZE(smb2_batt_props) + ARRAY_SIZE(extension_battery_appended);
}

int extension_battery_get_property(struct power_supply *psy,
	enum power_supply_property prp, union power_supply_propval *val) {

	int rc = extension_battery_get_property_pre(psy, prp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smb2_batt_get_prop(psy, prp, val);
	rc = extension_battery_get_property_post(psy, prp, val, rc);

	return rc;
}

int extension_battery_set_property(struct power_supply *psy,
	enum power_supply_property prp, const union power_supply_propval *val) {

	int rc = extension_battery_set_property_pre(psy, prp, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = smb2_batt_set_prop(psy, prp, val);
	rc = extension_battery_set_property_post(psy, prp, val, rc);

	return rc;
}

int extension_battery_property_is_writeable(struct power_supply *psy,
	enum power_supply_property prp) {
	int rc;

	switch (prp) {
	case POWER_SUPPLY_PROP_DEBUG_BATTERY :
	case POWER_SUPPLY_PROP_RESTRICTED_CHARGING :
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		rc = 1;
		break;
	default:
		rc = smb2_batt_prop_is_writeable(psy, prp);
		break;
	}
	return rc;
}


/*************************************************************
 * simple extension for usb psy.
 */

// Cached values
static enum charger_usbid
	   cache_usbid_type = CHARGER_USBID_INVALID;
static int cache_usbid_uvoltage = 0;
static int cache_usbid_ohm = 0;

static const char* adc_usbid_name(enum charger_usbid type) {
	switch (type) {
	case CHARGER_USBID_UNKNOWN:	return "UNKNOWN";
	case CHARGER_USBID_56KOHM:	return "56K";
	case CHARGER_USBID_130KOHM:	return "130K";
	case CHARGER_USBID_910KOHM:	return "910K";
	case CHARGER_USBID_OPEN:	return "OPEN";
	default :
		break;
	}

	return "INVALID";
}

#define MAX_ADC_RETRY		3
static int adc_usbid_uvoltage(struct device* dev, struct qpnp_vadc_chip* vadc, int channel) {
	struct qpnp_vadc_result result = {
		.physical = 0,
		.adc_code = 0,
	};
	bool success = true;
	int i = 0;

	for (i = 0; i < MAX_ADC_RETRY; i++) {
		// Read ADC if possible
		success &= workaround_avoiding_mbg_fault_usbid(true);
		success &= !qpnp_vadc_read(vadc, channel, &result);
		success &= workaround_avoiding_mbg_fault_usbid(false);

		if (!success)
			pr_info("USB-ID: Failed to read ADC\n");

		if (result.physical/1000 != 0)
			break;
	}
// Valid or not
	return success ? result.physical : 0;
}

static int adc_usbid_ohm(struct device* dev, int uvoltage) {
	static int pullup_mvol = 0;
	static int pullup_kohm = 0;

	if (pullup_mvol == 0) {
		if (lge_get_hydra_mode() == LGE_HYDRA_MODE_RENEWAL)
			of_property_read_s32(of_find_node_by_name(dev->of_node,
				"lge-extension-usb"), "lge,renewal-usbid-pullup-mvol", &pullup_mvol);
		else
			of_property_read_s32(of_find_node_by_name(dev->of_node,
				"lge-extension-usb"), "lge,usbid-pullup-mvol", &pullup_mvol);
		pr_info("USB-ID: Success to get pullup-mvol:%d\n", pullup_mvol);
	}
	if (pullup_kohm == 0) {
		of_property_read_s32(of_find_node_by_name(dev->of_node,
			"lge-extension-usb"), "lge,usbid-pullup-kohm", &pullup_kohm);
		pr_info("USB-ID: Success to get pullup-kohm:%d\n", pullup_kohm);
	}

	if (pullup_mvol && pullup_kohm) {
		#define MVOL_FROM_KOHM(x) (((x)*(pullup_mvol))/((x)+(pullup_kohm)))
		int usbid_uvol = min(uvoltage, (pullup_mvol-1)*1000); // Ceiling to reference voltage
		return usbid_uvol > 0
			? (pullup_kohm * usbid_uvol) / (pullup_mvol - usbid_uvol / 1000) : 0;
	}
	else
		pr_err("USB-ID: Error on getting USBID ADC (pull up %dmvol, %dkohm)\n",
			pullup_mvol, pullup_kohm);

	return 0;
}

static enum charger_usbid adc_usbid_type(struct device* dev, int ohm) {
	static struct usbid_entry {
		enum charger_usbid type;
		int		   min;
		int		   max;
	}				usbid_table [] = {
		{ .type = CHARGER_USBID_56KOHM },
		{ .type = CHARGER_USBID_130KOHM },
		{ .type = CHARGER_USBID_910KOHM },
		{ .type = CHARGER_USBID_OPEN },
	};
	static int			usbid_range = 0; // pct unit
	enum charger_usbid 		usbid_ret = CHARGER_USBID_UNKNOWN;

	if (usbid_range == 0) {
		of_property_read_s32(of_find_node_by_name(dev->of_node,
			"lge-extension-usb"), "lge,usbid-adc-range", &usbid_range);

		// Build up USB-ID table
		usbid_table[0].min = CHARGER_USBID_56KOHM  * (100-usbid_range) / 100;
		usbid_table[0].max = CHARGER_USBID_56KOHM  * (100+usbid_range) / 100;
		usbid_table[1].min = CHARGER_USBID_130KOHM * (100-usbid_range) / 100;
		usbid_table[1].max = CHARGER_USBID_130KOHM * (100+usbid_range) / 100;
		usbid_table[2].min = CHARGER_USBID_910KOHM * (100-usbid_range) / 100;
		usbid_table[2].max = CHARGER_USBID_910KOHM * (100+usbid_range) / 100;
		usbid_table[3].min = CHARGER_USBID_910KOHM * 2;
		usbid_table[3].max = INT_MAX;

		pr_info("USB-ID: Success to get adc-range:%d%%\n", usbid_range);
	}

	if (usbid_range) {
		int i;
		for (i=0; i<ARRAY_SIZE(usbid_table); ++i) {
			if (usbid_table[i].min <= ohm && ohm <=usbid_table[i].max) {
				if (usbid_ret == CHARGER_USBID_UNKNOWN)
					usbid_ret = usbid_table[i].type;
				else
					pr_err("USB-ID: Overlap in usbid table!\n");
			}
		}
	}
	else
		pr_err("USB-ID: Error on getting USBID ADC (usbid_range=%d%%)\n",
			usbid_range);

	return usbid_ret;
}

static DEFINE_MUTEX(psy_usbid_mutex);

static bool psy_usbid_update(struct device* dev) {
// Preset data
	static struct qpnp_vadc_chip*	usbid_vadc = NULL;
	static int			usbid_channel = -1;

	mutex_lock(&psy_usbid_mutex);
	if (IS_ERR_OR_NULL(usbid_vadc)) {
		usbid_vadc = qpnp_get_vadc(dev, "usbid");
		pr_info("USB-ID: %s to get qpnp-vadc\n",
			IS_ERR(usbid_vadc)? "Failed" : "Success");
	}
	if (usbid_channel < 0) {
		of_property_read_s32(of_find_node_by_name(dev->of_node,
			"lge-extension-usb"), "lge,usbid-adc-channel", &usbid_channel);
		pr_info("USB-ID: Success to get usbid-adc-channel: %d\n", usbid_channel);
	}

// Update all
	if (!IS_ERR_OR_NULL(usbid_vadc) && usbid_channel >= 0) {
		cache_usbid_uvoltage = adc_usbid_uvoltage(dev, usbid_vadc, usbid_channel);
		cache_usbid_ohm      = adc_usbid_ohm(dev, cache_usbid_uvoltage);
		cache_usbid_type     = adc_usbid_type(dev, cache_usbid_ohm);
		pr_info("USB-ID: Updated to %dmvol => %dkohm => %s\n",
			cache_usbid_uvoltage/1000, cache_usbid_ohm/1000, adc_usbid_name(cache_usbid_type));
	}
	else
		pr_err("USB-ID: Error on getting USBID ADC(mvol) (vadc:%p/chan:%d)\n",
			usbid_vadc, usbid_channel);
	mutex_unlock(&psy_usbid_mutex);

// Check validation of result
	return cache_usbid_uvoltage > 0 && cache_usbid_ohm > 0;
}

static enum charger_usbid psy_usbid_get(struct smb_charger* chg) {
	if (cache_usbid_type == CHARGER_USBID_INVALID) {
		mutex_lock(&psy_usbid_mutex);
		if (cache_usbid_type == CHARGER_USBID_INVALID) {
		       /* If cable detection is not initiated, refer to the cmdline */
			enum charger_usbid bootcable = unified_bootmode_usbid();
			pr_info("USB-ID: Not initiated yet, refer to boot USBID %s\n",
				adc_usbid_name(bootcable));
			cache_usbid_type = bootcable;
		}
		mutex_unlock(&psy_usbid_mutex);
	}

	return cache_usbid_type;
}

static bool fake_hvdcp_property(struct smb_charger *chg) {
	char buffer [16] = { 0, };
	int fakehvdcp;

	return unified_nodes_show("fake_hvdcp", buffer)
		&& sscanf(buffer, "%d", &fakehvdcp)
		&& !!fakehvdcp;
}

static bool fake_hvdcp_effected(struct smb_charger *chg) {
	union power_supply_propval val = {0, };

	if (fake_hvdcp_property(chg)
		&& smblib_get_prop_usb_voltage_now(chg, &val) >= 0
		&& val.intval/1000 >= 7000) {
		return true;
	}
	else
		return false;
}

static bool fake_hvdcp_enable(struct smb_charger *chg, bool enable) {
	if (fake_hvdcp_property(chg)) {
		u8 vallow = enable ? USBIN_ADAPTER_ALLOW_5V_TO_9V
			: USBIN_ADAPTER_ALLOW_5V_OR_9V_TO_12V;
		int rc = smblib_write(chg, USBIN_ADAPTER_ALLOW_CFG_REG, vallow);
		if (rc >= 0) {
			if (enable)
				vote(chg->usb_icl_votable, LEGACY_UNKNOWN_VOTER, true, 3000000);
			return true;
		}
		else
			pr_err("Couldn't write 0x%02x to USBIN_ADAPTER_ALLOW_CFG rc=%d\n",
				vallow, rc);
	}
	else
		pr_debug("fake_hvdcp is not set\n");

	return false;
}

static int charger_power_hvdcp(/*@Nonnull*/ struct power_supply* usb, int type) {
	#define HVDCP_VOLTAGE_MV_MIN	5000
	#define HVDCP_VOLTAGE_MV_MAX	9000
	#define HVDCP_CURRENT_MA_MAX	1800

	int power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_HVDCP || type == POWER_SUPPLY_TYPE_USB_HVDCP_3) {
		struct smb_charger* charger = power_supply_get_drvdata(usb);

		bool legacy_rphigh = is_client_vote_enabled(charger->hvdcp_disable_votable_indirect,
			VBUS_CC_SHORT_VOTER) && charger->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_HIGH;
		bool disabled_hvdcp = is_client_vote_enabled(charger->hvdcp_hw_inov_dis_votable,
			"DISABLE_HVDCP_VOTER");

		int voltage_mv = (legacy_rphigh || disabled_hvdcp)
			? HVDCP_VOLTAGE_MV_MIN : HVDCP_VOLTAGE_MV_MAX;
		int current_ma = /* 1.8A fixed for HVDCP */
			HVDCP_CURRENT_MA_MAX;

		power = voltage_mv * current_ma;
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_adaptive(/*@Nonnull*/ struct power_supply* usb, int type) {
	#define FAKING_QC (HVDCP_VOLTAGE_MV_MAX*HVDCP_CURRENT_MA_MAX)
	int power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_DCP || type == POWER_SUPPLY_TYPE_USB_CDP) {
		struct smb_charger* charger = power_supply_get_drvdata(usb);
		bool qcfaking = (type == POWER_SUPPLY_TYPE_USB_DCP)
			&& get_effective_result(charger->hvdcp_enable_votable);

		if (!qcfaking) {
			union power_supply_propval buf =
				{ .intval = 0, };
			int voltage_mv = /* 5V fixed for DCP and CDP */
				5000;
			int current_ma = !smblib_get_prop_input_current_settled(charger, &buf)
				? buf.intval / 1000 : 0;

			#define SCALE_300MA 300
			current_ma = ((current_ma - 1) / SCALE_300MA + 1) * SCALE_300MA;
			power = voltage_mv * current_ma;
		}
		else {
			power = FAKING_QC;
		}
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_sdp(/*@Nonnull*/ struct power_supply* usb, int type) {
	int power = 0;

	if (type == POWER_SUPPLY_TYPE_USB) {
		union power_supply_propval buf = { .intval = 0, };

		int voltage_mv = /* 5V fixed for SDP */
			5000;
		int current_ma = !smb2_usb_get_prop(usb, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &buf)
			? buf.intval / 1000 : 0;

		power = voltage_mv * current_ma;
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_pd(/*@Nonnull*/ struct power_supply* usb, int type) {
	int power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_PD) {
		union power_supply_propval buf = { .intval = 0, };

		int voltage_mv = !smb2_usb_get_prop(usb, POWER_SUPPLY_PROP_PD_VOLTAGE_MAX, &buf)
			? buf.intval / 1000 : 0;
		int current_ma = !smb2_usb_get_prop(usb, POWER_SUPPLY_PROP_PD_CURRENT_MAX, &buf)
			? buf.intval / 1000 : 0;

		power = voltage_mv * current_ma;
		pr_info("PD power %duW = %dmV * %dmA\n", power, voltage_mv, current_ma);
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static int charger_power_float(/*@Nonnull*/ struct power_supply* usb, int type) {
	int power = 0;

	if (type == POWER_SUPPLY_TYPE_USB_FLOAT) {
		int voltage_mv = 5000;
		int current_ma =  500;

		power = workaround_floating_during_rerun_working()
			? 0 : (voltage_mv * current_ma);
	}
	else {	// Assertion failed
		pr_info("%s: Check the caller\n", __func__);
	}

	return power;
}

static bool usbin_ov_check(/*@Nonnull*/ struct smb_charger *chg) {
	int rc = 0;
	u8 stat;

	rc = smblib_read(chg, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
	if (rc < 0) {
		pr_info("%s: Couldn't read USBIN_RT_STS rc=%d\n", __func__, rc);
		return false;
	}

	return (bool)(stat & USBIN_OV_RT_STS_BIT);
}

static bool usb_pcport_check(/*@Nonnull*/ struct smb_charger *chg) {
	enum power_supply_type* pst = &chg->real_charger_type;
	union power_supply_propval val = { 0, };
	u8 reg = 0;

	bool usb_connected = false;
	bool usb_pdtype = false;
	bool usb_pcport = false;

	if (smblib_get_prop_usb_online(chg, &val) < 0) {
		pr_err("PMI: usb_pcport_check: Couldn't read smblib_get_prop_usb_online\n");
		return false;
	}
	else
		usb_connected = !!val.intval;

	if (smblib_read(chg, APSD_RESULT_STATUS_REG, &reg) < 0) {
		pr_err("PMI: usb_pcport_check: Couldn't read APSD_RESULT_STATUS\n");
		return false;
	}
	else
		reg &= APSD_RESULT_STATUS_MASK;

	usb_pdtype = (*pst == POWER_SUPPLY_TYPE_USB_PD)
		&& (reg == SDP_CHARGER_BIT || reg == CDP_CHARGER_BIT);
	usb_pcport = (*pst == POWER_SUPPLY_TYPE_USB
		|| *pst == POWER_SUPPLY_TYPE_USB_CDP);

	return usb_connected && (usb_pdtype || usb_pcport);
}

static int usb_pcport_current(/*@Nonnull*/ struct smb_charger *chg, int req) {
	struct power_supply* veneer = power_supply_get_by_name("veneer");
	if (veneer) {
		union power_supply_propval val;
		if (req == 900000) {
			// Update veneer's supplier type to USB 3.x
			val.intval = POWER_SUPPLY_TYPE_USB;
			power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &val);
		}
		power_supply_get_property(veneer, POWER_SUPPLY_PROP_SDP_CURRENT_MAX, &val);
		power_supply_put(veneer);

		if (val.intval != VOTE_TOTALLY_RELEASED && !workaround_usb_compliance_mode_enabled())
			return val.intval;
	}

	return req;
}

///////////////////////////////////////////////////////////////////////////////

static enum power_supply_property extension_usb_appended [] = {
// Below 2 USB-ID properties don't need to be exported to user space.
	POWER_SUPPLY_PROP_RESISTANCE,		/* in uvol */
	POWER_SUPPLY_PROP_RESISTANCE_ID,	/* in ohms */
	POWER_SUPPLY_PROP_POWER_NOW,
};

enum power_supply_property* extension_usb_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb2_usb_props) + ARRAY_SIZE(extension_usb_appended)];
	int size_original = ARRAY_SIZE(smb2_usb_props);
	int size_appended = ARRAY_SIZE(extension_usb_appended);

	memcpy(extended_properties, smb2_usb_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_usb_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb2_usb_props, size_original,
		extension_usb_appended, size_appended);

	return extended_properties;
}

size_t extension_usb_num_properties(void) {
	return ARRAY_SIZE(smb2_usb_props) + ARRAY_SIZE(extension_usb_appended);
}

int extension_usb_get_property(struct power_supply *psy,
	enum power_supply_property prp, union power_supply_propval *val) {

	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;

	switch (prp) {
	case POWER_SUPPLY_PROP_POWER_NOW :
		if (!smb2_usb_get_prop(psy, POWER_SUPPLY_PROP_REAL_TYPE, val)) {
			if (fake_hvdcp_enable(chg, true) && fake_hvdcp_effected(chg))
				val->intval = POWER_SUPPLY_TYPE_USB_HVDCP;

			switch(val->intval) {
				case POWER_SUPPLY_TYPE_USB_HVDCP:	/* High Voltage DCP */
				case POWER_SUPPLY_TYPE_USB_HVDCP_3:	/* Efficient High Voltage DCP */
					val->intval = charger_power_hvdcp(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_DCP:		/* Dedicated Charging Port */
				case POWER_SUPPLY_TYPE_USB_CDP:		/* Charging Downstream Port */
					val->intval = charger_power_adaptive(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB:		/* Standard Downstream Port */
					val->intval = charger_power_sdp(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_PD:		/* Power Delivery */
					val->intval = charger_power_pd(psy, val->intval);
					break;
				case POWER_SUPPLY_TYPE_USB_FLOAT:	/* D+/D- are open but are not data lines */
					val->intval = charger_power_float(psy, val->intval);
					break;
				default :
					val->intval = 0;
					break;
			}
		}
		return 0;

	case POWER_SUPPLY_PROP_ONLINE : {
	// Getting charger type from veneer
		struct power_supply* veneer
			= power_supply_get_by_name("veneer");
		int chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
			val)) ? val->intval : POWER_SUPPLY_TYPE_UNKNOWN;
	// Pre-loading conditions
		bool online = !smb2_usb_get_prop(psy, POWER_SUPPLY_PROP_ONLINE, val)
			? !!val->intval : false;
		bool present = !extension_usb_get_property(psy, POWER_SUPPLY_PROP_PRESENT, val)
			? !!val->intval : false;
		bool ac = chgtype != POWER_SUPPLY_TYPE_UNKNOWN && chgtype != POWER_SUPPLY_TYPE_WIRELESS
			&& chgtype != POWER_SUPPLY_TYPE_USB && chgtype != POWER_SUPPLY_TYPE_USB_CDP;
		bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
			== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
		bool pc = usb_pcport_check(chg);
		if (veneer)
			power_supply_put(veneer);
		pr_debug("chgtype=%s, online=%d, present=%d, ac=%d, fo=%d, pc=%d\n",
			log_psy_type(chgtype), online, present, ac, fo, pc);

	// Branched returning
		if (!online && present && ac && !fo) {
			pr_debug("Set ONLINE by force\n");
			val->intval = true;
		}
		else if (pc) {
			pr_debug("Set OFFLINE due to non-AC\n");
			val->intval = false;
		}
		else
			val->intval = online;
	}	return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW : {
		static int preserved = 0;

		if (!workaround_skipping_vusb_delay_enabled()) {
			int rc = smb2_usb_get_prop(psy, prp, val);
			preserved = val->intval;
			return rc;
		}
		else
			val->intval = preserved;
	}	return 0;

	case POWER_SUPPLY_PROP_PRESENT :
		if (workaround_usb_compliance_mode_enabled())
			break;

		if (usbin_ov_check(chg)) {
			pr_debug("Unset PRESENT by force\n");
			val->intval = false;
			return 0;
		}
		if (chg->typec_en_dis_active ||
		    (chg->pd_hard_reset &&
			 chg->typec_mode >= POWER_SUPPLY_TYPEC_SOURCE_DEFAULT)) {
			pr_debug("Set PRESENT by force\n");
			val->intval = true;
			return 0;
		}
		break;

	case POWER_SUPPLY_PROP_RESISTANCE :	/* in uvol */
		val->intval = cache_usbid_uvoltage;
		return 0;

	case POWER_SUPPLY_PROP_RESISTANCE_ID :	/* in ohms */
		val->intval = psy_usbid_get(chg);
		return 0;

	case POWER_SUPPLY_PROP_USB_HC :
		val->intval = fake_hvdcp_effected(chg);
		return 0;

	default:
		break;
	}

	return smb2_usb_get_prop(psy, prp, val);
}

int extension_usb_set_property(struct power_supply* psy,
	enum power_supply_property prp, const union power_supply_propval* val) {

	struct smb2* chip = power_supply_get_drvdata(psy);
	struct smb_charger* chg = &chip->chg;
	int rc = 0;

	switch (prp) {
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		if (val->intval && chg->real_charger_type == POWER_SUPPLY_TYPE_USB_FLOAT
				&& !workaround_floating_during_rerun_working()) {
			struct power_supply* veneer
				= power_supply_get_by_name("veneer");
			union power_supply_propval floated
				= { .intval = POWER_SUPPLY_TYPE_USB_FLOAT, };

			if (veneer) {
				power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &floated);
				power_supply_changed(veneer);
				power_supply_put(veneer);
			}
		}
		rc = smb2_usb_set_prop(psy, prp, val);
		workaround_recovering_abnormal_apsd_pdreset(chg);
		return rc;

	case POWER_SUPPLY_PROP_PD_ACTIVE:
		if (val->intval) {
			struct power_supply* veneer
				= power_supply_get_by_name("veneer");
			union power_supply_propval pd
				= { .intval = POWER_SUPPLY_TYPE_USB_PD, };

			if (veneer) {
				power_supply_set_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE, &pd);
				power_supply_changed(veneer);
				power_supply_put(veneer);
			}
		}
		pr_info("PMI: smblib_set_prop_pd_active: update pd active %d \n", val->intval);
		workaround_recovering_abnormal_apsd_pdactive(chg, val->intval);
		break;

	/* _PD_VOLTAGE_MAX, _PD_VOLTAGE_MIN, _USB_HC are defined for fake_hvdcp */
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX :
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN :
		if (usbin_ov_check(chg)) {
			pr_info("Skip PD %s voltage control(%d mV) by ov\n",
				prp== POWER_SUPPLY_PROP_PD_VOLTAGE_MAX ? "Max":"Min", val->intval/1000);
			return 0;
		}
		if (fake_hvdcp_property(chg)
			&& chg->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
			pr_info("PMI: Skipping PD voltage control\n");
			return 0;
		}
		break;
	case POWER_SUPPLY_PROP_USB_HC :
		fake_hvdcp_enable(chg, !!val->intval);
		return 0;

	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX : {
		static union power_supply_propval isdp;
		isdp.intval = usb_pcport_current(chg, val->intval);
		if (isdp.intval != val->intval)
			pr_info("PMI: SDP_CURRENT_MAX %d is overridden to %d\n", val->intval, isdp.intval);
		val = &isdp;
	}	break;

	case POWER_SUPPLY_PROP_RESISTANCE :
		psy_usbid_update(chg->dev);
		return 0;
 
	default:
		break;
	}

	return smb2_usb_set_prop(psy, prp, val);
}

int extension_usb_property_is_writeable(struct power_supply *psy,
	enum power_supply_property prp) {
	int rc;

	switch (prp) {
	case POWER_SUPPLY_PROP_RESISTANCE :
		rc = 1;
		break;

	default:
		rc = smb2_usb_prop_is_writeable(psy, prp);
		break;
	}

	return rc;
}

/*************************************************************
 * simple extension for usb port psy.
 */

static enum power_supply_property extension_usb_port_appended [] = {
};

enum power_supply_property* extension_usb_port_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb2_usb_port_props) + ARRAY_SIZE(extension_usb_port_appended)];
	int size_original = ARRAY_SIZE(smb2_usb_port_props);
	int size_appended = ARRAY_SIZE(extension_usb_port_appended);

	memcpy(extended_properties, smb2_usb_port_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_usb_port_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb2_usb_port_props, size_original,
		extension_usb_port_appended, size_appended);

	return extended_properties;
}

size_t extension_usb_port_num_properties(void) {
	return ARRAY_SIZE(smb2_usb_port_props) + ARRAY_SIZE(extension_usb_port_appended);
}

int extension_usb_port_get_property(struct power_supply *psy,
	enum power_supply_property prp, union power_supply_propval *val) {

	struct smb2 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;

	switch (prp) {
	case POWER_SUPPLY_PROP_ONLINE : {
	// Prepare condition 'usb type' from veneer
		struct power_supply* veneer
			= power_supply_get_by_name("veneer");
		int  chgtype = (veneer && !power_supply_get_property(veneer, POWER_SUPPLY_PROP_REAL_TYPE,
			val)) ? val->intval : POWER_SUPPLY_TYPE_UNKNOWN;
		bool online = !smb2_usb_port_get_prop(psy, POWER_SUPPLY_PROP_ONLINE, val)
			? !!val->intval : false;
		bool usb = chgtype == POWER_SUPPLY_TYPE_USB
			|| chgtype == POWER_SUPPLY_TYPE_USB_CDP;
		bool fo = veneer_voter_suspended(VOTER_TYPE_IUSB)
			== CHARGING_SUSPENDED_WITH_FAKE_OFFLINE;
		bool pc = usb_pcport_check(chg);

		if (veneer)
			power_supply_put(veneer);
	// determine USB online
		val->intval = ((usb || pc) && !fo) ? true : online;
	}	return 0;

	default:
		break;
	}

	return smb2_usb_port_get_prop(psy, prp, val);
}


/*************************************************************
 * simple extension for dc psy. (for further purpose)
 */

bool adc_dcin_vnow(struct smb_charger* chg, int* adc) {
	static struct iio_channel* dcin_v_chan;
	union power_supply_propval val = { .intval = 0, };
	*adc = 0;

	if (smblib_get_prop_dc_present(chg, &val) || !val.intval) {
		pr_debug("PMI: DC input is not present\n");
		return true;
	}

	if (!dcin_v_chan || PTR_ERR(dcin_v_chan) == -EPROBE_DEFER) {
		pr_info("PMI: getting dcin_v_chan\n");
		dcin_v_chan = iio_channel_get(chg->dev, "dcin_v");
	}

	if (IS_ERR(dcin_v_chan)) {
		pr_info("PMI: Error on getting dcin_v_chan\n");
		return PTR_ERR(dcin_v_chan);
	}

	return iio_read_channel_processed(dcin_v_chan, adc) >= 0;
}

bool adc_dcin_inow(struct smb_charger* chg, int* adc) {
	static struct iio_channel* dcin_i_chan;
	union power_supply_propval val = { .intval = 0, };
	*adc = 0;

	if (smblib_get_prop_dc_present(chg, &val) || !val.intval) {
		pr_debug("PMI: DC input is not present\n");
		return true;
	}

	if (!dcin_i_chan || PTR_ERR(dcin_i_chan) == -EPROBE_DEFER) {
		pr_info("PMI: getting dcin_i_chan\n");
		dcin_i_chan = iio_channel_get(chg->dev, "dcin_i");
	}

	if (IS_ERR(dcin_i_chan)) {
		pr_info("PMI: Error on getting dcin_i_chan\n");
		return PTR_ERR(dcin_i_chan);
	}

	return iio_read_channel_processed(dcin_i_chan, adc) >= 0;
}

#ifdef CONFIG_IDTP9223_CHARGER
int smblib_get_prop_qipma_on(struct smb_charger *chg,
					union power_supply_propval *val)
{
	int rc = 0;
	u8 qipma_on_stat;

	rc = smblib_read(chg, BATIF_BASE + INT_RT_STS_OFFSET, &qipma_on_stat);
	if (rc < 0) {
		pr_err("Couldn't read qipma_on status rc=%d\n", rc);
		val->intval = -EINVAL;
		return 0;
	}
	val->intval = (bool)(qipma_on_stat & BAT_6_RT_STS_BIT);
	return rc;
}
#endif

static enum power_supply_property extension_dc_appended [] = {
};

enum power_supply_property* extension_dc_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(smb2_dc_props) + ARRAY_SIZE(extension_dc_appended)];
	int size_original = ARRAY_SIZE(smb2_dc_props);
	int size_appended = ARRAY_SIZE(extension_dc_appended);

	memcpy(extended_properties, smb2_dc_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_dc_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(smb2_dc_props, size_original,
		extension_dc_appended, size_appended);

	return extended_properties;
}

size_t extension_dc_num_properties(void) {
	return ARRAY_SIZE(smb2_dc_props) + ARRAY_SIZE(extension_dc_appended);
}

int extension_dc_get_property(struct power_supply* psy,
	enum power_supply_property prp, union power_supply_propval* val) {
	int rc = 0;
	struct smb_charger* chg = power_supply_get_drvdata(psy);

	switch (prp) {
		case POWER_SUPPLY_PROP_VOLTAGE_NOW :
			adc_dcin_vnow(chg, &val->intval);
			break;
		case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW :
			adc_dcin_inow(chg, &val->intval);
			break;
#ifdef CONFIG_IDTP9223_CHARGER
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = 0;
			break;
		case POWER_SUPPLY_PROP_QIPMA_ON:
			rc = smblib_get_prop_qipma_on(chg, val);
			break;
		case POWER_SUPPLY_PROP_QIPMA_ON_STATUS:
			val->intval = chg->qipma_on_status;
			break;
#endif
		default :
			rc = smb2_dc_get_prop(psy, prp, val);
			break;
	}

	return rc;
}


/*************************************************************
 * simple extension for usb main psy.
 */

int extension_usb_main_set_property(struct power_supply* psy,
	enum power_supply_property prp, const union power_supply_propval* val) {
	struct smb_charger* charger = power_supply_get_drvdata(psy);

	switch (prp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX : {
		enum charger_usbid usbid = psy_usbid_get(charger);
		bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
		bool pcport = charger->real_charger_type == POWER_SUPPLY_TYPE_USB;
		bool fabproc = fabid && pcport;

		int icl = val->intval;
		bool chgable = 25000 < icl && icl < INT_MAX;

		if (fabproc && chgable) {
			#define FABCURR 1500000
			int rc = 0;
			u8 reg = 0;

			/* 1. Set override latch bit */
			if (smblib_read(charger, APSD_RESULT_STATUS_REG, &reg) >= 0
				&& !(reg & ICL_OVERRIDE_LATCH_BIT)
				&& smblib_masked_write(charger, CMD_APSD_REG, ICL_OVERRIDE_BIT,
					ICL_OVERRIDE_BIT) >= 0)
				pr_info("Set ICL OVERRIDE for fabcable\n");
			else
				pr_debug("Skip to set ICL OVERRIDE for fabcable\n");

			/* 2. Configure USBIN_ICL_OPTIONS_REG
			(It doesn't need to check result : refer to the 'smblib_set_icl_current') */
			smblib_masked_write(charger, USBIN_ICL_OPTIONS_REG,
				USBIN_MODE_CHG_BIT | CFG_USB3P0_SEL_BIT | USB51_MODE_BIT,
				USBIN_MODE_CHG_BIT);

			/* 2. Configure current */
			rc = smblib_set_charge_param(charger, &charger->param.usb_icl, FABCURR);
			if (rc < 0) {
				pr_err("Couldn't set ICL for fabcable, rc=%d\n", rc);
				break;
			}

			/* 3. Enforce override */
			rc = smblib_icl_override(charger, true);
			if (rc < 0) {
				pr_err("Couldn't set ICL override rc=%d\n", rc);
				break;
			}

			/* 4. Unsuspend after configuring current and override */
			rc = smblib_set_usb_suspend(charger, false);
			if (rc < 0) {
				pr_err("Couldn't resume input rc=%d\n", rc);
				break;
			}

			if (icl != FABCURR)
				pr_info("Success to set IUSB (%d -> %d)mA for fabcable\n", icl/1000, FABCURR/1000);

			return 0;
		}
	}	break;

	default:
		break;
	}

	return smb2_usb_main_set_prop(psy, prp, val);
}

int extension_usb_main_get_property(struct power_supply* psy,
	enum power_supply_property prp, union power_supply_propval* val) {
	struct smb_charger* charger = power_supply_get_drvdata(psy);

	switch (prp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX : {
		enum charger_usbid usbid = psy_usbid_get(charger);
		bool fabid = usbid == CHARGER_USBID_56KOHM || usbid == CHARGER_USBID_130KOHM || usbid == CHARGER_USBID_910KOHM;
		bool pcport = charger->real_charger_type == POWER_SUPPLY_TYPE_USB;
		bool fabproc = fabid && pcport;

		if (fabproc) {
			int rc = smblib_get_charge_param(charger, &charger->param.usb_icl, &val->intval);
			if (rc < 0) {
				pr_err("Couldn't get ICL for fabcable, rc=%d\n", rc);
				break;
			}
			else
				return 0;
		}
	}	break;

	default:
		break;
	}

	return smb2_usb_main_get_prop(psy, prp, val);
}
