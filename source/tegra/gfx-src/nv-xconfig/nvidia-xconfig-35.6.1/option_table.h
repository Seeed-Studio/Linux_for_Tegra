/*
 * Options table; the fields are:
 *
 * name - this is the long option name
 *
 * shortname - this is the one character short option name
 *
 * flags - bitmask; see NVGETOPT_ constants in nvgetopt.h
 *
 * description - text for use by print_help() to describe the option
 */

#include "nvidia-xconfig.h"

enum {
    SCREEN_OPTION = 1024,
    LAYOUT_OPTION,
    X_PREFIX_OPTION,
    KEYBOARD_OPTION,
    KEYBOARD_LIST_OPTION,
    KEYBOARD_DRIVER_OPTION,
    MOUSE_OPTION,
    FORCE_GENERATE_OPTION,
    MOUSE_LIST_OPTION,
    MODE_OPTION,
    MODE_LIST_OPTION,
    REMOVE_MODE_OPTION,
    NVIDIA_CFG_PATH_OPTION,
    SLI_OPTION,
    DISABLE_SCF_OPTION,
    TRANSPARENT_INDEX_OPTION,
    STEREO_OPTION,
    QUERY_GPU_INFO_OPTION,
    EXTRACT_EDIDS_OUTPUT_FILE_OPTION,
    MULTI_GPU_OPTION,
    NVIDIA_XINERAMA_INFO_ORDER_OPTION,
    METAMODE_ORIENTATION_OPTION,
    VIRTUAL_OPTION,
    USE_DISPLAY_DEVICE_OPTION,
    CUSTOM_EDID_OPTION,
    TV_STANDARD_OPTION,
    TV_OUT_FORMAT_OPTION,
    TV_OVER_SCAN_OPTION,
    COOL_BITS_OPTION,
    ACPID_SOCKET_PATH_OPTION,
    HANDLE_SPECIAL_KEYS_OPTION,
    PRESERVE_DRIVER_NAME_OPTION,
    CONNECTED_MONITOR_OPTION,
    REGISTRY_DWORDS_OPTION,
    META_MODES_OPTION,
    COLOR_SPACE_OPTION,
    COLOR_RANGE_OPTION,
    BUSID_OPTION,
    DEVICE_OPTION,
    FLATPANEL_PROPERTIES_OPTION,
    NVIDIA_3DVISION_USB_PATH_OPTION,
    NVIDIA_3DVISIONPRO_CONFIG_FILE_OPTION,
    NVIDIA_3DVISION_DISPLAY_TYPE_OPTION,
    RESTORE_ORIGINAL_BACKUP_OPTION,
    NUM_X_SCREENS_OPTION,
    FORCE_COMPOSITION_PIPELINE_OPTION,
    FORCE_FULL_COMPOSITION_PIPELINE_OPTION,
    ALLOW_HMD_OPTION,
};

/*
 * To add a boolean option to nvidia-xconfig:
 *
 * 1) Add the definition of the constant to the "Boolean options" list
 *    in nvidia-xconfig.h
 *
 * 2) add an entry in the below (alphabetized) __options[] table; for
 *    the second field in the __options[] table, specify
 *    XCONFIG_BOOL_VAL(<your-new-constant>)
 *
 * 3) add an entry to the __options[] table at the top of options.c
 *    with the constant and the option name as it should appear in the X
 *    config file.
 *
 * nvidia-xconfig.c:parse_commandline() will record in
 * op->boolean_options whether the commandline option was specified,
 * and will record in op->boolean_option_values whether the option was
 * specified to be true or false.  options.c:update_options() will
 * apply your boolean option to the X config file.
 */


#define XCONFIG_BOOL_OPTION_START        128


/*
 * The XCONFIG_BOOL_VAL() macro packs boolean options into the val
 * field of the __option[] table; these are above 128, so that
 * isalpha(3) returns FALSE for them.
 */

#define XCONFIG_BOOL_VAL(x) (XCONFIG_BOOL_OPTION_START + (x))


static const NVGetoptOption __options[] = {
    /* These options are printed by "nvidia-xconfig --help" */

    { "xconfig", 'c', NVGETOPT_STRING_ARGUMENT | NVGETOPT_HELP_ALWAYS, NULL,
      "Use &XCONFIG& as the input X config file; if this option is not "
      "specified, then the same search path used by the X server will be "
      "used to find the X configuration file." },

    { "output-xconfig", 'o',
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_HELP_ALWAYS, NULL,
      "Use &OUTPUT-XCONFIG& as the output X configuration file; if this "
      "option is not specified, then the input X configuration filename will "
      "also be used as the output X configuration filename." },

    { "silent", 's', NVGETOPT_HELP_ALWAYS, NULL,
      "Run silently; no messages will be printed to stdout, except for "
      "warning and error messages to stderr." },

    { "tree", 't',  NVGETOPT_HELP_ALWAYS, NULL,
      "Read the X configuration file, print to stdout the X "
      "configuration data in a tree format, and exit." },

    { "version", 'v', NVGETOPT_HELP_ALWAYS, NULL,
      "Print the nvidia-xconfig version and exit." },

    { "help", 'h', NVGETOPT_HELP_ALWAYS,  NULL,
      "Print usage information for the common commandline options and exit." },

    { "advanced-help", 'A', NVGETOPT_HELP_ALWAYS,  NULL,
      "Print usage information for the common commandline options as well "
      "as the advanced options, and then exit." },

    /* These options are only printed by "nvidia-xconfig --advanced-help" */

    { "acpid-socket-path", 
      ACPID_SOCKET_PATH_OPTION, 
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Set this option to specify an alternate path to the Linux ACPI daemon "
      "(acpid)'s socket, which the NVIDIA X driver will use to connect to "
      "acpid." },

    { "add-argb-glx-visuals",
      XCONFIG_BOOL_VAL(ADD_ARGB_GLX_VISUALS_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN,  NULL,
      "Enables or disables support for OpenGL rendering into 32-bit ARGB "
      "windows and pixmaps." },

    { "allow-glx-with-composite",
      XCONFIG_BOOL_VAL(ALLOW_GLX_WITH_COMPOSITE_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"AllowGLXWithComposite\" X configuration "
      "option." },

    { "busid", BUSID_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "This option writes the specified BusID to the device section of the "
      "X configuration file.  If there are multiple device sections, then it "
      "adds the BusID field to each of them.  To add the BusID to only a "
      "specific device or screen section, use the '--device' or '--screen' "
      "options." },

    { "preserve-busid", XCONFIG_BOOL_VAL(PRESERVE_BUSID_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "By default, nvidia-xconfig preserves the existing BusID in the X "
      "configuration file only if there are multiple X screens configured "
      "for the X server.  Use '--preserve-busid' or '--no-preserve-busid' to "
      "force the BusID to be preserved or not preserved, overriding the "
      "default behavior." },

    { "cool-bits", COOL_BITS_OPTION,
      NVGETOPT_INTEGER_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"Coolbits\" X configuration option.  Setting this "
      "option will enable support in the NV-CONTROL X extension for manipulating "
      "GPU clock and GPU fan control settings. Default value is 0.  For fan\n"
      "control set it to 4.  " 
      "WARNING: this may cause system damage and void warranties." },

    { "composite",
      XCONFIG_BOOL_VAL(COMPOSITE_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"Composite\" X extension." },

    { "connected-monitor", CONNECTED_MONITOR_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "CONNECTED-MONITOR",
      "Enable or disable the  \"ConnectedMonitor\" X configuration option; "
      "setting this option forces the X driver to behave as if the specified "
      "display devices are connected to the GPU." },

    { "connect-to-acpid",
      XCONFIG_BOOL_VAL(CONNECT_TO_ACPID_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"ConnectToAcpid\" X configuration option.  "
      "If this option is set, the NVIDIA X driver will attempt to connect "
      "to the Linux ACPI daemon (acpid).  Set this option to off to prevent "
      "the X driver from attempting to connect to acpid." },

    { "constant-dpi",
      XCONFIG_BOOL_VAL(CONSTANT_DPI_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"ConstantDPI\" X configuration option, "
      "which controls whether the NVIDIA X driver maintains a constant "
      "dots per inch (DPI) value by recomputing the reported size in "
      "millimeters of the X screen when XRandR changes the size in pixels "
      "of the X screen." },

    { "custom-edid", CUSTOM_EDID_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "CUSTOM-EDID",
      "Enable or disable the  \"CustomEDID\" X configuration option; "
      "setting this option forces the X driver to use the EDID specified."
      "This option is a semicolon-separated list of pairs of display device names "
      "and filename pairs; e.g \"CRT-0:\\tmp\\edid.bin\". Note that a display "
      "device name must always be specified even if only one EDID is"
      " specified. " },

    { "dac-8bit", XCONFIG_BOOL_VAL(DAC_8BIT_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Most Quadro parts by default use a 10 bit color look up table (LUT) "
      "by default; setting this option to TRUE forces these graphics chips "
      "to use an 8 bit (LUT)." },

    { "depth", 'd', NVGETOPT_INTEGER_ARGUMENT, NULL,
      "Set the default depth to &DEPTH&; valid values for &DEPTH& are "
      "8, 15, 16, 24, and 30." },

    { "device", DEVICE_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The nvidia-xconfig utility operates on one or more devices in "
      "the X configuration file.  If this option is specified, the "
      "device named &DEVICE& in the X configuration file will be "
      "used.  If this option is not specified, all the devices within "
      "the X configuration file will be used." },

    { "disable-glx-root-clipping",
      XCONFIG_BOOL_VAL(DISABLE_GLX_ROOT_CLIPPING_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL, "Disable or enable clipping OpenGL rendering "
      "to the root window via the \"DisableGLXRootClipping\" "
      "X configuration option." },

    { "damage-events",
      XCONFIG_BOOL_VAL(DAMAGE_EVENTS_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL, "Use OS-level events to notify the X server "
      "when a direct-rendering client has performed rendering that needs to be "
      "composited to the screen.  Improves performance when using GLX with the "
      "composite extension." },

#if defined(NV_SUNOS)
    { "disable-scf", DISABLE_SCF_OPTION, 0, NULL,
      "On Solaris, nvidia-xconfig updates the service configuration "
      "repository with the default depth being set in the X configuration "
      "file.  The property 'default_depth' of the group 'options' in the "
      "selection 'application/x11/x11-server' is set to the default depth. "
      "Use this option to disable the service configuration repository "
      "update." },
#endif

    { "preserve-driver-name", PRESERVE_DRIVER_NAME_OPTION, 0, NULL,
      "By default nvidia-xconfig changes the  display  driver  to \"nvidia\" "
      "for all configured X screens; this option preserves the existing driver "
      "name of each X screen." },

    { "egpu", XCONFIG_BOOL_VAL(ENABLE_EXTERNAL_GPU_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"AllowExternalGpus\" X configuration option." },

    { "enable-all-gpus", 'a', 0, NULL,
      "Delete all existing X screens in the current configuration, "
      "then configure an X screen on every GPU in your system." },

    { "exact-mode-timings-dvi",
      XCONFIG_BOOL_VAL(EXACT_MODE_TIMINGS_DVI_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Forces the initialization of the X server with "
      "the exact timings specified in the ModeLine." },

    { "extract-edids-from-file", 'E', NVGETOPT_STRING_ARGUMENT, "FILE",
      "Extract any raw EDID byte blocks contained in the specified X "
      "log file &LOG&; raw EDID bytes are printed by the NVIDIA X driver to "
      "the X log as hexadecimal when verbose logging is enabled with the "
      "\"-logverbose 6\" X server commandline option.  Any extracted EDIDs "
      "are then written as binary data to individual files.  These files "
      "can later be used by the NVIDIA X driver through the \"CustomEDID\" "
      "X configuration option." },

    { "extract-edids-output-file",
      EXTRACT_EDIDS_OUTPUT_FILE_OPTION, NVGETOPT_STRING_ARGUMENT, "FILENAME",
      "When the '--extract-edids-from-file' option is used, nvidia-xconfig "
      "writes any extracted EDID to a file, typically \"edid.bin\" in the "
      "current directory.  Use this option to specify an alternate "
      "filename.  Note that nvidia-xconfig, if necessary, will append a "
      "unique number to the EDID filename, to avoid overwriting existing "
      "files (e.g., \"edid.bin.1\" if \"edid.bin\" already exists)." },

    { "flatpanel-properties", FLATPANEL_PROPERTIES_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Set the flat panel properties. The supported properties are "
      "'dithering' and 'ditheringmode'.  Please see the NVIDIA "
      "README 'Appendix B. X Config Options' for more details on the "
      "possible values and syntax." },

    { "flip", XCONFIG_BOOL_VAL(NOFLIP_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable OpenGL flipping" },

    { "force-generate", FORCE_GENERATE_OPTION, 0, NULL,
      "Force generation of a new X config file, ignoring any existing "
      "system X config file.  This is not typically recommended, as things "
      "like the mouse protocol, keyboard layout, font paths, etc, are setup "
      "by your Unix distribution.  While nvidia-xconfig can attempt to "
      "infer these values, it is best to use your Unix distribution's "
      "X config file for the basis of anything that nvidia-xconfig creates." },

    { "force-stereo-flipping",
      XCONFIG_BOOL_VAL(FORCE_STEREO_FLIPPING_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Normally, stereo flipping is only performed when a stereo drawable is "
      "visible. This option forces stereo flipping even when no stereo "
      "drawables are visible." },

    { "handle-special-keys", HANDLE_SPECIAL_KEYS_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "WHEN",
      "Specify when the X server should use the builtin keyboard handler to "
      "process special key combinations (such as Ctrl+Alt+Backspace); see "
      "the X configuration man page for details.  The value of &WHEN& can be "
      "'Always', 'Never', or 'WhenNeeded'." },

    { "include-implicit-metamodes",
      XCONFIG_BOOL_VAL(INCLUDE_IMPLICIT_METAMODES_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"IncludeImplicitMetaModes\" X configuration "
      "option." },

    { "keyboard", KEYBOARD_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "When generating a new X configuration file (which happens when no "
      "system X configuration file can be found, or the '--force-generate' "
      "option is specified), use &KEYBOARD& as the keyboard type, rather "
      "than attempting to probe the system for the keyboard type.  "
      "For a list of possible keyboard types, see the '--keyboard-list' "
      "option." },

    { "keyboard-driver", KEYBOARD_DRIVER_OPTION,
      NVGETOPT_STRING_ARGUMENT, "DRIVER",
      "In most cases nvidia-xconfig can automatically determine the correct "
      "keyboard driver to use (either 'kbd' or 'keyboard'). Use this "
      "option to override what nvidia-xconfig detects. Typically, if you are "
      "using an X.Org X server, use 'kdb'; if you are using an XFree86 X "
      "server, use 'keyboard'." },

    { "keyboard-list", KEYBOARD_LIST_OPTION, 0, NULL,
      "Print to stdout the available keyboard types recognized by the "
      "'--keyboard' option, and then exit." },

    { "layout", LAYOUT_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The nvidia-xconfig utility operates on a Server Layout within the X "
      "configuration file.  If this option is specified, the layout named "
      "&LAYOUT& in the X configuration file will be used.  If this option is "
      "not specified, the first Server Layout in the X configuration "
      "file is used." },

    { "mode",
      MODE_OPTION, NVGETOPT_IS_BOOLEAN | NVGETOPT_STRING_ARGUMENT, NULL,
      "Add the specified mode to the mode list." },

    { "mode-debug", XCONFIG_BOOL_VAL(MODE_DEBUG_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"ModeDebug\" X configuration option; when "
      "enabled, this option causes the X driver to print verbose details "
      "about mode validation to the X log file." },

    { "mode-list", MODE_LIST_OPTION, NVGETOPT_STRING_ARGUMENT, "MODELIST",
      "Remove all existing modes from the X configuration's modelist and "
      "add the one(s) specified in the &MODELIST& string." },

    { "remove-mode", REMOVE_MODE_OPTION, NVGETOPT_STRING_ARGUMENT, "MODE",
      "Remove the specified mode from the mode list." },

    { "metamodes", META_MODES_OPTION, NVGETOPT_STRING_ARGUMENT, "METAMODES",
      "Add the MetaMode X configuration option with the value &METAMODES& "
      "which will replace any existing MetaMode option already in the X "
      "configuration file." },

    { "mouse", MOUSE_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "When generating a new X configuration file (which happens when no "
      "system X configuration file can be found, or the '--force-generate' "
      "option is specified), use &MOUSE& as the mouse type, rather than "
      "attempting to probe the system for the mouse type.  For a list of "
      "possible mouse types, see the '--mouse-list' option." },

    { "mouse-list", MOUSE_LIST_OPTION, 0, NULL,
      "Print to stdout the available mouse types recognized by the "
      "'--mouse' option, and then exit." },

    { "multigpu", MULTI_GPU_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable MultiGPU.  Valid values for &MULTIGPU& are "
      "'Off' and 'Mosaic'." },

    { "multisample-compatibility",
      XCONFIG_BOOL_VAL(MULTISAMPLE_COMPATIBILITY_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the use of separate front and "
      "back multisample buffers." },

    { "nvidia-cfg-path",
      NVIDIA_CFG_PATH_OPTION, NVGETOPT_STRING_ARGUMENT, "PATH",
      "The nvidia-cfg library is used to communicate with the NVIDIA kernel "
      "module to query basic properties of every GPU in the system.  This "
      "library is typically only used by nvidia-xconfig when configuring "
      "multiple X screens.  This option tells nvidia-xconfig where to look "
      "for this library (in case it cannot find it on its own).  This option "
      "should normally not be needed." },

    { "only-one-x-screen", '1', 0, NULL,
      "Disable all but one X screen." },

    { "overlay",
      XCONFIG_BOOL_VAL(OVERLAY_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"Overlay\" X configuration option." },

    { "cioverlay",
      XCONFIG_BOOL_VAL(CIOVERLAY_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the color index overlay." },

    { "overlay-default-visual",
      XCONFIG_BOOL_VAL(OVERLAY_DEFAULT_VISUAL_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"OverlayDefaultVisual\" "
      "X configuration option." },

    { "transparent-index", TRANSPARENT_INDEX_OPTION,
      NVGETOPT_INTEGER_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "INDEX",
      "Pixel to use as transparent when using color index overlays.  "
      "Valid values for &TRANSPARENT-INDEX& are 0-255."},

    { "post-tree", 'T', 0, NULL,
      "Like the '--tree' option, but goes through the full process of "
      "applying any user requested updates to the X configuration, before "
      "printing the final configuration to stdout in a tree format.  "
      "Effectively, this option just causes the configuration to be printed "
      "to stdout as a tree instead of writing the results to file." },

    { "power-connector-check",
      XCONFIG_BOOL_VAL(NO_POWER_CONNECTOR_CHECK_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Disable or enable the \"NoPowerConnectorCheck\" "
      "X configuration option." },

    { "probe-all-gpus", XCONFIG_BOOL_VAL(PROBE_ALL_GPUS_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Disable or enable the \"ProbeAllGpus\" X configuration option." },

    { "query-gpu-info", QUERY_GPU_INFO_OPTION, 0, NULL,
      "Print information about all recognized NVIDIA GPUs in the system." },

    { "registry-dwords", REGISTRY_DWORDS_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"RegistryDwords\" X configuration option." },

    { "render-accel",
      XCONFIG_BOOL_VAL(RENDER_ACCEL_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"RenderAccel\" X configuration option." },

    { "render-extension",
      XCONFIG_BOOL_VAL(NO_RENDER_EXTENSION_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Disable or enable the \"NoRenderExtension\" X configuration option." },

    { "screen", SCREEN_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The nvidia-xconfig utility operates on one or more screens within a "
      "Server Layout in the X configuration file.  If this option is "
      "specified, the screen named &SCREEN& in the X configuration file will "
      "be used.  If this option is not specified, all screens within the "
      "selected Server Layout in the X configuration file "
      "will be used used." },

    { "separate-x-screens",
      XCONFIG_BOOL_VAL(SEPARATE_X_SCREENS_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "A GPU that supports multiple simultaneous display devices can either "
      "drive these display devices in a single X screen, or as separate X "
      "screens.  When the '--separate-x-screens' option is specified, each GPU "
      "on which an X screen is currently configured will be updated to have "
      "two or more (depending on the capabilities of that GPU) X screens "
      "configured.  The '--no-separate-x-screens' option will remove any "
      "extra configured X screens on each GPU.  Please see the NVIDIA README "
      "description of \"Separate X Screens on One GPU\" for further details." },

    { "x-screens-per-gpu", NUM_X_SCREENS_OPTION,
      NVGETOPT_INTEGER_ARGUMENT, NULL,
      "A GPU that supports multiple simultaneous display devices can either "
      "drive these display devices in a single X screen, or as separate X "
      "screens.  When the '--x-screens-per-gpu=<quantity>' option is "
      "specified, each GPU on which an X screen is currently configured will "
      "be updated to have <quantity> X screens. <quantity> has to be greater "
      "than 0. Setting <quantity> to 1 is equivalent to specifying the "
      "'--no-separate-x-screens' option.  Please see the NVIDIA README "
      "description of \"Separate X Screens on One GPU\" for further details." },

    { "sli", SLI_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable SLI.  Valid values for &SLI& are 'Off' and 'Mosaic'." },

    { "stereo", STEREO_OPTION,
      NVGETOPT_INTEGER_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the stereo mode.  Valid values for &STEREO& are: 0 "
      "(Disabled), 1 (DDC glasses), 2 (Blueline glasses), 3 (Onboard stereo), "
      "4 (multi-display clone mode stereo), 5 (SeeReal digital flat panel), 6 "
      "(Sharp3D digital flat panel), 7 (Arisawa/Hyundai/Zalman/Pavione/Miracube), "
      "8 (3D DLP), 9 (3D DLP INV), 10 (NVIDIA 3D VISION), "
      "11 (NVIDIA 3D VISION PRO), 12 (HDMI 3D), 13 (Tridelity SL)." },

    { "thermal-configuration-check",
      XCONFIG_BOOL_VAL(THERMAL_CONFIGURATION_CHECK_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Disable or enable the \"ThermalConfigurationCheck\" "
      "X configuration option." },

    { "tv-standard", TV_STANDARD_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "TV-STANDARD",
      "Enable or disable the \"TVStandard\" X configuration option. Valid "
      "values for \"TVStandard\" are: \"PAL-B\", \"PAL-D\", \"PAL-G\", "
      "\"PAL-H\", \"PAL-I\", \"PAL-K1\", \"PAL-M\", \"PAL-N\", \"PAL-NC\", "
      "\"NTSC-J\", \"NTSC-M\", \"HD480i\", \"HD480p\", \"HD720p\", "
      "\"HD1080i\", \"HD1080p\", \"HD576i\", \"HD576p\"." },

    { "tv-out-format", TV_OUT_FORMAT_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "TV-OUT-FORMAT",
      "Enable or disable the \"TVOutFormat\" X configuration option. Valid "
      "values for \"TVOutFormat\" are: \"SVIDEO\" and \"COMPOSITE\"." },

    { "metamode-orientation", METAMODE_ORIENTATION_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "ORIENTATION",
      "Specify the MetaModeOrientation.  Valid values for &ORIENTATION& are: "
      "\"RightOf\" (the default), \"LeftOf\", \"Above\", \"Below\", or "
      "\"Clone\"." },

    { "nvidia-xinerama-info",
      XCONFIG_BOOL_VAL(NVIDIA_XINERAMA_INFO_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable providing Xinerama information from the "
      "NVIDIA X driver." },

    { "nvidia-xinerama-info-order",
      NVIDIA_XINERAMA_INFO_ORDER_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"nvidiaXineramaInfoOrder\" X configuration "
      "option.  &NVIDIA-XINERAMA-INFO-ORDER& is a comma-separated list "
      "of display device names that describe the order in which "
      "nvidiaXineramaInfo should be reported.  E.g., \"CRT, DFP, TV\"." },

    { "ubb",
      XCONFIG_BOOL_VAL(UBB_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"UBB\" X configuration option." },

    { "use-edid",
      XCONFIG_BOOL_VAL(USE_EDID_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable use of the EDID (Extended Display Identification "
      "Data) from your display device(s).  The EDID will be used for driver "
      "operations such as building lists of available modes, determining "
      "valid frequency ranges, and computing the DPI (Dots Per Inch).  "
      "This option defaults to TRUE (the NVIDIA X driver will use the EDID, "
      "when available).  It is NOT recommended that you use this option to "
      "globally disable use of the EDID; instead, use '--no-use-edid-freqs' "
      "or '--no-use-edid-dpi' to disable specific uses of the EDID." },

    { "use-edid-dpi",
      XCONFIG_BOOL_VAL(USE_EDID_DPI_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable use of the physical size information in the display "
      "device's EDID, if any, to compute the DPI (Dots Per Inch) of the X "
      "screen.  This option defaults to TRUE (the NVIDIA X driver uses the "
      "EDID's physical size, when available, to compute the DPI)." },

    { "use-edid-freqs",
      XCONFIG_BOOL_VAL(USE_EDID_FREQS_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable use of the HorizSync and VertRefresh "
      "ranges given in a display device's EDID, if any.  EDID provided "
      "range information will override the HorizSync and VertRefresh ranges "
      "specified in the Monitor section.  This option defaults to TRUE (the "
      "NVIDIA X driver will use frequency information from the EDID, when "
      "available)." },

    { "use-display-device", USE_DISPLAY_DEVICE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "DISPLAY-DEVICE",
      "Force the X driver to use the display device specified." },

    { "use-events",
      XCONFIG_BOOL_VAL(USE_EVENTS_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable \"UseEvents\" X configuration option. Setting this "
      "option will enable the X driver to use the system events in some cases "
      "when it is waiting for the hardware. With this option X driver sets an "
      "event handler and waits for the hardware through the poll() system "
      "call. This option defaults to FALSE." },

    { "virtual", VIRTUAL_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "WIDTHxHEIGHT",
      "Specify the virtual screen resolution." },

    { "x-prefix", X_PREFIX_OPTION, NVGETOPT_STRING_ARGUMENT, NULL,
      "The X installation prefix; the default is /usr/X11R6/.  Only "
      "under rare circumstances should this option be needed." },

    { "xinerama", XCONFIG_BOOL_VAL(XINERAMA_BOOL_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL, "Enable or disable Xinerama." },

    { "color-space", COLOR_SPACE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "COLORSPACE",
      "Enable or disable the \"ColorSpace\" X configuration option. "
      "Valid values for \"COLORSPACE\" are: \"RGB\" and \"YCbCr444\"." },

    { "color-range", COLOR_RANGE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, "COLORRANGE",
      "Sets the \"ColorRange\" X configuration option. "
      "Valid values for \"COLORRANGE\" are: \"Full\" and \"Limited\"." },

    { "3dvision-usb-path", NVIDIA_3DVISION_USB_PATH_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL, "Set this option to specify the sysfs path of the connected "
      "USB dongle." },

    { "3dvisionpro-config-file", NVIDIA_3DVISIONPRO_CONFIG_FILE_OPTION, NVGETOPT_STRING_ARGUMENT,
      NULL, "Set this option to specify the NVIDIA 3DVisionPro "
      "configuration file. Ensure X server has a read and write access "
      "permissions to this file. NVIDIA X driver stores the hub and "
      "the pairing configuration in this file to re-use across X restarts. "
      "If this option is not provided, 3D VisionPro configuration will not "
      "be stored." },

    { "3dvision-display-type", NVIDIA_3DVISION_DISPLAY_TYPE_OPTION,
      NVGETOPT_INTEGER_ARGUMENT | NVGETOPT_ALLOW_DISABLE,
      NULL, "When NVIDIA 3D Vision is enabled with a non 3D Vision ready "
      "display, use this option to specify the display type. Valid values "
      "are: 0 (Assume it is a CRT), 1 (Assume it is a DLP) and "
      "2 (Assume it is a DLP TV and enable the checkerboard output)." },

    { "base-mosaic",
      XCONFIG_BOOL_VAL(BASE_MOSAIC_BOOL_OPTION), NVGETOPT_IS_BOOLEAN, NULL,
      "Enable or disable the \"BaseMosaic\" X configuration option." },

    { "restore-original-backup", RESTORE_ORIGINAL_BACKUP_OPTION, 0, NULL,
      "Restore a backup of the X configuration that was made before any "
      "changes were made by nvidia-xconfig, if such a backup is available. "
      "This type of backup is made by nvidia-xconfig before it modifies an "
      "X configuration file that it has not previously touched; this is "
      "assumed to be an X configuration file that predates the involvement "
      "of the NVIDIA X driver. As an example, nvidia-xconfig will copy an "
      "X configuration file at /etc/X11/xorg.conf to /etc/X11/xorg.conf."
      "nvidia-xconfig-original the first time it makes changes to that file."},

    { "allow-empty-initial-configuration", XCONFIG_BOOL_VAL(ALLOW_EMPTY_INITIAL_CONFIGURATION),
      NVGETOPT_IS_BOOLEAN, NULL, "Allow the X server to start even if no "
      "connected display devices could be detected." },

    { "inband-stereo-signaling", XCONFIG_BOOL_VAL(INBAND_STEREO_SIGNALING),
      NVGETOPT_IS_BOOLEAN, NULL, "Enable or disable the "
      "\"InbandStereoSignaling\" X configuration option." },

    { "force-yuv-420", XCONFIG_BOOL_VAL(FORCE_YUV_420),
      NVGETOPT_IS_BOOLEAN, NULL, "Enable or disable the "
      "\"ForceYUV420\" X configuration option. If the current display and GPU "
      "both support uncompressed RGB 4:4:4 output and YUV 4:2:0 compressed "
      "output with the current mode, then RGB 4:4:4 output is selected by "
      "default. This option forces the use of YUV 4:2:0 output (where "
      "supported) instead." },

    { "force-composition-pipeline", FORCE_COMPOSITION_PIPELINE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"ForceCompositionPipeline\" X "
      "configuration option." },

    { "force-full-composition-pipeline", FORCE_FULL_COMPOSITION_PIPELINE_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"ForceFullCompositionPipeline\" X "
      "configuration option." },

    { "allow-hmd", ALLOW_HMD_OPTION,
      NVGETOPT_STRING_ARGUMENT | NVGETOPT_ALLOW_DISABLE, NULL,
      "Enable or disable the \"AllowHMD\" X configuration option." },

    { "prime", XCONFIG_BOOL_VAL(ENABLE_PRIME_OPTION),
      NVGETOPT_IS_BOOLEAN, NULL,
      "Enable PRIME for the generated X config file. Cannot be run with "
      "--no-busid or --no-allow-empty-initial-configuration. On a system with "
      "more than 1 GPU, specify the GPU to use for PRIME with --busid or the "
      "first available will be chosen. Note that to enable PRIME it is "
      "necessary to run \"xrandr --setprovideroutputsource modesetting "
      "NVIDIA-0\" and \"xrandr --auto\" after completion." },

    { NULL, 0, 0, NULL, NULL },
};
