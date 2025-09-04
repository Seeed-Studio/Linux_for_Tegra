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
DCB (Display Control Block) describes the display outputs and their configurations on a given platform/board.
Detailed description of DCB can be found at https://download.nvidia.com/open-gpu-doc/DCB/2/DCB-4.x-Specification.html.
This describes the DCB used on dGPU. The DCB used for Tegra is derived from dGPU DCB, but only has a subset of the
fields.

Few of the differences between dGPU and Tegra DCBs:
1. Only the fields that describe display parameters required for Tegra are retained in Tegra DCB. Hence, some fields
   and values from dGPU DCB are not valid on Tegra DCB.
2. dGPU DCB is part of VBIOS ROM, whereas Tegra DCB is embedded in device tree files for a given Tegra platform.

dcb_tool:
DCB blob is made up of hex values, which makes it difficult to decode/edit manually. "dcb_tool" helps in parsing the
DCB blob in dtsi file, and also helps in modifying the values of the fields in DCB.



|================ 3. TERMINOLOGIES =====================|

1. Display Devices
    - This represents the output display device. It holds following properties:
        1. Type             - Output type (DP/HDMI/DSI)
        2. CCB              - This is the index of the CCB entry which would be mapped to this device
        3. Heads            - Valid head numbers (starting from 0) to which this output can be attached
        4. SOR/DSI          - SOR Index (or DSI index) of this output device
        5. DP Lane Count    - Number of lanes used by DP link (DP specific property)
        6. DP Link Rate     - Max link rate supported by this DP link (DP specific property)
        7. Connector        - Index from the list of "Connector Entries" to which this output device is mapped
        8. Bus              - Bus number. Two devices with same Bus number can't be enabled simulataneously.
                              Assumption is that these two devices share a hw resource (one SOR/DPAUX shared by two output devices).
                              Bus number can be any positive number, there's no defined range of bus numbers.
        9. HDMI capable     - Indicates whether the device can support TMDS. If its set for DP, then the connector supports DP++.

2. CCB
    - Communication Control Block. Each entry stores valid I2C or DPAUX port numbers on this platform.
    - CCB property of "Display Device" holds the index of one of the CCB entries.
    - CCB property is not applicable to DSI device since DSI doesn't use I2C/AUX ports for communication.
    - I2C/AUX Port 31 is currently used to indicate Unused ports.


3. Connectors
    - Connector indicates the physical display output port. E.g.: DP port, HDMI port, DSI connector, DP-over-usbc, etc.
    - Connector property of "Display Device" holds the index of one of the CCB entries.
        1. Type     - Output Port Type
        2. Hotplug  - Indicates whether the port (or output protocol) supports hotplug

4. TMDS settings / DP settings
    - These are the settings that correspond to characterized settings of the ouput pads (mostly electrical properties).


"Display Devices" has below fields that can be modified to update the Device Type:

- Device entries ::
  - Change device entries from below menu.
    "Modify DCB": 1 -->
        "Device Entries": 0 -->
            "Device Entry Index": 0/1/2... -->
                "Display Type": 0 -> 2 (To change to TMDS) -->
                    Press 'y' to continue modifying device entries, else N.

- Connector entries ::
  - Add/change Connector Type or Hotplug Type from below menu.
    "Modify DCB": 1 -->
        "Connector entries": 2 -->
            "Connector index": 0/1 -->
                "Connector Type": HDMI/DP/DSI -->
                    "Exit": 2

- CCB entries ::
  - No change is needed for this field as T234 has only single I2C/DpAux port since
    only single link (LinkA) is supported.

--------------------------------------------------------
For reference, HDMI connector will have below settings:
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
    CCB::
	    CCB Index : 0x0
		    I2C Port       : [6]
		    AUX Port       : [0]
    Connectors::
	    Connector Index : 0x0
		    Type           : [HDMI]
		    Hotplug        : A:[Y]

--------------------------------------------------------
For reference, DP connector will have below settings:
--------------------------------------------------------
   Display Devices::
	Display Devices : [0]
		Type               : [DP]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		DP Lane Count      : [4]
		DP Link Rate       : [8.1GHz]
		HDMI capable       : [1]
		Connector          : [0]
		Bus                : [0]
    CCB::
	    CCB Index : 0x0
		    I2C Port       : [6]
		    AUX Port       : [0]
    Connectors::
	    Connector Index : 0x0
		    Type           : [DP]
		    Hotplug        : A:[Y]





|======= 3. COMMAND to Modify/Display DCB Blob ======|
------------------------------------------------------
|Modify:                                             |
|./dcb_tool -m tegra234-display-dcb.dtsi             |
| >>                                                 |
| Display or modify DCB/Disp Macro parameters        |
| >>                                                 |
|                                                    |
| Output:                                            |
| tegraXXX-display-dcb.dtsi-modified.dtsi            |
|                                                    |
------------------------------------------------------

------------------------------------------------------
|Display/Read:                                       |
|./dcb_tool -r tegra234-display-dcb.dtsi             |
| >>                                                 |
| Display DCB/Disp Macro parameters                  |
| >>                                                 |
|                                                    |
------------------------------------------------------




|================== 4. EXAMPLE USAGE =====================|

----------------------------------------------------------------
Follow below steps to change Display Device Type from DP to TMDS
----------------------------------------------------------------

1. Copy the device-tree (tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi)
   to the dcb_tool directory.

2. Run below command
  #./dcb_tool -m tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi
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
	Display Devices : [1]
		Type               : [TMDS]
		CCB                : [0]
		Heads              : 0:[Y] 1:[Y]
		Sor                : [0 ]
		HDMI capable       : [1]
		Connector          : [0]
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

4. Since the device type is DP connector, it can have 2 options DP and DP++. So DCB
   has 2 device entries. For HDMI connector, it will have only one TMDS entry.

5. To change Device Type from DP to TMDS need to change below fields
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

    - Each of the above steps (a), (b) and (c) are explanined in detail below.

6. To modify Connector Type (5.a), select "Modify DCB - Option 1" from main menu

   ========================================================================
   |              Select which section to modify                          |
   ========================================================================
   | Device Entries : 0, CCB entries : 1,  Connector entries:2  Exit : 3..|
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
    DP: 0, HDMI: 1, DSI: 2, DP-over-USBC: 3
    =============================================================

   #Select 1  for HDMI connector (or) 0 for DP connector

   Press 'Y/y'to continue modifying CCB Entry (or) 'N/n' to Exit

   #N

   Now it enters the main menu.

   With above steps "Connector Type is changed from DP to HDMI"

7. To modify Display devices (5.c):
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

8. To cross-check your modified settings, select 2 to "Show modified DCB"

   Output should show single device entry with Type TMDS

9. Exit the tool
   #Select 9 in main menu: "Enter 9.. => Exit"

10. While exiting the tool, new dtsi would be created with name ending with
    modified like below:
    tegra234-dcb-p3701-0000-a02-p3737-0000-a01.dtsi-modified.dtsi

11. Replace original dtsi file with the modified dtsi file in source tree.

12. Recompile and flash.


|================== 5. LIMITATIONS ====================|

Below are the known limitaitons in the current version of dcb_tool:

1. Number of "Display Devices" is fixed to what is available in a given DCB
   blob. New Display devices cannot be added.

2. DP Settings:
   a. Link Rate Info is printed only for Link-A
   b. MST Enable field is not printed
   c. VSwing and SOR TX PU values are not printed
   d. Various levels of Drive Current, PreEmphasis and TX PU values are not printed
