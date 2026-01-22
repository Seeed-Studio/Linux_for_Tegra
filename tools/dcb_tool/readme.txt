------------------------------------------------------------------
DCB Tool
------------------------------------------------------------------

Table of Contents
    1. Description
    2. Terminologies
    3. Command
    4. Example Usage
    5. Limitations


|================== 1. DESCRIPTION ====================|

dcb_tool is command line tool that is used to modify the contents of the DCB blob for a
given dtsi file.

DCB (Display Control Block) describes the display outputs and the configurations
associated with them (explained in detail under section 2) on a given platform/board.

The DCB blob is not readable as it is comprised of hex symbols. This makes it difficult to
decode/edit manually. With the help of the dcb_tool, the contents of the DCB blob can
be displayed in a readable manner. dcb_tool also provides functionality to modify these
values.

|================ 2. TERMINOLOGIES =====================|

The configurations of DCB blob that can be read/modified are,
a) Display Devices, b) CCB, c) Connectors, d) TMDS/DP settings

Possible options under each configuration with a description:

a) Display Devices
    - This represents the output display device. It holds following properties:
        1. Type               - Output type (DP/HDMI/DSI)
        2. CCB                - This is the index of the CCB entry which would be mapped to this device
        3. Heads              - Valid head numbers (starting from 0) to which this output can be attached
        4. SOR/DSI            - SOR Index (or DSI index) of this output device
        5. DP Lane Count      - Number of lanes used by DP link (DP specific property)
        6. DP Link Rate       - Max link rate supported by this DP link (DP specific property)
        7. External Link Type - This field is exclusive to DP and specifies whether the link type is DP Serializer or not.
        8. Connector          - Index from the list of "Connector Entries" to which this output device is mapped
        9. Bus                - Bus number. Two devices with same Bus number can't be enabled simulataneously.
                                Assumption is that these two devices share a hw resource (one SOR/DPAUX shared by two output devices).
                                Bus number can be any positive number, there's no defined range of bus numbers.
        10. HDMI capable      - Indicates whether the device can support TMDS.
        11. Pad Link          - The numeric value of Pad Link associated with the connector assigned to this Display Device.
                                Note: Current version of DCB blob supports Padlink values 0 to 6.

2. CCB
    - Communication Control Block. Each entry stores valid I2C or DPAUX port numbers on this platform.
    - CCB property of "Display Device" holds the index of one of the CCB entries.
    - CCB property is not applicable to DSI device since DSI doesn't use I2C/AUX ports for communication.
    - I2C/AUX Port 31 is currently used to indicate Unused ports.

3. Connectors
    - Connector indicates the physical display output port. E.g.: DP port, HDMI port, DSI connector, DP-over-usbc, SKIP_ENTRY, etc.
    - Connector property of "Display Device" holds the index of one of the CCB entries.
        1. Type     - Output Port Type
        2. Hotplug  - Indicates whether the port (or output protocol) supports hotplug

4. TMDS/DP settings
    - These are the settings that correspond to characterized settings of the output pads (mostly electrical properties).

--------------------------------------------------------
For Example, HDMI connector will have below settings:
--------------------------------------------------------
   Display Devices::
	Display Devices : [0]
		Type               : [TMDS]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		HDMI capable       : [1]
		Connector          : [0]
		Bus                : [0]
		Pad Link           : [0]
    CCB::
	    CCB Index : 0x0
		    I2C Port       : [6]
		    AUX Port       : [0]
    Connectors::
	    Connector Index : 0x0
		    Type           : [HDMI]
		    Hotplug        : A:[Y]

--------------------------------------------------------
For Example, DP connector will have below settings:
--------------------------------------------------------
   Display Devices::
	Display Devices : [0]
		Type               : [DP]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		DP Lane Count      : [4]
		DP Link Rate       : [8.1GHz]
		External Link Type : DP
		Connector          : [0]
		Bus                : [0]
		Pad Link           : [0]
    CCB::
	    CCB Index : 0x0
		    I2C Port       : [6]
		    AUX Port       : [0]
    Connectors::
	    Connector Index : 0x0
		    Type           : [DP]
		    Hotplug        : A:[Y]


|======= 3. COMMAND to Modify/Display DCB Blob ======|

dcb_tool requires two inputs:

1) input dtsi file
specify input dtsi file using -m or -r options

To read:   -r <input_dtsi_file_name>
To modify: -m <input dtsi file name>

2) Chip variant

The chip variant option takes two possible options as inputs.
Based on the platform of use, specify T23X/T25X/T26X accordingly.

-c <T23X/T25X/T26X>

------------------------------------------------------
|Modify DCB blob for T23X platform:                  |
|./dcb_tool -m tegra234-display-dcb.dtsi -c T23X     |
| >>                                                 |
| Display or modify DCB/Disp Macro parameters        |
| >>                                                 |
|                                                    |
| Output:                                            |
| tegra234-display-dcb.dtsi-modified.dtsi            |
|                                                    |
------------------------------------------------------
------------------------------------------------------
|Display/Read for T23X platform:                     |
|./dcb_tool -r tegra234-display-dcb.dtsi -c T23X     |
| >>                                                 |
| Display DCB/Disp Macro parameters                  |
| >>                                                 |
|                                                    |
------------------------------------------------------
------------------------------------------------------
|Modify DCB blob for T26X platform:                  |
|./dcb_tool -m tegra264-display-dcb.dtsi -c T26X     |
| >>                                                 |
| Display or modify DCB/Disp Macro parameters        |
| >>                                                 |
|                                                    |
| Output:                                            |
| tegra264-display-dcb.dtsi-modified.dtsi            |
|                                                    |
------------------------------------------------------

------------------------------------------------------
|Display/Read for T26X platform:                     |
|./dcb_tool -r tegra264-display-dcb.dtsi -c T26X     |
| >>                                                 |
| Display DCB/Disp Macro parameters                  |
| >>                                                 |
|                                                    |
------------------------------------------------------
------------------------------------------------------
|Modify DCB blob for T25X platform:                  |
|./dcb_tool -m tegra256-display-dcb.dtsi -c T25X     |
| >>                                                 |
| Display or modify DCB/Disp Macro parameters        |
| >>                                                 |
|                                                    |
| Output:                                            |
| tegra256-display-dcb.dtsi-modified.dtsi            |
|                                                    |
------------------------------------------------------

------------------------------------------------------
|Display/Read for T25X platform:                     |
|./dcb_tool -r tegra256-display-dcb.dtsi -c T25X     |
| >>                                                 |
| Display DCB/Disp Macro parameters                  |
| >>                                                 |
|                                                    |
------------------------------------------------------

|================== 4. EXAMPLE USAGE =====================|

This example demonstrates the steps involved to modify device type from
DP to TMDS.

1. Copy the device-tree (tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi)
   to the dcb_tool directory

2. Run below command
  #./dcb_tool -m tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi -c T23X

This will display the DCB TOOL menu:
===================================================
|        DCB TOOL                                 |
===================================================
|    Enter 0  => Show Input DCB                   |
|    Enter 1  => Modify DCB                       |
|    Enter 2  => Show modified DCB                |
|    Enter 3  => Show TMDS settings               |
|    Enter 4  => Show DP settings                 |
|    Enter 5  => Modify TMDS settings             |
|    Enter 6  => Show modified TMDS settings      |
|    Enter 7  => Modify DP settings               |
|    Enter 8  => Show modified DP settings        |
|    Enter 9.. => Exit                            |
===================================================

3. To see current DCB entries, you can choose option "0". Output will be similar to below:

   Display Devices::
	 Display Devices : [0]
		Type               : [DP]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		DP Lane Count      : [4]
		DP Link Rate       : [8.1GHz]
		Connector          : [0]
		External Link Type : DP
		Connector          : [0]
		Bus                : [0]
		Pad Link           : [0]
	Display Devices : [1]
		Type               : [TMDS]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		HDMI capable       : [1]
		Connector          : [0]
		Bus                : [3]
		Pad Link           : [1]
    ########### CCB Entries #############
    CCB::
	    CCB Index : 0x0
		    I2C Port           : [6]
		    AUX Port           : [0]
	    CCB Index : 0x1
		    I2C Port           : [6]
		    AUX Port           : [0]
    ########### Connector entries #############
    Connectors::
	    Connector Index : 0x0
		    Type               : [DP]
		    Hotplug            : A:[Y]
	    Connector Index : 0x0
		    Type               : [HDMI]
		    Hotplug            : A:[Y]

Note:
Since the device type is DP connector, it can have 2 options DP and DP++. So DCB
has 2 device entries. For HDMI connector, it will have only one TMDS entry.

To change Device Type from DP to TMDS need to change below fields
   (a) Change Connector Type in Connector Index from DP to TMDS
   (b) Change CCB entry(I2C/AUX port).
        - No need to change this entry on T234 as it has only a single link
          (LinkA). If chip supports multiple links then this entry will have
          different I2C/DpAux ports.
   (c) Change Display devices entry
        - Disable "Display Devices : [1]" by marking device type as "SKIP"/"EOL"
        - Modify "Display Devices : [0]"
            - "Type" from "DP" to "TMDS"
	        - Set "HDMI Capable" to 1
	    - Optionally change other entries "Heads/Sor"

    - (a), (b) and (c) will be carried out in the next steps.

4. To modify Connector Type (a), select "Modify DCB - Option 1" from main menu
   Note: In the examples below, lines that start with "#" indicate what the
         user is actually typing/entering on the console when prompted. The
         lines that don't start with "#" are the output from the tool itself.

   ========================================================================
   |              Select which section to modify                          |
   ========================================================================
   | Device Entries : 0, CCB entries : 1,  Connector entries : 2  Exit : 3..
   ========================================================================

   #Select 2

   Enter Connector index to Modify

   #Select 0

   Select below options to modify
   =============================================================
   Type: 0, Hotplug: 1, Exit: 2..
   =============================================================

   #Select 0

   Enter Connector Type
    =============================================================
    DP: 0, HDMI: 1, DSI: 2, DP-over-USBC: 3, SKIP_ENTRY: 4
    =============================================================

   #Select 1  for HDMI connector (or) 0 for DP connector

   Press 'Y/y'to continue modifying CCB Entry (or) 'N/n' to Exit

   #Type 'N'

   Now it enters the main menu.

   With above steps "Connector Type is changed from DP to HDMI"

5. To modify Display devices (c):
    - First clear off the unwanted/unused device entry
    - Update the remaining device entries with required values

   Start with "Modify DCB - Option 1."

   ========================================================================
   | Device Entries : 0, CCB entries : 1,  Connector entries:2  Exit : 3..|
   ========================================================================

   #Select 0

   Enter the Display Device Entry index that needs modification

   #Select 1

   ================================================================
   | Display Type: 0, CCB: 1, Heads: 2, Sor: 3, DP Lane Count: 4  |
   | DP Link Rate: 5, HDMI Capable: 6, Connector: 7 Exit: 8..     |
   ================================================================

   #Select 0

   CRT:0, TV:1, TMDS:2, LVDS: 3, SDVO_BRDG:4, SDI:5, DP:6, DSI:7, WBD: 8, EOL:0xE, SKIP:0xF

   #Select 0xF -> "This disables device entry 1"

   Press 'Y/y'to continue modifying Display Device Entry (or) 'N/n' to Exit

   #Press Y/y

   Enter the Display Device Entry index that needs modification

   #Select 0

   ================================================================
   | Display Type: 0, CCB: 1, Heads: 2, Sor: 3, DP Lane Count: 4  |
   | DP Link Rate: 5, HDMI Capable: 6, Connector: 7 Exit: 8..     |
   ================================================================

   #Select 0

   CRT:0, TV:1, TMDS:2, LVDS: 3, SDVO_BRDG:4, SDI:5, DP:6, DSI:7, WBD: 8, EOL:0xE, SKIP:0xF

   #Select 2

   Press 'Y/y'to continue modifying Display Device Entry (or) 'N/n' to Exit

   #Select Y

   Enter the Display Device Entry index that needs modification

   #Select 0

   ================================================================
   | Display Type: 0, CCB: 1, Heads: 2, Sor: 3, DP Lane Count: 4  |
   | DP Link Rate: 5, HDMI Capable: 6, Connector: 7 Exit: 8..     |
   ================================================================

   #Select 6

   Enter 1 -> if HDMI capable, 0 -> if not HDMI capable

   #Select 1

   Press 'Y/y'to continue modifying Display Device Entry (or) 'N/n' to Exit

   #Select N/n

6. To cross-check your modified settings, select 2 to "Show modified DCB"

   Output should show single device entry with Type TMDS

7. Exit the tool
   #Select 9 in main menu: "Enter 9.. => Exit"

8. While exiting the tool, new dtsi would be created with name ending with
    modified like below:
    tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi-modified.dtsi

9. Replace original dtsi file with the modified dtsi file in source tree.

10. Recompile and flash.


|================== 5. LIMITATIONS ====================|

Below are the known limitaitons in the current version of dcb_tool:

1. Number of "Display Devices" is fixed to what is available in a given DCB
   blob. New Display devices cannot be added.

2. DP Settings: Link Rate Info is printed only for Link-A
