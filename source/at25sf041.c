/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Frederik Peter Aalund, SBT Instruments
 */
#include <at25sf041.h>
#include <version.h>
#include <linux/module.h>
#include <linux/mtd/spi-nor.h>
#include <linux/platform_device.h>


#define AT25SF041_MAN_ID 0x1F
#define AT25SF041_DEV_ID1 0x84
#define AT25SF041_DEV_ID2 0x01
#define AT25SF041_PAGE_SIZE 256
#define AT25SF041_TEST_CON


struct at25sf041 {
	struct spi_nor nor;
};

struct at25sf041_page {
	loff_t spi_addr_start;
	const u_char *buffer_start;
	size_t len;
};


static bool verbose = true;


/* Tests if the chip is connected by probing the status and ID registers.
 *
 * If either of the MISO, MOSI, or CLK pins are physically disconnected,
 * then the status register will read 0xFF.
 *
 * Note that it is not enough to probe the status register alone. Empirical
 * data shows, that if the CS pin is physically disconnected, then the status
 * register returns 0x00, which denotes 'device ready', unfortunately.
 * Therefore, we also probe the ID register.
 *
 * Note that we can't probe the ID register alone. If a write is in progress,
 * then the ID register will return 0xFF,0xFF,0xFF. Consequently, we have to
 * probe the status register first, to find out if a write is in progress.
 */
static int at25sf041_test_con(struct spi_device *spi)
{
	u8 op_rdsr = SPINOR_OP_RDSR;
	u8 status = 0xAB; /* dummy value */
	u8 op_rdid = SPINOR_OP_RDID;
	u8 id[3] = { 0, 0, 0 };
	struct spi_transfer sr_req = {
		.tx_buf = &op_rdsr,
		.len = 1,
	};
	struct spi_transfer sr_res = {
		.rx_buf = &status,
		.len = 1,
		/* pull chip select down between the two requests */
		.cs_change = 1,
	};
	struct spi_transfer id_req = {
		.tx_buf = &op_rdid,
		.len = 1,
	};
	struct spi_transfer id_res = {
		.rx_buf = id,
		.len = ARRAY_SIZE(id),
	};
	struct spi_message m;
	int result;
	spi_message_init(&m);
	spi_message_add_tail(&sr_req, &m);
	spi_message_add_tail(&sr_res, &m);
	spi_message_add_tail(&id_req, &m);
	spi_message_add_tail(&id_res, &m);
	result = spi_sync(spi, &m);
	if (0 != result) {
		return result;
	}
	/* the chip will override the dummy value (0xAB) with 0xFF if there is a problem
	 * with the physical connection. */
	if (0xFF == status) {
		return -EIO;
	}
	/* the ID will only be available if a write is not in progress (WIP). */
	if (!(status & SR_WIP)) {
		/* the id will be malformed if there is a problem with the physical
		 * connection. */
		if (AT25SF041_MAN_ID  != id[0] ||
			AT25SF041_DEV_ID1 != id[1] ||
			AT25SF041_DEV_ID2 != id[2]) {
			return -EIO;
		}
	}
	return 0;
}

static int at25sf041_read_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct spi_device *spi = container_of(nor->dev, struct spi_device, dev);
	struct spi_transfer command_t = {
		.tx_buf = &opcode,
		.len = 1,
	};
	struct spi_transfer data_t = {
		.rx_buf = buf,
		.len = len,
	};
	struct spi_message m;
	int result;
#ifdef AT25SF041_TEST_CON
	result = at25sf041_test_con(spi);
	if (0 != result) {
		if (verbose) {
			dev_warn(nor->dev, "read_reg test_con failed: %d\n", result);
		}
		return result;
	}
#endif
	spi_message_init(&m);
	spi_message_add_tail(&command_t, &m);
	if (0 < len) {
		spi_message_add_tail(&data_t, &m);
	}
	result = spi_sync(spi, &m);
	if (0 != result) {
		return result;
	}
	return 0;
}

static int at25sf041_write_reg(struct spi_nor *nor, u8 opcode, u8 *buf, int len)
{
	struct spi_device *spi = container_of(nor->dev, struct spi_device, dev);
	struct spi_transfer command_t = {
		.tx_buf = &opcode,
		.len = 1,
	};
	struct spi_transfer data_t = {
		.tx_buf = buf,
		.len = len,
	};
	struct spi_message m;
	int result;
#ifdef AT25SF041_TEST_CON
	result = at25sf041_test_con(spi);
	if (0 != result) {
		if (verbose) {
			dev_warn(nor->dev, "write_reg test_con failed: %d\n", result);
		}
		return result;
	}
#endif
	spi_message_init(&m);
	spi_message_add_tail(&command_t, &m);
	if (0 < len) {
		spi_message_add_tail(&data_t, &m);
	}
	result = spi_sync(spi, &m);
	if (0 != result) {
		return result;
	}
	return 0;
}

static ssize_t at25sf041_read(struct spi_nor *nor, loff_t from,
                                  size_t len, u_char *read_buf)
{
	struct spi_device *spi = container_of(nor->dev, struct spi_device, dev);
	u8 command_buf[] = {
		/* read array opcode */
		0x0b,
		/* address */
		(from >> 16) & 0xFF,
		(from >> 8) & 0xFF,
		from & 0xFF,
		/* dummy byte */
		0,
	};
	struct spi_transfer command_t = {
		.tx_buf = command_buf,
		.len = ARRAY_SIZE(command_buf),
	};
	loff_t max_addr = nor->mtd.size;
	loff_t end = min(max_addr, from + len);
	size_t read_len = end - from;
	struct spi_transfer data_t = {
		.rx_buf = read_buf,
		.len = read_len,
	};
	struct spi_message m;
	int result;
#ifdef AT25SF041_TEST_CON
	result = at25sf041_test_con(spi);
	if (0 != result) {
		if (verbose) {
			dev_warn(nor->dev, "read test_con failed: %d\n", result);
		}
		return result;
	}
#endif
	spi_message_init(&m);
	spi_message_add_tail(&command_t, &m);
	spi_message_add_tail(&data_t, &m);
	result = spi_sync(spi, &m);
	if (0 != result) {
		return result;
	}
	return read_len;
}

static ssize_t at25sf041_write_page(struct spi_nor *nor,
                            struct at25sf041_page *page)
{
	struct spi_device *spi = container_of(nor->dev, struct spi_device, dev);
	u8 command_buf[] = {
		/* program opcode */
		0x02,
		/* address */
		(page->spi_addr_start >> 16) & 0xFF,
		(page->spi_addr_start >> 8) & 0xFF,
		page->spi_addr_start & 0xFF,
	};
	struct spi_transfer command_t = {
		.tx_buf = command_buf,
		.len = ARRAY_SIZE(command_buf),
	};
	struct spi_transfer data_t = {
		.tx_buf = page->buffer_start,
		.len = page->len,
	};
	struct spi_message m;
	ssize_t result;
#ifdef AT25SF041_TEST_CON
	result = at25sf041_test_con(spi);
	if (0 != result) {
		if (verbose) {
			dev_warn(nor->dev, "write_page test_con failed: %d\n", result);
		}
		return result;
	}
#endif
	spi_message_init(&m);
	spi_message_add_tail(&command_t, &m);
	spi_message_add_tail(&data_t, &m);
	result = spi_sync(spi, &m);
	if (0 != result) {
		return result;
	}
	return page->len;
}

/* Writes longer than a page must be split into pages */
static ssize_t at25sf041_write(struct spi_nor *nor, loff_t to,
                          size_t len, const u_char *write_buf)
{
	loff_t max_addr = nor->mtd.size;
	loff_t end = min(max_addr, to + len);
	size_t write_len = end - to;
	size_t data_left = write_len;
	ssize_t result;
	for (; 0 < data_left;) {
		size_t page_off = to & 0xFF;
		size_t page_rem = AT25SF041_PAGE_SIZE - page_off;
		struct at25sf041_page page = {
			.spi_addr_start = to,
			.buffer_start = write_buf,
			.len = min(page_rem, data_left),
		};
		result = at25sf041_write_page(nor, &page);
		if (0 > result) {
			return result;
		}
		if (page.len != result) {
			return -EIO;
		}
		to += page.len;
		write_buf += page.len;
		data_left -= page.len;
	}
	return write_len;
}


/* Inspired by fbtft_device_spi_delete from fbtft */
static void at25sf041_del_device(struct spi_master *master, unsigned cs)
{
	struct device *dev;
	char str[32];
	snprintf(str, sizeof(str), "%s.%u", dev_name(&master->dev), cs);
	dev = bus_find_device_by_name(&spi_bus_type, NULL, str);
	if (dev) {
		if (verbose)
			dev_info(dev, "Deleting %s\n", str);
		device_del(dev);
	}
}


static int at25sf041_probe(struct platform_device *pdev)
{
	struct at25sf041 *at25;
	struct spi_device *spi_device;
	struct device *dev = &pdev->dev;
	struct at25sf041_platform_data *pdata = dev_get_platdata(dev);
	struct spi_master *master;
	int result;

	if (!pdata) {
		dev_err(dev, "Missing platform data\n");
		return -EINVAL;
	}

	/* allocate at25sf041 data */
	at25 = devm_kzalloc(dev, sizeof(struct at25sf041), GFP_KERNEL);
	if (!at25)
		return -ENOMEM;
	platform_set_drvdata(pdev, at25);

	/* get ref-counted pointer to spi master */
	master = spi_busnum_to_master(pdata->spi_binfo.bus_num);
	if (NULL == master) {
		dev_err(dev, "spi_busnum_to_master(%d) returned NULL\n",
		                                        pdata->spi_binfo.bus_num);
		return -EINVAL;
	}

	/* delete any existing spi devices that happens to be currently registered
	 * to the chosen 'chip select' number on the master. */
	at25sf041_del_device(master, pdata->spi_binfo.chip_select);

	/* register new spi device on the master. We (the caller) take ownership
	 * of the allocated memory */
	spi_device = spi_new_device(master, &pdata->spi_binfo);
	at25->nor.priv = spi_device;
	/* release ref-counted pointer to master */
	put_device(&master->dev);
	if (!spi_device) {
		dev_err(dev, "spi_new_device() returned NULL\n");
		return -EPERM;
	}

	/* initialize spi_nor struct */
	at25->nor.dev = &spi_device->dev;
	at25->nor.read_reg = at25sf041_read_reg;
	at25->nor.write_reg = at25sf041_write_reg;
	at25->nor.read = at25sf041_read;
	at25->nor.write = at25sf041_write;

	/* scan for flash chip */
	result = spi_nor_scan(&at25->nor, "at25sf041", SPI_NOR_NORMAL);
	if (0 != result) {
		dev_err(dev, "spi_nor_scan() returned %d\n", result);
		return result;
	}

	/* register memory technology device. E.g., /dev/mtd0 */
	result = mtd_device_register(&at25->nor.mtd, NULL, 0);
	if (0 != result) {
		dev_err(dev, "mtd_device_register() returned %d\n", result);
		return result;
	}

	dev_info(dev, "Probe found a device (bus:%d, cs:%d)\n",
			 pdata->spi_binfo.bus_num, pdata->spi_binfo.chip_select);
	return 0;
}

static int at25sf041_remove(struct platform_device *pdev)
{
	struct at25sf041 *at25 = platform_get_drvdata(pdev);
	struct spi_nor *nor = &at25->nor;
	struct spi_device *spi_device = nor->priv;
	mtd_device_unregister(&nor->mtd);
	device_del(&spi_device->dev);
	kfree(spi_device);
	return 0;
}


static const struct of_device_id at25sf041_of_match[] = {
	{.compatible = "at25sf041"},
	{},
};
MODULE_DEVICE_TABLE(of, at25sf041_of_match);


static struct platform_driver at25sf041_driver = {
	.driver = {
		.name = "at25sf041",
		.of_match_table = at25sf041_of_match
	},
	.probe = at25sf041_probe,
	.remove = at25sf041_remove,
};
module_platform_driver(at25sf041_driver)


MODULE_AUTHOR("Frederik Peter Aalund <fpa@sbtinstruments.com>");
MODULE_DESCRIPTION("AT25SF041 SPI Serial Flash Memory");
MODULE_LICENSE("GPL");
MODULE_VERSION(AT25SF041_VERSION);
