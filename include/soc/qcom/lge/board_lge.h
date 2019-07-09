#ifndef __ASM_ARCH_MSM_BOARD_LGE_H
#define __ASM_ARCH_MSM_BOARD_LGE_H

#if defined(CONFIG_LGE_DISPLAY_COMMON)
int lge_get_panel(void);
void lge_set_panel(int);
int lge_get_panel_flag_status(void);
int lge_get_uefi_panel_status(void);
#endif
#if defined(CONFIG_LGE_LCD_MFTS_MODE)
extern int lge_get_mfts_mode(void);
#endif

#ifdef CONFIG_LGE_LCD_OFF_DIMMING
extern int lge_get_bootreason_with_lcd_dimming(void);
#endif

#if defined(CONFIG_PRE_SELF_DIAGNOSIS)
int lge_pre_self_diagnosis(char *drv_bus_code, int func_code, char *dev_code, char *drv_code, int errno);
#endif
#if defined(CONFIG_PRE_SELF_DIAGNOSIS)
struct pre_selfd_platform_data {
	int (*set_values) (int r, int g, int b);
	int (*get_values) (int *r, int *g, int *b);
};
#endif

#if defined(CONFIG_MACH_MSM8998_LUCY) || defined(CONFIG_MACH_MSM8998_JOAN)
enum hw_rev_no {
	HW_REV_EVB1 = 0,
	HW_REV_EVB2,
	HW_REV_EVB3,
	HW_REV_0,
	HW_REV_0_1,
	HW_REV_0_2,
	HW_REV_A,
	HW_REV_B,
	HW_REV_C,
	HW_REV_D,
	HW_REV_E,
	HW_REV_F,
	HW_REV_1_0,
	HW_REV_1_1,
	HW_REV_1_2,
	HW_REV_1_3,
	HW_REV_MAX
};
#else
enum hw_rev_no {
	HW_REV_EVB1 = 0,
	HW_REV_EVB2,
	HW_REV_EVB3,
	HW_REV_0,
	HW_REV_0_1,
	HW_REV_A,
	HW_REV_B,
	HW_REV_C,
	HW_REV_D,
	HW_REV_E,
	HW_REV_F,
	HW_REV_G,
	HW_REV_1_0,
	HW_REV_1_1,
	HW_REV_1_2,
	HW_REV_1_3,
	HW_REV_MAX
};
#endif

char *lge_get_board_revision(void);
enum hw_rev_no lge_get_board_rev_no(void);

#ifdef CONFIG_LGE_USB_FACTORY
typedef enum {
	LGE_BOOT_MODE_NORMAL = 0,
	LGE_BOOT_MODE_CHARGER,
	LGE_BOOT_MODE_CHARGERLOGO,
	LGE_BOOT_MODE_QEM_56K,
	LGE_BOOT_MODE_QEM_130K,
	LGE_BOOT_MODE_QEM_910K,
	LGE_BOOT_MODE_PIF_56K,
	LGE_BOOT_MODE_PIF_130K,
	LGE_BOOT_MODE_PIF_910K,
	LGE_BOOT_MODE_MINIOS    /* LGE_UPDATE for MINIOS2.0 */
} lge_boot_mode_t;

typedef enum {
	LGE_FACTORY_CABLE_NONE = 0,
	LGE_FACTORY_CABLE_56K,
	LGE_FACTORY_CABLE_130K,
	LGE_FACTORY_CABLE_910K,
} lge_factory_cable_t;

typedef enum {
	LGE_LAF_MODE_NORMAL = 0,
	LGE_LAF_MODE_LAF,
} lge_laf_mode_t;

typedef enum {
	LT_CABLE_56K = 6,
	LT_CABLE_130K,
	USB_CABLE_400MA,
	USB_CABLE_DTC_500MA,
	ABNORMAL_USB_CABLE_400MA,
	LT_CABLE_910K,
	NONE_INIT_CABLE
} cable_boot_type;

cable_boot_type lge_get_boot_cable(void);
lge_boot_mode_t lge_get_boot_mode(void);
bool lge_get_factory_boot(void);
lge_factory_cable_t lge_get_factory_cable(void);
bool lge_get_android_dlcomplete(void);
lge_laf_mode_t lge_get_laf_mode(void);
#endif

extern int lge_get_bootreason(void);

#ifdef CONFIG_MACH_LGE
bool lge_check_recoveryboot(void);

typedef enum {
	LGE_HYDRA_MODE_ALPHA = 0,
	LGE_HYDRA_MODE_PRIME,
	LGE_HYDRA_MODE_PLUS,
	LGE_HYDRA_MODE_SIGNATURE,
	LGE_HYDRA_MODE_RENEWAL,
	LGE_HYDRA_MODE_NONE,
} lge_hydra_mode_t;
int lge_get_hydra_mode(void);
#endif

#endif /* __ASM_ARCH_MSM_BOARD_LGE_H */
