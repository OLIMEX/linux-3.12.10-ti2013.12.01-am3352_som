/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Jyri Sarha <jsarha@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Gpio controlled clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/device.h>

/**
 * DOC: basic gpio controlled clock which can be enabled and disabled
 *      with gpio output
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable and clk_disable are functional & control gpio
 * rate - inherits rate from parent.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_gpio(_hw) container_of(_hw, struct clk_gpio, hw)

static int clk_gpio_enable(struct clk_hw *hw)
{
	struct clk_gpio *gpio = to_clk_gpio(hw);
	int value = gpio->active_low ? 0 : 1;

	gpio_set_value(gpio->gpio, value);

	return 0;
}

static void clk_gpio_disable(struct clk_hw *hw)
{
	struct clk_gpio *gpio = to_clk_gpio(hw);
	int value = gpio->active_low ? 1 : 0;

	gpio_set_value(gpio->gpio, value);
}

static int clk_gpio_is_enabled(struct clk_hw *hw)
{
	struct clk_gpio *gpio = to_clk_gpio(hw);
	int value = gpio_get_value(gpio->gpio);

	return gpio->active_low ? !value : value;
}

const struct clk_ops clk_gpio_ops = {
	.enable = clk_gpio_enable,
	.disable = clk_gpio_disable,
	.is_enabled = clk_gpio_is_enabled,
};
EXPORT_SYMBOL_GPL(clk_gpio_ops);

/**
 * clk_register_gpio - register a gpip clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @flags: framework-specific flags for this clock
 * @gpio: gpio to control this clock
 * @active_low: gpio polarity
 */
struct clk *clk_register_gpio(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		unsigned int gpio, bool active_low)
{
	struct clk_gpio *clk_gpio;
	struct clk *clk = ERR_PTR(-EINVAL);
	struct clk_init_data init = { NULL };
	unsigned long gpio_flags;
	int err;

	if (active_low)
		gpio_flags = GPIOF_OUT_INIT_LOW;
	else
		gpio_flags = GPIOF_OUT_INIT_HIGH;

	if (dev)
		err = devm_gpio_request_one(dev, gpio, gpio_flags, name);
	else
		err = gpio_request_one(gpio, gpio_flags, name);

	if (err) {
		pr_err("%s: %s: Error requesting clock control gpio %u\n",
		       __func__, name, gpio);
		clk = ERR_PTR(err);
		goto clk_register_gpio_err;
	}

	if (dev)
		clk_gpio = devm_kzalloc(dev, sizeof(struct clk_gpio),
					GFP_KERNEL);
	else
		clk_gpio = kzalloc(sizeof(struct clk_gpio), GFP_KERNEL);

	if (!clk_gpio) {
		pr_err("%s: %s: could not allocate gpio clk\n", __func__, name);
		clk = ERR_PTR(-ENOMEM);
		goto clk_register_gpio_err;
	}

	init.name = name;
	init.ops = &clk_gpio_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	clk_gpio->gpio = gpio;
	clk_gpio->active_low = active_low;
	clk_gpio->hw.init = &init;

	clk = clk_register(dev, &clk_gpio->hw);

	if (!IS_ERR(clk))
		return clk;

	if (!dev)
		kfree(clk_gpio);

clk_register_gpio_err:
	if (!dev)
		gpio_free(gpio);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_gpio);

#ifdef CONFIG_OF
/**
 * The clk_register_gpio has to be delayed, because the EPROBE_DEFER
 * can not be handled properly at of_clk_init() call time.
 */

struct clk_gpio_delayed_register_data {
	struct device_node *node;
	struct mutex lock;
	struct clk *clk;
};

struct clk *of_clk_gpio_delayed_register_get(struct of_phandle_args *clkspec,
					     void *_data)
{
	struct clk_gpio_delayed_register_data *data =
		(struct clk_gpio_delayed_register_data *) _data;
	struct clk *clk;
	const char *clk_name = data->node->name;
	const char *parent_name;
	enum of_gpio_flags gpio_flags;
	int gpio;
	bool active_low;

	mutex_lock(&data->lock);

	if (data->clk) {
		mutex_unlock(&data->lock);
		return data->clk;
	}

	gpio = of_get_named_gpio_flags(data->node, "enable-gpios", 0,
				       &gpio_flags);

	if (gpio < 0) {
		mutex_unlock(&data->lock);
		if (gpio != -EPROBE_DEFER)
			pr_err("%s: %s: Can't get 'enable-gpios' DT property\n",
		       __func__, clk_name);
		return ERR_PTR(gpio);
	}

	active_low = gpio_flags & OF_GPIO_ACTIVE_LOW;

	parent_name = of_clk_get_parent_name(data->node, 0);

	clk = clk_register_gpio(NULL, clk_name, parent_name, 0,
				gpio, active_low);
	if (IS_ERR(clk)) {
		mutex_unlock(&data->lock);
		return clk;
	}

	data->clk = clk;
	mutex_unlock(&data->lock);

	return clk;
}

/**
 * of_gpio_clk_setup() - Setup function for gpio controlled clock
 */
void __init of_gpio_clk_setup(struct device_node *node)
{
	struct clk_gpio_delayed_register_data *data;

	data = kzalloc(sizeof(struct clk_gpio_delayed_register_data),
		       GFP_KERNEL);
	if (!data) {
		pr_err("%s: could not allocate gpio clk\n", __func__);
		return;
	}

	data->node = node;
	mutex_init(&data->lock);

	of_clk_add_provider(node, of_clk_gpio_delayed_register_get, data);
}
EXPORT_SYMBOL_GPL(of_gpio_clk_setup);
CLK_OF_DECLARE(gpio_clk, "gpio-clock", of_gpio_clk_setup);
#endif
