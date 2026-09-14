## Introduction

Turn a desktop, router, or server running the Linux operating system into a reliable programmable logic controller using the i2c-stm32-usb bridge and the in-kernel Linux driver `hid-cp2112.c`, which supports [GPIO with IRQs](https://www.kernel.org/doc/html/latest/driver-api/gpio/driver.html) and the [Industrial I/O (IIO)](https://www.kernel.org/doc/html/latest/driver-api/iio/index.html) subsystem.

The i2c-stm32-usb project is an open-source emulator project of the CP2112 chip for microcontrollers of the STM32 family, specifically the STM32F103 family and its numerous Chinese clones. Widely known ready-made Blue Pill boards priced around $1.5 are available for order on eBay and AliExpress. Compared to the original CP2112 chip, which is built on the MCS-51 architecture, the emulator running on an ARM architecture microcontroller works more reliably and faster. The original chip has critical bugs up to revision 3, whereas the emulator is free from them. The original chip uses part of the GPIO pins for LEDs, while the emulator provides separate LED pins and 8 dedicated GPIO pins. The original chip incorrectly illuminates LEDs on composite read-write commands, whereas the emulator faithfully lights both read and write LEDs simultaneously. Even according to its datasheet, the original chip does not reliably pull off a 100 kHz frequency. Thanks to the higher performance of the STM32F103, `cp2112_emulator` operates seamlessly at 100 kHz and demonstrates faster system read times for 24C256 memory in benchmarks. The emulator code occupies only about 10 KB of flash memory, leaving a huge margin for adding various security and safety features required by an industrial controller. Any USB hangups or crashes can be completely eliminated for typical operational scenarios.

Why was CP2112 chosen for writing the emulator? There are only a few chips for which the Linux kernel driver supports the system I2C bus. These are drivers for:

*  [i2c-tiny-usb](https://github.com/harbaum/I2C-Tiny-USB)
*  cp2112
*  mcp2221a
*  ft260

Examining them in detail, `i2c-tiny-usb` has no GPIO at all, while the latter two do not have their GPIO connected to the Linux kernel IRQ subsystem. Thus, the CP2112 driver is unique among Linux USB converter drivers, allowing developers to write event-driven programs instead of continuously polling inputs. Furthermore, the FT260 is not widely spread and lacks a ready-to-use driver for OpenWrt Linux routers, while the `i2c-tiny-usb` driver clutters the `dmesg` log when running `i2cdetect` on empty addresses. Unfortunately, this is a feature of its driver where a normal scenario of device absence on the I2C bus is reported directly to the Linux system log. USB traffic analysis shows the highest performance potential for CP2112 and `i2c-tiny-usb` when transitioning to a modern microcontroller. Unfortunately, the [i2c-star](https://github.com/daniel-thompson/i2c-star) project, which uses the `i2c-tiny-usb` driver, was abandoned many years ago with a bug causing erroneous 24C256 reads on every 128th bit and debug mode permanently enabled, tripling the code size. Therefore, CP2112 is currently the best candidate for writing open-source emulation code.

## Software

The emulator project is built on the open-source [libopencm3](https://github.com/libopencm3/libopencm3) library.

The list of supported CP2112 commands was taken from the open document [AN495: CP2112 Interface Specification](https://www.silabs.com/documents/public/application-notes/an495-cp2112-interface-specification.pdf) and verified against the source code of the [hid-cp2112.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-cp2112.c) driver.

A comparison showed that by no means everything is used by the Linux kernel driver. For example, request `0x01 Device Reset` is not used in the driver due to bugs in early revisions of the chip. Consequently, the emulator does not support this request. Another feature relates to Autosend. The chip designers implemented automatic sending but poorly thought out the interaction. In practice, Autosend is disabled in Linux and not recommended for use in Windows. Data transfer always occurs reactively in response to a corresponding host request. Another feature of the emulator's operation is that CP2112 is declared as an SMBus controller and has its own slave address in case multiple masters are present on the bus. This feature is currently not implemented in the emulator, as the main workload is intended for an I2C bus with a single master. It can be added, but there is no need so far. Even when reading battery controllers, it is always done outside the primary host device, with only one master present. The Linux driver is hard-coded for an SCL frequency of 100 kHz, so the emulator is programmed to work at this frequency. Moving to 400 kHz seems impractical because main delays occur during the USB exchange stage. Increasing the frequency would limit the range of compatible ICs while yielding minimal exchange performance gains.

For compatibility with existing software, the emulator supports programming USB parameters, but stores settings in volatile RAM. Upon power-down, all these settings revert to default values. If your usage scenario requires specific custom settings, they can be configured directly in the source descriptors without extra programming steps.

## Compatible Microcontrollers

The utilized `libopencm3` library supports a variety of microcontrollers, among which the most accessible currently belong to the STM32F1 series. It should be noted that purchasing genuine STM32F103 ICs individually or on cheap boards via eBay and AliExpress is virtually impossible unless salvaged from electronic scrap. Almost all boards feature relabeled Chinese chips, and determining the true manufacturer is non-trivial. To ensure the emulator runs reliably on clones, minor delays were added to the code. In practice, the emulator worked on all tested Chinese boards, although displaying a slight variance in SCL frequency from clone to clone due to differing memory and bus access times. Without calibration (using default project settings), observed frequencies ranged from 80 to 95 kHz. This can easily be adjusted by tuning delay values if needed. I2C communication is implemented via bit-banging/software, since even in authentic STM32F103 chips this hardware module contains numerous critical bugs, and on clones things can be worse. The software I2C implemented works at 100 kHz with total stability and reliability while supporting sensor clock stretching.

Among non-relabeled microcontrollers, the device has been successfully tested on APM32F103 and GD32F103.

## Building the Project and Flashing the Microcontroller

Since the project is distributed in source code format, build output depends on the installed version of the `gcc-arm-none-eabi` compiler.

You can check your version with:

```bash
$ arm-none-eabi-gcc --version
```

The project has been confirmed to compile up to `arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1 20241119`.

To build, clone the entire repository recursively.

 1. git clone --recurse-submodules https://github.com/AntonKxxx/i2c-stm32-usb our-project
 2. cd our-project 
 3. make -C libopencm3 # (Only needed once)
 4. make clean -C src_72mhz 
 5. make -C src_72mhz # This will build a `.bin` file used to flash the microcontroller.

Flashing the microcontroller can be done in several ways. The most reliable method is using the built-in BOOT0 serial bootloader. This has been verified to work on virtually all Chinese clones, provided they haven't placed excessively large resistors in the BOOT0 jumper circuit.

Flashing requires installing the `stm32flash` utility. You will also need a serial port via USB or connected directly to `ttyS0` with 3.3V logic levels.

Connect TX and RX to PA9 and PA10 according to microcontroller documentation. Do not swap wires; on Chinese converter boards, TX does not always mean transmitter. Verify with a multimeter that logic high (3.3V) is present when idle. This indicates an output line, which should be connected to the RXD (PA10) input of the microcontroller.

Command to flash when connected to `ttyACM0`:

```bash
stm32flash -w cp2112_emulator.bin -v -g 0x08000000 /dev/ttyACM0
```

Alternatively, an ST-Link debugger can be used, though it may not work with all clone chips:

```bash
sudo apt install stlink-tools
st-flash write cp2112_emulator.bin 0x08000000
```

## Hardware Connections

    GPIO0 -> PA0
    GPIO1 -> PA1
    GPIO2 -> PA2
    GPIO3 -> PA3
    GPIO4 -> PA4
    GPIO5 -> PA5
    GPIO6 -> PA6
    GPIO7 -> PA7

    SCL -> PB6 (external I2C pull-up resistor required, e.g., 4.7kΩ to 3.3V)
    SDA -> PB7 (external I2C pull-up resistor required, e.g., 4.7kΩ to 3.3V)

    READ_LED -> PB1
    WRITE_LED -> PB10
    CLOCK_OK_LED -> PC13

## Verification and Testing

The `i2c-tools` package is typically used to interact with the I2C bus.

After connecting the emulator, check if it is recognized using `lsusb`:

It should appear approximately as follows:

```bash
Bus 001 Device 009: ID 10c4:ea90 Silicon Labs CP2112 HID I2C Bridge
```

If the device is visible, run:

```bash
sudo i2cdetect -l
```

This will list all available I2C buses.

If the list is empty, on some Debian-derived distributions execute as root:

```bash
 # modprobe i2c-dev
```

Then repeat:

```bash
sudo i2cdetect -l
```

You should see the emulator listed similarly to:

```bash
i2c-6   i2c             CP2112 SMBus Bridge on hidraw3          I2C adapter
```

This indicates the emulator reserved bus 6. To query the emulator capabilities list:

```bash
$ sudo i2cdetect 6 -F
Functionalities implemented by /dev/i2c-6:
I2C                              yes
SMBus Quick Command              no
SMBus Send Byte                  yes
SMBus Receive Byte               yes
SMBus Write Byte                 yes
SMBus Read Byte                  yes
SMBus Write Word                 yes
SMBus Read Word                  yes
SMBus Process Call               yes
SMBus Block Write                yes
SMBus Block Read                 yes
SMBus Block Process Call         yes
SMBus PEC                        no
I2C Block Write                  yes
I2C Block Read                   yes
SMBus Host Notify                no
10-bit addressing                no
Target mode                      no
```

As shown, the driver operates via SMBus commands; thus, instead of:

```bash
$ sudo i2cdetect -y 6
Warning: Can't use SMBus Quick Write command, will skip some addresses
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                                                 
10:                                                 
20:                                                 
30: -- -- -- -- -- -- -- --                         
40:                                                 
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60:                                                 
70:                                          
```

to scan devices across all available addresses, use:

```bash
$ sudo i2cdetect -y -r 6
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: 20 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --                      
```

From the matrix, PCF8574 is attached at address `0x20` and 24C256 at address `0x50`.

To verify GPIO system operation, first identify the chip number with:

```bash
$ sudo gpioinfo
gpiochip1 - 8 lines:
        line   0:       unnamed                 input
        line   1:       unnamed                 input
        line   2:       unnamed                 input
        line   3:       unnamed                 input
        line   4:       unnamed                 input
        line   5:       unnamed                 input
        line   6:       unnamed                 input
        line   7:       unnamed                 input
```

Here, our emulator was assigned `gpiochip1` (information for `gpiochip0` omitted), and all 8 pins are configured as pull-up inputs. Note that command syntax changed significantly following Linux's migration from `gpiod` v1 to v2.

For version 2, read input states using:

```bash
$ sudo gpioget -c gpiochip1 0 1 2 3 4 5 6 7
"0"=active "1"=active "2"=active "3"=active "4"=active "5"=active "6"=active "7"=active
```

When handling interrupts, all inputs are polled by the system in a single request with a 50ms period.

In `gpiod` v2, switching GPIOs from inputs to outputs and setting their state simultaneously is performed as follows:

```bash
$ sudo gpioset -c gpiochip1 -t 0 0=0 1=1 2=0
```

This converts GPIO0, GPIO1, and GPIO2 into output mode, setting GPIO1 to logic high and the remaining two to logic low. `-t 0` specifies an immediate exit while retaining configured output states.

## Benchmark cp2112_emulator

To evaluate emulator performance, a benchmark measuring system read times for a 24C256 EEPROM is used. Boards with this EEPROM are readily available in Arduino supply shops. The benchmark verifies read and write accuracy alongside total execution speed. Write timing is excluded from evaluation as it is dictated by the memory chip characteristics and Linux settings (full writes typically require several minutes).

The benchmark script `benchmark_cat.sh` is defined as follows:

```bash
#!/bin/sh

EEPROM_PATH="/sys/bus/i2c/devices/0-0050/eeprom"
EEPROM_SIZE=32768

if [ ! -f "$EEPROM_PATH" ]; then
    echo "ERROR: EEPROM file not found at $EEPROM_PATH"
    exit 1
fi

# Get start time from /proc/uptime
start_time=$(awk '{print $1}' /proc/uptime)

# Read all data
cat "$EEPROM_PATH" > /dev/null

# Get end time
end_time=$(awk '{print $1}' /proc/uptime)

# Calculate results using awk
awk -v start="$start_time" -v end="$end_time" -v size="$EEPROM_SIZE" 'BEGIN {
    total_ms = (end - start) * 1000;
    byte_ms = total_ms / size;
    byte_us = byte_ms * 1000;
    
    printf "EEPROM 256Kbit read time: %.2f ms
", total_ms;
    printf "Average time per 1 byte:  %.4f ms (or %.2f us)
", byte_ms, byte_us;
}'
```

After saving to `/tmp`, make it executable:

```bash
chmod +x /tmp/benchmark_cat.sh
```

Next, perform the following verification steps:

Check if the kernel module is loaded:

```bash
lsmod | grep at24
```

Bind the 24C256 memory chip to bus #0:

```bash
echo 24c256 0x50 > /sys/bus/i2c/devices/i2c-0/new_device
```

Verify that the `at24` driver created the sysfs node:

```bash
ls -l /sys/bus/i2c/devices/0-0050/
```

Read data from the chip:

```bash
hexdump -C /sys/bus/i2c/devices/0-0050/eeprom
```

Generate a random file matching the full 256 Kbit size:

```bash
dd if=/dev/urandom of=/tmp/test_256.bin bs=1 count=32768
```

Write the test binary into EEPROM:

```bash
cat /tmp/test_256.bin > /sys/bus/i2c/devices/0-0050/eeprom
```

Read data back from EEPROM:

```bash
cat /sys/bus/i2c/devices/0-0050/eeprom > /tmp/result_256.bin
```

Compare original and retrieved files:

```bash
cmp /tmp/test_256.bin /tmp/result_256.bin
cmp -l /tmp/test_256.bin /tmp/result_256.bin | head -n 10
```

If `cmp` outputs nothing, memory operation is flawless—all 32,768 bytes were written and read without errors. If mismatch output appears (e.g., `differ at byte...`), a failure occurred during transfer.

Once write and read verification passes without error, run the timing benchmark:

```bash
/tmp/benchmark_cat.sh 
```

Benchmark results across various microcontrollers and host systems are presented below:

```bash
china original cp2112
root@Server_01:/# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 5640.00 ms
Average time per 1 byte:  0.1721 ms (or 172.12 us)

emulator cp2112 arm-none-eabi-gcc (15:9-2019-q4-0ubuntu1) 9.2.1 20191025
root@Server_01:~# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 5140.00 ms
Average time per 1 byte:  0.1569 ms (or 156.86 us)

emulator cp2112 100kHz arm-none-eabi-gcc (15:13.2.rel1-2) 13.2.1 20231009
root@Itoc:~# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 4890.00 ms
Average time per 1 byte:  0.1492 ms (or 149.23 us)

GD32F103 emulator cp2112
root@Server_01:~# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 5140.00 ms
Average time per 1 byte:  0.1569 ms (or 156.86 us)

i2c-tiny-usb with debug and ERROR
root@Server_01:~# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 4880.00 ms
Average time per 1 byte:  0.1489 ms (or 148.93 us)

Beaglebone Black
debian@beaglebone:~$ /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 3100.00 ms
Average time per 1 byte:  0.0946 ms (or 94.60 us)

86kHz emulator cp2112
root@Itoc:~# /tmp/benchmark_cat.sh
EEPROM 256Kbit read time: 5400.00 ms
Average time per 1 byte:  0.1648 ms (or 164.79 us)

at Intel(R) Core(TM) i5-10500T CPU @ 2.30GHz
EEPROM 256Kbit read time: 5380.00 ms
Average time per 1 byte:  0.1642 ms (or 164.18 us)
```

Unless otherwise noted, tests were conducted on a Geode LX800 500 MHz system running OpenWrt 25.12. The original CP2112 chip (revision 3) leads the comparison list. It demonstrates slower read performance than both the emulator and alternative adapters. While `i2c-tiny-usb` (flashed with `i2c-star`) executed reads quickly, it produced read errors as noted earlier.

BeagleBone Black is included for maximum throughput baseline comparisons using built-in native hardware I2C without USB overhead.

The `86kHz emulator cp2112` entry represents another STM32F103 clone measured prior to testing. Tested first on the Geode host and subsequently on a powerful desktop CPU, results confirm that emulator throughput remains consistent even on lower-power 500 MHz host processors.

In summary, the `i2c-stm32-usb` project makes it possible to construct a `cp2112_emulator` bridge based on general-purpose microcontrollers that achieves higher throughput than the original CP2112 hardware chip.
