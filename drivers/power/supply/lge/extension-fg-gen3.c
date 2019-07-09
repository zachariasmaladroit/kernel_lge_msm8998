/*
 * CAUTION! :
 * 	This file will be included at the end of "qpnp-fg-gen3.c".
 * 	So "qpnp-fg-gen3.c" should be touched before you start to build.
 * 	If not, your work will not be applied to the built image
 * 	because the build system doesn't care the update time of this file.
 */

#include <linux/thermal.h>
#include <linux/kernel.h>
#include "veneer-primitives.h"

#define LGE_FG_INITVAL -1

struct _fake {
	int temperature;
	int capacity;
	int uvoltage;
};

struct _fginfo {
/* Capacity */
	int capacity_rescaled;
	int capacity_monotonic;
	int capacity_chargecnt;
	int capacity_learned;
/* v/i ADCs */
	int battery_inow;
	int battery_vnow;
	int battery_vpredict;
	int battery_ocv;
	int input_vusb;
	int input_iusb;
	int input_vwlc;
	int input_iwlc;
	int input_aicl;
/* Temperature */
	int temp_compensated;
	int temp_thermister;
	int temp_vts;
/* Impedance */
	int impedance_esr;
	int impedance_rslow;
/* Misc */
	int misc_cycle;
	int misc_batid;
/* SoCs */
	int misc_battsoc;
	int misc_ccsocsw;
	int misc_ccsoc;
};

#define TCOMP_TABLE_MAX 3
#define TCOMP_COUNT 25
struct tcomp_param {
	bool load_done;
	int load_max;
	bool icoeff_load_done;
	struct tcomp_entry {
		int temp_cell;
		int temp_bias;
	} table[TCOMP_TABLE_MAX][TCOMP_COUNT];
	int icoeff;

	bool rise_cut;
	int rise_cut_trig;
	bool fall_cut;
	int fall_cut_trig;

	bool qnovo_charging;
	bool logging;
};

struct _rescale {
	bool lge_monotonic;
	int	criteria;	// 0 ~ 255
	int	rawsoc;		// 0 ~ 255
	int	result;		// 0 ~ 100
};

enum tcomp_chg_type {
	TCOMP_CHG_NONE = 0,
	TCOMP_CHG_USB,
	TCOMP_CHG_WLC_LCDOFF,
	TCOMP_CHG_WLC_LCDON
};

/* Gloval variable for extension-fg-gen4 */
static struct _fake fake = {
	.temperature = LGE_FG_INITVAL,
	.capacity = LGE_FG_INITVAL,
	.uvoltage = LGE_FG_INITVAL,
};

static struct _fginfo fggen3 = {
/* Capacity  */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
/* v/i ADCs  */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
                LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
                LGE_FG_INITVAL,
/* Temp      */ LGE_FG_INITVAL, LGE_FG_INITVAL, -1000,
/* impedance */ LGE_FG_INITVAL, LGE_FG_INITVAL,
/* Misc      */ LGE_FG_INITVAL, LGE_FG_INITVAL,
/* SoCs      */ LGE_FG_INITVAL, LGE_FG_INITVAL, LGE_FG_INITVAL,
};

static struct tcomp_param tcomp = {
	.load_done = false,
	.icoeff_load_done = false,
	.icoeff = 0,

	.rise_cut = false,
	.rise_cut_trig = -999,
	.fall_cut = false,
	.fall_cut_trig = -999,

	.qnovo_charging = false,
	.logging = false,
};

/* For SoC rescaling, .rawsoc(0~255) is updated ONLY ON
 * 'fg_delta_msoc_irq_handler' and it is rescaled to 0~100
 */
static struct _rescale rescale = {
	.lge_monotonic = false,
	.criteria = 247,
	.rawsoc = LGE_FG_INITVAL,
	.result = LGE_FG_INITVAL,
};

static int get_charging_type(struct fg_chip *chip)
{
	union power_supply_propval val = { 0, };
	char slimit[20] = "";

	if (chip->batt_psy) {
		if (!power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val))
			if (val.intval != POWER_SUPPLY_STATUS_CHARGING)
				return TCOMP_CHG_NONE;
	}

	if (chip->usb_psy) {
		if (!power_supply_get_property(chip->usb_psy,
			POWER_SUPPLY_PROP_PRESENT, &val))
			if (val.intval)
				return TCOMP_CHG_USB;
	}

	if (chip->dc_psy) {
		if (!power_supply_get_property(chip->dc_psy,
			POWER_SUPPLY_PROP_PRESENT, &val)) {
			if (val.intval) {
				if (unified_nodes_show("charging_restriction", slimit)) {
					if (slimit[0] == '-')
						return TCOMP_CHG_WLC_LCDOFF;
					return TCOMP_CHG_WLC_LCDON;
				} else
					return TCOMP_CHG_WLC_LCDOFF;
			}
		}
	}

	return TCOMP_CHG_NONE;
}

static int get_batt_temp_current(struct fg_chip *chip)
{
	static int ichg = 0;
	bool is_cc = false;
	bool is_fast_charging = false;
	union power_supply_propval val = { 0, };

	if (!chip || !chip->batt_psy || !chip->usb_psy)
		return 0;

	if (!power_supply_get_property(chip->batt_psy,
			POWER_SUPPLY_PROP_STATUS, &val)
				&& val.intval == POWER_SUPPLY_STATUS_CHARGING) {
		if (!power_supply_get_property(chip->usb_psy,
				POWER_SUPPLY_PROP_REAL_TYPE, &val)) {

			if (val.intval == POWER_SUPPLY_TYPE_USB_HVDCP ||
				val.intval == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
				val.intval == POWER_SUPPLY_TYPE_USB_PD)
				is_fast_charging = true;

			if (!power_supply_get_property(chip->batt_psy,
					POWER_SUPPLY_PROP_CHARGE_TYPE, &val) &&
				(val.intval == POWER_SUPPLY_CHARGE_TYPE_TRICKLE ||
				 val.intval == POWER_SUPPLY_CHARGE_TYPE_FAST))
				is_cc = true;

			/*  in case of fast charging, fcc is refered instead of
				real current for avoiding qni negative pulse effect */
			if (is_fast_charging && is_cc ) {
				ichg = !power_supply_get_property(chip->batt_psy,
							POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val) ?
								val.intval / -1000 : ichg;
				goto out;
			} else {
				/* if charging current is over -25mA,
					batt_therm compensation current keeps the previous value */
				if (!power_supply_get_property(chip->batt_psy,
						POWER_SUPPLY_PROP_CURRENT_NOW, &val) &&
							val.intval < -25000)
					ichg = val.intval / 1000;

				goto out;
			}
		}
	}

	ichg = 0;

out:
	return ichg;
}

static int filtered_batt_therm(bool changed, int comp, int batt)
{
	bool tbl_changed = changed;
	int battemp_cell = batt;
	int battemp_comp = comp;
	static int pre_battemp_cell = -9999;
	static int pre_battemp_comp = -9999;
	static bool is_filtering_rise = false;
	static bool is_filtering_fall = false;
	int battemp_cell_diff = 0;
	int battemp_comp_diff = 0;

	/* if it is not in the trigger condition, No apply */
	if (!((tbl_changed && tcomp.rise_cut
			&& (battemp_comp > tcomp.rise_cut_trig)) ||
		(tbl_changed && tcomp.fall_cut
			&& (battemp_comp < tcomp.fall_cut_trig))))
		tbl_changed = false;

	if ((tbl_changed || is_filtering_rise || is_filtering_fall)
		&& (pre_battemp_cell > -9999 && pre_battemp_comp > -9999)) {
		battemp_cell_diff = battemp_cell - pre_battemp_cell;
		battemp_comp_diff = battemp_comp - pre_battemp_comp;
		// rise
		if (tcomp.rise_cut && (battemp_comp >= pre_battemp_comp)) {
			if (is_filtering_fall)
				is_filtering_fall = false;

			if (battemp_comp_diff > battemp_cell_diff) {
				is_filtering_rise = true;
				if ( battemp_cell_diff > 0 )
					battemp_comp = pre_battemp_comp + battemp_cell_diff;
				else
					battemp_comp = pre_battemp_comp;
			}
			else {
				is_filtering_rise = false;
			}
		}
		// fall
		else if (tcomp.fall_cut) {
			if (is_filtering_rise)
				is_filtering_rise = false;

			if (battemp_cell_diff > battemp_comp_diff ) {
				is_filtering_fall = true;
				if (battemp_cell_diff < 0)
					battemp_comp = pre_battemp_comp + battemp_cell_diff;
				else
					battemp_comp = pre_battemp_comp;
			}
			else {
				is_filtering_fall = false;
			}
		}
		else if (tcomp.rise_cut) {
			if (is_filtering_rise)
				is_filtering_rise = false;
		}
	}

	pre_battemp_cell = battemp_cell;
	pre_battemp_comp = battemp_comp;
	return battemp_comp;
}

/* calculate_battery_temperature
 *     bias  : 1st compensation by predefined diffs
 *     icomp : 2nd compensation by (i^2 * k)
 */
static int calculate_battery_temperature(/* @Nonnull */ struct fg_chip *chip)
{
	int battemp_bias, battemp_icomp, battemp_cell = 0;
	int i, temp, ichg = 0, tbl_pt = 0;
	union power_supply_propval val = { 0, };
	bool tbl_changed = false;
	static int pre_tbl_pt = -1;

	if (fg_get_battery_temp(chip, &battemp_cell)){
		pr_info("get real batt therm error\n");
		return LGE_FG_INITVAL;
	}

	if (!tcomp.load_done) {
		pr_info("not ready tcomp table. raw temp=%d\n", battemp_cell);
		return battemp_cell;
	}

	if (!tcomp.icoeff_load_done) {
		pr_info("not ready icoeff. raw temp=%d\n", battemp_cell);
		return battemp_cell;
	}

	if (tcomp.load_max > 1) {
		switch (get_charging_type(chip)) {
			case TCOMP_CHG_WLC_LCDOFF: tbl_pt = 1; break;
			case TCOMP_CHG_WLC_LCDON:  tbl_pt = 2; break;
			default: tbl_pt = 0; break;
		}
	}
	else
		tbl_pt = 0;

	if (pre_tbl_pt >= 0 )
		if (pre_tbl_pt != tbl_pt)
			tbl_changed = true;
	pre_tbl_pt = tbl_pt;

	/* Compensating battemp_bias */
	for (i = 0; i < TCOMP_COUNT; i++) {
		if (battemp_cell < tcomp.table[tbl_pt][i].temp_cell)
			break;
	}

	if (i == 0)
		battemp_bias = tcomp.table[tbl_pt][0].temp_bias;
	else if (i == TCOMP_COUNT)
		battemp_bias = tcomp.table[tbl_pt][TCOMP_COUNT-1].temp_bias;
	else
		battemp_bias =
		(	(tcomp.table[tbl_pt][i].temp_bias -
				tcomp.table[tbl_pt][i-1].temp_bias)
			* (battemp_cell - tcomp.table[tbl_pt][i-1].temp_cell)
			/ (tcomp.table[tbl_pt][i].temp_cell -
				tcomp.table[tbl_pt][i-1].temp_cell)
		) + tcomp.table[tbl_pt][i-1].temp_bias;

	/* Compensating battemp_icomp */
	if (chip->batt_psy) {
		if (tcomp.qnovo_charging) {
			ichg = get_batt_temp_current(chip);
		}
		else {
			if (!power_supply_get_property(
				chip->batt_psy, POWER_SUPPLY_PROP_STATUS, &val)
				&& val.intval == POWER_SUPPLY_STATUS_CHARGING
				&& !power_supply_get_property(
					chip->batt_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val)
				&& val.intval < 0)
				ichg = ((val.intval) / 1000);
		}
	} else {
		pr_info("Battery is not available, %d(=%d+%d) as batt temp\n",
			battemp_cell + battemp_bias, battemp_cell, battemp_bias);
	}

	battemp_icomp = ichg * ichg * tcomp.icoeff / 10000000;
	temp = battemp_cell + battemp_bias - battemp_icomp;

	if (tcomp.logging)
		pr_info("Battery temperature : "
				"%d = (%d)(cell) + (%d)(bias) - %d(icomp), "
				"icoeff = %d, ichg = %d\n",
			temp, battemp_cell, battemp_bias, battemp_icomp,
			tcomp.icoeff, ichg);

	if (tcomp.rise_cut || tcomp.fall_cut)
		return filtered_batt_therm(tbl_changed, temp, battemp_cell);

	return temp;
}

#define LGE_FG_CHARGING         1
#define LGE_FG_DISCHARGING      0
static int  lge_is_fg_charging(struct fg_chip *chip)
{
	union power_supply_propval val = { 0, };
	bool power_status = false;

	if (!chip || !chip->batt_psy || !chip->usb_psy)
		return -1;

	if (!power_supply_get_property(chip->batt_psy,
					POWER_SUPPLY_PROP_STATUS_RAW, &val))
		power_status = (val.intval == POWER_SUPPLY_STATUS_CHARGING) ? true : false;
	else
		return -1;

	if (!power_status)
		return LGE_FG_DISCHARGING;

	if (!power_supply_get_property(chip->batt_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW, &val)) {
		if (val.intval < -25000 )
			return LGE_FG_CHARGING;
		else
			return LGE_FG_DISCHARGING;
	}

	return -1;
}

static int calculate_rescaled_soc(struct fg_chip *chip)
{
	int msoc_raw = 0, rc = 0;
	int new_result = 0;

	rc = fg_get_msoc_raw(chip, &msoc_raw);
	if (rc < 0)
		pr_info("Error in getting msoc_raw, rc=%d\n", rc);

	new_result = min(FULL_CAPACITY,
		DIV_ROUND_CLOSEST(msoc_raw * FULL_CAPACITY, rescale.criteria));

	rescale.rawsoc = msoc_raw;

	if (!rescale.lge_monotonic) {
		rescale.result = new_result;
		return 0;
	}

	if (rescale.result <= 0 ||
		max(rescale.result, new_result) - min(rescale.result, new_result) > 5 )
		rescale.result = new_result;

	switch (lge_is_fg_charging(chip)) {
		case LGE_FG_CHARGING:
			pr_info("fg_rescale: charging: %d = max(old=%d, new=%d)\n",
				max(rescale.result, new_result), rescale.result, new_result);
			rescale.result = max(rescale.result, new_result);
			break;
		case LGE_FG_DISCHARGING:
			pr_info("fg_rescale: discharging: %d = min(old=%d, new=%d)\n",
				min(rescale.result, new_result), rescale.result, new_result);
			rescale.result = min(rescale.result, new_result);
			break;
		default:
			pr_info("fg_rescale: error: old=%d, new=%d\n", rescale.result, new_result);
			rescale.result = new_result;
			break;
	}

	return 0;
}

static int extension_battery_cycle(/* @Nonnull */ struct fg_chip *chip) {
	static int bucket_sum_backup = 0;

	int cycle = 0;
	if (chip->cyc_ctr.en) {
		int i, bucket_sum_now = 0;

		mutex_lock(&chip->cyc_ctr.lock);
		for (i=0; i<BUCKET_COUNT; i++)
			bucket_sum_now += chip->cyc_ctr.count[i];
		cycle = bucket_sum_now / BUCKET_COUNT;
		mutex_unlock(&chip->cyc_ctr.lock);

		if (bucket_sum_backup != bucket_sum_now) {
			pr_info("Battery cycle : %d (= %d / %d)\n",
				cycle, bucket_sum_now, BUCKET_COUNT);
			bucket_sum_backup = bucket_sum_now;
		}
	}

	return cycle;
}

static void fggen3_snapshot_print(void) {
	/* To remove TAG in the logs, 'printk' is used */

	printk("PMINFO: [CSV] "
	/* Capacity    */ "cSYS:%d, cMNT:%d.%d, cCHG:%d, cLRN:%d, "
	/* v/i ADCs    */ "iBAT:%d, vBAT:%d, vPRD:%d, vOCV:%d, vUSB:%d, iUSB:%d, vWLC:%d, iWCL:%d, AiCL:%d, "
	/* Temperature */ "tSYS:%d, tORI:%d, tVTS:%d, "
	/* Impedance   */ "rTOT:%d, rESR:%d, rSLW:%d, "
	/* Misc        */ "CYCLE:%d, BATID:%d, "
	/* SoCs        */ "sBATT:%d, sCCSW:%d, sCC:%d\n",

	/* Capacity    */ fggen3.capacity_rescaled,
			  fggen3.capacity_monotonic/10, fggen3.capacity_monotonic%10,
			  fggen3.capacity_chargecnt, fggen3.capacity_learned,
	/* Battery     */ fggen3.battery_inow, fggen3.battery_vnow, fggen3.battery_vpredict, fggen3.battery_ocv,
	/* Input       */ fggen3.input_vusb, fggen3.input_iusb, fggen3.input_vwlc, fggen3.input_iwlc, fggen3.input_aicl,
	/* Temperature */ fggen3.temp_compensated, fggen3.temp_thermister, fggen3.temp_vts,
	/* Impedance   */ fggen3.impedance_esr + fggen3.impedance_rslow,
			  fggen3.impedance_esr, fggen3.impedance_rslow,
	/* Misc        */ fggen3.misc_cycle, fggen3.misc_batid,
	/* SoCs        */ fggen3.misc_battsoc, fggen3.misc_ccsocsw, fggen3.misc_ccsoc);
}

static void fggen3_snapshot_inputnow(int* vusb, int* iusb, int* vwlc, int* iwlc, int* aicl) {
	struct power_supply* psy_main  = power_supply_get_by_name("main");
	struct power_supply* psy_usb   = power_supply_get_by_name("usb");
	struct power_supply* psy_dc    = power_supply_get_by_name("dc");
	union power_supply_propval val = { 0, };

	*aicl = (psy_main && !power_supply_get_property(psy_main, POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED, &val))
		? val.intval/1000 : LGE_FG_INITVAL;
	*vusb = (psy_usb && !power_supply_get_property(psy_usb, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val))
		? val.intval/1000 : LGE_FG_INITVAL;
	*iusb = (psy_usb && !power_supply_get_property(psy_usb, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val))
		? val.intval/1000 : LGE_FG_INITVAL;
	*vwlc = (psy_dc && !power_supply_get_property(psy_dc, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val))
		? val.intval/1000 : LGE_FG_INITVAL;
	*iwlc = (psy_dc && !power_supply_get_property(psy_dc, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &val))
		? val.intval/1000 : LGE_FG_INITVAL;

	if (psy_main)
		power_supply_put(psy_main);
	if (psy_usb)
		power_supply_put(psy_usb);
	if (psy_dc)
		power_supply_put(psy_dc);
}

static void fggen3_snapshot_update(struct power_supply *psy) {
	struct thermal_zone_device*	tzd = thermal_zone_get_zone_by_name("vts");
	struct fg_chip*			fg3 = power_supply_get_drvdata(psy);
	int				buf = 0;
	union power_supply_propval	val = { 0, };

	if (!tzd || !fg3) {
		// Not ready to log
		return;
	}

/* Capacity */
	fggen3.capacity_rescaled = rescale.result < 0
		? LGE_FG_INITVAL : rescale.result;
	fggen3.capacity_monotonic = !fg_get_msoc_raw(fg3, &buf)
		? buf * 1000 / 255 : LGE_FG_INITVAL;
	fggen3.capacity_chargecnt = !fg_get_charge_counter(fg3, &buf)
		? buf / 1000 : LGE_FG_INITVAL;
	fggen3.capacity_learned
		= (fg3->cl.learned_cc_uah) / 1000;
/* Battery */
	fggen3.battery_inow = !fg_get_battery_current(fg3, &buf)
		? buf / 1000 : LGE_FG_INITVAL;
	fggen3.battery_vnow = !fg_get_battery_voltage(fg3, &buf)
		? buf / 1000 : LGE_FG_INITVAL;
	fggen3.battery_vpredict = !fg_get_sram_prop(fg3, FG_SRAM_VOLTAGE_PRED, &buf)
		? buf / 1000 : LGE_FG_INITVAL;
	fggen3.battery_ocv = !fg_get_sram_prop(fg3, FG_SRAM_OCV, &buf)
		? buf / 1000 : LGE_FG_INITVAL;
/* Input */
	fggen3_snapshot_inputnow(&fggen3.input_vusb, &fggen3.input_iusb,
		&fggen3.input_vwlc, &fggen3.input_iwlc,
		&fggen3.input_aicl);
/* Temperature */
	fggen3.temp_compensated
		= calculate_battery_temperature(fg3);
	fggen3.temp_thermister = !fg_get_battery_temp(fg3, &buf)
		? buf : LGE_FG_INITVAL;
	fggen3.temp_vts = !thermal_zone_get_temp(tzd, &buf)
		? buf : -1000;
/* Impedance */
	fggen3.impedance_esr = !fg_get_sram_prop(fg3, FG_SRAM_ESR, &buf)
		? buf : LGE_FG_INITVAL;
	fggen3.impedance_rslow = !fg_get_sram_prop(fg3, FG_SRAM_RSLOW, &buf)
		? buf : LGE_FG_INITVAL;
/* Misc */
	fggen3.misc_cycle = !power_supply_get_property(fg3->fg_psy,
			POWER_SUPPLY_PROP_CYCLE_COUNT, &val)
		? val.intval : LGE_FG_INITVAL;

	fggen3.misc_batid
		= fg3->batt_id_ohms / 1000;
/* SoCs */
	fggen3.misc_battsoc = !fg_get_sram_prop(fg3, FG_SRAM_BATT_SOC, &buf)
		? ((u32)buf >> 24)*1000/255 : LGE_FG_INITVAL;
	fggen3.misc_ccsocsw = !fg_get_sram_prop(fg3, FG_SRAM_CC_SOC_SW, &buf)
		? div_s64((s64)buf*1000, CC_SOC_30BIT) : LGE_FG_INITVAL;
	fggen3.misc_ccsoc = !fg_get_sram_prop(fg3, FG_SRAM_CC_SOC, &buf)
		? div_s64((s64)buf*1000, CC_SOC_30BIT) : LGE_FG_INITVAL;

	/* logging finally */
	fggen3_snapshot_print();
}

#define KI_CURR_LOWTH_DISCHG_DEFAULT	500
#define KI_CURR_HIGHTH_DISCHG_DEFAULT	1000
#define KI_COEFF_LOW_DISCHG_DEFAULT	800
#define KI_COEFF_MED_DISCHG_DEFAULT	1500
#define KI_COEFF_HI_DISCHG_DEFAULT	2200
int override_fg_adjust_ki_coeff_dischg(struct fg_chip *chip)
{
	int rc, i, msoc;
	int ki_curr_lowth = KI_CURR_LOWTH_DISCHG_DEFAULT;
	int ki_curr_highth = KI_CURR_HIGHTH_DISCHG_DEFAULT;
	int ki_coeff_low = KI_COEFF_LOW_DISCHG_DEFAULT;
	int ki_coeff_med = KI_COEFF_MED_DISCHG_DEFAULT;
	int ki_coeff_hi = KI_COEFF_HI_DISCHG_DEFAULT;
	int cutoff_volt = chip->dt.cutoff_volt_mv;
	int cutoff_curr = chip->dt.cutoff_curr_ma;
	static int old_ki_coeff_low = 0;
	static int old_ki_coeff_med = 0;
	static int old_ki_coeff_hi = 0;
	u8 buf[4], val;

	if (!chip->ki_coeff_dischg_en)
		return 0;

	rc = fg_get_msoc(chip, &msoc);
	if (rc < 0) {
		pr_err("Error in getting capacity, rc=%d\n", rc);
		return rc;
	}

	if (chip->esr_flt_sts == LOW_TEMP &&
		chip->charge_status == POWER_SUPPLY_STATUS_DISCHARGING) {
		cutoff_volt = chip->dt.cutoff_lt_volt_mv;
		cutoff_curr = chip->dt.cutoff_lt_curr_ma;
		for (i = KI_COEFF_SOC_LEVELS - 1; i >= 0; i--) {
			if (msoc < chip->dt.ki_coeff_soc[i]) {
				ki_curr_lowth = chip->dt.ki_curr_lowth_dischg;
				ki_curr_highth = chip->dt.ki_curr_highth_dischg;
				ki_coeff_low = chip->dt.ki_coeff_low_dischg[i];
				ki_coeff_med = chip->dt.ki_coeff_med_dischg[i];
				ki_coeff_hi = chip->dt.ki_coeff_hi_dischg[i];
			}
		}
	}

	if (old_ki_coeff_low == ki_coeff_low
		&& old_ki_coeff_med == ki_coeff_med
		&& old_ki_coeff_hi == ki_coeff_hi)
		return 0;

	old_ki_coeff_low = ki_coeff_low;
	old_ki_coeff_med = ki_coeff_med;
	old_ki_coeff_hi = ki_coeff_hi;

	fg_encode(chip->sp, FG_SRAM_KI_CURR_LOWTH_DISCHG, ki_curr_lowth, &val);
	rc = fg_sram_write(chip,
			chip->sp[FG_SRAM_KI_CURR_LOWTH_DISCHG].addr_word,
			chip->sp[FG_SRAM_KI_CURR_LOWTH_DISCHG].addr_byte, &val,
			chip->sp[FG_SRAM_KI_CURR_LOWTH_DISCHG].len,
			FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing ki_coeff_low_dischg, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_KI_CURR_HIGHTH_DISCHG, ki_curr_highth, &val);
	rc = fg_sram_write(chip,
			chip->sp[FG_SRAM_KI_CURR_HIGHTH_DISCHG].addr_word,
			chip->sp[FG_SRAM_KI_CURR_HIGHTH_DISCHG].addr_byte, &val,
			chip->sp[FG_SRAM_KI_CURR_HIGHTH_DISCHG].len,
			FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing ki_coeff_low_dischg, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_KI_COEFF_LOW_DISCHG, ki_coeff_low, &val);
	rc = fg_sram_write(chip,
			chip->sp[FG_SRAM_KI_COEFF_LOW_DISCHG].addr_word,
			chip->sp[FG_SRAM_KI_COEFF_LOW_DISCHG].addr_byte, &val,
			chip->sp[FG_SRAM_KI_COEFF_LOW_DISCHG].len,
			FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing ki_coeff_low, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_KI_COEFF_MED_DISCHG, ki_coeff_med, &val);
	rc = fg_sram_write(chip,
			chip->sp[FG_SRAM_KI_COEFF_MED_DISCHG].addr_word,
			chip->sp[FG_SRAM_KI_COEFF_MED_DISCHG].addr_byte, &val,
			chip->sp[FG_SRAM_KI_COEFF_MED_DISCHG].len,
			FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing ki_coeff_med, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_KI_COEFF_HI_DISCHG, ki_coeff_hi, &val);
	rc = fg_sram_write(chip,
			chip->sp[FG_SRAM_KI_COEFF_HI_DISCHG].addr_word,
			chip->sp[FG_SRAM_KI_COEFF_HI_DISCHG].addr_byte, &val,
			chip->sp[FG_SRAM_KI_COEFF_HI_DISCHG].len,
			FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing ki_coeff_hi, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_CUTOFF_VOLT, cutoff_volt, buf);
	rc = fg_sram_write(chip, chip->sp[FG_SRAM_CUTOFF_VOLT].addr_word,
			chip->sp[FG_SRAM_CUTOFF_VOLT].addr_byte, buf,
			chip->sp[FG_SRAM_CUTOFF_VOLT].len, FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing cutoff_volt, rc=%d\n", rc);
		return rc;
	}

	fg_encode(chip->sp, FG_SRAM_CUTOFF_CURR, cutoff_curr, buf);
	rc = fg_sram_write(chip, chip->sp[FG_SRAM_CUTOFF_CURR].addr_word,
			chip->sp[FG_SRAM_CUTOFF_CURR].addr_byte, buf,
			chip->sp[FG_SRAM_CUTOFF_CURR].len, FG_IMA_DEFAULT);
	if (rc < 0) {
		pr_err("Error in writing cutoff_curr, rc=%d\n", rc);
		return rc;
	}

	fg_dbg(chip, FG_LGE,
		"Wrote [ki_coeff: low=%d, med=%d, hi=%d]-"
		"[ki curr: lowth=%d, highth=%d]-[cutoff: %dmV, %dmA]-"
		"[status: %s, soc=%d, temp=%s]\n",
		ki_coeff_low, ki_coeff_med, ki_coeff_hi, ki_curr_lowth, ki_curr_highth,
		cutoff_volt, cutoff_curr,
		(chip->charge_status == POWER_SUPPLY_STATUS_DISCHARGING) ? "DISC": "CHG",
		msoc, (chip->esr_flt_sts == LOW_TEMP) ? "low": "room or relex");

	return 0;
}

int override_fg_parse_for_battery_charactistics(struct fg_chip *chip, struct device_node *node)
{
	int rc, temp;

	rc = of_property_read_u32(node, "lge,fg-cutoff-voltage", &temp);
	if (!rc)
		chip->dt.cutoff_volt_mv = temp;

	rc = of_property_read_u32(node, "lge,fg-cutoff-current", &temp);
	if (!rc)
		chip->dt.cutoff_curr_ma = temp;

	chip->dt.cutoff_lt_volt_mv = chip->dt.cutoff_volt_mv;
	rc = of_property_read_u32(node, "lge,fg-cutoff-lt-voltage", &temp);
	if (!rc)
		chip->dt.cutoff_lt_volt_mv = temp;

	chip->dt.cutoff_lt_curr_ma = chip->dt.cutoff_curr_ma;
	rc = of_property_read_u32(node, "lge,fg-cutoff-lt-current", &temp);
	if (!rc)
		chip->dt.cutoff_lt_curr_ma = temp;

	rc = of_property_read_u32(node, "lge,fg-esr-tight-lt-filter-micro-pct", &temp);
	if (!rc)
		chip->dt.esr_tight_lt_flt_upct = temp;

	rc = of_property_read_u32(node, "lge,fg-esr-broad-lt-filter-micro-pct", &temp);
	if (!rc)
		chip->dt.esr_broad_lt_flt_upct = temp;

	fg_dbg(chip, FG_LGE,
		"Wrote [battery charactistics: cutoff=(%dmV, %dmA), cutoff_lt=(%dmV, %dmA), "
		"esr_tight_lt_filter=%d, esr_broad_lt_filter=%d]\n",
		chip->dt.cutoff_volt_mv, chip->dt.cutoff_curr_ma,
		chip->dt.cutoff_lt_volt_mv, chip->dt.cutoff_lt_curr_ma,
		chip->dt.esr_tight_lt_flt_upct, chip->dt.esr_broad_lt_flt_upct);

	return 0;
}

int override_fg_parse_ki_coefficient(struct fg_chip *chip, struct device_node *node)
{
	int rc, i, temp;

	rc = of_property_read_u32(node, "qcom,ki-coeff-full-dischg", &temp);
	if (!rc)
		chip->dt.ki_coeff_full_soc_dischg = temp;

	rc = of_property_read_u32(node, "lge,ki-coeff-curr-lowth-dischg", &temp);
	if (!rc)
		chip->dt.ki_curr_lowth_dischg = temp;

	rc = of_property_read_u32(node, "lge,ki-coeff-curr-highth-dischg", &temp);
	if (!rc)
		chip->dt.ki_curr_highth_dischg = temp;

	rc = fg_parse_dt_property_u32_array(node, "lge,ki-coeff-soc-dischg",
		chip->dt.ki_coeff_soc, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = fg_parse_dt_property_u32_array(node, "lge,ki-coeff-low-dischg",
		chip->dt.ki_coeff_low_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = fg_parse_dt_property_u32_array(node, "lge,ki-coeff-med-dischg",
		chip->dt.ki_coeff_med_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	rc = fg_parse_dt_property_u32_array(node, "lge,ki-coeff-hi-dischg",
		chip->dt.ki_coeff_hi_dischg, KI_COEFF_SOC_LEVELS);
	if (rc < 0)
		return rc;

	for (i = 0; i < KI_COEFF_SOC_LEVELS; i++) {
		if (chip->dt.ki_coeff_soc[i] < 0 ||
			chip->dt.ki_coeff_soc[i] > FULL_CAPACITY) {
			pr_err("Error in ki_coeff_soc_dischg values\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_low_dischg[i] < 0 ||
			chip->dt.ki_coeff_low_dischg[i] > KI_COEFF_MAX) {
			pr_err("Error in ki_coeff_low_dischg values\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_med_dischg[i] < 0 ||
			chip->dt.ki_coeff_med_dischg[i] > KI_COEFF_MAX) {
			pr_err("Error in ki_coeff_med_dischg values\n");
			return -EINVAL;
		}

		if (chip->dt.ki_coeff_hi_dischg[i] < 0 ||
			chip->dt.ki_coeff_hi_dischg[i] > KI_COEFF_MAX) {
			pr_err("Error in ki_coeff_hi_dischg values\n");
			return -EINVAL;
		}
	}
	chip->ki_coeff_dischg_en = true;

	return 0;
}

///////////////////////////////////////////////////////////////////////////////

#define PROPERTY_CONSUMED_WITH_SUCCESS	0
#define PROPERTY_CONSUMED_WITH_FAIL	EINVAL
#define PROPERTY_BYPASS_REASON_NOENTRY	ENOENT
#define PROPERTY_BYPASS_REASON_ONEMORE	EAGAIN

static int workaround_backup_bms_property[POWER_SUPPLY_PROP_BATTERY_TYPE];

static enum power_supply_property extension_bms_appended [] = {
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_UPDATE_NOW,
};

static int extension_bms_get_property_pre(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val) {
	int rc = PROPERTY_CONSUMED_WITH_SUCCESS;

	struct fg_chip* chip = power_supply_get_drvdata(psy);
	struct irq_desc* irq = irq_to_desc(chip->irqs[SOC_UPDATE_IRQ].irq);

	switch (prop) {
	case POWER_SUPPLY_PROP_RESISTANCE:
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
	case POWER_SUPPLY_PROP_CHARGE_NOW_RAW:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
	case POWER_SUPPLY_PROP_CHARGE_COUNTER_SHADOW:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		if (irq && !irq->depth) {
			val->intval = workaround_backup_bms_property[prop];
			pr_info("[W/A] BBP) SOC_UPDATE_IRQ enabled! (==Reading blocked!) "
				"Skip to read property(%d) and reuse %d for faster access time\n",
				prop, val->intval);
		}
		else {
			pr_debug("[W/A] BBP) Not blocked! Bypass to get prp (%d) from bms psy\n",
				prop);
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		}
		break;

	case POWER_SUPPLY_PROP_CAPACITY :
		// Battery fake setting has top priority
		if (fake.capacity != LGE_FG_INITVAL)
			val->intval = fake.capacity;
		else if (rescale.result != LGE_FG_INITVAL)
			val->intval = rescale.result;
		else
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		break;

	case POWER_SUPPLY_PROP_TEMP :
		if (fake.temperature == LGE_FG_INITVAL)
			val->intval = calculate_battery_temperature(chip); // Use compensated temperature
		else
			val->intval = fake.temperature;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		if (fake.uvoltage == LGE_FG_INITVAL)
			rc = -PROPERTY_BYPASS_REASON_ONEMORE;
		else
			val->intval = fake.uvoltage;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_BOOT : {
		int buf = 0;
		val->intval = !fg_get_sram_prop(chip, FG_SRAM_VOLTAGE_PRED, &buf)
			? buf : LGE_FG_INITVAL;
	}	break;

	case POWER_SUPPLY_PROP_ESR_COUNT : {
		int buf = 0;
		val->intval = !fg_get_sram_prop(chip, FG_SRAM_ESR, &buf)
			? buf : LGE_FG_INITVAL;
	}	break;

	case POWER_SUPPLY_PROP_UPDATE_NOW :
		/* Do nothing and just consume getting */
		val->intval = -1;
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = extension_battery_cycle(chip);
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
		break;
	}

	return rc;
}

static int extension_bms_get_property_post(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val, int rc) {
	workaround_backup_bms_property[prop] = val->intval;
	return rc;
}

static int extension_bms_set_property_pre(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val) {

	int  rc = PROPERTY_CONSUMED_WITH_SUCCESS;
	int* fakeset = NULL;

	switch (prop) {
	case POWER_SUPPLY_PROP_ESR_COUNT : {
		struct fg_chip *chip = power_supply_get_drvdata(psy);
		if (!val->intval) {
			pm_stay_awake(chip->dev);
			schedule_work(&chip->esr_filter_work);
		}
		else
			__fg_esr_filter_config(chip, RELAX_TEMP);
	}	break;

	case POWER_SUPPLY_PROP_UPDATE_NOW :
		if (val->intval)
			fggen3_snapshot_update(psy);
		break;

	case POWER_SUPPLY_PROP_TEMP :
		fakeset = &fake.temperature;
		break;
	case POWER_SUPPLY_PROP_CAPACITY :
		fakeset = &fake.capacity;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW :
		fakeset = &fake.uvoltage;
		break;

	default:
		rc = -PROPERTY_BYPASS_REASON_NOENTRY;
	}

	if (fakeset && *fakeset != val->intval) {
		struct fg_chip *chip = power_supply_get_drvdata(psy);
		*fakeset = val->intval;

		power_supply_changed(chip->batt_psy);
	}

	return rc;
}

static int extension_bms_set_property_post(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val, int rc) {
	return rc;
}

///////////////////////////////////////////////////////////////////////////////
enum power_supply_property* extension_bms_properties(void) {
	static enum power_supply_property extended_properties[ARRAY_SIZE(fg_psy_props) + ARRAY_SIZE(extension_bms_appended)];
	int size_original = ARRAY_SIZE(fg_psy_props);
	int size_appended = ARRAY_SIZE(extension_bms_appended);

	memcpy(extended_properties, fg_psy_props,
		size_original * sizeof(enum power_supply_property));
	memcpy(&extended_properties[size_original], extension_bms_appended,
		size_appended * sizeof(enum power_supply_property));

	veneer_extension_pspoverlap(fg_psy_props, size_original,
		extension_bms_appended, size_appended);

	return extended_properties;
}

size_t extension_bms_num_properties(void) {
	return ARRAY_SIZE(fg_psy_props) + ARRAY_SIZE(extension_bms_appended);
}

int extension_bms_get_property(struct power_supply *psy,
	enum power_supply_property prop, union power_supply_propval *val) {

	int rc = extension_bms_get_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = fg_psy_get_property(psy, prop, val);
	rc = extension_bms_get_property_post(psy, prop, val, rc);

	return rc;
}

int extension_bms_set_property(struct power_supply *psy,
	enum power_supply_property prop, const union power_supply_propval *val) {

	int rc = extension_bms_set_property_pre(psy, prop, val);
	if (rc == -PROPERTY_BYPASS_REASON_NOENTRY || rc == -PROPERTY_BYPASS_REASON_ONEMORE)
		rc = fg_psy_set_property(psy, prop, val);
	rc = extension_bms_set_property_post(psy, prop, val, rc);

	return rc;
}

int extension_bms_property_is_writeable(struct power_supply *psy,
	enum power_supply_property prop) {
	int rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_TEMP:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_UPDATE_NOW:
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		rc = 1;
		break;
	default:
		rc = fg_property_is_writeable(psy, prop);
		break;
	}
	return rc;
}

struct device_node* extension_get_batt_profile(struct device_node* container,
	int resistance_id) {
       /* Search with resistance_id and
	* Hold the result to an unified node(sysfs) for the fab process
	*/
	char buffer [8] = { '\0', };
	struct device_node* profile = of_batterydata_get_best_profile(container,
		resistance_id, NULL);

	/* If no matching, set it as default */
	if (!profile) {
		#define DTNODE_DEFAULT_BATTERY_ROOT "lge-battery-supplement"
		#define DTNODE_DEFAULT_BATTERY_NAME "default-battery-name"
		#define DTNODE_DEFAULT_BATTERY_KOHM "default-battery-kohm"

		struct device_node* node = of_find_node_by_name(NULL,
			DTNODE_DEFAULT_BATTERY_ROOT);
		const char* name = of_property_read_string(node, DTNODE_DEFAULT_BATTERY_NAME,
			&name) ? NULL : name;
		int kohm = of_property_read_u32(node, DTNODE_DEFAULT_BATTERY_KOHM, &kohm)
			? 0 : kohm;
		profile = of_batterydata_get_best_profile(container,
			kohm, name);
		pr_err("Getting default battery profile(%s): %s\n", name,
			profile ? "success" : "fail");
	}

	// At this time, 'battery_valid' may be true always for the embedded battery model
	#define UNINODE_BATTERY_VALID	"battery_valid"
	snprintf(buffer, sizeof(buffer), "%d", !!profile);
	unified_nodes_store(UNINODE_BATTERY_VALID, buffer, sizeof(buffer));

	return profile;
}

static int extension_fg_load_icoeff_dt(struct fg_chip *chip)
{
	struct device_node* tcomp_dtroot;
	struct device_node* tcomp_override;
	int dt_icomp = 0;

	if (tcomp.icoeff_load_done) {
		pr_info("icoeff had already been loaded.\n");
		return 0;
	}

	if (!chip->soc_reporting_ready) {
		pr_info("fG profile is not ready.\n");
		return LGE_FG_INITVAL;
	}

	tcomp_dtroot = of_find_node_by_name(NULL, "lge-battery-supplement");
	if (!tcomp_dtroot) {
		pr_info("failed to find lge-battery-supplement\n");
		return LGE_FG_INITVAL;
	}

	if (chip->bp.batt_type_str) {
		tcomp_override = of_find_node_by_name(
				tcomp_dtroot, chip->bp.batt_type_str);
		if (tcomp_override &&
				of_property_read_u32(
					tcomp_override, "tempcomp-icoeff", &dt_icomp) >= 0)
			pr_info("ICOEFF is overridden to %d for %s\n", dt_icomp, chip->bp.batt_type_str);
	}

	if (!dt_icomp) {
		if (of_property_read_u32(tcomp_dtroot, "tempcomp-icoeff", &dt_icomp) >= 0) {
			pr_info("ICOEFF is set to %d by default\n", dt_icomp);
		} else {
			pr_info("ICOEFF isn't set. error.\n");
			return -1;
		}
	}

	tcomp.icoeff = dt_icomp;
	tcomp.icoeff_load_done = true;
	return 0;
}

static int extension_fg_load_dt(void)
{
	const char str_tempcomp[TCOMP_TABLE_MAX][30] = {
		"tempcomp-offset",
		"tempcomp-offset-wlc-lcdoff",
		"tempcomp-offset-wlc-lcdon"
	};

	struct device_node* tcomp_dtroot;
	int dtarray_count = TCOMP_COUNT * 2;
	u32 dtarray_data [TCOMP_COUNT * 2];
	int i = 0, j = 0;

	if (tcomp.load_done) {
		pr_info("tcomp table had already been loaded.\n");
		return 0;
	}

	tcomp_dtroot = of_find_node_by_name(NULL, "lge-battery-supplement");
	if (!tcomp_dtroot) {
		pr_info("failed to find lge-battery-supplement\n");
		return -1;
	}

	if (of_property_read_bool(tcomp_dtroot, "tempcomp-offset-wlc-enable"))
		tcomp.load_max = 3;
	else
		tcomp.load_max = 1;

	for (j = 0; j < tcomp.load_max; j++ ) {
		/* Finding tcomp_table and tcomp_icoeff */
		if (of_property_read_u32_array(tcomp_dtroot, str_tempcomp[j],
				dtarray_data, dtarray_count) >= 0) {
			for (i = 0; i < dtarray_count; i += 2) {
				tcomp.table[j][i/2].temp_cell = dtarray_data[i];
				tcomp.table[j][i/2].temp_bias = dtarray_data[i+1];
				pr_debug("Index = %02d : %4d - %4d\n",
					i/2,
					tcomp.table[j][i/2].temp_cell,
					tcomp.table[j][i/2].temp_bias);
			}
		} else {
			pr_info("%s is not found, error\n", str_tempcomp[j]);
			tcomp.table[j][0].temp_cell = INT_MAX;
			tcomp.table[j][0].temp_bias = 0;
			return -1;
		}
	}

	tcomp.rise_cut = of_property_read_bool(tcomp_dtroot,
		"tempcomp-offset-wlc-rise-filter-enable");
	if (tcomp.rise_cut)
		of_property_read_u32(tcomp_dtroot,
			"tempcomp-offset-wlc-rise-filter-trigger", &tcomp.rise_cut_trig);
	tcomp.fall_cut = of_property_read_bool(tcomp_dtroot,
		"tempcomp-offset-wlc-fall-filter-enable");
	if (tcomp.fall_cut)
		of_property_read_u32(tcomp_dtroot,
			"tempcomp-offset-wlc-fall-filter-trigger", &tcomp.fall_cut_trig);

	of_property_read_u32(tcomp_dtroot,
		"capacity-raw-full", &rescale.criteria);
	rescale.lge_monotonic = of_property_read_bool(tcomp_dtroot,
		"lg-monotonic-soc-enable");
	tcomp.logging = of_property_read_bool(tcomp_dtroot,
		"tempcomp-logging");
	tcomp.qnovo_charging = of_property_read_bool(tcomp_dtroot,
		"tempcomp-qnovo-charging");

	if (j == tcomp.load_max) {
		tcomp.load_done = true;

		pr_info("[tempcomp config] table count: %s (%d/%d), "
			"rise cut filter: %s (trigger = %d degree), "
			"fall cut filter: %s (trigger = %d degree)\n",
			(j == tcomp.load_max) ? "done" : "error", j, tcomp.load_max,
			tcomp.rise_cut ? "enabled" : "disabled", tcomp.rise_cut_trig,
			tcomp.fall_cut ? "enabled" : "disabled", tcomp.fall_cut_trig);
	}

	return 0;
}
