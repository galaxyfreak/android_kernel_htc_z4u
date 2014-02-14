/*
 * Bitbanging I2C bus driver using the GPIO API
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c-gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of_i2c.h>

struct i2c_cpld_private_data {
	struct i2c_adapter adap;
	struct i2c_algo_bit_data bit_data;
	struct i2c_gpio_platform_data pdata;
};

static void i2c_cpld_setsda_dir(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	if (state)
		gpio_direction_input(pdata->sda_pin);
	else
		gpio_direction_output(pdata->sda_pin, 0);
}

static void i2c_cpld_setsda_val(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	gpio_set_value(pdata->sda_pin, state);
}

static void i2c_cpld_setscl_dir(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	if (state)
		gpio_direction_input(pdata->scl_pin);
	else
		gpio_direction_output(pdata->scl_pin, 0);
}

static void i2c_cpld_setscl_val(void *data, int state)
{
	struct i2c_gpio_platform_data *pdata = data;

	gpio_set_value(pdata->scl_pin, state);
}

static int i2c_cpld_getsda(void *data)
{
	struct i2c_gpio_platform_data *pdata = data;

	return gpio_get_value(pdata->sda_pin);
}

static int i2c_cpld_getscl(void *data)
{
	struct i2c_gpio_platform_data *pdata = data;

	return gpio_get_value(pdata->scl_pin);
}

static int __devinit of_i2c_cpld_probe(struct device_node *np,
			     struct i2c_gpio_platform_data *pdata)
{
	u32 reg;

	if (of_gpio_count(np) < 2)
		return -ENODEV;

	pdata->sda_pin = of_get_gpio(np, 0);
	pdata->scl_pin = of_get_gpio(np, 1);

	if (!gpio_is_valid(pdata->sda_pin) || !gpio_is_valid(pdata->scl_pin)) {
		pr_err("%s: invalid GPIO pins, sda=%d/scl=%d\n",
		       np->full_name, pdata->sda_pin, pdata->scl_pin);
		return -ENODEV;
	}

	of_property_read_u32(np, "i2c-cpld,delay-us", &pdata->udelay);

	if (!of_property_read_u32(np, "i2c-cpld,timeout-ms", &reg))
		pdata->timeout = msecs_to_jiffies(reg);

	pdata->sda_is_open_drain =
		of_property_read_bool(np, "i2c-cpld,sda-open-drain");
	pdata->scl_is_open_drain =
		of_property_read_bool(np, "i2c-cpld,scl-open-drain");
	pdata->scl_is_output_only =
		of_property_read_bool(np, "i2c-cpld,scl-output-only");

	return 0;
}

static int __devinit i2c_cpld_probe(struct platform_device *pdev)
{
	struct i2c_cpld_private_data *priv;
	struct i2c_gpio_platform_data *pdata;
	struct i2c_algo_bit_data *bit_data;
	struct i2c_adapter *adap;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	adap = &priv->adap;
	bit_data = &priv->bit_data;
	pdata = &priv->pdata;

	if (pdev->dev.of_node) {
		ret = of_i2c_cpld_probe(pdev->dev.of_node, pdata);
		if (ret)
			return ret;
	} else {
		if (!pdev->dev.platform_data)
			return -ENXIO;
		memcpy(pdata, pdev->dev.platform_data, sizeof(*pdata));
	}

	ret = gpio_request(pdata->sda_pin, "sda");
	if (ret)
		goto err_request_sda;
	ret = gpio_request(pdata->scl_pin, "scl");
	if (ret)
		goto err_request_scl;

	if (pdata->sda_is_open_drain) {
		gpio_direction_output(pdata->sda_pin, 1);
		bit_data->setsda = i2c_cpld_setsda_val;
	} else {
		gpio_direction_input(pdata->sda_pin);
		bit_data->setsda = i2c_cpld_setsda_dir;
	}

	if (pdata->scl_is_open_drain || pdata->scl_is_output_only) {
		gpio_direction_output(pdata->scl_pin, 1);
		bit_data->setscl = i2c_cpld_setscl_val;
	} else {
		gpio_direction_input(pdata->scl_pin);
		bit_data->setscl = i2c_cpld_setscl_dir;
	}

	if (!pdata->scl_is_output_only)
		bit_data->getscl = i2c_cpld_getscl;
	bit_data->getsda = i2c_cpld_getsda;

	if (pdata->udelay)
		bit_data->udelay = pdata->udelay;
	else if (pdata->scl_is_output_only)
		bit_data->udelay = 50;			
	else
		bit_data->udelay = 5;			

	if (pdata->timeout)
		bit_data->timeout = pdata->timeout;
	else
		bit_data->timeout = HZ / 10;		

	bit_data->data = pdata;

	adap->owner = THIS_MODULE;
	snprintf(adap->name, sizeof(adap->name), "i2c-cpld%d", pdev->id);
	adap->algo_data = bit_data;
	adap->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	adap->nr = (pdev->id != -1) ? pdev->id : 0;
	ret = i2c_bit_add_numbered_bus(adap);
	if (ret)
		goto err_add_bus;

	of_i2c_register_devices(adap);

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "using pins %u (SDA) and %u (SCL%s)\n",
		 pdata->sda_pin, pdata->scl_pin,
		 pdata->scl_is_output_only
		 ? ", no clock stretching" : "");

	return 0;

err_add_bus:
	gpio_free(pdata->scl_pin);
err_request_scl:
	gpio_free(pdata->sda_pin);
err_request_sda:
	return ret;
}

static int __devexit i2c_cpld_remove(struct platform_device *pdev)
{
	struct i2c_cpld_private_data *priv;
	struct i2c_gpio_platform_data *pdata;
	struct i2c_adapter *adap;

	priv = platform_get_drvdata(pdev);
	adap = &priv->adap;
	pdata = &priv->pdata;

	i2c_del_adapter(adap);
	gpio_free(pdata->scl_pin);
	gpio_free(pdata->sda_pin);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id i2c_cpld_dt_ids[] = {
	{ .compatible = "i2c-cpld", },
	{  }
};

MODULE_DEVICE_TABLE(of, i2c_cpld_dt_ids);
#endif

static struct platform_driver i2c_cpld_driver = {
	.driver		= {
		.name	= "i2c-cpld",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(i2c_cpld_dt_ids),
	},
	.probe		= i2c_cpld_probe,
	.remove		= __devexit_p(i2c_cpld_remove),
};

static int __init i2c_cpld_init(void)
{
	int ret;

	ret = platform_driver_register(&i2c_cpld_driver);
	if (ret)
		printk(KERN_ERR "i2c-cpld: probe failed: %d\n", ret);

	return ret;
}
subsys_initcall(i2c_cpld_init);

static void __exit i2c_cpld_exit(void)
{
	platform_driver_unregister(&i2c_cpld_driver);
}
module_exit(i2c_cpld_exit);

MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_DESCRIPTION("Platform-independent bitbanging I2C driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:i2c-cpld");