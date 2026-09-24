/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_REBOOT_H
#define _LINUX_REBOOT_H


#include <linux/notifier.h>
#include <uapi/linux/reboot.h>

struct device;
struct sys_off_handler;

#define SYS_DOWN	0x0001	/* Notify of system down */
#define SYS_RESTART	SYS_DOWN
#define SYS_HALT	0x0002	/* Notify of system halt */
#define SYS_POWER_OFF	0x0003	/* Notify of system power off */

enum reboot_mode {
	REBOOT_UNDEFINED = -1,
	REBOOT_COLD = 0,
	REBOOT_WARM,
	REBOOT_HARD,
	REBOOT_SOFT,
	REBOOT_GPIO,
};
extern enum reboot_mode reboot_mode;
extern enum reboot_mode panic_reboot_mode;

enum reboot_type {
	BOOT_TRIPLE	= 't',
	BOOT_KBD	= 'k',
	BOOT_BIOS	= 'b',
	BOOT_ACPI	= 'a',
	BOOT_EFI	= 'e',
	BOOT_CF9_FORCE	= 'p',
	BOOT_CF9_SAFE	= 'q',
};
extern enum reboot_type reboot_type;

extern int reboot_default;
extern int reboot_cpu;
extern int reboot_force;


extern int register_reboot_notifier(struct notifier_block *);
extern int unregister_reboot_notifier(struct notifier_block *);

extern int devm_register_reboot_notifier(struct device *, struct notifier_block *);

extern int register_restart_handler(struct notifier_block *);
extern int unregister_restart_handler(struct notifier_block *);
extern void do_kernel_restart(char *cmd);

/*
 * Architecture-specific implementations of sys_reboot commands.
 */

extern void migrate_to_reboot_cpu(void);
extern void machine_restart(char *cmd);
extern void machine_halt(void);
extern void machine_power_off(void);

extern void machine_shutdown(void);
struct pt_regs;
extern void machine_crash_shutdown(struct pt_regs *);

void do_kernel_power_off(void);

/*
 * sys-off handler API.
 */

/*
 * Standard sys-off priority levels. Users are expected to set priorities
 * relative to the standard levels.
 *
 * SYS_OFF_PRIO_PLATFORM:	Use this for platform-level handlers.
 *
 * SYS_OFF_PRIO_LOW:		Use this for handler of last resort.
 *
 * SYS_OFF_PRIO_DEFAULT:	Use this for normal handlers.
 *
 * SYS_OFF_PRIO_HIGH:		Use this for higher priority handlers.
 *
 * SYS_OFF_PRIO_FIRMWARE:	Use this if handler uses firmware call.
 */
#define SYS_OFF_PRIO_PLATFORM		-256
#define SYS_OFF_PRIO_LOW		-128
#define SYS_OFF_PRIO_DEFAULT		0
#define SYS_OFF_PRIO_HIGH		192
#define SYS_OFF_PRIO_FIRMWARE		224

enum sys_off_mode {
	/**
	 * @SYS_OFF_MODE_POWER_OFF_PREPARE:
	 *
	 * Handlers prepare system to be powered off. Handlers are
	 * allowed to sleep.
	 */
	SYS_OFF_MODE_POWER_OFF_PREPARE,

	/**
	 * @SYS_OFF_MODE_POWER_OFF:
	 *
	 * Handlers power-off system. Handlers are disallowed to sleep.
	 */
	SYS_OFF_MODE_POWER_OFF,

	/**
	 * @SYS_OFF_MODE_RESTART_PREPARE:
	 *
	 * Handlers prepare system to be restarted. Handlers are
	 * allowed to sleep.
	 */
	SYS_OFF_MODE_RESTART_PREPARE,

	/**
	 * @SYS_OFF_MODE_RESTART:
	 *
	 * Handlers restart system. Handlers are disallowed to sleep.
	 */
	SYS_OFF_MODE_RESTART,
};

/**
 * struct sys_off_data - sys-off callback argument
 *
 * @mode: Mode ID. Currently used only by the sys-off restart mode,
 *        see enum reboot_mode for the available modes.
 * @cb_data: User's callback data.
 * @cmd: Command string. Currently used only by the sys-off restart mode,
 *       NULL otherwise.
 * @dev: Device of the sys-off handler. Only if known (devm_register_*),
 *       NULL otherwise.
 */
struct sys_off_data {
	int mode;
	void *cb_data;
	const char *cmd;
	struct device *dev;
};

struct sys_off_handler *
register_sys_off_handler(enum sys_off_mode mode,
			 int priority,
			 int (*callback)(struct sys_off_data *data),
			 void *cb_data);
void unregister_sys_off_handler(struct sys_off_handler *handler);

int devm_register_sys_off_handler(struct device *dev,
				  enum sys_off_mode mode,
				  int priority,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data);

int devm_register_power_off_handler(struct device *dev,
				    int (*callback)(struct sys_off_data *data),
				    void *cb_data);

int devm_register_restart_handler(struct device *dev,
				  int (*callback)(struct sys_off_data *data),
				  void *cb_data);

int register_platform_power_off(void (*power_off)(void));
void unregister_platform_power_off(void (*power_off)(void));

/*
 * Architecture independent implemenations of sys_reboot commands.
 */

extern void kernel_restart_prepare(char *cmd);
extern void kernel_restart(char *cmd);
extern void kernel_halt(void);
extern void kernel_power_off(void);
extern bool kernel_can_power_off(void);

void ctrl_alt_del(void);

extern void orderly_poweroff(bool force);
extern void orderly_reboot(void);


/**
 * enum psc_reason - Enumerates reasons for power state changes.
 *
 * This enum defines various reasons why a system might transition into a
 * shutdown, reboot, or kexec state. While originally intended for hardware
 * protection events, `psc_reason` can be extended to track other system
 * transitions, such as controlled reboots triggered by software or
 * maintenance operations.
 *
 * The values in this enumeration provide structured and standardized
 * identifiers that replace free-form string descriptions. They are designed
 * to be stored efficiently, making them suitable for use in environments
 * with limited storage, such as battery-backed RTC registers, non-volatile
 * memory, or bootloader communication mechanisms.
 *
 * Importantly, the order of these values **must remain stable**, as
 * bootloaders, user-space tools, or post-mortem investigation utilities
 * may rely on their numerical representation for consistent behavior.
 *
 * @PSCR_UNKNOWN: Unknown or unspecified reason for the power state change.
 *	This value serves as a default when no explicit cause is recorded.
 *
 * @PSCR_UNDER_VOLTAGE: Shutdown or reboot triggered due to supply voltage
 *      dropping below a safe threshold. This helps prevent instability or
 *      corruption caused by insufficient power.
 *
 * @PSCR_OVER_CURRENT: System shutdown or reboot due to excessive current draw,
 *      which may indicate a short circuit, an overloaded power rail, or other
 *      hardware faults requiring immediate action.
 *
 * @PSCR_REGULATOR_FAILURE: A critical failure in a voltage regulator, causing
 *      improper power delivery. This may be due to internal component failure,
 *      transient conditions, or external load issues requiring mitigation.
 *
 * @PSCR_OVER_TEMPERATURE: System shutdown or reboot due to excessive thermal
 *	conditions. This attempts to prevent hardware damage when temperature
 *	sensors detect unsafe levels, often impacting CPUs, GPUs, or power
 *	components.
 *
 * @PSCR_EC_PANIC: Shutdown or reboot triggered by an Embedded Controller (EC)
 *	panic. The EC is a microcontroller responsible for low-level system
 *	management, including power sequencing, thermal control, and battery
 *	management. An EC panic may indicate critical firmware issues, power
 *	management errors, or an unrecoverable hardware fault requiring
 *	immediate response.
 *
 * @PSCR_POWER_ON: Regular cold power-on (e.g. via a PMIC power-on request).
 *
 * @PSCR_WATCHDOG: Reset caused by a watchdog timeout.
 *
 * @PSCR_SOFTWARE: Software-initiated reset or reboot.
 *
 * @PSCR_EXTERNAL: Reset asserted through an external pin or reset input.
 *
 * @PSCR_RTC: Wake-up or power-on triggered by the RTC.
 *
 * @PSCR_RESET_BUTTON: Reset triggered by a user reset button.
 *
 * @PSCR_CPU_CLK_FAIL: Reset caused by a CPU clock failure.
 *
 * @PSCR_XTAL_FAIL: Reset caused by a crystal oscillator failure.
 *
 * @PSCR_REASON_COUNT: Number of defined power state change reasons. This
 *	value is useful for range checking and potential future extensions
 *	while maintaining compatibility.
 *
 * The reasons mirror the POWER_ON_REASON_* strings in
 * <linux/power/power_on_reason.h>; keep the two in sync when extending.
 */
enum psc_reason {
	PSCR_UNKNOWN,
	PSCR_UNDER_VOLTAGE,
	PSCR_OVER_CURRENT,
	PSCR_REGULATOR_FAILURE,
	PSCR_OVER_TEMPERATURE,
	PSCR_EC_PANIC,
	/* Append new reasons here; the numeric order is ABI. */
	PSCR_POWER_ON,
	PSCR_WATCHDOG,
	PSCR_SOFTWARE,
	PSCR_EXTERNAL,
	PSCR_RTC,
	PSCR_RESET_BUTTON,
	PSCR_CPU_CLK_FAIL,
	PSCR_XTAL_FAIL,

	/* Number of reasons */
	PSCR_REASON_COUNT,
};

#define PSCR_MAX_REASON	(PSCR_REASON_COUNT - 1)

const char *psc_reason_to_str(enum psc_reason reason);
const char *psc_reason_to_token(enum psc_reason reason);
int psc_reason_from_token(const char *token, enum psc_reason *reason);

/**
 * enum hw_protection_action - Hardware protection action
 *
 * @HWPROT_ACT_DEFAULT:
 *      The default action should be taken. This is HWPROT_ACT_SHUTDOWN
 *      by default, but can be overridden.
 * @HWPROT_ACT_SHUTDOWN:
 *	The system should be shut down (powered off) for HW protection.
 * @HWPROT_ACT_REBOOT:
 *	The system should be rebooted for HW protection.
 */
enum hw_protection_action { HWPROT_ACT_DEFAULT, HWPROT_ACT_SHUTDOWN, HWPROT_ACT_REBOOT };

void __hw_protection_trigger(enum psc_reason reason, int ms_until_forced,
			     enum hw_protection_action action);

/**
 * hw_protection_trigger - Trigger default emergency system hardware protection action
 *
 * @reason:		Reason of emergency shutdown or reboot.
 * @ms_until_forced:	Time to wait for orderly shutdown or reboot before
 *			triggering it. Negative value disables the forced
 *			shutdown or reboot.
 *
 * Initiate an emergency system shutdown or reboot in order to protect
 * hardware from further damage. The exact action taken is controllable at
 * runtime and defaults to shutdown.
 */
static inline void hw_protection_trigger(enum psc_reason reason,
					 int ms_until_forced)
{
	__hw_protection_trigger(reason, ms_until_forced, HWPROT_ACT_DEFAULT);
}

enum psc_reason get_psc_reason(void);
enum psc_reason get_psc_first_reason(void);
void set_psc_reason(enum psc_reason reason);

/*
 * Emergency restart, callable from an interrupt handler.
 */

extern void emergency_restart(void);
#include <asm/emergency-restart.h>

#endif /* _LINUX_REBOOT_H */
