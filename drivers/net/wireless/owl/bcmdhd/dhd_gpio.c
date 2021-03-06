
#include <osl.h>
#include <dhd_linux.h>

#ifdef CONFIG_MACH_ODROID_4210
/*
#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/sdhci.h>
#include <plat/devs.h>
#define	sdmmc_channel	s3c_device_hsmmc0
*/

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif

#ifdef CONFIG_PLATFORM_OWL
#include <linux/gpio.h>
#include "../../../../mmc/host/owl_wlan_plat_data.h"
extern struct wlan_plat_data *owl_get_wlan_plat_data(void);
#endif

struct resource dhd_wlan_resources = {0};
struct wifi_platform_data dhd_wlan_control = {0};

#ifdef CUSTOMER_OOB
uint bcm_wlan_get_oob_irq(void)
{
	uint host_oob_irq = 0;

#ifdef CONFIG_MACH_ODROID_4210
	struct device_node *np = NULL;
	uint wifi_wakeup_host_gpios = 0;

	np = of_find_compatible_node(NULL, NULL, "wifi,bt,power,ctl");
	if (NULL == np) {
		printk("%s \"wifi,bt,power,ctl\" node found in dts\n", __FUNCTION__);
	} else {
		if (of_find_property(np, "wifi_wakeup_host_gpios", NULL)) {
			wifi_wakeup_host_gpios = of_get_named_gpio(np, "wifi_wakeup_host_gpios", 0);

			if (gpio_is_valid(wifi_wakeup_host_gpios)) {
				printk("GPIO(WL_HOST_WAKE) wifi_wakeup_host_gpios = %d\n", wifi_wakeup_host_gpios);
				host_oob_irq = gpio_to_irq(wifi_wakeup_host_gpios);
				gpio_direction_input(wifi_wakeup_host_gpios);
			} else {
				printk("%s gpio for wifi_wakeup_host_gpios invalid\n", __FUNCTION__);
			}
		}
	}
/*
	printf("GPIO(WL_HOST_WAKE) = EXYNOS4_GPX0(7) = %d\n", EXYNOS4_GPX0(7));
	host_oob_irq = gpio_to_irq(EXYNOS4_GPX0(7));
	gpio_direction_input(EXYNOS4_GPX0(7));
*/
#endif
	printf("host_oob_irq: %d\n", host_oob_irq);

	return host_oob_irq;
}

uint bcm_wlan_get_oob_irq_flags(void)
{
	uint host_oob_irq_flags = 0;

#ifdef CONFIG_MACH_ODROID_4210
#ifdef HW_OOB
#ifdef HW_OOB_LOW_LEVEL
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL | IORESOURCE_IRQ_SHAREABLE;
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE;
#endif
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE | IORESOURCE_IRQ_SHAREABLE;
#endif
#endif

	printf("host_oob_irq_flags=0x%X\n", host_oob_irq_flags);

	return host_oob_irq_flags;
}
#endif

int bcm_wlan_set_power(bool on)
{
	int err = 0;
#ifdef CONFIG_PLATFORM_OWL
	struct wlan_plat_data *pdata = owl_get_wlan_plat_data();
#endif

	if (on) {
		printf("======== PULL WL_REG_ON HIGH! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		gpio_set_value(pdata->wifi_en_gpios, 1);
#endif
		/* Lets customer power to get stable */
		mdelay(100);
	} else {
		printf("======== PULL WL_REG_ON LOW! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		gpio_set_value(pdata->wifi_en_gpios, 0);
#endif
	}

	return err;
}

int bcm_wlan_set_carddetect(bool present)
{
	int err = 0;
#ifdef CONFIG_PLATFORM_OWL
	struct wlan_plat_data *pdata = owl_get_wlan_plat_data();

	if (!(pdata && pdata->set_carddetect)) {
		return -1;
	}
#endif

	if (present) {
		printf("======== Card detection to detect SDIO card! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		err = pdata->set_carddetect(1);
#endif
	} else {
		printf("======== Card detection to remove SDIO card! ========\n");
#ifdef CONFIG_PLATFORM_OWL
		err = pdata->set_carddetect(0);
#endif
	}

	return err;
}

int bcm_wlan_get_mac_address(unsigned char *buf)
{
	int err = 0;

	printf("======== %s ========\n", __FUNCTION__);
#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return err;
}

#ifdef CONFIG_DHD_USE_STATIC_BUF
extern void *bcmdhd_mem_prealloc(int section, unsigned long size);
void* bcm_wlan_prealloc(int section, unsigned long size)
{
	void *alloc_ptr = NULL;
	alloc_ptr = bcmdhd_mem_prealloc(section, size);
	if (alloc_ptr) {
		printf("success alloc section %d, size %ld\n", section, size);
		if (size != 0L)
			bzero(alloc_ptr, size);
		return alloc_ptr;
	}
	printf("can't alloc section %d\n", section);
	return NULL;
}
#endif

#if !defined(WL_WIRELESS_EXT)
struct cntry_locales_custom {
	char iso_abbrev[WLC_CNTRY_BUF_SZ];	/* ISO 3166-1 country abbreviation */
	char custom_locale[WLC_CNTRY_BUF_SZ];	/* Custom firmware locale */
	int32 custom_locale_rev;		/* Custom local revisin default -1 */
};
#endif

static struct cntry_locales_custom brcm_wlan_translate_custom_table[] = {
	/* Table should be filled out based on custom platform regulatory requirement */
	{"",   "XT", 49},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
};

static void *bcm_wlan_get_country_code(char *ccode)
{
	struct cntry_locales_custom *locales;
	int size;
	int i;

	if (!ccode)
		return NULL;

	locales = brcm_wlan_translate_custom_table;
	size = ARRAY_SIZE(brcm_wlan_translate_custom_table);

	for (i = 0; i < size; i++)
		if (strcmp(ccode, locales[i].iso_abbrev) == 0)
			return &locales[i];
	return NULL;
}

int bcm_wlan_set_plat_data(void) {
	printf("======== %s ========\n", __FUNCTION__);
	dhd_wlan_control.set_power = bcm_wlan_set_power;
	dhd_wlan_control.set_carddetect = bcm_wlan_set_carddetect;
	dhd_wlan_control.get_mac_addr = bcm_wlan_get_mac_address;
#ifdef CONFIG_DHD_USE_STATIC_BUF
	dhd_wlan_control.mem_prealloc = bcm_wlan_prealloc;
#endif
	dhd_wlan_control.get_country_code = bcm_wlan_get_country_code;
	return 0;
}

