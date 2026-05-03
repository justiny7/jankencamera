#include "imx219.h"
#include "i2c.h"
#include "gpio.h"
#include "lib.h"
#include "mailbox_interface.h"
#include "sys_timer.h"

#define CAM_GPIO_PIN  44
#define CM_BASE       (0x20101000 | KERNEL_VBASE)
#define CM_CAM1CTL    (CM_BASE + 0x48)
#define CM_CAM1DIV    (CM_BASE + 0x4C)
#define CM_PASSWD     0x5A000000

static I2C g_i2c;
static IMX219ModeInfo g_mode;
static bool g_powered = false;

typedef struct { uint16_t reg; uint8_t val; } RegVal;

static const RegVal regs_common[] = {
    {0x0100, 0x00},
    {0x30EB, 0x05}, {0x30EB, 0x0C}, {0x300A, 0xFF}, {0x300B, 0xFF},
    {0x30EB, 0x05}, {0x30EB, 0x09}, {0x0114, 0x01}, {0x0128, 0x00},
    {0x012A, 0x18}, {0x012B, 0x00}, {0}
};

static const RegVal regs_640x480[] = {
    {0x0164, 0x03}, {0x0165, 0xE8}, {0x0166, 0x08}, {0x0167, 0xE7},
    {0x0168, 0x02}, {0x0169, 0xF0}, {0x016A, 0x06}, {0x016B, 0xAF},
    {0x016C, 0x02}, {0x016D, 0x80}, {0x016E, 0x01}, {0x016F, 0xE0},
    {0x0170, 0x01}, {0x0171, 0x01}, {0x0174, 0x03}, {0x0175, 0x03},
    {0x0301, 0x05}, {0x0303, 0x01}, {0x0304, 0x03}, {0x0305, 0x03},
    {0x0306, 0x00}, {0x0307, 0x39}, {0x030B, 0x01}, {0x030C, 0x00},
    {0x030D, 0x72}, {0x0624, 0x06}, {0x0625, 0x68}, {0x0626, 0x04},
    {0x0627, 0xD0}, {0x455E, 0x00}, {0x471E, 0x4B}, {0x4767, 0x0F},
    {0x4750, 0x14}, {0x4540, 0x00}, {0x47B4, 0x14}, {0x4713, 0x30},
    {0x478B, 0x10}, {0x478F, 0x10}, {0x4793, 0x10}, {0x4797, 0x0E},
    {0x479B, 0x0E}, {0}
};

static const RegVal regs_1920x1080[] = {
    {0x0164, 0x02}, {0x0165, 0xA8}, {0x0166, 0x0A}, {0x0167, 0x27},
    {0x0168, 0x02}, {0x0169, 0xB4}, {0x016A, 0x06}, {0x016B, 0xEB},
    {0x016C, 0x07}, {0x016D, 0x80}, {0x016E, 0x04}, {0x016F, 0x38},
    {0x0170, 0x01}, {0x0171, 0x01}, {0x0174, 0x00}, {0x0175, 0x00},
    {0x0301, 0x05}, {0x0303, 0x01}, {0x0304, 0x03}, {0x0305, 0x03},
    {0x0306, 0x00}, {0x0307, 0x39}, {0x030B, 0x01}, {0x030C, 0x00},
    {0x030D, 0x72}, {0x0624, 0x07}, {0x0625, 0x80}, {0x0626, 0x04},
    {0x0627, 0x38}, {0x455E, 0x00}, {0x471E, 0x4B}, {0x4767, 0x0F},
    {0x4750, 0x14}, {0x4540, 0x00}, {0x47B4, 0x14}, {0x4713, 0x30},
    {0x478B, 0x10}, {0x478F, 0x10}, {0x4793, 0x10}, {0x4797, 0x0E},
    {0x479B, 0x0E}, {0}
};

static const RegVal regs_raw8[] = {
    {0x018C, 0x08}, {0x018D, 0x08}, {0x0309, 0x08}, {0}
};

static const RegVal regs_raw10[] = {
    {0x018C, 0x0A}, {0x018D, 0x0A}, {0x0309, 0x0A}, {0}
};

static bool write_reg(uint16_t reg, uint8_t val) {
    uint8_t buf[3] = { reg >> 8, reg & 0xFF, val };
    return i2c_send_data(&g_i2c, 3, buf) == I2C_RESULT_OK;
}

static bool write_reg16(uint16_t reg, uint16_t val) {
    uint8_t buf[4] = { reg >> 8, reg & 0xFF, val >> 8, val & 0xFF };
    return i2c_send_data(&g_i2c, 4, buf) == I2C_RESULT_OK;
}

static bool read_reg16(uint16_t reg, uint16_t* val) {
    uint8_t buf[2] = { reg >> 8, reg & 0xFF };
    if (i2c_send_data(&g_i2c, 2, buf) != I2C_RESULT_OK) return false;
    if (i2c_receive_data(&g_i2c, 2, buf) != I2C_RESULT_OK) return false;
    *val = (buf[0] << 8) | buf[1];
    return true;
}

static bool write_regs(const RegVal* regs) {
    for (; regs->reg; regs++) {
        if (!write_reg(regs->reg, regs->val)) return false;
    }
    return true;
}

static void set_cam_power(bool on) {
    gpio_select((Pin){CAM_GPIO_PIN}, GPIO_OUTPUT);
    if (on) {
        gpio_set_high((Pin){CAM_GPIO_PIN});
    } else {
        gpio_set_low((Pin){CAM_GPIO_PIN});
    }
    sys_timer_delay_us(100);
    PinOutput level = gpio_read((Pin){CAM_GPIO_PIN});
    printk("imx219: GPIO %d set to %s, read back=%d\n", CAM_GPIO_PIN, on ? "HIGH" : "LOW", level);
}

bool imx219_init() {
    set_cam_power(true);
    g_powered = true;
    sys_timer_delay_us(6200);
    
    g_i2c.bsc = BSC0;
    g_i2c.sda = (Pin) { 28 };
    g_i2c.scl = (Pin) { 29 };
    g_i2c.speed_hz = 100000;
    g_i2c.slave_addr = IMX219_I2C_ADDR;
    
    gpio_set_pull(g_i2c.sda, GPIO_PULL_UP);
    gpio_set_pull(g_i2c.scl, GPIO_PULL_UP);
    i2c_init(&g_i2c);

    return imx219_probe();
}

void imx219_deinit() {
    if (g_powered) {
        write_reg(IMX219_MODE_SELECT, IMX219_MODE_STANDBY);
        set_cam_power(false);
        g_powered = false;
    }
}

bool imx219_probe() {
    uint16_t chip_id;
    if (!read_reg16(IMX219_CHIP_ID_REG, &chip_id)) return false;
    if (chip_id != IMX219_CHIP_ID) return false;
    
    write_reg(IMX219_MODE_SELECT, IMX219_MODE_STREAMING);
    sys_timer_delay_us(100);
    write_reg(IMX219_MODE_SELECT, IMX219_MODE_STANDBY);
    sys_timer_delay_us(100);
    
    return true;
}

bool imx219_set_mode(IMX219Mode mode, uint8_t depth) {
    write_reg(IMX219_MODE_SELECT, IMX219_MODE_STANDBY);
    
    uint8_t val = 0xFF;
    uint8_t buf[2] = { 0x01, 0x00 };
    i2c_send_data(&g_i2c, 2, buf);
    i2c_receive_data(&g_i2c, 1, &val);
    printk("imx219: mode_select after standby = %x (expect 0)\n", val);
    
    if (!write_regs(regs_common)) return false;

    const RegVal* mode_regs;
    switch (mode) {
        case IMX219_MODE_640x480:
            mode_regs = regs_640x480;
            g_mode = (IMX219ModeInfo){640, 480, 0x6E3, 2};
            break;
        case IMX219_MODE_1920x1080:
            mode_regs = regs_1920x1080;
            g_mode = (IMX219ModeInfo){1920, 1080, 0x6E3, 1};
            break;
        default:
            mode_regs = regs_640x480;
            g_mode = (IMX219ModeInfo){640, 480, 0x6E3, 2};
            break;
    }

    if (!write_regs(mode_regs)) return false;
    if (!write_regs(depth == 10 ? regs_raw10 : regs_raw8)) return false;
    
    uint8_t lane_mode = 0xFF;
    uint8_t buf2[2] = { 0x01, 0x14 };
    i2c_send_data(&g_i2c, 2, buf2);
    i2c_receive_data(&g_i2c, 1, &lane_mode);
    printk("imx219: CSI_LANE_MODE (0x0114) = %x (expect 1 for 2 lanes)\n", lane_mode);
    return true;
}

IMX219ModeInfo imx219_get_mode_info() {
    return g_mode;
}

bool imx219_start_streaming() {
    if (!write_reg(IMX219_MODE_SELECT, IMX219_MODE_STREAMING)) {
        printk("imx219: failed to start streaming\n");
        return false;
    }
    sys_timer_delay_us(50000);
    
    uint16_t frame_count = 0;
    read_reg16(0x0005, &frame_count);
    printk("imx219: frame_count=%x\n", frame_count);
    
    uint16_t status = 0;
    read_reg16(0x0000, &status);
    printk("imx219: chip_id=%x (should be 0x0219)\n", status);
    
    uint8_t mode = 0;
    uint8_t buf[2] = { IMX219_MODE_SELECT >> 8, IMX219_MODE_SELECT & 0xFF };
    i2c_send_data(&g_i2c, 2, buf);
    i2c_receive_data(&g_i2c, 1, &mode);
    printk("imx219: mode_select=%x (1=streaming)\n", mode);
    return true;
}

void imx219_stop_streaming() {
    write_reg(IMX219_MODE_SELECT, IMX219_MODE_STANDBY);
}

bool imx219_set_exposure(uint16_t lines) {
    return write_reg16(IMX219_EXPOSURE, lines / g_mode.rate_factor);
}

bool imx219_set_gain(uint8_t gain) {
    if (gain > IMX219_GAIN_MAX) gain = IMX219_GAIN_MAX;
    return write_reg(IMX219_ANALOG_GAIN, gain);
}

bool imx219_set_digital_gain(uint16_t gain) {
    if (gain < IMX219_DGAIN_MIN) gain = IMX219_DGAIN_MIN;
    if (gain > IMX219_DGAIN_MAX) gain = IMX219_DGAIN_MAX;
    return write_reg16(IMX219_DIGITAL_GAIN, gain);
}

bool imx219_set_vflip(bool enable) {
    uint8_t buf[2], val;
    buf[0] = IMX219_ORIENTATION >> 8;
    buf[1] = IMX219_ORIENTATION & 0xFF;
    if (i2c_send_data(&g_i2c, 2, buf) != I2C_RESULT_OK) return false;
    if (i2c_receive_data(&g_i2c, 1, &val) != I2C_RESULT_OK) return false;
    val = enable ? (val | 2) : (val & ~2);
    return write_reg(IMX219_ORIENTATION, val);
}

bool imx219_set_hflip(bool enable) {
    uint8_t buf[2], val;
    buf[0] = IMX219_ORIENTATION >> 8;
    buf[1] = IMX219_ORIENTATION & 0xFF;
    if (i2c_send_data(&g_i2c, 2, buf) != I2C_RESULT_OK) return false;
    if (i2c_receive_data(&g_i2c, 1, &val) != I2C_RESULT_OK) return false;
    val = enable ? (val | 1) : (val & ~1);
    return write_reg(IMX219_ORIENTATION, val);
}
