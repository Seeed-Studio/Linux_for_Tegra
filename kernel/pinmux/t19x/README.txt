pinmux dts2cfg
--------------

This tool converts pinmux, gpio and pad dts file to cfg format.
Usage:
	python pinmux-dts2cfg.py [--pad | --pinmux] arguments...
where arguments depend if user passed --pad or --pinmux parameter. The --pinmux is optional.

0. FILE DETAILS
---------------
Below is the description of each file in this folder for reference:

pinmux-dts2cfg.py - Script tool used to generate CFG format file from given pinmux, gpio, pad platform dts files.
addr_info.txt - Contains the database of pins available on tegra. Information includes, pin name, address, pinmux functionality of pin.
gpio_addr_info.txt - Contains the database of tegra gpio port name and its base address.
pad_info.txt -  Contains the database of tegra pads. Information includes pad name, voltage type (1.2V, 1.8V, 3.3V) and bit fields information in pad register.
por_val.txt - Contains the database of pinmux address and value pairs which should not be modified while running the tool.

1. PINMUX and GPIO
------------------

To convert pinmux and gpio file to cfg format, use the following command line:
	python pinmux-dts2cfg.py [--pinmux] ADDRESS_FILE GPIO_ADDRESS_FILE POR_VAL_FILE MANDATORY_PINMUX_FILE PINMUX_DTS GPIO_DTS VERSION [--help]
Here
	--pinmux is an optional parameter and is default one (as opposite of --pad)
	--mask is an optional parameter that controls the output (useful to build GR sheets)
	--help shows the help
	ADDRESS_FILE is the address info file
	GPIO_ADDRESS_FILE is the GPIO address info file
	POR_VAL_FILE is the por val pair info file
	--mandatory_pinmux_file MANDATORY_PINMUX_FILE_NAME is optional pinmux values info file
	PINMUX_DTS is the device tree source file for pinmux
	GPIO_DTS is the device tree source file for GPIO
	VERSION is the numeric version, like 1.0

The resulting .cfg file will be printed to standard output, so redirect it to the file
Example:
	python pinmux-dts2cfg.py \
		--pinmux					\
		addr_info.txt gpio_addr_info.txt por_val.txt	\
		--mandatory_pinmux_file mandatory_pinmux.txt				\
		tegra19x-p2888_galen_devkit-pinmux.dtsi \
		tegra19x-p2888_galen_devkit-gpio-default.dtsi \
		1.0 						\
			> galen.cfg

2. PAD VOLTAGES
---------------

To convert pad file to cfg format, use the following command line:
	python pinmux-dts2cfg.py --pad PAD_FILE PAD_DTS VERSION [--mask] [--help]
Here
	--mask is an optional parameter that controls the output (useful to build GR sheets)
	--help shows the help
	PAD_FILE is the pad info file
	PAD_DTS is the device tree source file for pads
	VERSION is the numeric version, like 1.0
The resulting .cfg file will be printed to standard output, so redirect it to the file
Example:
	python pinmux-dts2cfg.py \
		--pad							   \
		pad_info.txt						   \
		tegra19x-e3550-b00-x1-padvoltage-default.dtsi	   \
		1.0 							   \
			> tegra19x-mb1-pad-e3550-b00.cfg

3.NOTE
------

Make sure there is no blank line at the end of address info file.

4.MANDATORY PINMUX
------------------

Mandatory pinmux information file entry should follow below format for an entry.

e.g. For pin cam_i2c_sda_po3 with function i2c3 and trisate, enable-input, lpdr as enabled below entry should be added.
cam_i2c_sda_po3:nvidia,function=i2c3:nvidia,tristate=<TEGRA_PIN_ENABLE>:nvidia,enable-input=<TEGRA_PIN_ENABLE>:nvidia,lpdr=<TEGRA_PIN_ENABLE>

Tool currently supports below fields and value.

nvidia,pull supports <TEGRA_PIN_NONE> or <TEGRA_PIN_PULL_DOWN> or <TEGRA_PIN_PULL_UP> or <TEGRA_PIN_RSVD>
nvidia,tristate supports <TEGRA_PIN_ENABLE> or <TEGRA_PIN_DISABLE>
nvidia,park supports <TEGRA_PIN_PARKED> or <TEGRA_PIN_NORMAL>
nvidia,enable-input supports <TEGRA_PIN_ENABLE> or <TEGRA_PIN_DISABLE>
nvidia,lock supports <TEGRA_PIN_ENABLE> or <TEGRA_PIN_DISABLE>
nvidia,lpdr supports <TEGRA_PIN_ENABLE> or <TEGRA_PIN_DISABLE>
