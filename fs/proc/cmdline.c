#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/setup.h>
#include <soc/qcom/lge/board_lge.h>
#include <linux/slab.h>

static char updated_command_line[COMMAND_LINE_SIZE];
static char new_command_line[COMMAND_LINE_SIZE];

static void proc_cmdline_set(char *name, char *value)
{
	char *flag_pos, *flag_after;
	char *flag_pos_str = kmalloc(sizeof(char), COMMAND_LINE_SIZE);
	scnprintf(flag_pos_str, COMMAND_LINE_SIZE, "%s=", name);
	flag_pos = strstr(updated_command_line, flag_pos_str);
	if (flag_pos) {
		flag_after = strchr(flag_pos, ' ');
		if (!flag_after)
			flag_after = "";
		scnprintf(updated_command_line, COMMAND_LINE_SIZE, "%.*s%s=%s%s",
				(int)(flag_pos - updated_command_line),
				updated_command_line, name, value, flag_after);
	} else {
		// flag was found, insert it
		scnprintf(updated_command_line, COMMAND_LINE_SIZE, "%s %s=%s", updated_command_line, name, value);
	}
}

static int cmdline_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_MACH_LGE
	if (lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO) {
		proc_cmdline_set("androidboot.mode", "charger");
	}
#endif
	seq_printf(m, "%s\n", updated_command_line);
	seq_printf(m, "%s\n", new_command_line);
	return 0;
}

static int cmdline_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, cmdline_proc_show, NULL);
}

static const struct file_operations cmdline_proc_fops = {
	.open		= cmdline_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void patch_flag(char *cmd, const char *flag, const char *val)
{
	size_t flag_len, val_len;
	char *start, *end;

	start = strstr(cmd, flag);
	if (!start)
		return;

	flag_len = strlen(flag);
	val_len = strlen(val);
	end = start + flag_len + strcspn(start + flag_len, " ");
	memmove(start + flag_len + val_len, end, strlen(end) + 1);
	memcpy(start + flag_len, val, val_len);
}

static void patch_safetynet_flags(char *cmd)
{
	patch_flag(cmd, "androidboot.flash.locked=", "1");
	patch_flag(cmd, "androidboot.verifiedbootstate=", "green");
	patch_flag(cmd, "androidboot.veritymode=", "enforcing");
}

static int __init proc_cmdline_init(void)
{
	// copy it only once
	strcpy(updated_command_line, saved_command_line);
	strcpy(new_command_line, saved_command_line);

	/*
	 * Patch various flags from command line seen by userspace in order to
	 * pass SafetyNet checks.
	 */
	patch_safetynet_flags(new_command_line);

	proc_create("cmdline", 0, NULL, &cmdline_proc_fops);
	return 0;
}
fs_initcall(proc_cmdline_init);
