-- Jetson BMP blob tool instructions --

To create a Jetson-compatible BMP blob (bmp.blob) for the boot splash screen
display on Jetson boards, change to the 'Linux_for_Tegra/tools/bmp-splash'
directory and enter this command at the Linux prompt:

OUT=$PWD ./genbmpblob_L4T.sh t210 ./config_file ./BMP_generator_L4T.py /usr/bin/lz4c my-bmp.blob

If you want to put the bmp.blob in a designated output directory, you
can change OUT=$PWD to OUT=<your-output-directory>, for instance ./out.

Note that the 't210' parameter tells the BMP scripts to use the format that
is fully backward compatible with all CBoot versions, whether based on a T210,
T186 or T194 SoC.

The config_file contains the names of the BMP files you want to provide
for CBoot to display on HDMI during boot. The standard NVIDIA bmp.blob
contains three BMPs: 1080, 720 and 480 'NVIDIA' green-on-black images.
You can replace them with one, two or three of your own BMP images,
depending on what screen resolutions you want to support. Use a normal
PC bitmap, Windows 3.x format, 24BPP. If you want to support only 1080p,
which is standard on most HDMI displays, you can provide just one entry.
Use the Linux 'file' command to make sure your BMP is an uncompressed
Windows 3.x, 24BPP BMP file. RLE, layers, etc. are not supported.

The standard NVIDIA blob is compressed to save space. Specify the
location of your lz4c binary as the fourth argument to genmbpblob_l4t.sh
to have your BMP blob compressed. The lz4c tool is typically found in
the liblz4-1 package. Use 'apt-get install liblz4-1' or similar tool to
install it, typically to /usr/bin, depending on your Linux distro.

You can place any BMP files you want to add to the blob in the current
directory, or you can modify the config_file to point to them. For
instance, the standard config_file example provided lists them as:

nvidia480.bmp nvidia 480;
nvidia720.bmp nvidia 720;
nvidia1080.bmp nvidia 1080

[Note that all lines but the last should end with a semi-colon]

You can point to your BMP files with an absolute or relative path:

./bmp/my480.bmp nvidia 480;
./bmp/my1080.bmp nvidia 1080

Once you've created your own bmp.blob, place it in your BSP's bootloader
directory (Linux_for_Tegra/bootloader/bmp.blob) and reflash your board
to have the blob written to flash and used by CBoot on the next boot.

The size of the bmp.blob shipped with current L4T releases is 70988
bytes. Jetson boards allocate a BMP partition in flash*.xml that is
large enough for that size blob, plus some slack. Limit your blob to three
BMP files at most, and use compression so that it will fit on all boards.
You can flash it with a full reflash, or by specifying the BMP partition
as the only one to reflash on the command line as below:

sudo ./flash.sh -r -k BMP <your-board-config> <your-target-flash>

Note that BMP splash happens late in boot, just before the kernel is
loaded and executed.



