/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#if 0
#define CPUFREQ_DBG(format, ...) \
	printk(KERN_NOTICE "owl cpufreq: " format, ## __VA_ARGS__)

#else /* DEBUG */
#define CPUFREQ_DBG(format, ...)
#endif

#define CPUFREQ_ERR(format, ...) \
	printk(KERN_ERR "owl cpufreq: " format, ## __VA_ARGS__)

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/opp.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/bootafinfo.h>

#define CPUFREQ_DISTINGUISH_MAX_FREQ
#define CPUFREQ_LOWER_BURN_MAX_FREQ		1200000

#define CPU_REGULATOR_NAME	"dcdc1"
#define CPUM_REGULATOR_NAME	"dcdc4"

struct cpu0_opp_table {
	unsigned long clk; /*khz*/
	unsigned long volt; /*uv*/
};


static unsigned int transition_latency;

static struct device *cpu_dev;
static struct clk *cpu_clk;
static struct clk *core_pll;
static struct clk *hosc_clk;

static unsigned int locking_frequency;

static struct regulator *cpu_reg;
static struct regulator *cpum_reg;
struct cpufreq_frequency_table *freq_table;

static DEFINE_MUTEX(freq_table_mux);

static struct cpu0_opp_table cpu0_table[] = {
	/*khz		uV*/
	{ 1752000, 1200000},
	{ 1608000, 1125000},
	{ 1440000, 1075000},
	{ 1104000, 925000},
	{  600000, 850000},
	{  240000, 850000},
};

struct device cpu0_dev;

struct cpu0_opp_info {
	struct device *dev;
	struct cpu0_opp_table *cpu0_table;
	int cpu0_table_num;
};

static struct cpu0_opp_info cpu0_opp_info;
static struct cpu0_opp_info *cpu0_opp_info_cur;

static u32 cpuinfo_max_freq;
static u32 scaling_max_freq;
static u32 cpuinfo_min_freq;

/*
	find max_freq/min_freq in cpu0_opp_info
*/
static int find_cpu0_opp_info_max_min_freq(u32 *max_freq, u32 *scaling_max_freq, u32 *min_freq)
{
	u32 max_tmp = 0;
	u32 scaling_max_tmp = 0;
	u32 min_tmp = UINT_MAX;

	struct cpu0_opp_table *opp_table;
	int opp_table_num;

	opp_table = cpu0_opp_info.cpu0_table;
	opp_table_num = cpu0_opp_info.cpu0_table_num;
	if (opp_table[0].clk > max_tmp)
		max_tmp = opp_table[0].clk;
		
	if (opp_table[1].clk > scaling_max_tmp)
		scaling_max_tmp = opp_table[1].clk;

	if (opp_table[opp_table_num - 1].clk < min_tmp)
		min_tmp = opp_table[opp_table_num - 1].clk;

	if (max_tmp < min_tmp)
		return -EINVAL;

	*max_freq = max_tmp;
#ifdef CPUFREQ_DISTINGUISH_MAX_FREQ	
	*scaling_max_freq = scaling_max_tmp;
#else
	*scaling_max_freq = max_tmp;
#endif
	*min_freq = min_tmp;
	return 0;
}

/*
	init cpu0_opp_info/cpu0_opp_info_user
*/
static int init_cpu0_opp_info(void)
{
#ifdef CPUFREQ_LOWER_BURN_MAX_FREQ
	int mode, item_num;

	item_num = ARRAY_SIZE(cpu0_table);
	mode = owl_get_boot_mode();
	if (mode == OWL_PRODUCE) {
		int i;

		pr_info("%s: lower the freqence < %d when burn firmware\n",
			__func__, CPUFREQ_LOWER_BURN_MAX_FREQ);

		i = item_num;
		while (--i) {
			if (cpu0_table[i].clk > CPUFREQ_LOWER_BURN_MAX_FREQ)
				break;
		}
		cpu0_opp_info.cpu0_table = &cpu0_table[i];
		cpu0_opp_info.cpu0_table_num = item_num - i;
	} else {
		cpu0_opp_info.cpu0_table = cpu0_table;
		cpu0_opp_info.cpu0_table_num = item_num;
	}
#else
	cpu0_opp_info.cpu0_table = cpu0_table;
	cpu0_opp_info.cpu0_table_num = ARRAY_SIZE(cpu0_table);
#endif
	cpu0_opp_info.dev = &cpu0_dev;
    
	return 0;
}

/*
	get cpu0_opp_info_cur according to online_cpus
*/
static int select_cur_opp_info(void)
{

	cpu0_opp_info_cur = &cpu0_opp_info;

	return 0;
}

int cpu0_add_opp_table(struct device *cpu_dev, struct cpu0_opp_table *table, int table_size)
{
	int i;
	int err;

	if (!cpu_dev) {
		printk("cpufreq map: failed to get cpu0 device\n");
		return -ENODEV;
	}

	for (i = 0; i < table_size; i++) {
		/*table.clk is in khz*/

		printk("  table %p, i %d, cpu_dev %p, clk %ld, volt %ld\n",
			table, i, cpu_dev,
			table[i].clk * 1000, table[i].volt);

		err = opp_add(cpu_dev, table[i].clk * 1000, table[i].volt);
		if (err) {
			printk("cpufreq map: Cannot add opp entries. err %d\n", err);
			return err;
		}
	}

	return 0;
}

static int owl_cpu0_clk_init(void)
{
	int ret;

	core_pll = clk_get(NULL, "core_pll");
	if (IS_ERR(core_pll)) {
		ret = PTR_ERR(core_pll);
		CPUFREQ_ERR("failed to get core pll: %d\n", ret);
		return ret;
	}

	cpu_clk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(cpu_clk)) {
		ret = PTR_ERR(cpu_clk);
		CPUFREQ_ERR("failed to get cpu clk: %d\n", ret);
		return ret;
	}

	hosc_clk = clk_get(NULL, "hosc");
	if (IS_ERR(hosc_clk)) {
		ret = PTR_ERR(hosc_clk);
		CPUFREQ_ERR("failed to get hosc clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static void owl_cpu0_clk_free(void)
{
	clk_put(core_pll);
	clk_put(cpu_clk);
	clk_put(hosc_clk);
}

static long owl_round_cpu0_clk(unsigned long rate)
{
	long freq_HZ;
	freq_HZ = clk_round_rate(core_pll, rate);
	return freq_HZ;
}

static unsigned long owl_get_cpu0_clk(void)
{
	unsigned long freq_HZ;
	freq_HZ = clk_get_rate(cpu_clk);
	return freq_HZ;
}

static int owl_set_cpu0_clk(unsigned long rate)
{
	int ret = 0;

	ret = clk_set_parent(cpu_clk, hosc_clk);
	if (ret) {
		CPUFREQ_ERR("failed to set cpu parent to hosc, ret %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(core_pll, rate);
	if (ret) {
		CPUFREQ_ERR("failed to set cpu rate to %ld, ret %d\n", rate, ret);
		return ret;
	}

	udelay(50);
	ret = clk_set_parent(cpu_clk, core_pll);
	if (ret) {
		CPUFREQ_ERR("failed to set cpu parent to core_pll, ret %d\n", ret);
	}

	return ret;
}

static int cpu0_cpuinfo_update(struct cpufreq_policy *policy, u32 max_freq, u32 min_freq)
{
	policy->min = policy->cpuinfo.min_freq = min_freq;
	policy->max = policy->cpuinfo.max_freq = max_freq;
	return 0;
}

static int cpu0_verify_speed(struct cpufreq_policy *policy)
{
	int ret;

	if (mutex_lock_interruptible(&freq_table_mux))
		return -EINVAL;

	ret = cpufreq_frequency_table_verify(policy, freq_table);

	mutex_unlock(&freq_table_mux);
	return ret;
}

static unsigned int cpu0_get_speed(unsigned int cpu)
{
	int freq_khz;

	freq_khz = owl_get_cpu0_clk() / 1000;
	return freq_khz;
}

#define volt_align(d, a) (((d) + (a - 1)) & ~(a - 1))

static int cpu0_set_target(struct cpufreq_policy *policy,
			   unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	struct opp *opp;
	unsigned long freq_Hz, volt = 0, volt_old = 0;
	unsigned long volt_delta = 0, volt_step = 0;	
	unsigned int index;
	int ret = 0;

	CPUFREQ_DBG("%s , target freq = %d\n", __func__, target_freq);
	if (mutex_lock_interruptible(&freq_table_mux))
		return -EINVAL;

	ret = cpufreq_frequency_table_target(policy, freq_table,
				target_freq, relation, &index);
	if (ret) {
		CPUFREQ_ERR("failed to match target freqency %d: %d\n",
		       target_freq, ret);
		return ret;
	}

	target_freq = freq_table[index].frequency;

	freq_Hz = owl_round_cpu0_clk(target_freq * 1000);
	if (freq_Hz < 0)
		freq_Hz = target_freq * 1000;
	
	freqs.new = freq_Hz / 1000;
	freqs.old = owl_get_cpu0_clk() / 1000;

	if (freqs.old == freqs.new) {
		ret = 0;
		goto target_out;
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	if (cpu_reg) {
		rcu_read_lock();
		opp = opp_find_freq_ceil(cpu0_opp_info_cur->dev, &freq_Hz);
		if (IS_ERR(opp)) {
			rcu_read_unlock();
			CPUFREQ_ERR("failed to find OPP for %ld\n", freq_Hz);
			ret = PTR_ERR(opp);
			goto target_out;
		}
		volt = opp_get_voltage(opp);
		rcu_read_unlock();
		volt_old = regulator_get_voltage(cpu_reg);
		if (volt_old < 0) {
			ret = (int)volt_old;
			CPUFREQ_ERR("failed to get cpu voltage, ret %d\n", ret);
			goto target_out;
		}
	}

	CPUFREQ_DBG("%u MHz, %ld mV --> %u MHz, %ld mV\n",
		 freqs.old / 1000, volt_old ? volt_old / 1000 : -1,
		 freqs.new / 1000, volt ? volt / 1000 : -1);

	/* scaling up?  scale voltage before frequency */
	if (cpu_reg && freqs.new > freqs.old && (volt != volt_old)) {
		CPUFREQ_DBG("set voltage PRE freqscale\n");

		if (cpum_reg)
			/* cpum_vdd has same voltage */
			ret = regulator_set_voltage(cpum_reg, volt, INT_MAX);
		else 
			ret = 0;

		ret |= regulator_set_voltage(cpu_reg, volt, INT_MAX);
		if (ret) {
			CPUFREQ_ERR("failed to scale voltage up: %d\n", ret);
			freqs.new = freqs.old;
			goto target_out;;
		}
		CPUFREQ_DBG("set voltage PRE freqscale OK\n");
	}

	ret = owl_set_cpu0_clk(freqs.new * 1000);

	if (ret) {
		CPUFREQ_ERR("failed to set clock rate: %d\n", ret);
		if (cpu_reg)
			regulator_set_voltage(cpu_reg, volt_old, INT_MAX);

		goto target_out;
	}

	/* scaling down?  scale voltage after frequency */
	if (cpu_reg && freqs.new < freqs.old && (volt != volt_old)) {
		CPUFREQ_DBG("set voltage after freqscale\n");
		volt_delta = volt_old - volt;
		if(volt_delta >= 100*1000)	{
			volt_step = volt + volt_align(volt_delta/2, 25*1000);
			ret = regulator_set_voltage(cpu_reg, volt_step, INT_MAX);
			if(ret == 0) {
				ret = regulator_set_voltage(cpu_reg, volt, INT_MAX);
			}
		} else {
			ret = regulator_set_voltage(cpu_reg, volt, INT_MAX);
		}

		if (cpum_reg) {
			/* cpum_vdd has same voltage */
			ret |= regulator_set_voltage(cpum_reg, volt, INT_MAX);
		}

		if (ret) {
			CPUFREQ_ERR("failed to scale voltage down: %d\n", ret);
			owl_set_cpu0_clk(freqs.old * 1000);
			freqs.new = freqs.old;
			
			goto target_out;
		}

		CPUFREQ_DBG("set voltage after freqscale OK\n");
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
target_out:
	mutex_unlock(&freq_table_mux);
	return ret;
}


static int cpu0_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret = 0;
	int freq_khz;
	unsigned int index;

	if (mutex_lock_interruptible(&freq_table_mux))
		return -EINVAL;

	if (policy->cpu != 0) {

		/*let cpu1,2,3 as the managered cpu*/
		policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
		cpumask_setall(policy->cpus);

#ifdef ARCH_ARM
		/*The newly booted cpu will corrupt loops_per_jiffy*/
		/*and its own percpu loops_per_jiffy is also wrong*/
		/*these 2 value were init calc value without cpufreq*/
		/*so we should change them back according to current*/
		/*freq.*/
		loops_per_jiffy =
			per_cpu(cpu_data, policy->cpu).loops_per_jiffy =
			per_cpu(cpu_data, 0).loops_per_jiffy;
#endif
		ret = 0;
		goto init_out;
	}
	/*****get the real cpufreq, align to real freq_table************/
	ret = cpu0_cpuinfo_update(policy, cpuinfo_max_freq, cpuinfo_min_freq);
	if (ret) {
		CPUFREQ_ERR("invalid frequency table: %d\n", ret);
		goto init_out;
	}

	freq_khz = owl_get_cpu0_clk() / 1000;
	cpufreq_frequency_table_target(policy, freq_table, freq_khz,
					CPUFREQ_RELATION_L, &index);
	freq_khz = freq_table[index].frequency;
	/***************************************/

	/***we need to report freq table******/
	ret = cpu0_cpuinfo_update(policy, cpuinfo_max_freq, cpuinfo_min_freq);
	if (ret) {
		CPUFREQ_ERR("invalid frequency table: %d\n", ret);
		goto init_out;
	}

	policy->min = cpuinfo_min_freq;
	policy->max = scaling_max_freq;

	policy->cpuinfo.transition_latency = transition_latency;
	policy->cur = freq_khz;

	policy->suspend_freq = locking_frequency;

	/*
	 * The driver only supports the SMP configuartion where all processors
	 * share the clock and voltage and clock.  Use cpufreq affected_cpus
	 * interface to have all CPUs scaled together.
	 */
	policy->shared_type = CPUFREQ_SHARED_TYPE_ANY;
	cpumask_setall(policy->cpus);

	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);

init_out:
	mutex_unlock(&freq_table_mux);
	return ret;
}

static int cpu0_cpufreq_exit(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return 0;

	cpufreq_frequency_table_put_attr(policy->cpu);

	return 0;
}

static struct freq_attr *cpu0_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver cpu0_cpufreq_driver = {
	.flags = CPUFREQ_STICKY,
	.verify = cpu0_verify_speed,
	.target = cpu0_set_target,
	.get = cpu0_get_speed,
	.init = cpu0_cpufreq_init,
	.exit = cpu0_cpufreq_exit,
	.name = "owl_cpu0",
	.attr = cpu0_cpufreq_attr,
#ifdef CONFIG_PM
	.suspend = cpufreq_generic_suspend,
#endif
};

static int cpu0_cpufreq_driver_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		CPUFREQ_ERR("failed to find cpu0 node\n");
		return -ENOENT;
	}

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		CPUFREQ_ERR("failed to get cpu0 device\n");
		ret = -ENODEV;
		goto out_put_node;
	}

	cpu_dev->of_node = np;

	ret = owl_cpu0_clk_init();
	if (ret) {
		CPUFREQ_ERR("failed to get cpu0 clock: %d\n", ret);
		goto out_put_node;
	}
	cpu_reg = devm_regulator_get(cpu_dev, CPU_REGULATOR_NAME);
	if (IS_ERR(cpu_reg)) {
		CPUFREQ_ERR("failed to get cpu0 regulator\n");
		cpu_reg = NULL;
	}

	cpum_reg = devm_regulator_get(cpu_dev, CPUM_REGULATOR_NAME);
	if (IS_ERR(cpum_reg)) {
		CPUFREQ_ERR("failed to get cpu0 memory regulator\n");
		cpum_reg = NULL;
	}

	/*of_init_opp_table*/
	init_cpu0_opp_info();
	find_cpu0_opp_info_max_min_freq(&cpuinfo_max_freq, &scaling_max_freq, &cpuinfo_min_freq);
	select_cur_opp_info();

	ret = cpu0_add_opp_table(cpu0_opp_info.dev, cpu0_opp_info.cpu0_table,
			cpu0_opp_info.cpu0_table_num);
	if (ret) {
		CPUFREQ_ERR("failed to init OPP table: %d\n", ret);	
		goto out_put_clk;
	}

	ret = opp_init_cpufreq_table(cpu0_opp_info_cur->dev, &freq_table);
	if (ret) {
		CPUFREQ_ERR("failed to init cpufreq table: %d\n", ret);
		goto out_put_clk;
	}

	if (of_property_read_u32(np, "clock-latency", &transition_latency)){
//		transition_latency = CPUFREQ_ETERNAL;
		transition_latency = 300 * 1000;
	}

	if (cpu_reg) {
		struct opp *opp;
		unsigned long min_uV, max_uV;
		int i;

		/*
		 * OPP is maintained in order of increasing frequency, and
		 * freq_table initialised from OPP is therefore sorted in the
		 * same order.
		 */
		for (i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++)
			;
		rcu_read_lock();
		opp = opp_find_freq_exact(cpu0_opp_info_cur->dev,
				freq_table[0].frequency * 1000, true);
		min_uV = opp_get_voltage(opp);
		opp = opp_find_freq_exact(cpu0_opp_info_cur->dev,
				freq_table[i-1].frequency * 1000, true);
		max_uV = opp_get_voltage(opp);
		rcu_read_unlock();
#ifdef CONFIG_REGULATOR
		ret = regulator_set_voltage_time(cpu_reg, min_uV, max_uV);
		if (ret > 0)
			transition_latency += ret;

		if (cpum_reg)		
			regulator_set_voltage_time(cpum_reg, min_uV, max_uV);

#endif
	}

	/* Done here as we want to capture boot frequency (kHz) */
	locking_frequency = owl_get_cpu0_clk() / 1000;
	pr_info("%s: locking_frequency: %d\n", __func__, locking_frequency);

	ret = cpufreq_register_driver(&cpu0_cpufreq_driver);
	if (ret) {
		CPUFREQ_ERR("failed register driver: %d\n", ret);
		goto out_free_convert_table;
	}

	of_node_put(np);

	return 0;

out_free_convert_table:
	opp_free_cpufreq_table(cpu0_opp_info_cur->dev, &freq_table);
out_put_clk:
	owl_cpu0_clk_free();
out_put_node:
	of_node_put(np);
	return ret;
}

static void __exit cpu0_cpufreq_driver_exit(void)
{
	cpufreq_unregister_driver(&cpu0_cpufreq_driver);
	opp_free_cpufreq_table(cpu0_opp_info_cur->dev, &freq_table);
	owl_cpu0_clk_free();
}

late_initcall(cpu0_cpufreq_driver_init);
module_exit(cpu0_cpufreq_driver_exit);

MODULE_AUTHOR("Actions semi");
MODULE_DESCRIPTION("owl CPU0 cpufreq driver");
MODULE_LICENSE("GPL");
