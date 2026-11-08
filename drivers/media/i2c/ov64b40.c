// SPDX-License-Identifier: GPL-2.0
/*
 * V4L2 sensor driver for OmniVision OV64B40
 *
 * Single 4624x3472 RAW10 mode (2x2 binned) over a 3-trio C-PHY link, with
 * the register settings of the Xiaomi Poco F5 (marble) camera module.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#include "ov64b40_regs.h"

#define OV64B40_XCLK_FREQ		19200000

#define OV64B40_REG_CHIP_ID		CCI_REG24(0x300a)
#define OV64B40_CHIP_ID			0x566442

#define OV64B40_REG_SOFT_RESET		CCI_REG8(0x0103)
#define OV64B40_REG_STREAM		CCI_REG8(0x0100)

#define OV64B40_REG_GROUP_HOLD		CCI_REG8(0x3208)
#define OV64B40_GROUP_HOLD_START	0x00
#define OV64B40_GROUP_HOLD_END		0x10
#define OV64B40_GROUP_HOLD_LAUNCH	0xa0

#define OV64B40_REG_VTS_HIGH		CCI_REG8(0x3840)
#define OV64B40_REG_VTS			CCI_REG16(0x380e)
#define OV64B40_VTS_MAX			0xffffff

#define OV64B40_REG_EXPOSURE		CCI_REG24(0x3500)
#define OV64B40_EXPOSURE_MIN		8
#define OV64B40_EXPOSURE_MARGIN		28

#define OV64B40_REG_ANA_GAIN		CCI_REG16(0x3508)
#define OV64B40_ANA_GAIN_MIN		0x80
#define OV64B40_ANA_GAIN_MAX		0xff0
#define OV64B40_ANA_GAIN_DEFAULT	0x80

#define OV64B40_REG_DIG_GAIN		CCI_REG24(0x350a)
#define OV64B40_DIG_GAIN_MIN		0x10000
#define OV64B40_DIG_GAIN_MAX		0xfffff
#define OV64B40_DIG_GAIN_DEFAULT	0x10000

#define OV64B40_REG_TEST_PATTERN	CCI_REG8(0x50c1)

/* written by the stock driver right before streaming */
#define OV64B40_REG_3019		CCI_REG8(0x3019)
#define OV64B40_REG_4308		CCI_REG8(0x4308)

#define OV64B40_NATIVE_WIDTH		9280
#define OV64B40_NATIVE_HEIGHT		6976

#define OV64B40_WIDTH			4624
#define OV64B40_HEIGHT			3472
#define OV64B40_VTS			3628
/*
 * HTS (0x380c = 1056) is not in pixel units; the line length below assumes
 * the stock 30 fps for VTS 3628 and is only used to report timings.
 */
#define OV64B40_LINE_LENGTH		8448
#define OV64B40_PIXEL_RATE		(OV64B40_LINE_LENGTH * OV64B40_VTS * 30LL)
#define OV64B40_VBLANK_MIN		(OV64B40_VTS - OV64B40_HEIGHT)

/*
 * C-PHY: the stock driver programs the CSIPHY for 3.84 Gsps. camss only uses
 * the link frequency to pick clock rates on C-PHY.
 */
#define OV64B40_LINK_FREQ		1280000000

static const s64 ov64b40_link_freqs[] = { OV64B40_LINK_FREQ };

static const char * const ov64b40_test_pattern_menu[] = {
	"Disabled",
	"Type1",
	"Type2",
	"Type3",
	"Type4",
};

static const int ov64b40_test_pattern_val[] = {
	0x00,
	BIT(0),
	BIT(4) | BIT(0),
	BIT(5) | BIT(0),
	BIT(5) | BIT(4) | BIT(0),
};

static const char * const ov64b40_supply_names[] = {
	"avdd",		/* Analog (2.8V) supply */
	"dovdd",	/* I/O (1.8V) supply */
	"dvdd",		/* Digital core (1.1V) supply */
};

struct ov64b40 {
	struct device *dev;
	struct regmap *cci;
	struct v4l2_subdev sd;
	struct media_pad pad;

	struct clk *xclk;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(ov64b40_supply_names)];

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *vblank;
};

static inline struct ov64b40 *sd_to_ov64b40(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov64b40, sd);
}

static int ov64b40_write_vts(struct ov64b40 *ov64b40, u32 vts, int *ret)
{
	cci_write(ov64b40->cci, OV64B40_REG_VTS_HIGH, vts >> 16, ret);
	return cci_write(ov64b40->cci, OV64B40_REG_VTS, vts & 0xffff, ret);
}

static int ov64b40_start_streaming(struct ov64b40 *ov64b40,
				   struct v4l2_subdev_state *state)
{
	int ret;

	ret = pm_runtime_resume_and_get(ov64b40->dev);
	if (ret < 0)
		return ret;

	ret = cci_write(ov64b40->cci, OV64B40_REG_SOFT_RESET, 0x01, NULL);
	if (ret)
		goto error_power_off;
	fsleep(5000);

	ret = cci_multi_reg_write(ov64b40->cci, ov64b40_init,
				  ARRAY_SIZE(ov64b40_init), NULL);
	if (ret)
		goto error_power_off;

	ret = cci_multi_reg_write(ov64b40->cci, ov64b40_4624x3472,
				  ARRAY_SIZE(ov64b40_4624x3472), NULL);
	if (ret)
		goto error_power_off;

	ret = __v4l2_ctrl_handler_setup(&ov64b40->ctrl_handler);
	if (ret)
		goto error_power_off;

	cci_write(ov64b40->cci, OV64B40_REG_3019, 0xd2, &ret);
	cci_write(ov64b40->cci, OV64B40_REG_4308, 0x00, &ret);
	cci_write(ov64b40->cci, OV64B40_REG_STREAM, 0x01, &ret);
	if (ret)
		goto error_power_off;

	return 0;

error_power_off:
	pm_runtime_put(ov64b40->dev);
	return ret;
}

static int ov64b40_stop_streaming(struct ov64b40 *ov64b40)
{
	cci_write(ov64b40->cci, OV64B40_REG_STREAM, 0x00, NULL);
	pm_runtime_put(ov64b40->dev);

	return 0;
}

static int ov64b40_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov64b40 *ov64b40 = sd_to_ov64b40(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	if (enable)
		ret = ov64b40_start_streaming(ov64b40, state);
	else
		ret = ov64b40_stop_streaming(ov64b40);
	v4l2_subdev_unlock_state(state);

	return ret;
}

static void ov64b40_fill_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->width = OV64B40_WIDTH;
	fmt->height = OV64B40_HEIGHT;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_NONE;
}

static int ov64b40_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_rect *crop = v4l2_subdev_state_get_crop(state, 0);

	ov64b40_fill_format(v4l2_subdev_state_get_format(state, 0));

	crop->left = 0;
	crop->top = 0;
	crop->width = OV64B40_NATIVE_WIDTH;
	crop->height = OV64B40_NATIVE_HEIGHT;

	return 0;
}

static int ov64b40_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov64b40_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index || fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width = OV64B40_WIDTH;
	fse->max_width = OV64B40_WIDTH;
	fse->min_height = OV64B40_HEIGHT;
	fse->max_height = OV64B40_HEIGHT;

	return 0;
}

static int ov64b40_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *fmt)
{
	ov64b40_fill_format(&fmt->format);
	*v4l2_subdev_state_get_format(state, 0) = fmt->format;

	return 0;
}

static int ov64b40_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = OV64B40_NATIVE_WIDTH;
		sel->r.height = OV64B40_NATIVE_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_subdev_video_ops ov64b40_video_ops = {
	.s_stream = ov64b40_set_stream,
};

static const struct v4l2_subdev_pad_ops ov64b40_pad_ops = {
	.enum_mbus_code = ov64b40_enum_mbus_code,
	.enum_frame_size = ov64b40_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = ov64b40_set_fmt,
	.get_selection = ov64b40_get_selection,
};

static const struct v4l2_subdev_ops ov64b40_subdev_ops = {
	.video = &ov64b40_video_ops,
	.pad = &ov64b40_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov64b40_internal_ops = {
	.init_state = ov64b40_init_state,
};

static int ov64b40_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov64b40 *ov64b40 = sd_to_ov64b40(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ov64b40_supply_names),
				    ov64b40->supplies);
	if (ret) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ov64b40->xclk);
	if (ret) {
		regulator_bulk_disable(ARRAY_SIZE(ov64b40_supply_names),
				       ov64b40->supplies);
		return ret;
	}

	gpiod_set_value_cansleep(ov64b40->reset_gpio, 0);
	fsleep(10000);

	return 0;
}

static int ov64b40_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct ov64b40 *ov64b40 = sd_to_ov64b40(sd);

	gpiod_set_value_cansleep(ov64b40->reset_gpio, 1);
	clk_disable_unprepare(ov64b40->xclk);
	regulator_bulk_disable(ARRAY_SIZE(ov64b40_supply_names),
			       ov64b40->supplies);

	return 0;
}

static int ov64b40_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov64b40 *ov64b40 = container_of(ctrl->handler, struct ov64b40,
					       ctrl_handler);
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		int exp_max = OV64B40_HEIGHT + ctrl->val - OV64B40_EXPOSURE_MARGIN;

		__v4l2_ctrl_modify_range(ov64b40->exposure,
					 ov64b40->exposure->minimum, exp_max,
					 1, min(ov64b40->exposure->val, exp_max));
	}

	if (!pm_runtime_get_if_in_use(ov64b40->dev))
		return 0;

	cci_write(ov64b40->cci, OV64B40_REG_GROUP_HOLD,
		  OV64B40_GROUP_HOLD_START, &ret);

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		cci_write(ov64b40->cci, OV64B40_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(ov64b40->cci, OV64B40_REG_ANA_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		cci_write(ov64b40->cci, OV64B40_REG_DIG_GAIN, ctrl->val, &ret);
		break;
	case V4L2_CID_VBLANK:
		ov64b40_write_vts(ov64b40, OV64B40_HEIGHT + ctrl->val, &ret);
		break;
	case V4L2_CID_TEST_PATTERN:
		cci_write(ov64b40->cci, OV64B40_REG_TEST_PATTERN,
			  ov64b40_test_pattern_val[ctrl->val], &ret);
		break;
	default:
		dev_err(ov64b40->dev, "Unhandled control: %#x\n", ctrl->id);
		ret = -EINVAL;
		break;
	}

	cci_write(ov64b40->cci, OV64B40_REG_GROUP_HOLD,
		  OV64B40_GROUP_HOLD_END, &ret);
	cci_write(ov64b40->cci, OV64B40_REG_GROUP_HOLD,
		  OV64B40_GROUP_HOLD_LAUNCH, &ret);

	pm_runtime_put(ov64b40->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov64b40_ctrl_ops = {
	.s_ctrl = ov64b40_set_ctrl,
};

static int ov64b40_init_controls(struct ov64b40 *ov64b40)
{
	struct v4l2_ctrl_handler *hdlr = &ov64b40->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	int ret;

	v4l2_ctrl_handler_init(hdlr, 11);

	v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops, V4L2_CID_PIXEL_RATE,
			  OV64B40_PIXEL_RATE, OV64B40_PIXEL_RATE, 1,
			  OV64B40_PIXEL_RATE);

	ctrl = v4l2_ctrl_new_int_menu(hdlr, &ov64b40_ctrl_ops,
				      V4L2_CID_LINK_FREQ, 0, 0,
				      ov64b40_link_freqs);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops, V4L2_CID_HBLANK,
				 OV64B40_LINE_LENGTH - OV64B40_WIDTH,
				 OV64B40_LINE_LENGTH - OV64B40_WIDTH, 1,
				 OV64B40_LINE_LENGTH - OV64B40_WIDTH);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov64b40->vblank = v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops,
					    V4L2_CID_VBLANK, OV64B40_VBLANK_MIN,
					    OV64B40_VTS_MAX - OV64B40_HEIGHT, 1,
					    OV64B40_VBLANK_MIN);

	ov64b40->exposure = v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OV64B40_EXPOSURE_MIN,
					      OV64B40_VTS - OV64B40_EXPOSURE_MARGIN,
					      1,
					      OV64B40_VTS - OV64B40_EXPOSURE_MARGIN);

	v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV64B40_ANA_GAIN_MIN, OV64B40_ANA_GAIN_MAX, 1,
			  OV64B40_ANA_GAIN_DEFAULT);

	v4l2_ctrl_new_std(hdlr, &ov64b40_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV64B40_DIG_GAIN_MIN, OV64B40_DIG_GAIN_MAX, 1,
			  OV64B40_DIG_GAIN_DEFAULT);

	v4l2_ctrl_new_std_menu_items(hdlr, &ov64b40_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov64b40_test_pattern_menu) - 1,
				     0, 0, ov64b40_test_pattern_menu);

	if (hdlr->error) {
		ret = hdlr->error;
		dev_err(ov64b40->dev, "control init failed: %d\n", ret);
		goto error_free_hdlr;
	}

	ret = v4l2_fwnode_device_parse(ov64b40->dev, &props);
	if (ret)
		goto error_free_hdlr;

	ret = v4l2_ctrl_new_fwnode_properties(hdlr, &ov64b40_ctrl_ops, &props);
	if (ret)
		goto error_free_hdlr;

	ov64b40->sd.ctrl_handler = hdlr;

	return 0;

error_free_hdlr:
	v4l2_ctrl_handler_free(hdlr);
	return ret;
}

static int ov64b40_identify(struct ov64b40 *ov64b40)
{
	u64 id;
	int ret;

	ret = cci_read(ov64b40->cci, OV64B40_REG_CHIP_ID, &id, NULL);
	if (ret) {
		dev_err(ov64b40->dev, "Failed to read chip id: %d\n", ret);
		return ret;
	}

	if (id != OV64B40_CHIP_ID) {
		dev_err(ov64b40->dev, "chip id mismatch: %#llx\n", id);
		return -ENODEV;
	}

	return 0;
}

static int ov64b40_parse_dt(struct ov64b40 *ov64b40)
{
	struct v4l2_fwnode_endpoint v4l2_fwnode = {
		.bus_type = V4L2_MBUS_CSI2_CPHY
	};
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(ov64b40->dev),
						  NULL);
	if (!endpoint) {
		dev_err(ov64b40->dev, "Failed to find endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &v4l2_fwnode);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(ov64b40->dev, "Failed to parse endpoint\n");
		return ret;
	}

	if (v4l2_fwnode.bus.mipi_csi2.num_data_lanes != 3) {
		dev_err(ov64b40->dev, "Unsupported number of trios: %u\n",
			v4l2_fwnode.bus.mipi_csi2.num_data_lanes);
		return -EINVAL;
	}

	return 0;
}

static int ov64b40_probe(struct i2c_client *client)
{
	struct ov64b40 *ov64b40;
	unsigned long xclk_freq;
	unsigned int i;
	int ret;

	ov64b40 = devm_kzalloc(&client->dev, sizeof(*ov64b40), GFP_KERNEL);
	if (!ov64b40)
		return -ENOMEM;

	ov64b40->dev = &client->dev;
	v4l2_i2c_subdev_init(&ov64b40->sd, client, &ov64b40_subdev_ops);

	ov64b40->cci = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ov64b40->cci))
		return dev_err_probe(&client->dev, PTR_ERR(ov64b40->cci),
				     "Failed to initialize CCI\n");

	ret = ov64b40_parse_dt(ov64b40);
	if (ret)
		return ret;

	ov64b40->xclk = devm_v4l2_sensor_clk_get(&client->dev, NULL);
	if (IS_ERR(ov64b40->xclk))
		return dev_err_probe(&client->dev, PTR_ERR(ov64b40->xclk),
				     "Failed to get clock\n");

	xclk_freq = clk_get_rate(ov64b40->xclk);
	if (xclk_freq != OV64B40_XCLK_FREQ)
		return dev_err_probe(&client->dev, -EINVAL,
				     "Unsupported xclk frequency %lu\n",
				     xclk_freq);

	for (i = 0; i < ARRAY_SIZE(ov64b40_supply_names); i++)
		ov64b40->supplies[i].supply = ov64b40_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(ov64b40_supply_names),
				      ov64b40->supplies);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Failed to get regulators\n");

	ov64b40->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						      GPIOD_OUT_HIGH);
	if (IS_ERR(ov64b40->reset_gpio))
		return dev_err_probe(&client->dev, PTR_ERR(ov64b40->reset_gpio),
				     "Failed to get reset gpio\n");

	ret = ov64b40_power_on(&client->dev);
	if (ret)
		return ret;

	ret = ov64b40_identify(ov64b40);
	if (ret)
		goto error_poweroff;

	ret = ov64b40_init_controls(ov64b40);
	if (ret)
		goto error_poweroff;

	ov64b40->sd.internal_ops = &ov64b40_internal_ops;
	ov64b40->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov64b40->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ov64b40->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov64b40->sd.entity, 1, &ov64b40->pad);
	if (ret)
		goto error_free_ctrls;

	ov64b40->sd.state_lock = ov64b40->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&ov64b40->sd);
	if (ret)
		goto error_cleanup_entity;

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev, 1000);
	pm_runtime_use_autosuspend(&client->dev);

	ret = v4l2_async_register_subdev_sensor(&ov64b40->sd);
	if (ret)
		goto error_pm;

	pm_runtime_put_autosuspend(&client->dev);

	return 0;

error_pm:
	pm_runtime_disable(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	v4l2_subdev_cleanup(&ov64b40->sd);
error_cleanup_entity:
	media_entity_cleanup(&ov64b40->sd.entity);
error_free_ctrls:
	v4l2_ctrl_handler_free(&ov64b40->ctrl_handler);
error_poweroff:
	ov64b40_power_off(&client->dev);

	return ret;
}

static void ov64b40_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov64b40 *ov64b40 = sd_to_ov64b40(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&ov64b40->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		ov64b40_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct of_device_id ov64b40_of_ids[] = {
	{ .compatible = "ovti,ov64b40" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ov64b40_of_ids);

static DEFINE_RUNTIME_DEV_PM_OPS(ov64b40_pm_ops, ov64b40_power_off,
				 ov64b40_power_on, NULL);

static struct i2c_driver ov64b40_i2c_driver = {
	.driver	= {
		.name = "ov64b40",
		.of_match_table	= ov64b40_of_ids,
		.pm = pm_ptr(&ov64b40_pm_ops),
	},
	.probe	= ov64b40_probe,
	.remove	= ov64b40_remove,
};
module_i2c_driver(ov64b40_i2c_driver);

MODULE_DESCRIPTION("OmniVision OV64B40 sensor driver");
MODULE_LICENSE("GPL");
