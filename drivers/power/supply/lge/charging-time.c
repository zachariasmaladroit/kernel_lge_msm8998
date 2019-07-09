#define pr_fmt(fmt) "CHGTIME: %s: " fmt, __func__
#define pr_chgtime(reason, fmt, ...)			\
do {							\
	if (pr_debugmask & (reason))			\
		pr_info(fmt, ##__VA_ARGS__);		\
	else						\
		pr_debug(fmt, ##__VA_ARGS__);		\
} while (0)

static int pr_debugmask;

#define EMPTY			-1
#define NOTYET			-1
#define PROFILE_SLOT_COUNT	256
#define SAMPLING_PERIOD_MS	1500

#include <linux/of.h>
#include <linux/slab.h>

#include "veneer-primitives.h"

static struct charging_time {
	/* structures for operating */
	struct device_node*	chgtime_dnode;
	struct delayed_work	chgtime_dwork;
	bool		      (*chgtime_get)(int* power);
	void		      (*chgtime_notify)(int power);
	int			chgtime_fullraw;	// the raw soc for the rescaled 100%

	/* overstatement_coefficient = weight / base */
	int	overstatement_enabled;
	int	overstatement_weight;
	int	overstatement_base;
	/* ceiling params for max charging (mw) */
	int	maxcharging_chargerlogo;
	int	maxcharging_normal;

	/* static properties on charger profiles */
	int	profile_power;
	int	profile_consuming[PROFILE_SLOT_COUNT];
	int	profile_remaining[PROFILE_SLOT_COUNT];

	/* runtime properties */
	int	rawsoc_begin;
	int	rawsoc_now;
	int	runtime_consumed[PROFILE_SLOT_COUNT];
	int	runtime_remained[PROFILE_SLOT_COUNT];
	int	runtime_reported[PROFILE_SLOT_COUNT];

	/* Timestamps */
	long	starttime_of_charging;
	long	starttime_of_rawsoc;
} time_me;

static int chgtime_upperbound(void) {
	return (!unified_bootmode_chargerlogo() && time_me.maxcharging_normal)
		? time_me.maxcharging_normal : INT_MAX;
}

static bool chgtime_profiling(int power, int fullraw) {
	int i;
	struct device_node* upper_dnode = of_get_next_child(time_me.chgtime_dnode, NULL);
	struct device_node* lower_dnode; // uses as a iterator
	int upper_power = INT_MAX, lower_power;

	for_each_child_of_node(time_me.chgtime_dnode, lower_dnode) {
		if (of_property_read_u32(lower_dnode, "charger-power", &lower_power))
			return false;

		if (power < lower_power) {
			upper_power = lower_power;
			upper_dnode = lower_dnode;
			/* Go through to next iteration */
		}
		else {
			if (upper_power == INT_MAX)
				upper_power = lower_power;
			break;
		}
	}

	/* Interpolation */
	pr_chgtime(VERBOSE, "Upper power = %d, Lower power = %d\n", upper_power, lower_power);
	for (i=0; i<PROFILE_SLOT_COUNT; ++i) {
		int upper_time, lower_time;
		if (of_property_read_u32_index(upper_dnode, "charger-profile", i, &upper_time)
			|| of_property_read_u32_index(lower_dnode, "charger-profile", i, &lower_time)) {
			return false;
		}

		time_me.profile_consuming[i] = upper_time
			+ (lower_time-upper_time)*(upper_power-power)/(upper_power-lower_power);
	}
	/* Calculate 'profile_remaining' for each soc */
	/* start point set to fullraw-2 for displaying +0 min at UI 100% */
	memset(time_me.profile_remaining, 0, sizeof(int)*PROFILE_SLOT_COUNT);
	for (i=fullraw-2; 0<=i; --i) {
		time_me.profile_remaining[i] = time_me.profile_consuming[i]
			+ time_me.profile_remaining[i+1];
	}
	/* Print for debugging */
	for (i=0; i<PROFILE_SLOT_COUNT; ++i)
		pr_chgtime(VERBOSE, "SoC %2d : Profiled remains : %5d\n", i, time_me.profile_remaining[i]);

	return true;
}

static void chgtime_dworkf(struct work_struct *work) {
	int power = time_me.chgtime_get(&power) ? min(power, chgtime_upperbound()) : 0;

	if (time_me.profile_power != power) {
		pr_chgtime(UPDATE, "One more sampling (power:%dmw)\n",
			power);

		/* Input power 0mW is not permitted to be updated */
		if (power != 0) {
			/* Update information */
			time_me.profile_power = power;
		}

		schedule_delayed_work(&time_me.chgtime_dwork,
			msecs_to_jiffies(SAMPLING_PERIOD_MS));

		return;
	}

	chgtime_profiling(time_me.profile_power, time_me.chgtime_fullraw);
	time_me.chgtime_notify(time_me.profile_power);
}

static void chgtime_evaluate(long eoc) {
	// Evaluation has meaning only on charging termination (== soc 100%)
	int i, begin_soc = time_me.rawsoc_begin;
	int really_remained[PROFILE_SLOT_COUNT + 1];

	if (begin_soc == time_me.chgtime_fullraw) {
		/* If charging is started on 100%,
		 * Skip to evaluate
		 */
		return;
	}

	memset(really_remained, 0, sizeof(really_remained));
	for (i=time_me.chgtime_fullraw; begin_soc<=i; --i)
		really_remained[i] = time_me.runtime_consumed[i] + really_remained[i+1];

	pr_chgtime(EVALUATE, "Evaluating... %d[mW] charging from %2d(%ld) to 100(%ld), (duration %ld)\n",
		time_me.profile_power, begin_soc, time_me.starttime_of_charging, eoc, eoc-time_me.starttime_of_charging);

	pr_chgtime(EVALUATE, ", soc, really consumed, really remained"
		", profiled remaining, profiled consuming\n");
	for (i=begin_soc; i<PROFILE_SLOT_COUNT; ++i) {
		pr_chgtime(EVALUATE, ", %d, %d, %d, %d, %d\n",
			i, time_me.runtime_consumed[i], really_remained[i],
			time_me.profile_remaining[i], time_me.profile_consuming[i]
		);
	}
}

int charging_time_remains(int rawsoc) {

	// Simple check
	if ( !(0 < time_me.profile_power && 0 <= rawsoc && rawsoc < PROFILE_SLOT_COUNT) ) {
		/* Invalid invokation */
		return NOTYET;
	}

	// This calling may NOT be bound with SoC changing
	if (time_me.rawsoc_now != rawsoc) {
		long		now;
		struct timespec	tspec;
		get_monotonic_boottime(&tspec);
		now = tspec.tv_sec;

		if (time_me.starttime_of_charging == EMPTY) {
			// New insertion
			time_me.rawsoc_begin = rawsoc;
			time_me.starttime_of_charging = now;
		}
		else {	// Soc rasing up
			time_me.runtime_consumed[rawsoc > 0 ? rawsoc-1 : 0]
				= now - time_me.starttime_of_rawsoc;
		}

		/* Update time_me */
		time_me.rawsoc_now = rawsoc;
		time_me.starttime_of_rawsoc = now;

		if (rawsoc == time_me.chgtime_fullraw) {
			/* Evaluate NOW! (at the rescaled 100% soc) :
			 * Evaluation has meaning only on full(100%) charged status
			 */
			chgtime_evaluate(now);
		}
		else {
			pr_chgtime(UPDATE, "rawsoc %d, elapsed %ds...\n",
				rawsoc, time_me.runtime_consumed[rawsoc > 0 ? rawsoc-1 : 0]);
		}
	}

	// Overstate if needed
	if (time_me.overstatement_enabled)
		time_me.runtime_reported[rawsoc]
			= (time_me.overstatement_base + time_me.overstatement_weight)
			* time_me.profile_remaining[rawsoc]
			/ time_me.overstatement_base;
	else
		time_me.runtime_reported[rawsoc]
			= time_me.profile_remaining[rawsoc];

	return time_me.runtime_reported[rawsoc];
}

bool charging_time_update(enum charging_supplier charger) {
	static enum charging_supplier type = CHARGING_SUPPLY_TYPE_NONE;

	if (type != charger) {
		bool charging = charger != CHARGING_SUPPLY_TYPE_UNKNOWN
			&& charger != CHARGING_SUPPLY_TYPE_NONE;

		if (charging) {
			pr_chgtime(UPDATE, "Charging started, Start sampling\n");
			cancel_delayed_work_sync(&time_me.chgtime_dwork);
			schedule_delayed_work(&time_me.chgtime_dwork,
				msecs_to_jiffies(SAMPLING_PERIOD_MS));
		}
		else {
			pr_chgtime(UPDATE, "Charging stopped\n");
			charging_time_clear();
		}

		type = charger;
		return true;
	}
	else {
		pr_chgtime(VERBOSE, "Skip to initiate\n");
		return false;
	}
}

void charging_time_clear(void) {
	int i;
	cancel_delayed_work_sync(&time_me.chgtime_dwork);

	// PRESERVE '_remains[256]' as '0'
	for (i=0; i<PROFILE_SLOT_COUNT; i++) {
		time_me.profile_consuming[i] = EMPTY;
		time_me.profile_remaining[i] = EMPTY;

		time_me.runtime_consumed[i] = EMPTY;
		time_me.runtime_remained[i] = EMPTY;
	}

	/* For runtime values */
	time_me.rawsoc_begin = NOTYET;
	time_me.rawsoc_now = NOTYET;
	time_me.starttime_of_charging = NOTYET;
	time_me.starttime_of_rawsoc = NOTYET;

	/* For beginning status */
	time_me.profile_power = NOTYET;
}

void charging_time_destroy(void) {
	// dwork should be canceled before setting pointers to null
	charging_time_clear();

	time_me.chgtime_dnode = NULL;
	time_me.chgtime_get = NULL;
	time_me.chgtime_notify = NULL;
}

bool charging_time_create(struct device_node* dnode, int fullraw,
	bool (*feed_charging_time)(int* power),
	void (*back_charging_time)(int power)) {
	pr_debugmask = ERROR | UPDATE | EVALUATE;

	if (dnode && feed_charging_time && back_charging_time
		&& !of_property_read_u32(dnode, "lge,maxcharging-mw-chargerlogo", &time_me.maxcharging_chargerlogo)
		&& !of_property_read_u32(dnode, "lge,maxcharging-mw-normal", &time_me.maxcharging_normal)
		&& !of_property_read_u32(dnode, "lge,overstatement-enable", &time_me.overstatement_enabled)) {
		if (time_me.overstatement_enabled) {
			if (of_property_read_u32(dnode, "lge,overstatement-weight", &time_me.overstatement_weight)
				|| of_property_read_u32(dnode, "lge,overstatement-base", &time_me.overstatement_base))
				goto fail;
		}

		time_me.chgtime_dnode = dnode;
	}
	else
		goto fail;

	if (0 > fullraw || fullraw >= PROFILE_SLOT_COUNT) {
		pr_chgtime(ERROR, "'chgtime_fullraw'(%d) is out of range\n", fullraw);
		goto fail;
	}
	else
		time_me.chgtime_fullraw = fullraw;

	time_me.chgtime_get = feed_charging_time;
	time_me.chgtime_notify = back_charging_time;
	INIT_DELAYED_WORK(&time_me.chgtime_dwork, chgtime_dworkf);

	/* Redundant initializing, but be sure to PRESERVE '_remains[PROFILE_SLOT_COUNT]' as '0' */
	time_me.profile_remaining[PROFILE_SLOT_COUNT-1] = 0;
	time_me.runtime_remained[PROFILE_SLOT_COUNT-1] = 0;

	return true;

fail:	pr_chgtime(ERROR, "Failed to create charging time\n");
	return false;
}

