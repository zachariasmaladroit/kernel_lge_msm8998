#define pr_fmt(fmt) "UNINODE: %s: " fmt, __func__
#define pr_uninode(fmt, ...) pr_err(fmt, ##__VA_ARGS__)

#define UNIFIED_NODES_DEVICE	"lge-unified-nodes"
#define UNIFIED_NODES_DISABLED	-1000000

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "veneer-primitives.h"

#define VOTER_NAME_THERMALD	"THERMALD"
#define VOTER_NAME_HIDDENM	"HIDDENM"
#define RESTRICTION_MAX_COUNT	8
#define RESTRICTION_MAX_NAME	32

struct unified_nodes {
	struct device_node*		devnode_restrictchg;
	struct device_node*		devnode_fakebatt;
	struct device_node*		devnode_battage;

	struct voter_entry		thermald_iusb;
	struct voter_entry		thermald_ibat;
	struct voter_entry		thermald_idc;
	struct voter_entry		thermald_vdc;
	struct voter_entry		charging_restriction [RESTRICTION_MAX_COUNT];
	struct voter_entry		charging_enable [2]; // 0 for iusb, 1 for idc
	const char*			charging_step;
	bool				charging_showcase;
	void*				charging_completed;
	bool				fake_battery;
	bool				fake_sdpmax;
	bool				fake_hvdcp;
	void*				battery_age;
	void*				battery_condition;
	void*				battery_cycle;
	bool				battery_valid;
	char				charger_name [64];
	bool				charger_highspeed;
	bool*				charger_incompatible;
	void*				charger_usbid;
	enum charging_verbosity*	charger_verbose;
	// Supporting features : used for verifying device
	int				support_fastpl;
	bool				support_fastchg;
};

static void voter_unregister(struct unified_nodes* uninodes) {
	int i;

	// For Restriction
	for (i=0; i<RESTRICTION_MAX_COUNT; ++i) {
		struct voter_entry* restriction = &uninodes->charging_restriction[i];
		if (restriction->type != VOTER_TYPE_INVALID)
			veneer_voter_unregister(restriction);
	}
	// For Thermald
	veneer_voter_unregister(&uninodes->thermald_iusb);
	veneer_voter_unregister(&uninodes->thermald_ibat);
	veneer_voter_unregister(&uninodes->thermald_idc);
	// For Hiddenm
	veneer_voter_unregister(&uninodes->charging_enable[0]); // 0 for iusb,
	veneer_voter_unregister(&uninodes->charging_enable[1]); // 1 for idc,
}

static bool voter_register(struct unified_nodes* uninodes) {
	bool ret = true;

// For Sysnode
	struct device_node* child;
	int i = 0;
	for_each_child_of_node(uninodes->devnode_restrictchg, child) {
		if (i < RESTRICTION_MAX_COUNT) {
			struct voter_entry* restrict_voter = &uninodes->charging_restriction[i];
			const char* restrict_name;
			const char* restrict_type;

			if (ret && !of_property_read_string(child, "restrict-name", &restrict_name)
				&& !of_property_read_string(child, "restrict-type", &restrict_type)
				&& strlen(restrict_name) < RESTRICTION_MAX_NAME) {
				ret = ret && veneer_voter_register(restrict_voter, restrict_name, vote_fromdt(restrict_type), false);
			}
			else {
				pr_uninode("Parsing error on restricted charging nodes\n");
				return false;
			}
			++i;
		}
		else {
			pr_uninode("Overflow! Check the count of restrictions\n");
			return false;
		}
	}

	ret = ret
// For Thermald
	&& veneer_voter_register(&uninodes->thermald_iusb, VOTER_NAME_THERMALD, VOTER_TYPE_IUSB, false)
	&& veneer_voter_register(&uninodes->thermald_ibat, VOTER_NAME_THERMALD, VOTER_TYPE_IBAT, false)
	&& veneer_voter_register(&uninodes->thermald_idc, VOTER_NAME_THERMALD, VOTER_TYPE_IDC, false)
// For Hiddenm
	&& veneer_voter_register(&uninodes->charging_enable[0], VOTER_NAME_HIDDENM, VOTER_TYPE_IUSB, true)
	&& veneer_voter_register(&uninodes->charging_enable[1], VOTER_NAME_HIDDENM, VOTER_TYPE_IDC, true);

	if (!ret) {
		pr_uninode("failed to register voters\n");
	}

	return ret;
}

static ssize_t voter_store(struct voter_entry* voter, const char* buf, size_t size) {
	long limit;
	pr_uninode("%s(%s) writes %s\n", voter->name, vote_title(voter->type), buf);

	if ( !(kstrtol(buf, 10, &limit)) && (0<=limit) ) {
		// uninodes->voter_thermald_iusb->limit will be updated to limit_ma
		// after veneer_voter_set
		veneer_voter_set(voter, limit);
	}
	else
		veneer_voter_release(voter);

	return size;
}

static ssize_t voter_show(struct voter_entry* voter, char* buf) {
	return snprintf(buf, PAGE_SIZE, "%d", voter->limit);
}

static bool unified_nodes_devicetree(struct device_node* devnode, struct unified_nodes* uninodes) {
	int buf = 0;

	uninodes->support_fastpl = (!of_property_read_u32(devnode, "lge,feature-charging-parallel", &buf)
		&& buf == 1) ? 0 : -1; /* if parallel charging is not supported, set it to '-1' */
	uninodes->support_fastchg = !of_property_read_u32(devnode, "lge,feature-charging-highspeed", &buf)
		? !!buf : 0;

	uninodes->devnode_restrictchg = of_find_node_by_name(devnode, "lge,restrict-charging");
	uninodes->devnode_fakebatt = of_find_node_by_name(devnode, "lge,fake-battery");
	uninodes->devnode_battage = of_find_node_by_name(devnode, "lge,battery-age");

	return uninodes->devnode_restrictchg
		 && uninodes->devnode_fakebatt
		 && uninodes->devnode_battage;
}

static int charging_restriction_limit(struct device_node* parent,
	const char* voter_name, const char* voter_type, const char* command) {
	int limit = VOTE_TOTALLY_RELEASED;

	struct device_node*	child;
	for_each_child_of_node(parent, child) {
		const char	*name, *type;
		u32		val;
		int		ret;

		ret = of_property_read_string(child, "restrict-name", &name);
		if (ret != 0) {
			pr_uninode("Failed to read 'restrict-name'(%d)\n", ret);
			break;
		}

		ret = strcmp(voter_name, name);
		if (ret != 0) {
			pr_debug("Continue to search %s\n", name);
			continue;
		}

		ret = of_property_read_string(child, "restrict-type", &type);
		if (ret != 0) {
			pr_uninode("Failed to read 'restrict-type'(%d)\n", ret);
			break;
		}

		ret = strcmp(voter_type, type);
		if (ret != 0) {
			pr_debug("Continue to search %s\n", type);
			continue;
		}

		ret = of_property_match_string(child, "restrict-commands", command);
		if (ret < 0) {
			pr_uninode("Failed to read 'restrict-commands' %s, (%d)\n", command, ret);
			break;
		}

		ret = of_property_read_u32_index(child, "restrict-values", ret, &val);
		if (ret != 0) {
			pr_uninode("Failed to read 'restrict-values'[%s], (%d)\n", command, ret);
			break;
		}

		if (val != 0) {
			pr_uninode("Finally success to find limit value %d for '%s(%s) %s'\n",
				val, voter_name, voter_type, command);
			limit = (int)val;
			break;
		}
	}

	return limit;
}
static ssize_t charging_restriction_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes* uninodes = (struct unified_nodes*)dev->platform_data;
		struct voter_entry* restrictions = uninodes->charging_restriction;
		const char* space = strchr(buf, ' ');
		char cmd[16] = { 0, };
		int i;

		strncpy(cmd, space+1, 15);
		strreplace(cmd, '\n', '\0');

		for (i=0; i<RESTRICTION_MAX_COUNT; ++i) {
			struct voter_entry* iter = &restrictions[i];

			if (!strncasecmp(buf, iter->name, space-buf)) {
				int limit = charging_restriction_limit(uninodes->devnode_restrictchg,
					iter->name, vote_title(iter->type), cmd);

				pr_uninode("%s votes %d to %s\n", iter->name, limit, vote_title(iter->type));
				veneer_voter_set(iter, limit);
			}
		}
	}
	else
		pr_uninode("Error on setting restriction\n");

	return size;
}
static ssize_t charging_restriction_show(struct device* dev, struct device_attribute* attr, char* buf) {
	// Do print logs, but Don't return anything

	int ret = -1;
	if (dev && dev->platform_data) {
		struct voter_entry* restrictions = ((struct unified_nodes*)dev->platform_data)->charging_restriction;
		int i;

		for (i=0; i<RESTRICTION_MAX_COUNT; ++i) {
			struct voter_entry* restriction = &restrictions[i];
			if (restriction->type != VOTER_TYPE_INVALID) {
				if (!strcmp(restriction->name, "LCD"))
					ret = (restriction->limit == VOTE_TOTALLY_RELEASED) ?
						-1 : restriction->limit;
				pr_uninode("voting Name=%s, Value=%d\n", restriction->name,
					(restriction->limit == VOTE_TOTALLY_RELEASED ? -1 : restriction->limit));
			}
		}
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t thermald_iusb_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct voter_entry* iusb = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_iusb.type == VOTER_TYPE_IUSB)
		iusb = &((struct unified_nodes*)dev->platform_data)->thermald_iusb;
	else
		pr_uninode("Error on getting voter\n");

	return iusb ? voter_store(iusb, buf, size) : size;
}
static ssize_t thermald_iusb_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct voter_entry* iusb = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_iusb.type == VOTER_TYPE_IUSB)
		iusb = &((struct unified_nodes*)dev->platform_data)->thermald_iusb;
	else
		pr_uninode("Error on getting voter\n");

	return iusb ? voter_show(iusb, buf) : 0;
}

static ssize_t thermald_ibat_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct voter_entry* ibat = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_ibat.type == VOTER_TYPE_IBAT)
		ibat = &((struct unified_nodes*)dev->platform_data)->thermald_ibat;
	else
		pr_uninode("Error on getting voter\n");

	return ibat ? voter_store(ibat, buf, size) : size;
}
static ssize_t thermald_ibat_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct voter_entry* ibat = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_ibat.type == VOTER_TYPE_IBAT)
		ibat = &((struct unified_nodes*)dev->platform_data)->thermald_ibat;
	else
		pr_uninode("Error on getting voter\n");

	return ibat ? voter_show(ibat, buf) : 0;
}

static ssize_t thermald_idc_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct voter_entry* idc = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_idc.type == VOTER_TYPE_IDC)
		idc = &((struct unified_nodes*)dev->platform_data)->thermald_idc;
	else
		pr_uninode("Error on getting voter\n");

	return idc ? voter_store(idc, buf, size) : size;
}
static ssize_t thermald_idc_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct voter_entry* idc = NULL;

	if (dev && dev->platform_data &&
		((struct unified_nodes*)dev->platform_data)->thermald_idc.type == VOTER_TYPE_IDC)
		idc = &((struct unified_nodes*)dev->platform_data)->thermald_idc;
	else
		pr_uninode("Error on getting voter\n");

	return idc ? voter_show(idc, buf) : 0;
}

static ssize_t thermald_vdc_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct power_supply* wireless = power_supply_get_by_name("wireless");

	if (dev && dev->platform_data) {
		int value = -1;
		sscanf(buf, "%d", &value);

		if (wireless) {
			union power_supply_propval voltage = { .intval = value, };
			power_supply_set_property(wireless, POWER_SUPPLY_PROP_VOLTAGE_MAX, &voltage);
			power_supply_put(wireless);
		}
	} else
		pr_uninode("Error on getting voter\n");

	return size;
}
static ssize_t thermald_vdc_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct power_supply* wireless = power_supply_get_by_name("wireless");
	union power_supply_propval voltage = { .intval = 0, };

	if (dev && dev->platform_data) {
		if (wireless) {
			power_supply_get_property(wireless, POWER_SUPPLY_PROP_VOLTAGE_MAX, &voltage);
			power_supply_put(wireless);
		}
	} else
		pr_uninode("Error on getting voter\n");

	return snprintf(buf, PAGE_SIZE, "%d", voltage.intval);
}

static ssize_t charging_enable_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		struct voter_entry*	iusb = &ref->charging_enable[0];
		struct voter_entry*	idc = &ref->charging_enable[1];

		if (iusb->type == VOTER_TYPE_IUSB && idc->type == VOTER_TYPE_IDC) {
			int enable = true;
			int limit;
			sscanf(buf, "%d", &enable);
			limit = !!enable ? VOTE_TOTALLY_RELEASED : VOTE_TOTALLY_BLOCKED;

			veneer_voter_set(iusb, limit);
			veneer_voter_set(idc, limit);

			pr_uninode("Success to set charging_enabled\n");
		}
	}

	return size;
}
static ssize_t charging_enable_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 1;
	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		struct voter_entry*	iusb = &ref->charging_enable[0];
		struct voter_entry*	idc = &ref->charging_enable[1];

		if (iusb->type == VOTER_TYPE_IUSB && idc->type == VOTER_TYPE_IDC
			&& iusb->limit == idc->limit) {
			ret = (iusb->limit == VOTE_TOTALLY_RELEASED);
			pr_uninode("Success to get charging_enabled (%d)\n", ret);
		}
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t charging_step_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	enum charging_step_index {
		DISCHG = 0,
		TRICKLE,
		PRECHARGING,
		FAST,
		FULLON,
		TAPER,
		EOC,
		INHIBIT,
	};
	static const char* const charging_step_string [] = {
		"0 DISCHG",
		"1 TRICKLE",
		"2 PRECHARGING",
		"3 FAST",
		"4 FULLON",
		"5 TAPER",
		"6 EOC",
		"7 INHIBIT",
	};
	const char* ret = charging_step_string[DISCHG];

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		int			new;
		sscanf(buf, "%d", &new);

		switch (new) {
		case CHARGING_STEP_TRICKLE:	ret = charging_step_string[TRICKLE];
			break;
		case CHARGING_STEP_CC:		ret = charging_step_string[FAST];
			break;
		case CHARGING_STEP_CV:		ret = charging_step_string[TAPER];
			break;
		case CHARGING_STEP_TERMINATED:	ret = charging_step_string[EOC];
			break;
		default:
			break;
		}

		ref->charging_step = ret;
	}

	return size;
}
static ssize_t charging_step_show(struct device* dev, struct device_attribute* attr, char* buf) {
	const char* ret = "0 DISCHG";

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		if (ref->charging_step)
			ret = ref->charging_step;
	}

	return snprintf(buf, PAGE_SIZE, "%s", ret);
}

static ssize_t charging_showcase_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		bool*			ori = &ref->charging_showcase;
		int			new;

		sscanf(buf, "%d", &new);
		new = !!new;
		if (*ori != new) {
			*ori = new;
			protection_showcase_update();
		}
	}

	return size;
}
static ssize_t charging_showcase_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->charging_showcase;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t charging_completed_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 1;
	if (dev && dev->platform_data) {
		struct power_supply* battery = power_supply_get_by_name("battery");

		if (battery) {
			union power_supply_propval capacity = { .intval = 0, };
			if (!power_supply_get_property(battery, POWER_SUPPLY_PROP_CAPACITY, &capacity)
				&& capacity.intval == 100)
				ret = 0;
			power_supply_put(battery);
		}
	}
	pr_uninode("returning chcomp to %d", ret);
	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t fake_battery_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		bool*			ori = &ref->fake_battery;
		int			new;

		const char* fake_psy;
		int fake_temperature, fake_mvoltage, fake_capacity;

		sscanf(buf, "%d", &new);
		new = !!new;
		if (*ori != new
			&& !of_property_read_string(ref->devnode_fakebatt, "fakebatt-psy", &fake_psy)
			&& !of_property_read_u32(ref->devnode_fakebatt, "fakebatt-temperature", &fake_temperature)
			&& !of_property_read_u32(ref->devnode_fakebatt, "fakebatt-mvoltage", &fake_mvoltage)
			&& !of_property_read_u32(ref->devnode_fakebatt, "fakebatt-capacity", &fake_capacity)) {

			struct power_supply* psy = power_supply_get_by_name(fake_psy);
			const union power_supply_propval
				prp_temp = { .intval = new ? fake_temperature : -1, },
				prp_capacity = { .intval = new ? fake_capacity : -1, },
				prp_voltagenow = { .intval = new ? fake_mvoltage*1000 : -1, };

			if (psy) {
				if (!power_supply_set_property(psy, POWER_SUPPLY_PROP_TEMP, &prp_temp)
					&& !power_supply_set_property(psy, POWER_SUPPLY_PROP_CAPACITY, &prp_capacity)
					&& !power_supply_set_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &prp_voltagenow))
					*ori = new;
				power_supply_put(psy);
			}
		}
	}

	return size;
}
static ssize_t fake_battery_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->fake_battery;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t fake_sdpmax_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		bool*			ori = &ref->fake_sdpmax;
		int			new;

		sscanf(buf, "%d", &new);
		new = !!new;
		if (*ori != new) {
			*ori = new;
			charging_ceiling_sdpmax(new);
		}
	}

	return size;
}
static ssize_t fake_sdpmax_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->fake_sdpmax;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t fake_hvdcp_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct power_supply*	psy = power_supply_get_by_name("usb");
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		bool*			ori = &ref->fake_hvdcp;
		int			new;

		sscanf(buf, "%d", &new);
		new = !!new;

		if (*ori != new) {
			if (psy) {
				union power_supply_propval cmd = { .intval = new, };
				*ori = new;
				power_supply_set_property(psy, POWER_SUPPLY_PROP_USB_HC, &cmd);
				power_supply_put(psy);
			}
		}
	}

	return size;
}
static ssize_t fake_hvdcp_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->fake_hvdcp;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t battery_age_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		const char* 		psy;

		if (!of_property_read_string(ref->devnode_battage, "battage-psy", &psy)) {
			struct power_supply* capacity_psy = power_supply_get_by_name(psy);
			union power_supply_propval capacity_designed = { .intval = 0, },
				capacity_learned = { .intval = 0, };

			if (capacity_psy
				&& !power_supply_get_property(capacity_psy,
					POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN, &capacity_designed)
				&& !power_supply_get_property(capacity_psy,
					POWER_SUPPLY_PROP_CHARGE_FULL, &capacity_learned)
				&& capacity_designed.intval > 0) {

				ret = min((capacity_learned.intval * 100) / capacity_designed.intval,
					100);

				pr_uninode("battery age %d = %d/%d\n",
					ret, capacity_learned.intval, capacity_designed.intval);
			}
			else
				pr_uninode("Calculating battery age is not ready\n");

			if (capacity_psy)
				power_supply_put(capacity_psy);
		}
		else
			pr_uninode("Check the device tree\n");
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}
static ssize_t battery_condition_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int age;	// ratio type for battery age, i.e 95(%)
	int ret = 0;

	if (!!battery_age_show(dev, attr, buf) && !!sscanf(buf, "%d", &age) && age >= 0) {
		int age_thresold_best = 80;
		int age_thresold_good = 50;
		int age_thresold_bad = 0;
		int condition_ret_best = 1;
		int condition_ret_good = 2;
		int condition_ret_bad = 3;
		int condition_ret_abnormal = 0;

		if (age >= age_thresold_best)
			ret = condition_ret_best;
		else if (age >= age_thresold_good)
			ret = condition_ret_good;
		else if (age >= age_thresold_bad)
			ret = condition_ret_bad;
		else
			ret = condition_ret_abnormal;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t battery_cycle_show(struct device* dev, struct device_attribute* attr, char* buf)
{
	int ret = -1;
	if (dev && dev->platform_data) {
		struct unified_nodes* ref = (struct unified_nodes*)dev->platform_data;
		const char* psy = NULL;

		if (!of_property_read_string(ref->devnode_battage, "battage-psy", &psy)) {
			struct power_supply* battage_psy = power_supply_get_by_name(psy);
			union power_supply_propval cycle_count = { .intval = 0, };

			if (battage_psy
				&& !power_supply_get_property(battage_psy,
					POWER_SUPPLY_PROP_CYCLE_COUNT, &cycle_count))
				ret = cycle_count.intval;

			if (battage_psy)
				power_supply_put(battage_psy);
		}
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t battery_valid_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		bool*	ori = &((struct unified_nodes*)dev->platform_data)->battery_valid;
		int	new;
		sscanf(buf, "%d", &new);
		*ori = !!new;
	}

	return size;
}
static ssize_t battery_valid_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->battery_valid;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t charger_name_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		char* ori = ((struct unified_nodes*)dev->platform_data)->charger_name;
		strcpy(ori, buf);
		ori[strlen(buf)] = '\0';
	}

	return size;
}
static ssize_t charger_name_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		char* ori = ((struct unified_nodes*)dev->platform_data)->charger_name;
		strncpy(buf, ori, strlen(ori));
		ret = strlen(ori);
	}

	return ret;
}

static ssize_t charger_highspeed_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("Storing %s\n", buf);

	if (dev && dev->platform_data) {
		bool*	ori = &((struct unified_nodes*)dev->platform_data)->charger_highspeed;
		int	new;
		sscanf(buf, "%d", &new);
		*ori = !!new;
	}

	return size;
}
static ssize_t charger_highspeed_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->charger_highspeed;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t charger_incompatible_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	static bool container = false;

	if (dev && dev->platform_data) {
		bool**	ori = &((struct unified_nodes*)dev->platform_data)->charger_incompatible;
		int	new;
		if (!(*ori))
			*ori = &container;

		sscanf(buf, "%d", &new);
		container = !!new;
	}

	pr_uninode("Storing %s\n", buf);
	return size;
}
static ssize_t charger_incompatible_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = UNIFIED_NODES_DISABLED;
	if (dev && dev->platform_data) {
		bool* val = ((struct unified_nodes*)dev->platform_data)->charger_incompatible;
		if (val)
			ret = !!(*val);
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t charger_usbid_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct power_supply* 		usb = power_supply_get_by_name("usb");
	union power_supply_propval	prp = { .intval = 0, };

	if (dev && dev->platform_data) {
		if (usb) {
			power_supply_get_property(usb, POWER_SUPPLY_PROP_RESISTANCE, &prp);
			power_supply_put(usb);
		}
	}

	pr_uninode("returning usbid(mv) is %d\n", prp.intval);
	return snprintf(buf, PAGE_SIZE, "Result:%d Raw:ffff\n", prp.intval);
}

static ssize_t charger_verbose_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	static enum charging_verbosity container = VERBOSE_CHARGER_NONE;

	if (dev && dev->platform_data) {
		enum charging_verbosity** ori = &((struct unified_nodes*)dev->platform_data)->charger_verbose;
		int			  new;
		if (!(*ori))
			*ori = &container;

		sscanf(buf, "%d", &new);
		container = new;
	}

	pr_uninode("Storing %s\n", buf);
	return size;
}
static ssize_t charger_verbose_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = UNIFIED_NODES_DISABLED;
	if (dev && dev->platform_data) {
		enum charging_verbosity* val = ((struct unified_nodes*)dev->platform_data)->charger_verbose;
		if (val)
			ret = *val;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t support_fastpl_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	pr_uninode("FASTPL: Storing %s\n", buf);

	if (dev && dev->platform_data) {
		struct unified_nodes*	ref = (struct unified_nodes*)dev->platform_data;
		int*			ori = &ref->support_fastpl;
		int			new = 0;
		sscanf(buf, "%d", &new);

		// If parallel charging is not supported (*ori == -1),
		// then do not update it via this command.
		if (*ori >= 0 && new >= 0 && *ori != new) {
			*ori = new;
		}
	}
	return size;
}
static ssize_t support_fastpl_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->support_fastpl;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static ssize_t support_fastchg_show(struct device* dev, struct device_attribute* attr, char* buf) {
	int ret = 0;
	if (dev && dev->platform_data) {
		ret = ((struct unified_nodes*)dev->platform_data)->support_fastchg;
	}

	return snprintf(buf, PAGE_SIZE, "%d", ret);
}

static struct device_attribute unified_nodes_dattrs [] = {
	__ATTR(thermald_iusb,		0664, thermald_iusb_show,		thermald_iusb_store),
	__ATTR(thermald_ibat,		0664, thermald_ibat_show,		thermald_ibat_store),
	__ATTR(thermald_idc,		0664, thermald_idc_show,		thermald_idc_store),
	__ATTR(thermald_vdc,		0664, thermald_vdc_show,		thermald_vdc_store),
	__ATTR(charging_restriction,	0664, charging_restriction_show,	charging_restriction_store),
	__ATTR(charging_enable,		0664, charging_enable_show,		charging_enable_store),
	__ATTR(charging_step,		0644, charging_step_show,		charging_step_store),
	__ATTR(charging_showcase,	0664, charging_showcase_show,		charging_showcase_store),
	__ATTR(charging_completed,	0444, charging_completed_show,		NULL),
	__ATTR(fake_battery,		0664, fake_battery_show,		fake_battery_store),
	__ATTR(fake_sdpmax,		0664, fake_sdpmax_show,			fake_sdpmax_store),
	__ATTR(fake_hvdcp,		0664, fake_hvdcp_show, 			fake_hvdcp_store),
	__ATTR(battery_age,		0444, battery_age_show,			NULL),
	__ATTR(battery_condition,	0444, battery_condition_show,		NULL),
	__ATTR(battery_cycle,		0444, battery_cycle_show,		NULL),
	__ATTR(battery_valid,		0664, battery_valid_show,		battery_valid_store),
	__ATTR(charger_name,		0664, charger_name_show,		charger_name_store),
	__ATTR(charger_highspeed,	0664, charger_highspeed_show,		charger_highspeed_store),
	__ATTR(charger_incompatible,	0664, charger_incompatible_show,	charger_incompatible_store),
	__ATTR(charger_usbid,		0444, charger_usbid_show,		NULL),
	__ATTR(charger_verbose,		0664, charger_verbose_show,		charger_verbose_store),
	__ATTR(support_fastpl,		0664, support_fastpl_show,		support_fastpl_store),
	__ATTR(support_fastchg,		0444, support_fastchg_show,		NULL),
};

static struct platform_device unified_nodes_device = {
	.name = UNIFIED_NODES_DEVICE,
	.id = -1,	// Set -1 explicitly to make device name simple
	.dev = {
		.platform_data = NULL,
	}
};

static void unified_nodes_clear(/*@Nonnull*/ struct unified_nodes* uninodes) {
	memset(uninodes, 0, sizeof(struct unified_nodes));
	// It makes all the voter_entry.type to INVALID(0)
}

static struct device_attribute* unified_nodes_search(const char* key) {
       /*	struct device_attribute {
	*		struct attribute attr;
	*		ssize_t (*show)(struct device* dev, struct device_attribute* attr, char* buf);
	*		ssize_t (*store)(struct device* dev, struct device_attribute* attr, const char* buf, size_t count);
	*	};
	*	struct attribute {
	*		const char* name;
	*		struct module* owner;
	*		mode_t mode;
	*	};
	*	#define __ATTR(_name,_mode,_show,_store) { .attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE }, .show = _show, .store = _store, }
	*/
	int i;
	for (i=0; i<ARRAY_SIZE(unified_nodes_dattrs); ++i) {
		if (!strcmp(unified_nodes_dattrs[i].attr.name, key)) {
			return &unified_nodes_dattrs[i];
		}
	}

	return NULL;
}

bool unified_nodes_store(const char* key, const char* value, size_t size) {
	struct device* device = &unified_nodes_device.dev;
	struct device_attribute* dattr = unified_nodes_search(key);
	if (dattr) {
		dattr->store(device, dattr, value, size);
		return true;
	}
	else
		return false;
}

bool unified_nodes_show(const char* key, char* value) {
	struct device* device = &unified_nodes_device.dev;
	struct device_attribute* dattr = unified_nodes_search(key);
	if (dattr) {
		dattr->show(device, dattr, value);
		return true;
	}
	else
		return false;
}

static struct attribute_group* unified_nodes_attrgroup(void) {
	static struct attribute*	s_attrs [ARRAY_SIZE(unified_nodes_dattrs) + 1];
	static struct attribute_group	s_files = { .attrs  = NULL, };

	if (s_files.attrs == NULL) {
		int i;
		s_files.attrs = s_attrs;

		for (i=0; i<ARRAY_SIZE(unified_nodes_dattrs); ++i)
			s_attrs[i] = &unified_nodes_dattrs[i].attr;
		s_attrs[i] = NULL;
	}

	return &s_files;
}

bool unified_nodes_create(struct device_node* devnode) {
	struct unified_nodes* uninodes = kzalloc(sizeof(struct unified_nodes), GFP_KERNEL);
	pr_uninode("Creating unified nodes\n");

	if (!uninodes) {
		pr_uninode("Failed to alloc unified_nodes\n");
		goto failed;
	}
	else
		unified_nodes_device.dev.platform_data = uninodes;

	if (!unified_nodes_devicetree(devnode, uninodes)) {
		pr_uninode("Failed to parse restrictions\n");
		goto failed;
	}
	if (!voter_register(uninodes)) {
		pr_uninode("Failed to preset voters\n");
		goto failed;
	}
	if (sysfs_create_group(&unified_nodes_device.dev.kobj, unified_nodes_attrgroup())) {
		pr_uninode("Failed to create sysfs voters\n");
		goto failed;
	}

	pr_uninode("Success to create unified nodes\n");
	return true;

failed:	unified_nodes_destroy();
	return false;
}

void unified_nodes_destroy(void) {
	pr_uninode("Destroying unified nodes\n");

	if (unified_nodes_device.dev.platform_data) {
		struct unified_nodes* uninodes = unified_nodes_device.dev.platform_data;

		voter_unregister(uninodes);
		unified_nodes_clear(uninodes);
		kfree(uninodes);

		unified_nodes_device.dev.platform_data = NULL;
	}

	sysfs_remove_group(&unified_nodes_device.dev.kobj, unified_nodes_attrgroup());
}

static int __init unified_nodes_init(void) {
	pr_uninode("platform_device_register : unified_nodes_device\n");
	return platform_device_register(&unified_nodes_device);
}

static void __exit unified_nodes_exit(void) {
	pr_uninode("platform_device_unregister : unified_nodes_device\n");
	platform_device_unregister(&unified_nodes_device);
}

module_init(unified_nodes_init);
module_exit(unified_nodes_exit);
