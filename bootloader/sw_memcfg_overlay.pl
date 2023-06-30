#!/usr/bin/perl
#
# Copyright (c) 2017-2018, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

use strict;
use warnings;
use File::Basename;
use Getopt::Std;
use Data::Dumper;

## Defining Globals ##
my $script_path = dirname($0);
my $script_name = basename($0);
my $verbose = 0;
my $force = 0;
my $bad_reg = "__BAD_REG__";
my %master_bct = ();
my %rev_master_bct = ();
my %sw_overridable_bct_params = ();
my %sw_overrides = ();

## These strings will be defined in the concatenated file ##
my $FS_sw_overridable_bct_params = "";
my $FS_bct_table = "";
sub init_file_strings; #Forward declaration of subroutine to initialize the string

## Defining Regex patterns ##
my $swo_table_pattern_2 = qr/^\s*([a-zA-Z0-9_]+)\s*=\s*(\d+)/;
my $cfg_pattern_5       = qr/^(\s*SDRAM\[[1]?[0-9]\].)([A-Za-z0-9_]+)(\s*=\s*)(0x[0-9a-fA-F]+)(;\s*)$/;
my $bct_table_pattern_3 = qr/^\s*'([a-zA-Z0-9_]+)':\s*'([a-zA-Z0-9_]+)'\s*,\s*'(.*)'\s*$/;
my $swo_pattern_2       = qr/^\s*([A-Za-z0-9_]+)\s*[=:]\s*(0x[0-9a-fA-F]+)\s*(;+\s*#.*|\s+#.*|;*\s*)$/;

## Print usage along with error message (if present) ##
sub usage
{
    my $message = shift;
    if ( defined $message ) {
        print "$message\n";
    }
    die "Usage: $script_name 
           -c <memcfg>       (mandatory: verified memory cfg from mem-qual)
           -s <sw-override>  (mandatory: SW cfg values to be overwritten 'BCT = 0xHEX_VALUE')
           -o <output_swcfg> (mandatory: Output file with sw-configs overriden . Should be writeable)
           -v                (optional:  For higher verbosity)
           -f                (optional:  Forcefully change recoverable errors as warnings)
           -l                (optional:  List the BCTs/Registers software is allowed to override)
           -h                (optional:  Print usage)\n";
}

## Sanitizing Command line arguments ##
sub cmdline_sanity
{
    my %cmdline_opts = ();
    getopts('c:s:o:vhfl', \%cmdline_opts) or usage("ERROR($script_name): Unknown options provided");
    usage() if $cmdline_opts{h};
    $verbose = 1 if $cmdline_opts{v};
    $force   = 1 if $cmdline_opts{f};
    print_sw_list() if $cmdline_opts{l};
    usage("ERROR($script_name): Memory Cfg file not provided") if !$cmdline_opts{c};
    usage("ERROR($script_name): Software Override file not provided") if !$cmdline_opts{s};
    usage("ERROR($script_name): Output SW cfg file not provided") if !$cmdline_opts{o};
    return ($cmdline_opts{c},$cmdline_opts{s},$cmdline_opts{o});
}

## Reading list of BCT params which S/W is allowed to modify ##
sub read_sw_overridable_bct_params
{
    print "INFO($script_name): Reading SW overridable BCT params\n" if $verbose;
    for (split /^/, $FS_sw_overridable_bct_params) {
        if (/$swo_table_pattern_2/) {
            $sw_overridable_bct_params{$1} = $2;
        }
    }
}

## Reading list of all possible BCT params. Generated from emc_reg_calc based tool gen_bct_table ##
sub read_bct_table
{
    print "INFO($script_name): Reading BCT table\n" if $verbose;
    for (split /^/, $FS_bct_table) {
        if (/$bct_table_pattern_3/) {
            my ($bct,$reg,$desc)  = ($1,$2,$3);
            $master_bct{$bct} = [$reg,$desc];
            if ( $reg ne "NA" && !exists $rev_master_bct{$reg}) {
                $rev_master_bct{$reg} = $bct;
            }
        } else {
            die "ERROR($script_name): Parse error unknown pattern `$_' in bct table\n";
        }
    }
}

## Subroutine to print list of BCTs/Registers software is allowed to modify
sub print_sw_list
{
    init_file_strings;
    read_sw_overridable_bct_params;
    read_bct_table;
    my $fmt = "%-45s %55s\n";
    my $tmp = "/tmp/b_r_" . $$;
    open(my $F_tmp, '>', $tmp) || die "ERROR($script_name): /tmp/ is not writeable ...\n";
    printf($F_tmp $fmt, 'BCT', 'Register');
    foreach my $b ( keys %sw_overridable_bct_params ) {
        printf($F_tmp $fmt, $b, $master_bct{$b}[0]);
    }
    close($F_tmp);
    system("sort $tmp");
    unlink($tmp);
    exit 0;
}

## Sanity check the software override input            ##
## It needs to be either a register name or a bct name ##
## If register name is used,                           ##
##     return the corresponding bct name               ##
## Flag errors accordingly if reg/bct not found        ##
sub override_sanity
{
    my ($bct, $val) = @_;
    my $rbct = $bct;
    # change name to bct name is register name is provided
    if (exists $rev_master_bct{$bct}) {
        $rbct = $rev_master_bct{$bct}
    }
    if (exists $sw_overridable_bct_params{$rbct}) {
        if (exists $sw_overrides{$rbct}) {
            ## Duplicate entries
            if ($force) {
                print "WARN($script_name): Duplicate specification of BCT($rbct)/Register($master_bct{$rbct}[0]) in swo file. Will overwrite\n";
            } else {
                die "ERROR($script_name): Duplicate specification of BCT($rbct)/Register($master_bct{$rbct}[0]) in swo file.\n";
            }
        }
        # Provision for additional checks
        return $rbct
    }
    if (exists $master_bct{$rbct}) {
        if ($force) {
            print "WARN($script_name): Software is not allowed to modify register/bct `$bct'. Will skip\n";
            return $bad_reg;
        } else {
            die "ERROR($script_name): Software is not allowed to modify register/bct `$bct'\n";
        }
    } else {
        if ($force) {
            print "WARN($script_name): Register/bct `$bct' not found in master bct table. Will skip\n";
            return $bad_reg;
        } else {
            die "ERROR($script_name): Register/bct `$bct' not found in master bct table\n";
        }
    }
}

## Read the software override file,                    ##
## Check if param is allowed to be overwritten by s/w, ##
## Populate perl hash %sw_overrides                    ##
sub read_sw_overrides
{
    my $swo = shift;
    print "INFO($script_name): Reading software override file `$swo'\n" if $verbose;
    open(F_swo, $swo) || die "ERROR($script_name): error while loading software override file `$swo'\n";
    while (<F_swo>) {
        chomp;
        if (/$swo_pattern_2/) {
            my ($bct, $val) = ($1, $2);
            $bct = override_sanity($bct, $val);#Change name to BCT name if register name is provided
            $sw_overrides{$bct} = $val if $bct ne $bad_reg;
        } elsif (/^\s*#.*/) {
            #comments
        } elsif (/^\s*$/) {
            #empty line
        } else {
            if ($force) {
                print "WARN($script_name): Parse error; Unexpected pattern `$_' in software override file `$swo'\n";
            } else {
                die "ERROR($script_name): Parse error; Unexpected pattern `$_' in software override file `$swo'\n";
            }
        }
    }
    close(F_swo);
}

## Read the software override file,         ##
## Read the memory cfg file                 ##
## If entry is present in %sw_overrides too,## 
##    override the value in output file     ##
sub overlay_sw_params
{
    my ($memcfg, $swo, $swcfg) = @_;
    if ( -f $swcfg) {
        if ($force) {
            print "WARN($script_name): Output SWcfg file `$swcfg' already exists. Will overwrite *****.\n";
        } else {
            die "ERROR($script_name): Output SWcfg file `$swcfg' already exists. Exiting.\n";
        }
    }
    print "INFO($script_name): Opening SWcfg file `$swcfg' for writing\n" if $verbose;
    open(my $F_swcfg, '>', $swcfg) || die "ERROR($script_name): Could not open file `$swcfg' for writing\n";
    read_sw_overrides($swo);
    print "INFO($script_name): Reading memory config file `$memcfg'\n" if $verbose;
    open(F_memcfg, $memcfg) || die "ERROR($script_name): error while loading memory cfg file `$memcfg'\n";
    print $F_swcfg "# Software Config Override flow\n";
    print $F_swcfg "# Base memcfg : $memcfg\n";
    print $F_swcfg "# Software override : $swo\n";
    while (<F_memcfg>) {
        chomp;
        if (/$cfg_pattern_5/) {
            if (exists $sw_overrides{$2}) {
                my $new_val = $sw_overrides{$2};
                print "INFO($script_name): Updating cfg value of `$2' from `$4' to `$new_val'\n" if ($verbose && $new_val ne $4);
                print $F_swcfg "$1$2$3$new_val$5\n";
            } else {
                print $F_swcfg "$1$2$3$4$5\n";
            }
        } else {
            print $F_swcfg "$_\n";
        }
    }
    close(F_memcfg);
    close($F_swcfg);
}

sub main
{
    print "INFO($script_name): Starting software config overlay\n" if $verbose;
    my @cmdline_args = cmdline_sanity;
    init_file_strings;
    read_sw_overridable_bct_params;
    read_bct_table;
    overlay_sw_params @cmdline_args;

    my $read_count     = scalar keys %master_bct;
    my $pure_reg_count = scalar keys %rev_master_bct;
    my $swo_count      = scalar keys %sw_overridable_bct_params;
    my $ovr_count      = scalar keys %sw_overrides;
    print "INFO($script_name): Read Count = T($read_count) R($pure_reg_count) S($swo_count) O($ovr_count)\n" if $verbose;
    print "INFO($script_name): Completed software config overlay\n" if $verbose;
}

main();

1;
## End of main script file
## sw_overridable_bct_params and bct_table will be appended 
sub init_file_strings {
$FS_sw_overridable_bct_params = <<'END1';
# The value with each BCT indicates a code 
# based on which checks will be done
# For now 0 means no extra sanity check
McGSCInitMask = 0
MssEncryptGenKeys = 0
MssEncryptDistKeys = 0
McSmmuBypassConfig = 0
McBypassSidInit = 0
McVideoProtectBom = 0
McVideoProtectBomAdrHi = 0
McVideoProtectSizeMb = 0
McVideoProtectVprOverride = 0
McVideoProtectVprOverride1 = 0
McVideoProtectVprOverride2 = 0
McVideoProtectGpuOverride0 = 0
McVideoProtectGpuOverride1 = 0
McVideoProtectWriteAccess = 0
McGeneralizedCarveout1Bom = 0
McGeneralizedCarveout1BomHi = 0
McGeneralizedCarveout1Size128kb = 0
McGeneralizedCarveout1Access0 = 0
McGeneralizedCarveout1Access1 = 0
McGeneralizedCarveout1Access2 = 0
McGeneralizedCarveout1Access3 = 0
McGeneralizedCarveout1Access4 = 0
McGeneralizedCarveout1Access5 = 0
McGeneralizedCarveout1Access6 = 0
McGeneralizedCarveout1Access7 = 0
McGeneralizedCarveout1ForceInternalAccess0 = 0
McGeneralizedCarveout1ForceInternalAccess1 = 0
McGeneralizedCarveout1ForceInternalAccess2 = 0
McGeneralizedCarveout1ForceInternalAccess3 = 0
McGeneralizedCarveout1ForceInternalAccess4 = 0
McGeneralizedCarveout1ForceInternalAccess5 = 0
McGeneralizedCarveout1ForceInternalAccess6 = 0
McGeneralizedCarveout1ForceInternalAccess7 = 0
McGeneralizedCarveout1Cfg0 = 0
McGeneralizedCarveout2Bom = 0
McGeneralizedCarveout2BomHi = 0
McGeneralizedCarveout2Size128kb = 0
McGeneralizedCarveout2Access0 = 0
McGeneralizedCarveout2Access1 = 0
McGeneralizedCarveout2Access2 = 0
McGeneralizedCarveout2Access3 = 0
McGeneralizedCarveout2Access4 = 0
McGeneralizedCarveout2Access5 = 0
McGeneralizedCarveout2Access6 = 0
McGeneralizedCarveout2Access7 = 0
McGeneralizedCarveout2ForceInternalAccess0 = 0
McGeneralizedCarveout2ForceInternalAccess1 = 0
McGeneralizedCarveout2ForceInternalAccess2 = 0
McGeneralizedCarveout2ForceInternalAccess3 = 0
McGeneralizedCarveout2ForceInternalAccess4 = 0
McGeneralizedCarveout2ForceInternalAccess5 = 0
McGeneralizedCarveout2ForceInternalAccess6 = 0
McGeneralizedCarveout2ForceInternalAccess7 = 0
McGeneralizedCarveout2Cfg0 = 0
McGeneralizedCarveout3Bom = 0
McGeneralizedCarveout3BomHi = 0
McGeneralizedCarveout3Size128kb = 0
McGeneralizedCarveout3Access0 = 0
McGeneralizedCarveout3Access1 = 0
McGeneralizedCarveout3Access2 = 0
McGeneralizedCarveout3Access3 = 0
McGeneralizedCarveout3Access4 = 0
McGeneralizedCarveout3Access5 = 0
McGeneralizedCarveout3Access6 = 0
McGeneralizedCarveout3Access7 = 0
McGeneralizedCarveout3ForceInternalAccess0 = 0
McGeneralizedCarveout3ForceInternalAccess1 = 0
McGeneralizedCarveout3ForceInternalAccess2 = 0
McGeneralizedCarveout3ForceInternalAccess3 = 0
McGeneralizedCarveout3ForceInternalAccess4 = 0
McGeneralizedCarveout3ForceInternalAccess5 = 0
McGeneralizedCarveout3ForceInternalAccess6 = 0
McGeneralizedCarveout3ForceInternalAccess7 = 0
McGeneralizedCarveout3Cfg0 = 0
McGeneralizedCarveout4Bom = 0
McGeneralizedCarveout4BomHi = 0
McGeneralizedCarveout4Size128kb = 0
McGeneralizedCarveout4Access0 = 0
McGeneralizedCarveout4Access1 = 0
McGeneralizedCarveout4Access2 = 0
McGeneralizedCarveout4Access3 = 0
McGeneralizedCarveout4Access4 = 0
McGeneralizedCarveout4Access5 = 0
McGeneralizedCarveout4Access6 = 0
McGeneralizedCarveout4Access7 = 0
McGeneralizedCarveout4ForceInternalAccess0 = 0
McGeneralizedCarveout4ForceInternalAccess1 = 0
McGeneralizedCarveout4ForceInternalAccess2 = 0
McGeneralizedCarveout4ForceInternalAccess3 = 0
McGeneralizedCarveout4ForceInternalAccess4 = 0
McGeneralizedCarveout4ForceInternalAccess5 = 0
McGeneralizedCarveout4ForceInternalAccess6 = 0
McGeneralizedCarveout4ForceInternalAccess7 = 0
McGeneralizedCarveout4Cfg0 = 0
McGeneralizedCarveout5Bom = 0
McGeneralizedCarveout5BomHi = 0
McGeneralizedCarveout5Size128kb = 0
McGeneralizedCarveout5Access0 = 0
McGeneralizedCarveout5Access1 = 0
McGeneralizedCarveout5Access2 = 0
McGeneralizedCarveout5Access3 = 0
McGeneralizedCarveout5Access4 = 0
McGeneralizedCarveout5Access5 = 0
McGeneralizedCarveout5Access6 = 0
McGeneralizedCarveout5Access7 = 0
McGeneralizedCarveout5ForceInternalAccess0 = 0
McGeneralizedCarveout5ForceInternalAccess1 = 0
McGeneralizedCarveout5ForceInternalAccess2 = 0
McGeneralizedCarveout5ForceInternalAccess3 = 0
McGeneralizedCarveout5ForceInternalAccess4 = 0
McGeneralizedCarveout5ForceInternalAccess5 = 0
McGeneralizedCarveout5ForceInternalAccess6 = 0
McGeneralizedCarveout5ForceInternalAccess7 = 0
McGeneralizedCarveout5Cfg0 = 0
McGeneralizedCarveout6Bom = 0
McGeneralizedCarveout6BomHi = 0
McGeneralizedCarveout6Size128kb = 0
McGeneralizedCarveout6Access0 = 0
McGeneralizedCarveout6Access1 = 0
McGeneralizedCarveout6Access2 = 0
McGeneralizedCarveout6Access3 = 0
McGeneralizedCarveout6Access4 = 0
McGeneralizedCarveout6Access5 = 0
McGeneralizedCarveout6Access6 = 0
McGeneralizedCarveout6Access7 = 0
McGeneralizedCarveout6ForceInternalAccess0 = 0
McGeneralizedCarveout6ForceInternalAccess1 = 0
McGeneralizedCarveout6ForceInternalAccess2 = 0
McGeneralizedCarveout6ForceInternalAccess3 = 0
McGeneralizedCarveout6ForceInternalAccess4 = 0
McGeneralizedCarveout6ForceInternalAccess5 = 0
McGeneralizedCarveout6ForceInternalAccess6 = 0
McGeneralizedCarveout6ForceInternalAccess7 = 0
McGeneralizedCarveout6Cfg0 = 0
McGeneralizedCarveout7Bom = 0
McGeneralizedCarveout7BomHi = 0
McGeneralizedCarveout7Size128kb = 0
McGeneralizedCarveout7Access0 = 0
McGeneralizedCarveout7Access1 = 0
McGeneralizedCarveout7Access2 = 0
McGeneralizedCarveout7Access3 = 0
McGeneralizedCarveout7Access4 = 0
McGeneralizedCarveout7Access5 = 0
McGeneralizedCarveout7Access6 = 0
McGeneralizedCarveout7Access7 = 0
McGeneralizedCarveout7ForceInternalAccess0 = 0
McGeneralizedCarveout7ForceInternalAccess1 = 0
McGeneralizedCarveout7ForceInternalAccess2 = 0
McGeneralizedCarveout7ForceInternalAccess3 = 0
McGeneralizedCarveout7ForceInternalAccess4 = 0
McGeneralizedCarveout7ForceInternalAccess5 = 0
McGeneralizedCarveout7ForceInternalAccess6 = 0
McGeneralizedCarveout7ForceInternalAccess7 = 0
McGeneralizedCarveout7Cfg0 = 0
McGeneralizedCarveout8Bom = 0
McGeneralizedCarveout8BomHi = 0
McGeneralizedCarveout8Size128kb = 0
McGeneralizedCarveout8Access0 = 0
McGeneralizedCarveout8Access1 = 0
McGeneralizedCarveout8Access2 = 0
McGeneralizedCarveout8Access3 = 0
McGeneralizedCarveout8Access4 = 0
McGeneralizedCarveout8Access5 = 0
McGeneralizedCarveout8Access6 = 0
McGeneralizedCarveout8Access7 = 0
McGeneralizedCarveout8ForceInternalAccess0 = 0
McGeneralizedCarveout8ForceInternalAccess1 = 0
McGeneralizedCarveout8ForceInternalAccess2 = 0
McGeneralizedCarveout8ForceInternalAccess3 = 0
McGeneralizedCarveout8ForceInternalAccess4 = 0
McGeneralizedCarveout8ForceInternalAccess5 = 0
McGeneralizedCarveout8ForceInternalAccess6 = 0
McGeneralizedCarveout8ForceInternalAccess7 = 0
McGeneralizedCarveout8Cfg0 = 0
McGeneralizedCarveout9Bom = 0
McGeneralizedCarveout9BomHi = 0
McGeneralizedCarveout9Size128kb = 0
McGeneralizedCarveout9Access0 = 0
McGeneralizedCarveout9Access1 = 0
McGeneralizedCarveout9Access2 = 0
McGeneralizedCarveout9Access3 = 0
McGeneralizedCarveout9Access4 = 0
McGeneralizedCarveout9Access5 = 0
McGeneralizedCarveout9Access6 = 0
McGeneralizedCarveout9Access7 = 0
McGeneralizedCarveout9ForceInternalAccess0 = 0
McGeneralizedCarveout9ForceInternalAccess1 = 0
McGeneralizedCarveout9ForceInternalAccess2 = 0
McGeneralizedCarveout9ForceInternalAccess3 = 0
McGeneralizedCarveout9ForceInternalAccess4 = 0
McGeneralizedCarveout9ForceInternalAccess5 = 0
McGeneralizedCarveout9ForceInternalAccess6 = 0
McGeneralizedCarveout9ForceInternalAccess7 = 0
McGeneralizedCarveout9Cfg0 = 0
McGeneralizedCarveout10Bom = 0
McGeneralizedCarveout10BomHi = 0
McGeneralizedCarveout10Size128kb = 0
McGeneralizedCarveout10Access0 = 0
McGeneralizedCarveout10Access1 = 0
McGeneralizedCarveout10Access2 = 0
McGeneralizedCarveout10Access3 = 0
McGeneralizedCarveout10Access4 = 0
McGeneralizedCarveout10Access5 = 0
McGeneralizedCarveout10Access6 = 0
McGeneralizedCarveout10Access7 = 0
McGeneralizedCarveout10ForceInternalAccess0 = 0
McGeneralizedCarveout10ForceInternalAccess1 = 0
McGeneralizedCarveout10ForceInternalAccess2 = 0
McGeneralizedCarveout10ForceInternalAccess3 = 0
McGeneralizedCarveout10ForceInternalAccess4 = 0
McGeneralizedCarveout10ForceInternalAccess5 = 0
McGeneralizedCarveout10ForceInternalAccess6 = 0
McGeneralizedCarveout10ForceInternalAccess7 = 0
McGeneralizedCarveout10Cfg0 = 0
McGeneralizedCarveout11Bom = 0
McGeneralizedCarveout11BomHi = 0
McGeneralizedCarveout11Size128kb = 0
McGeneralizedCarveout11Access0 = 0
McGeneralizedCarveout11Access1 = 0
McGeneralizedCarveout11Access2 = 0
McGeneralizedCarveout11Access3 = 0
McGeneralizedCarveout11Access4 = 0
McGeneralizedCarveout11Access5 = 0
McGeneralizedCarveout11Access6 = 0
McGeneralizedCarveout11Access7 = 0
McGeneralizedCarveout11ForceInternalAccess0 = 0
McGeneralizedCarveout11ForceInternalAccess1 = 0
McGeneralizedCarveout11ForceInternalAccess2 = 0
McGeneralizedCarveout11ForceInternalAccess3 = 0
McGeneralizedCarveout11ForceInternalAccess4 = 0
McGeneralizedCarveout11ForceInternalAccess5 = 0
McGeneralizedCarveout11ForceInternalAccess6 = 0
McGeneralizedCarveout11ForceInternalAccess7 = 0
McGeneralizedCarveout11Cfg0 = 0
McGeneralizedCarveout12Bom = 0
McGeneralizedCarveout12BomHi = 0
McGeneralizedCarveout12Size128kb = 0
McGeneralizedCarveout12Access0 = 0
McGeneralizedCarveout12Access1 = 0
McGeneralizedCarveout12Access2 = 0
McGeneralizedCarveout12Access3 = 0
McGeneralizedCarveout12Access4 = 0
McGeneralizedCarveout12Access5 = 0
McGeneralizedCarveout12Access6 = 0
McGeneralizedCarveout12Access7 = 0
McGeneralizedCarveout12ForceInternalAccess0 = 0
McGeneralizedCarveout12ForceInternalAccess1 = 0
McGeneralizedCarveout12ForceInternalAccess2 = 0
McGeneralizedCarveout12ForceInternalAccess3 = 0
McGeneralizedCarveout12ForceInternalAccess4 = 0
McGeneralizedCarveout12ForceInternalAccess5 = 0
McGeneralizedCarveout12ForceInternalAccess6 = 0
McGeneralizedCarveout12ForceInternalAccess7 = 0
McGeneralizedCarveout12Cfg0 = 0
McGeneralizedCarveout13Bom = 0
McGeneralizedCarveout13BomHi = 0
McGeneralizedCarveout13Size128kb = 0
McGeneralizedCarveout13Access0 = 0
McGeneralizedCarveout13Access1 = 0
McGeneralizedCarveout13Access2 = 0
McGeneralizedCarveout13Access3 = 0
McGeneralizedCarveout13Access4 = 0
McGeneralizedCarveout13Access5 = 0
McGeneralizedCarveout13Access6 = 0
McGeneralizedCarveout13Access7 = 0
McGeneralizedCarveout13ForceInternalAccess0 = 0
McGeneralizedCarveout13ForceInternalAccess1 = 0
McGeneralizedCarveout13ForceInternalAccess2 = 0
McGeneralizedCarveout13ForceInternalAccess3 = 0
McGeneralizedCarveout13ForceInternalAccess4 = 0
McGeneralizedCarveout13ForceInternalAccess5 = 0
McGeneralizedCarveout13ForceInternalAccess6 = 0
McGeneralizedCarveout13ForceInternalAccess7 = 0
McGeneralizedCarveout13Cfg0 = 0
McGeneralizedCarveout14Bom = 0
McGeneralizedCarveout14BomHi = 0
McGeneralizedCarveout14Size128kb = 0
McGeneralizedCarveout14Access0 = 0
McGeneralizedCarveout14Access1 = 0
McGeneralizedCarveout14Access2 = 0
McGeneralizedCarveout14Access3 = 0
McGeneralizedCarveout14Access4 = 0
McGeneralizedCarveout14Access5 = 0
McGeneralizedCarveout14Access6 = 0
McGeneralizedCarveout14Access7 = 0
McGeneralizedCarveout14ForceInternalAccess0 = 0
McGeneralizedCarveout14ForceInternalAccess1 = 0
McGeneralizedCarveout14ForceInternalAccess2 = 0
McGeneralizedCarveout14ForceInternalAccess3 = 0
McGeneralizedCarveout14ForceInternalAccess4 = 0
McGeneralizedCarveout14ForceInternalAccess5 = 0
McGeneralizedCarveout14ForceInternalAccess6 = 0
McGeneralizedCarveout14ForceInternalAccess7 = 0
McGeneralizedCarveout14Cfg0 = 0
McGeneralizedCarveout15Bom = 0
McGeneralizedCarveout15BomHi = 0
McGeneralizedCarveout15Size128kb = 0
McGeneralizedCarveout15Access0 = 0
McGeneralizedCarveout15Access1 = 0
McGeneralizedCarveout15Access2 = 0
McGeneralizedCarveout15Access3 = 0
McGeneralizedCarveout15Access4 = 0
McGeneralizedCarveout15Access5 = 0
McGeneralizedCarveout15Access6 = 0
McGeneralizedCarveout15Access7 = 0
McGeneralizedCarveout15ForceInternalAccess0 = 0
McGeneralizedCarveout15ForceInternalAccess1 = 0
McGeneralizedCarveout15ForceInternalAccess2 = 0
McGeneralizedCarveout15ForceInternalAccess3 = 0
McGeneralizedCarveout15ForceInternalAccess4 = 0
McGeneralizedCarveout15ForceInternalAccess5 = 0
McGeneralizedCarveout15ForceInternalAccess6 = 0
McGeneralizedCarveout15ForceInternalAccess7 = 0
McGeneralizedCarveout15Cfg0 = 0
McGeneralizedCarveout16Bom = 0
McGeneralizedCarveout16BomHi = 0
McGeneralizedCarveout16Size128kb = 0
McGeneralizedCarveout16Access0 = 0
McGeneralizedCarveout16Access1 = 0
McGeneralizedCarveout16Access2 = 0
McGeneralizedCarveout16Access3 = 0
McGeneralizedCarveout16Access4 = 0
McGeneralizedCarveout16Access5 = 0
McGeneralizedCarveout16Access6 = 0
McGeneralizedCarveout16Access7 = 0
McGeneralizedCarveout16ForceInternalAccess0 = 0
McGeneralizedCarveout16ForceInternalAccess1 = 0
McGeneralizedCarveout16ForceInternalAccess2 = 0
McGeneralizedCarveout16ForceInternalAccess3 = 0
McGeneralizedCarveout16ForceInternalAccess4 = 0
McGeneralizedCarveout16ForceInternalAccess5 = 0
McGeneralizedCarveout16ForceInternalAccess6 = 0
McGeneralizedCarveout16ForceInternalAccess7 = 0
McGeneralizedCarveout16Cfg0 = 0
McGeneralizedCarveout17Bom = 0
McGeneralizedCarveout17BomHi = 0
McGeneralizedCarveout17Size128kb = 0
McGeneralizedCarveout17Access0 = 0
McGeneralizedCarveout17Access1 = 0
McGeneralizedCarveout17Access2 = 0
McGeneralizedCarveout17Access3 = 0
McGeneralizedCarveout17Access4 = 0
McGeneralizedCarveout17Access5 = 0
McGeneralizedCarveout17Access6 = 0
McGeneralizedCarveout17Access7 = 0
McGeneralizedCarveout17ForceInternalAccess0 = 0
McGeneralizedCarveout17ForceInternalAccess1 = 0
McGeneralizedCarveout17ForceInternalAccess2 = 0
McGeneralizedCarveout17ForceInternalAccess3 = 0
McGeneralizedCarveout17ForceInternalAccess4 = 0
McGeneralizedCarveout17ForceInternalAccess5 = 0
McGeneralizedCarveout17ForceInternalAccess6 = 0
McGeneralizedCarveout17ForceInternalAccess7 = 0
McGeneralizedCarveout17Cfg0 = 0
McGeneralizedCarveout18Bom = 0
McGeneralizedCarveout18BomHi = 0
McGeneralizedCarveout18Size128kb = 0
McGeneralizedCarveout18Access0 = 0
McGeneralizedCarveout18Access1 = 0
McGeneralizedCarveout18Access2 = 0
McGeneralizedCarveout18Access3 = 0
McGeneralizedCarveout18Access4 = 0
McGeneralizedCarveout18Access5 = 0
McGeneralizedCarveout18Access6 = 0
McGeneralizedCarveout18Access7 = 0
McGeneralizedCarveout18ForceInternalAccess0 = 0
McGeneralizedCarveout18ForceInternalAccess1 = 0
McGeneralizedCarveout18ForceInternalAccess2 = 0
McGeneralizedCarveout18ForceInternalAccess3 = 0
McGeneralizedCarveout18ForceInternalAccess4 = 0
McGeneralizedCarveout18ForceInternalAccess5 = 0
McGeneralizedCarveout18ForceInternalAccess6 = 0
McGeneralizedCarveout18ForceInternalAccess7 = 0
McGeneralizedCarveout18Cfg0 = 0
McGeneralizedCarveout19Bom = 0
McGeneralizedCarveout19BomHi = 0
McGeneralizedCarveout19Size128kb = 0
McGeneralizedCarveout19Access0 = 0
McGeneralizedCarveout19Access1 = 0
McGeneralizedCarveout19Access2 = 0
McGeneralizedCarveout19Access3 = 0
McGeneralizedCarveout19Access4 = 0
McGeneralizedCarveout19Access5 = 0
McGeneralizedCarveout19Access6 = 0
McGeneralizedCarveout19Access7 = 0
McGeneralizedCarveout19ForceInternalAccess0 = 0
McGeneralizedCarveout19ForceInternalAccess1 = 0
McGeneralizedCarveout19ForceInternalAccess2 = 0
McGeneralizedCarveout19ForceInternalAccess3 = 0
McGeneralizedCarveout19ForceInternalAccess4 = 0
McGeneralizedCarveout19ForceInternalAccess5 = 0
McGeneralizedCarveout19ForceInternalAccess6 = 0
McGeneralizedCarveout19ForceInternalAccess7 = 0
McGeneralizedCarveout19Cfg0 = 0
McGeneralizedCarveout20Bom = 0
McGeneralizedCarveout20BomHi = 0
McGeneralizedCarveout20Size128kb = 0
McGeneralizedCarveout20Access0 = 0
McGeneralizedCarveout20Access1 = 0
McGeneralizedCarveout20Access2 = 0
McGeneralizedCarveout20Access3 = 0
McGeneralizedCarveout20Access4 = 0
McGeneralizedCarveout20Access5 = 0
McGeneralizedCarveout20Access6 = 0
McGeneralizedCarveout20Access7 = 0
McGeneralizedCarveout20ForceInternalAccess0 = 0
McGeneralizedCarveout20ForceInternalAccess1 = 0
McGeneralizedCarveout20ForceInternalAccess2 = 0
McGeneralizedCarveout20ForceInternalAccess3 = 0
McGeneralizedCarveout20ForceInternalAccess4 = 0
McGeneralizedCarveout20ForceInternalAccess5 = 0
McGeneralizedCarveout20ForceInternalAccess6 = 0
McGeneralizedCarveout20ForceInternalAccess7 = 0
McGeneralizedCarveout20Cfg0 = 0
McGeneralizedCarveout21Bom = 0
McGeneralizedCarveout21BomHi = 0
McGeneralizedCarveout21Size128kb = 0
McGeneralizedCarveout21Access0 = 0
McGeneralizedCarveout21Access1 = 0
McGeneralizedCarveout21Access2 = 0
McGeneralizedCarveout21Access3 = 0
McGeneralizedCarveout21Access4 = 0
McGeneralizedCarveout21Access5 = 0
McGeneralizedCarveout21Access6 = 0
McGeneralizedCarveout21Access7 = 0
McGeneralizedCarveout21ForceInternalAccess0 = 0
McGeneralizedCarveout21ForceInternalAccess1 = 0
McGeneralizedCarveout21ForceInternalAccess2 = 0
McGeneralizedCarveout21ForceInternalAccess3 = 0
McGeneralizedCarveout21ForceInternalAccess4 = 0
McGeneralizedCarveout21ForceInternalAccess5 = 0
McGeneralizedCarveout21ForceInternalAccess6 = 0
McGeneralizedCarveout21ForceInternalAccess7 = 0
McGeneralizedCarveout21Cfg0 = 0
McGeneralizedCarveout22Bom = 0
McGeneralizedCarveout22BomHi = 0
McGeneralizedCarveout22Size128kb = 0
McGeneralizedCarveout22Access0 = 0
McGeneralizedCarveout22Access1 = 0
McGeneralizedCarveout22Access2 = 0
McGeneralizedCarveout22Access3 = 0
McGeneralizedCarveout22Access4 = 0
McGeneralizedCarveout22Access5 = 0
McGeneralizedCarveout22Access6 = 0
McGeneralizedCarveout22Access7 = 0
McGeneralizedCarveout22ForceInternalAccess0 = 0
McGeneralizedCarveout22ForceInternalAccess1 = 0
McGeneralizedCarveout22ForceInternalAccess2 = 0
McGeneralizedCarveout22ForceInternalAccess3 = 0
McGeneralizedCarveout22ForceInternalAccess4 = 0
McGeneralizedCarveout22ForceInternalAccess5 = 0
McGeneralizedCarveout22ForceInternalAccess6 = 0
McGeneralizedCarveout22ForceInternalAccess7 = 0
McGeneralizedCarveout22Cfg0 = 0
McGeneralizedCarveout23Bom = 0
McGeneralizedCarveout23BomHi = 0
McGeneralizedCarveout23Size128kb = 0
McGeneralizedCarveout23Access0 = 0
McGeneralizedCarveout23Access1 = 0
McGeneralizedCarveout23Access2 = 0
McGeneralizedCarveout23Access3 = 0
McGeneralizedCarveout23Access4 = 0
McGeneralizedCarveout23Access5 = 0
McGeneralizedCarveout23Access6 = 0
McGeneralizedCarveout23Access7 = 0
McGeneralizedCarveout23ForceInternalAccess0 = 0
McGeneralizedCarveout23ForceInternalAccess1 = 0
McGeneralizedCarveout23ForceInternalAccess2 = 0
McGeneralizedCarveout23ForceInternalAccess3 = 0
McGeneralizedCarveout23ForceInternalAccess4 = 0
McGeneralizedCarveout23ForceInternalAccess5 = 0
McGeneralizedCarveout23ForceInternalAccess6 = 0
McGeneralizedCarveout23ForceInternalAccess7 = 0
McGeneralizedCarveout23Cfg0 = 0
McGeneralizedCarveout24Bom = 0
McGeneralizedCarveout24BomHi = 0
McGeneralizedCarveout24Size128kb = 0
McGeneralizedCarveout24Access0 = 0
McGeneralizedCarveout24Access1 = 0
McGeneralizedCarveout24Access2 = 0
McGeneralizedCarveout24Access3 = 0
McGeneralizedCarveout24Access4 = 0
McGeneralizedCarveout24Access5 = 0
McGeneralizedCarveout24Access6 = 0
McGeneralizedCarveout24Access7 = 0
McGeneralizedCarveout24ForceInternalAccess0 = 0
McGeneralizedCarveout24ForceInternalAccess1 = 0
McGeneralizedCarveout24ForceInternalAccess2 = 0
McGeneralizedCarveout24ForceInternalAccess3 = 0
McGeneralizedCarveout24ForceInternalAccess4 = 0
McGeneralizedCarveout24ForceInternalAccess5 = 0
McGeneralizedCarveout24ForceInternalAccess6 = 0
McGeneralizedCarveout24ForceInternalAccess7 = 0
McGeneralizedCarveout24Cfg0 = 0
McGeneralizedCarveout25Bom = 0
McGeneralizedCarveout25BomHi = 0
McGeneralizedCarveout25Size128kb = 0
McGeneralizedCarveout25Access0 = 0
McGeneralizedCarveout25Access1 = 0
McGeneralizedCarveout25Access2 = 0
McGeneralizedCarveout25Access3 = 0
McGeneralizedCarveout25Access4 = 0
McGeneralizedCarveout25Access5 = 0
McGeneralizedCarveout25Access6 = 0
McGeneralizedCarveout25Access7 = 0
McGeneralizedCarveout25ForceInternalAccess0 = 0
McGeneralizedCarveout25ForceInternalAccess1 = 0
McGeneralizedCarveout25ForceInternalAccess2 = 0
McGeneralizedCarveout25ForceInternalAccess3 = 0
McGeneralizedCarveout25ForceInternalAccess4 = 0
McGeneralizedCarveout25ForceInternalAccess5 = 0
McGeneralizedCarveout25ForceInternalAccess6 = 0
McGeneralizedCarveout25ForceInternalAccess7 = 0
McGeneralizedCarveout25Cfg0 = 0
McGeneralizedCarveout26Bom = 0
McGeneralizedCarveout26BomHi = 0
McGeneralizedCarveout26Size128kb = 0
McGeneralizedCarveout26Access0 = 0
McGeneralizedCarveout26Access1 = 0
McGeneralizedCarveout26Access2 = 0
McGeneralizedCarveout26Access3 = 0
McGeneralizedCarveout26Access4 = 0
McGeneralizedCarveout26Access5 = 0
McGeneralizedCarveout26Access6 = 0
McGeneralizedCarveout26Access7 = 0
McGeneralizedCarveout26ForceInternalAccess0 = 0
McGeneralizedCarveout26ForceInternalAccess1 = 0
McGeneralizedCarveout26ForceInternalAccess2 = 0
McGeneralizedCarveout26ForceInternalAccess3 = 0
McGeneralizedCarveout26ForceInternalAccess4 = 0
McGeneralizedCarveout26ForceInternalAccess5 = 0
McGeneralizedCarveout26ForceInternalAccess6 = 0
McGeneralizedCarveout26ForceInternalAccess7 = 0
McGeneralizedCarveout26Cfg0 = 0
McGeneralizedCarveout27Bom = 0
McGeneralizedCarveout27BomHi = 0
McGeneralizedCarveout27Size128kb = 0
McGeneralizedCarveout27Access0 = 0
McGeneralizedCarveout27Access1 = 0
McGeneralizedCarveout27Access2 = 0
McGeneralizedCarveout27Access3 = 0
McGeneralizedCarveout27Access4 = 0
McGeneralizedCarveout27Access5 = 0
McGeneralizedCarveout27Access6 = 0
McGeneralizedCarveout27Access7 = 0
McGeneralizedCarveout27ForceInternalAccess0 = 0
McGeneralizedCarveout27ForceInternalAccess1 = 0
McGeneralizedCarveout27ForceInternalAccess2 = 0
McGeneralizedCarveout27ForceInternalAccess3 = 0
McGeneralizedCarveout27ForceInternalAccess4 = 0
McGeneralizedCarveout27ForceInternalAccess5 = 0
McGeneralizedCarveout27ForceInternalAccess6 = 0
McGeneralizedCarveout27ForceInternalAccess7 = 0
McGeneralizedCarveout27Cfg0 = 0
McGeneralizedCarveout28Bom = 0
McGeneralizedCarveout28BomHi = 0
McGeneralizedCarveout28Size128kb = 0
McGeneralizedCarveout28Access0 = 0
McGeneralizedCarveout28Access1 = 0
McGeneralizedCarveout28Access2 = 0
McGeneralizedCarveout28Access3 = 0
McGeneralizedCarveout28Access4 = 0
McGeneralizedCarveout28Access5 = 0
McGeneralizedCarveout28Access6 = 0
McGeneralizedCarveout28Access7 = 0
McGeneralizedCarveout28ForceInternalAccess0 = 0
McGeneralizedCarveout28ForceInternalAccess1 = 0
McGeneralizedCarveout28ForceInternalAccess2 = 0
McGeneralizedCarveout28ForceInternalAccess3 = 0
McGeneralizedCarveout28ForceInternalAccess4 = 0
McGeneralizedCarveout28ForceInternalAccess5 = 0
McGeneralizedCarveout28ForceInternalAccess6 = 0
McGeneralizedCarveout28ForceInternalAccess7 = 0
McGeneralizedCarveout28Cfg0 = 0
McGeneralizedCarveout29Bom = 0
McGeneralizedCarveout29BomHi = 0
McGeneralizedCarveout29Size128kb = 0
McGeneralizedCarveout29Access0 = 0
McGeneralizedCarveout29Access1 = 0
McGeneralizedCarveout29Access2 = 0
McGeneralizedCarveout29Access3 = 0
McGeneralizedCarveout29Access4 = 0
McGeneralizedCarveout29Access5 = 0
McGeneralizedCarveout29Access6 = 0
McGeneralizedCarveout29Access7 = 0
McGeneralizedCarveout29ForceInternalAccess0 = 0
McGeneralizedCarveout29ForceInternalAccess1 = 0
McGeneralizedCarveout29ForceInternalAccess2 = 0
McGeneralizedCarveout29ForceInternalAccess3 = 0
McGeneralizedCarveout29ForceInternalAccess4 = 0
McGeneralizedCarveout29ForceInternalAccess5 = 0
McGeneralizedCarveout29ForceInternalAccess6 = 0
McGeneralizedCarveout29ForceInternalAccess7 = 0
McGeneralizedCarveout29Cfg0 = 0
McGeneralizedCarveout30Bom = 0
McGeneralizedCarveout30BomHi = 0
McGeneralizedCarveout30Size128kb = 0
McGeneralizedCarveout30Access0 = 0
McGeneralizedCarveout30Access1 = 0
McGeneralizedCarveout30Access2 = 0
McGeneralizedCarveout30Access3 = 0
McGeneralizedCarveout30Access4 = 0
McGeneralizedCarveout30Access5 = 0
McGeneralizedCarveout30Access6 = 0
McGeneralizedCarveout30Access7 = 0
McGeneralizedCarveout30ForceInternalAccess0 = 0
McGeneralizedCarveout30ForceInternalAccess1 = 0
McGeneralizedCarveout30ForceInternalAccess2 = 0
McGeneralizedCarveout30ForceInternalAccess3 = 0
McGeneralizedCarveout30ForceInternalAccess4 = 0
McGeneralizedCarveout30ForceInternalAccess5 = 0
McGeneralizedCarveout30ForceInternalAccess6 = 0
McGeneralizedCarveout30ForceInternalAccess7 = 0
McGeneralizedCarveout30Cfg0 = 0
McGeneralizedCarveout31Bom = 0
McGeneralizedCarveout31BomHi = 0
McGeneralizedCarveout31Size128kb = 0
McGeneralizedCarveout31Access0 = 0
McGeneralizedCarveout31Access1 = 0
McGeneralizedCarveout31Access2 = 0
McGeneralizedCarveout31Access3 = 0
McGeneralizedCarveout31Access4 = 0
McGeneralizedCarveout31Access5 = 0
McGeneralizedCarveout31Access6 = 0
McGeneralizedCarveout31Access7 = 0
McGeneralizedCarveout31ForceInternalAccess0 = 0
McGeneralizedCarveout31ForceInternalAccess1 = 0
McGeneralizedCarveout31ForceInternalAccess2 = 0
McGeneralizedCarveout31ForceInternalAccess3 = 0
McGeneralizedCarveout31ForceInternalAccess4 = 0
McGeneralizedCarveout31ForceInternalAccess5 = 0
McGeneralizedCarveout31ForceInternalAccess6 = 0
McGeneralizedCarveout31ForceInternalAccess7 = 0
McGeneralizedCarveout31Cfg0 = 0
McEccRegion0Cfg0 = 0
McEccRegion0Bom = 0
McEccRegion0BomHi = 0
McEccRegion0Size = 0
McEccRegion1Cfg0 = 0
McEccRegion1Bom = 0
McEccRegion1BomHi = 0
McEccRegion1Size = 0
McEccRegion2Cfg0 = 0
McEccRegion2Bom = 0
McEccRegion2BomHi = 0
McEccRegion2Size = 0
McEccRegion3Cfg0 = 0
McEccRegion3Bom = 0
McEccRegion3BomHi = 0
McEccRegion3Size = 0
McMtsCarveoutSizeMb = 0
McMtsCarveoutRegCtrl = 0
McSidStreamidOverrideConfigPtcr = 0
McSidStreamidSecurityConfigPtcr = 0
McSidStreamidOverrideConfigHdar = 0
McSidStreamidSecurityConfigHdar = 0
McSidStreamidOverrideConfigHost1xdmar = 0
McSidStreamidSecurityConfigHost1xdmar = 0
McSidStreamidOverrideConfigNvencsrd = 0
McSidStreamidSecurityConfigNvencsrd = 0
McSidStreamidOverrideConfigSatar = 0
McSidStreamidSecurityConfigSatar = 0
McSidStreamidOverrideConfigMpcorer = 0
McSidStreamidSecurityConfigMpcorer = 0
McSidStreamidOverrideConfigNvencswr = 0
McSidStreamidSecurityConfigNvencswr = 0
McSidStreamidOverrideConfigHdaw = 0
McSidStreamidSecurityConfigHdaw = 0
McSidStreamidOverrideConfigMpcorew = 0
McSidStreamidSecurityConfigMpcorew = 0
McSidStreamidOverrideConfigSataw = 0
McSidStreamidSecurityConfigSataw = 0
McSidStreamidOverrideConfigIspra = 0
McSidStreamidSecurityConfigIspra = 0
McSidStreamidOverrideConfigIspfalr = 0
McSidStreamidSecurityConfigIspfalr = 0
McSidStreamidOverrideConfigIspwa = 0
McSidStreamidSecurityConfigIspwa = 0
McSidStreamidOverrideConfigIspwb = 0
McSidStreamidSecurityConfigIspwb = 0
McSidStreamidOverrideConfigXusb_hostr = 0
McSidStreamidSecurityConfigXusb_hostr = 0
McSidStreamidOverrideConfigXusb_hostw = 0
McSidStreamidSecurityConfigXusb_hostw = 0
McSidStreamidOverrideConfigXusb_devr = 0
McSidStreamidSecurityConfigXusb_devr = 0
McSidStreamidOverrideConfigXusb_devw = 0
McSidStreamidSecurityConfigXusb_devw = 0
McSidStreamidOverrideConfigTsecsrd = 0
McSidStreamidSecurityConfigTsecsrd = 0
McSidStreamidOverrideConfigTsecswr = 0
McSidStreamidSecurityConfigTsecswr = 0
McSidStreamidOverrideConfigSdmmcra = 0
McSidStreamidSecurityConfigSdmmcra = 0
McSidStreamidOverrideConfigSdmmcr = 0
McSidStreamidSecurityConfigSdmmcr = 0
McSidStreamidOverrideConfigSdmmcrab = 0
McSidStreamidSecurityConfigSdmmcrab = 0
McSidStreamidOverrideConfigSdmmcwa = 0
McSidStreamidSecurityConfigSdmmcwa = 0
McSidStreamidOverrideConfigSdmmcw = 0
McSidStreamidSecurityConfigSdmmcw = 0
McSidStreamidOverrideConfigSdmmcwab = 0
McSidStreamidSecurityConfigSdmmcwab = 0
McSidStreamidOverrideConfigVicsrd = 0
McSidStreamidSecurityConfigVicsrd = 0
McSidStreamidOverrideConfigVicswr = 0
McSidStreamidSecurityConfigVicswr = 0
McSidStreamidOverrideConfigViw = 0
McSidStreamidSecurityConfigViw = 0
McSidStreamidOverrideConfigNvdecsrd = 0
McSidStreamidSecurityConfigNvdecsrd = 0
McSidStreamidOverrideConfigNvdecswr = 0
McSidStreamidSecurityConfigNvdecswr = 0
McSidStreamidOverrideConfigAper = 0
McSidStreamidSecurityConfigAper = 0
McSidStreamidOverrideConfigApew = 0
McSidStreamidSecurityConfigApew = 0
McSidStreamidOverrideConfigNvjpgsrd = 0
McSidStreamidSecurityConfigNvjpgsrd = 0
McSidStreamidOverrideConfigNvjpgswr = 0
McSidStreamidSecurityConfigNvjpgswr = 0
McSidStreamidOverrideConfigSesrd = 0
McSidStreamidSecurityConfigSesrd = 0
McSidStreamidOverrideConfigSeswr = 0
McSidStreamidSecurityConfigSeswr = 0
McSidStreamidOverrideConfigAxiapr = 0
McSidStreamidSecurityConfigAxiapr = 0
McSidStreamidOverrideConfigAxiapw = 0
McSidStreamidSecurityConfigAxiapw = 0
McSidStreamidOverrideConfigEtrr = 0
McSidStreamidSecurityConfigEtrr = 0
McSidStreamidOverrideConfigEtrw = 0
McSidStreamidSecurityConfigEtrw = 0
McSidStreamidOverrideConfigTsecsrdb = 0
McSidStreamidSecurityConfigTsecsrdb = 0
McSidStreamidOverrideConfigTsecswrb = 0
McSidStreamidSecurityConfigTsecswrb = 0
McSidStreamidOverrideConfigAxisr = 0
McSidStreamidSecurityConfigAxisr = 0
McSidStreamidOverrideConfigAxisw = 0
McSidStreamidSecurityConfigAxisw = 0
McSidStreamidOverrideConfigEqosr = 0
McSidStreamidSecurityConfigEqosr = 0
McSidStreamidOverrideConfigEqosw = 0
McSidStreamidSecurityConfigEqosw = 0
McSidStreamidOverrideConfigUfshcr = 0
McSidStreamidSecurityConfigUfshcr = 0
McSidStreamidOverrideConfigUfshcw = 0
McSidStreamidSecurityConfigUfshcw = 0
McSidStreamidOverrideConfigNvdisplayr = 0
McSidStreamidSecurityConfigNvdisplayr = 0
McSidStreamidOverrideConfigBpmpr = 0
McSidStreamidSecurityConfigBpmpr = 0
McSidStreamidOverrideConfigBpmpw = 0
McSidStreamidSecurityConfigBpmpw = 0
McSidStreamidOverrideConfigBpmpdmar = 0
McSidStreamidSecurityConfigBpmpdmar = 0
McSidStreamidOverrideConfigBpmpdmaw = 0
McSidStreamidSecurityConfigBpmpdmaw = 0
McSidStreamidOverrideConfigAonr = 0
McSidStreamidSecurityConfigAonr = 0
McSidStreamidOverrideConfigAonw = 0
McSidStreamidSecurityConfigAonw = 0
McSidStreamidOverrideConfigAondmar = 0
McSidStreamidSecurityConfigAondmar = 0
McSidStreamidOverrideConfigAondmaw = 0
McSidStreamidSecurityConfigAondmaw = 0
McSidStreamidOverrideConfigScer = 0
McSidStreamidSecurityConfigScer = 0
McSidStreamidOverrideConfigScew = 0
McSidStreamidSecurityConfigScew = 0
McSidStreamidOverrideConfigScedmar = 0
McSidStreamidSecurityConfigScedmar = 0
McSidStreamidOverrideConfigScedmaw = 0
McSidStreamidSecurityConfigScedmaw = 0
McSidStreamidOverrideConfigApedmar = 0
McSidStreamidSecurityConfigApedmar = 0
McSidStreamidOverrideConfigApedmaw = 0
McSidStreamidSecurityConfigApedmaw = 0
McSidStreamidOverrideConfigNvdisplayr1 = 0
McSidStreamidSecurityConfigNvdisplayr1 = 0
McSidStreamidOverrideConfigVicsrd1 = 0
McSidStreamidSecurityConfigVicsrd1 = 0
McSidStreamidOverrideConfigNvdecsrd1 = 0
McSidStreamidSecurityConfigNvdecsrd1 = 0
McSidStreamidOverrideConfigVifalr = 0
McSidStreamidSecurityConfigVifalr = 0
McSidStreamidOverrideConfigVifalw = 0
McSidStreamidSecurityConfigVifalw = 0
McSidStreamidOverrideConfigDla0rda = 0
McSidStreamidSecurityConfigDla0rda = 0
McSidStreamidOverrideConfigDla0falrdb = 0
McSidStreamidSecurityConfigDla0falrdb = 0
McSidStreamidOverrideConfigDla0wra = 0
McSidStreamidSecurityConfigDla0wra = 0
McSidStreamidOverrideConfigDla0falwrb = 0
McSidStreamidSecurityConfigDla0falwrb = 0
McSidStreamidOverrideConfigDla1rda = 0
McSidStreamidSecurityConfigDla1rda = 0
McSidStreamidOverrideConfigDla1falrdb = 0
McSidStreamidSecurityConfigDla1falrdb = 0
McSidStreamidOverrideConfigDla1wra = 0
McSidStreamidSecurityConfigDla1wra = 0
McSidStreamidOverrideConfigDla1falwrb = 0
McSidStreamidSecurityConfigDla1falwrb = 0
McSidStreamidOverrideConfigPva0rda = 0
McSidStreamidSecurityConfigPva0rda = 0
McSidStreamidOverrideConfigPva0rdb = 0
McSidStreamidSecurityConfigPva0rdb = 0
McSidStreamidOverrideConfigPva0rdc = 0
McSidStreamidSecurityConfigPva0rdc = 0
McSidStreamidOverrideConfigPva0wra = 0
McSidStreamidSecurityConfigPva0wra = 0
McSidStreamidOverrideConfigPva0wrb = 0
McSidStreamidSecurityConfigPva0wrb = 0
McSidStreamidOverrideConfigPva0wrc = 0
McSidStreamidSecurityConfigPva0wrc = 0
McSidStreamidOverrideConfigPva1rda = 0
McSidStreamidSecurityConfigPva1rda = 0
McSidStreamidOverrideConfigPva1rdb = 0
McSidStreamidSecurityConfigPva1rdb = 0
McSidStreamidOverrideConfigPva1rdc = 0
McSidStreamidSecurityConfigPva1rdc = 0
McSidStreamidOverrideConfigPva1wra = 0
McSidStreamidSecurityConfigPva1wra = 0
McSidStreamidOverrideConfigPva1wrb = 0
McSidStreamidSecurityConfigPva1wrb = 0
McSidStreamidOverrideConfigPva1wrc = 0
McSidStreamidSecurityConfigPva1wrc = 0
McSidStreamidOverrideConfigRcer = 0
McSidStreamidSecurityConfigRcer = 0
McSidStreamidOverrideConfigRcew = 0
McSidStreamidSecurityConfigRcew = 0
McSidStreamidOverrideConfigRcedmar = 0
McSidStreamidSecurityConfigRcedmar = 0
McSidStreamidOverrideConfigRcedmaw = 0
McSidStreamidSecurityConfigRcedmaw = 0
McSidStreamidOverrideConfigNvenc1srd = 0
McSidStreamidSecurityConfigNvenc1srd = 0
McSidStreamidOverrideConfigNvenc1swr = 0
McSidStreamidSecurityConfigNvenc1swr = 0
McSidStreamidOverrideConfigPcie0r = 0
McSidStreamidSecurityConfigPcie0r = 0
McSidStreamidOverrideConfigPcie0w = 0
McSidStreamidSecurityConfigPcie0w = 0
McSidStreamidOverrideConfigPcie1r = 0
McSidStreamidSecurityConfigPcie1r = 0
McSidStreamidOverrideConfigPcie1w = 0
McSidStreamidSecurityConfigPcie1w = 0
McSidStreamidOverrideConfigPcie2ar = 0
McSidStreamidSecurityConfigPcie2ar = 0
McSidStreamidOverrideConfigPcie2aw = 0
McSidStreamidSecurityConfigPcie2aw = 0
McSidStreamidOverrideConfigPcie3r = 0
McSidStreamidSecurityConfigPcie3r = 0
McSidStreamidOverrideConfigPcie3w = 0
McSidStreamidSecurityConfigPcie3w = 0
McSidStreamidOverrideConfigPcie4r = 0
McSidStreamidSecurityConfigPcie4r = 0
McSidStreamidOverrideConfigPcie4w = 0
McSidStreamidSecurityConfigPcie4w = 0
McSidStreamidOverrideConfigPcie5r = 0
McSidStreamidSecurityConfigPcie5r = 0
McSidStreamidOverrideConfigPcie5w = 0
McSidStreamidSecurityConfigPcie5w = 0
McSidStreamidOverrideConfigIspfalw = 0
McSidStreamidSecurityConfigIspfalw = 0
McSidStreamidOverrideConfigDla0rda1 = 0
McSidStreamidSecurityConfigDla0rda1 = 0
McSidStreamidOverrideConfigDla1rda1 = 0
McSidStreamidSecurityConfigDla1rda1 = 0
McSidStreamidOverrideConfigPva0rda1 = 0
McSidStreamidSecurityConfigPva0rda1 = 0
McSidStreamidOverrideConfigPva0rdb1 = 0
McSidStreamidSecurityConfigPva0rdb1 = 0
McSidStreamidOverrideConfigPva1rda1 = 0
McSidStreamidSecurityConfigPva1rda1 = 0
McSidStreamidOverrideConfigPva1rdb1 = 0
McSidStreamidSecurityConfigPva1rdb1 = 0
McSidStreamidOverrideConfigPcie5r1 = 0
McSidStreamidSecurityConfigPcie5r1 = 0
McSidStreamidOverrideConfigNvencsrd1 = 0
McSidStreamidSecurityConfigNvencsrd1 = 0
McSidStreamidOverrideConfigNvenc1srd1 = 0
McSidStreamidSecurityConfigNvenc1srd1 = 0
McSidStreamidOverrideConfigIspra1 = 0
McSidStreamidSecurityConfigIspra1 = 0
McSidStreamidOverrideConfigPcie0r1 = 0
McSidStreamidSecurityConfigPcie0r1 = 0
McSidStreamidOverrideConfigNvdec1srd = 0
McSidStreamidSecurityConfigNvdec1srd = 0
McSidStreamidOverrideConfigNvdec1srd1 = 0
McSidStreamidSecurityConfigNvdec1srd1 = 0
McSidStreamidOverrideConfigNvdec1swr = 0
McSidStreamidSecurityConfigNvdec1swr = 0
McSidStreamidOverrideConfigMiu0r = 0
McSidStreamidSecurityConfigMiu0r = 0
McSidStreamidOverrideConfigMiu0w = 0
McSidStreamidSecurityConfigMiu0w = 0
McSidStreamidOverrideConfigMiu1r = 0
McSidStreamidSecurityConfigMiu1r = 0
McSidStreamidOverrideConfigMiu1w = 0
McSidStreamidSecurityConfigMiu1w = 0
McSidStreamidOverrideConfigMiu2r = 0
McSidStreamidSecurityConfigMiu2r = 0
McSidStreamidOverrideConfigMiu2w = 0
McSidStreamidSecurityConfigMiu2w = 0
McSidStreamidOverrideConfigMiu3r = 0
McSidStreamidSecurityConfigMiu3r = 0
McSidStreamidOverrideConfigMiu3w = 0
McSidStreamidSecurityConfigMiu3w = 0
McSidStreamidOverrideConfigMiu4r = 0
McSidStreamidSecurityConfigMiu4r = 0
McSidStreamidOverrideConfigMiu4w = 0
McSidStreamidSecurityConfigMiu4w = 0
McSidStreamidOverrideConfigMiu5r = 0
McSidStreamidSecurityConfigMiu5r = 0
McSidStreamidOverrideConfigMiu5w = 0
McSidStreamidSecurityConfigMiu5w = 0
McSidStreamidOverrideConfigMiu6r = 0
McSidStreamidSecurityConfigMiu6r = 0
McSidStreamidOverrideConfigMiu6w = 0
McSidStreamidSecurityConfigMiu6w = 0
McSidStreamidOverrideConfigMiu7r = 0
McSidStreamidSecurityConfigMiu7r = 0
McSidStreamidOverrideConfigMiu7w = 0
McSidStreamidSecurityConfigMiu7w = 0
END1
$FS_bct_table = <<'END2';
 'PllMInputDivider': 'CLK_RST_CONTROLLER_PLLM_BASE_0', 'Specifies the M value for PllM, PLLM_BASE' 
 'PllMFeedbackDivider': 'CLK_RST_CONTROLLER_PLLM_BASE_0', 'Specifies the N value for PllM, PLLM_BASE' 
 'PllMStableTime': 'NA', 'Specifies the time to wait for PLLM to lock (in microseconds)' 
 'PllMSetupControl': 'CLK_RST_CONTROLLER_PLLM_MISC1_0', 'Specifies misc. control bits, PLLM_MISC1' 
 'PllMPostDivider': 'CLK_RST_CONTROLLER_PLLM_BASE_0', 'Specifies the P value for PLLM, PLLM_BASE' 
 'PllMKCP': 'CLK_RST_CONTROLLER_PLLM_MISC2_0', 'Specifies value for Charge Pump Gain Control, PLLM_MISC2' 
 'PllMKVCO': 'CLK_RST_CONTROLLER_PLLM_MISC2_0', 'Specirfic VCO gain, PLLM_MISC2' 
 'InitExtraForFpga': 'NA', 'Specifies extra init sequence specifically for FPGA' 
 'EmcBctSpare0': 'NA', 'Spare BCT param' 
 'EmcBctSpare1': 'NA', 'Spare BCT param' 
 'EmcBctSpare2': 'NA', 'Spare BCT param' 
 'EmcBctSpare3': 'NA', 'Spare BCT param' 
 'EmcBctSpare4': 'NA', 'Spare BCT param' 
 'EmcBctSpare5': 'NA', 'Spare BCT param' 
 'EmcBctSpare6': 'NA', 'Spare BCT param' 
 'EmcBctSpare7': 'NA', 'Spare BCT param' 
 'EmcBctSpare8': 'NA', 'Spare BCT param' 
 'EmcBctSpare9': 'NA', 'Spare BCT param' 
 'EmcBctSpare10': 'NA', 'Spare BCT param' 
 'EmcBctSpare11': 'NA', 'Spare BCT param' 
 'EmcBctSpare12': 'NA', 'Spare BCT param' 
 'EmcBctSpare13': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure0': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure1': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure2': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure3': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure4': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure5': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure6': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure7': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure8': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure9': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure10': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure11': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure12': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure13': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure14': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure15': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure16': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure17': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure18': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure19': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure20': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure21': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure22': 'NA', 'Spare BCT param' 
 'EmcBctSpareSecure23': 'NA', 'Spare BCT param' 
 'EmcClockSource': 'NA', 'Defines EMC_2X_CLK_SRC, EMC_2X_CLK_DIVISOR, EMC_INVERT_DCD' 
 'EmcClockSourceDll': 'NA', 'Defines EMC_2X_CLK_SRC, EMC_2X_CLK_DIVISOR, EMC_INVERT_DCD' 
 'ClkRstControllerPllmMisc2Override': 'NA', 'Defines possible override for PLLLM_MISC2' 
 'ClkRstControllerPllmMisc2OverrideEnable': 'NA', 'enables override for PLLLM_MISC2' 
 'ClkRstClkEnbClrEmc': 'CLK_RST_CONTROLLER_CLK_OUT_ENB_EMC_CLR_0', 'Disables MSS clocks = register CLK_RST_CONTROLLER_CLK_OUT_ENB_EMC_CLR' 
 'ClkRstClkEnbClrEmcSb': 'CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSB_CLR_0', 'Disables MSS clocks = register CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSB_CLR' 
 'ClkRstClkEnbClrEmcSc': 'CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSC_CLR_0', 'Disables MSS clocks = register CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSB_CLR' 
 'ClkRstClkEnbClrEmcSd': 'CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSD_CLR_0', 'Disables MSS clocks = register CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCSB_CLR' 
 'ClkRstClkEnbClrEmchub': 'CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCHUB_CLR_0', 'Disables MSS clocks = register CLK_RST_CONTROLLER_CLK_OUT_ENB_EMCHUB_CLR' 
 'ClkRstEmcMisc': 'CLK_RST_CONTROLLER_EMC_MISC_0', 'Hub clock settings register CLK_RST_CONTROLLER_EMC_MISC' 
 'EmcAutoCalInterval': 'EMC_AUTO_CAL_INTERVAL_0', '@Auto-calibration of EMC pads@@Specifies the value for EMC_AUTO_CAL_INTERVAL' 
 'EmcAutoCalConfig': 'EMC_AUTO_CAL_CONFIG_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG@Note: Trigger bits are set by the SDRAM code.' 
 'EmcAutoCalConfig2': 'EMC_AUTO_CAL_CONFIG2_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG2' 
 'EmcAutoCalConfig3': 'EMC_AUTO_CAL_CONFIG3_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG3' 
 'EmcAutoCalConfig4': 'EMC_AUTO_CAL_CONFIG4_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG4' 
 'EmcAutoCalConfig5': 'EMC_AUTO_CAL_CONFIG5_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG5' 
 'EmcAutoCalConfig6': 'EMC_AUTO_CAL_CONFIG6_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG6' 
 'EmcAutoCalConfig7': 'EMC_AUTO_CAL_CONFIG7_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG7' 
 'EmcAutoCalConfig8': 'EMC_AUTO_CAL_CONFIG8_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG8' 
 'EmcAutoCalConfig9': 'EMC_AUTO_CAL_CONFIG9_0', 'Specifies the value for EMC_AUTO_CAL_CONFIG9' 
 'EmcAutoCalVrefSel0': 'EMC_AUTO_CAL_VREF_SEL_0_0', 'Specifies the value for EMC_AUTO_CAL_VREF_SEL_0' 
 'EmcAutoCalVrefSel1': 'EMC_AUTO_CAL_VREF_SEL_1_0', 'Specifies the value for EMC_AUTO_CAL_VREF_SEL_1' 
 'EmcAutoCalChannel': 'EMC_AUTO_CAL_CHANNEL_0', 'Specifies the value for EMC_AUTO_CAL_CHANNEL' 
 'EmcPmacroAutocalCfg0_0': 'EMC_PMACRO_AUTOCAL_CFG_0_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_0_CH0' 
 'EmcPmacroAutocalCfg2_0': 'EMC_PMACRO_AUTOCAL_CFG_2_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_2_CH0' 
 'EmcPmacroRxTerm': 'EMC_PMACRO_RX_TERM_0', 'Specifies the value for EMC_PMACRO_RX_TERM' 
 'EmcPmacroDqTxDrv': 'EMC_PMACRO_DQ_TX_DRV_0', 'Specifies the value for EMC_PMACRO_DQ_TX_DRV' 
 'EmcPmacroCaTxDrv': 'EMC_PMACRO_CA_TX_DRV_0', 'Specifies the value for EMC_PMACRO_CA_TX_DRV' 
 'EmcPmacroCmdTxDrv': 'EMC_PMACRO_CMD_TX_DRV_0', 'Specifies the value for EMC_PMACRO_CMD_TX_DRV' 
 'EmcPmacroAutocalCfgCommon': 'EMC_PMACRO_AUTOCAL_CFG_COMMON_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_COMMON' 
 'EmcPmacroZctrl': 'EMC_PMACRO_ZCTRL_0', 'Specifies the value for EMC_PMACRO_ZCTRL' 
 'EmcAutoCalWait': 'NA', 'Specifies the time for the calibration to stabilize (in microseconds)' 
 'EmcXm2CompPadCtrl': 'EMC_XM2COMPPADCTRL_0', 'Specifies the value for EMC_XM2COMPPADCTRL' 
 'EmcXm2CompPadCtrl2': 'EMC_XM2COMPPADCTRL2_0', 'Specifies the value for EMC_XM2COMPPADCTRL2' 
 'EmcXm2CompPadCtrl3': 'EMC_XM2COMPPADCTRL3_0', 'Specifies the value for EMC_XM2COMPPADCTRL3' 
 'EmcAdrCfg': 'EMC_ADR_CFG_0', '@DRAM size information@Specifies the value for EMC_ADR_CFG' 
 'EmcPinProgramWait': 'NA', '@Specifies the time to wait after asserting pin CKE (in microseconds)' 
 'EmcPinExtraWait': 'NA', 'Specifies the extra delay before/after pin RESET/CKE command' 
 'EmcPinGpioEn': 'NA', 'Specifies the value for GPIO_EN in EMC_PIN' 
 'EmcPinGpioRamdump': 'NA', 'Specifies the value for GPIO in  EMC_PIN during ramdump lp4 case' 
 'EmcPinGpio': 'NA', 'Specifies the value for GPIO in  EMC_PIN' 
 'EmcTimingControlWait': 'NA', 'Specifies the extra delay after the first writing of EMC_TIMING_CONTROL' 
 'EmcRc': 'EMC_RC_0', '@Timing parameters required for the SDRAM@@Specifies the value for EMC_RC' 
 'EmcRfc': 'EMC_RFC_0', 'Specifies the value for EMC_RFC' 
 'EmcRfcPb': 'EMC_RFCPB_0', 'Specifies the value for EMC_RFCPB' 
 'EmcPbr2Pbr': 'EMC_PBR2PBR_0', 'Specifies the value for EMC_PBR2PBR' 
 'EmcRefctrl2': 'EMC_REFCTRL2_0', 'Specifies the value for EMC_REFCTRL2' 
 'EmcRfcSlr': 'EMC_RFC_SLR_0', 'Specifies the value for EMC_RFC_SLR' 
 'EmcRas': 'EMC_RAS_0', 'Specifies the value for EMC_RAS' 
 'EmcRp': 'EMC_RP_0', 'Specifies the value for EMC_RP' 
 'EmcR2r': 'EMC_R2R_0', 'Specifies the value for EMC_R2R' 
 'EmcW2w': 'EMC_W2W_0', 'Specifies the value for EMC_W2W' 
 'EmcTr2nontr': 'NA', 'Specifies the value for EMC_TR2NONTR' 
 'EmcNonrw2trrw': 'NA', 'Specifies the value for EMC_NONRW2TRRW' 
 'EmcR2w': 'EMC_R2W_0', 'Specifies the value for EMC_R2W' 
 'EmcW2r': 'EMC_W2R_0', 'Specifies the value for EMC_W2R' 
 'EmcR2p': 'EMC_R2P_0', 'Specifies the value for EMC_R2P' 
 'EmcW2p': 'EMC_W2P_0', 'Specifies the value for EMC_W2P' 
 'EmcTppd': 'EMC_TPPD_0', 'Specifies the value for EMC_TPPD' 
 'EmcTrtm': 'EMC_TRTM_0', 'Specifies the value for EMC_TRTM' 
 'EmcTwtm': 'EMC_TWTM_0', 'Specifies the value for EMC_TWTM' 
 'EmcTratm': 'EMC_TRATM_0', 'Specifies the value for EMC_TRATM' 
 'EmcTwatm': 'EMC_TWATM_0', 'Specifies the value for EMC_TWATM' 
 'EmcTr2ref': 'EMC_TR2REF_0', 'Specifies the value for EMC_TR2REF' 
 'EmcCcdmw': 'EMC_CCDMW_0', 'Specifies the value for EMC_CCDMW' 
 'EmcRdRcd': 'EMC_RD_RCD_0', 'Specifies the value for EMC_RD_RCD' 
 'EmcWrRcd': 'EMC_WR_RCD_0', 'Specifies the value for EMC_WR_RCD' 
 'EmcRrd': 'EMC_RRD_0', 'Specifies the value for EMC_RRD' 
 'EmcRext': 'EMC_REXT_0', 'Specifies the value for EMC_REXT' 
 'EmcWext': 'EMC_WEXT_0', 'Specifies the value for EMC_WEXT' 
 'EmcWdv': 'EMC_WDV_0', 'Specifies the value for EMC_WDV' 
 'EmcWdvChk': 'EMC_WDV_CHK_0', 'Specifies the value for EMC_WDV_CHK' 
 'EmcWsv': 'EMC_WSV_0', 'Specifies the value for EMC_WSV' 
 'EmcWev': 'EMC_WEV_0', 'Specifies the value for EMC_WSV' 
 'EmcWdvMask': 'EMC_WDV_MASK_0', 'Specifies the value for EMC_WDV_MASK' 
 'EmcWsDuration': 'EMC_WS_DURATION_0', 'Specifies the value for EMC_WS_DURATION' 
 'EmcWeDuration': 'EMC_WE_DURATION_0', 'Specifies the value for EMC_WS_DURATION' 
 'EmcQUse': 'EMC_QUSE_0', 'Specifies the value for EMC_QUSE' 
 'EmcQuseWidth': 'EMC_QUSE_WIDTH_0', 'Specifies the value for EMC_QUSE_WIDTH' 
 'EmcIbdly': 'EMC_IBDLY_0', 'Specifies the value for EMC_IBDLY' 
 'EmcObdly': 'EMC_OBDLY_0', 'Specifies the value for EMC_OBDLY' 
 'EmcEInput': 'EMC_EINPUT_0', 'Specifies the value for EMC_EINPUT' 
 'EmcEInputDuration': 'EMC_EINPUT_DURATION_0', 'Specifies the value for EMC_EINPUT_DURATION' 
 'EmcPutermExtra': 'EMC_PUTERM_EXTRA_0', 'Specifies the value for EMC_PUTERM_EXTRA' 
 'EmcPutermWidth': 'EMC_PUTERM_WIDTH_0', 'Specifies the value for EMC_PUTERM_WIDTH' 
 'EmcQRst': 'EMC_QRST_0', 'Specifies the value for EMC_QRST' 
 'EmcQSafe': 'EMC_QSAFE_0', 'Specifies the value for EMC_QSAFE' 
 'EmcRdv': 'EMC_RDV_0', 'Specifies the value for EMC_RDV' 
 'EmcRdvMask': 'EMC_RDV_MASK_0', 'Specifies the value for EMC_RDV_MASK' 
 'EmcRdvEarly': 'EMC_RDV_EARLY_0', 'Specifies the value for EMC_RDV_EARLY' 
 'EmcRdvEarlyMask': 'EMC_RDV_EARLY_MASK_0', 'Specifies the value for EMC_RDV_EARLY_MASK' 
 'EmcQpop': 'EMC_QPOP_0', 'Specifies the value for EMC_QPOP' 
 'EmcRefresh': 'EMC_REFRESH_0', 'Specifies the value for EMC_REFRESH' 
 'EmcBurstRefreshNum': 'EMC_BURST_REFRESH_NUM_0', 'Specifies the value for EMC_BURST_REFRESH_NUM' 
 'EmcPreRefreshReqCnt': 'EMC_PRE_REFRESH_REQ_CNT_0', 'Specifies the value for EMC_PRE_REFRESH_REQ_CNT' 
 'EmcPdEx2Wr': 'EMC_PDEX2WR_0', 'Specifies the value for EMC_PDEX2WR' 
 'EmcPdEx2Rd': 'EMC_PDEX2RD_0', 'Specifies the value for EMC_PDEX2RD' 
 'EmcPChg2Pden': 'EMC_PCHG2PDEN_0', 'Specifies the value for EMC_PCHG2PDEN' 
 'EmcAct2Pden': 'EMC_ACT2PDEN_0', 'Specifies the value for EMC_ACT2PDEN' 
 'EmcAr2Pden': 'EMC_AR2PDEN_0', 'Specifies the value for EMC_AR2PDEN' 
 'EmcRw2Pden': 'EMC_RW2PDEN_0', 'Specifies the value for EMC_RW2PDEN' 
 'EmcCke2Pden': 'EMC_CKE2PDEN_0', 'Specifies the value for EMC_CKE2PDEN' 
 'EmcPdex2Cke': 'EMC_PDEX2CKE_0', 'Specifies the value for EMC_PDEX2CKE' 
 'EmcPdex2Mrr': 'EMC_PDEX2MRR_0', 'Specifies the value for EMC_PDEX2MRR' 
 'EmcTxsr': 'EMC_TXSR_0', 'Specifies the value for EMC_TXSR' 
 'EmcTxsrDll': 'EMC_TXSRDLL_0', 'Specifies the value for EMC_TXSRDLL' 
 'EmcTcke': 'EMC_TCKE_0', 'Specifies the value for EMC_TCKE' 
 'EmcTckesr': 'EMC_TCKESR_0', 'Specifies the value for EMC_TCKESR' 
 'EmcTpd': 'EMC_TPD_0', 'Specifies the value for EMC_TPD' 
 'EmcTfaw': 'EMC_TFAW_0', 'Specifies the value for EMC_TFAW' 
 'EmcTrpab': 'EMC_TRPAB_0', 'Specifies the value for EMC_TRPAB' 
 'EmcTClkStable': 'EMC_TCLKSTABLE_0', 'Specifies the value for EMC_TCLKSTABLE' 
 'EmcTClkStop': 'EMC_TCLKSTOP_0', 'Specifies the value for EMC_TCLKSTOP' 
 'EmcTRefBw': 'EMC_TREFBW_0', 'Specifies the value for EMC_TREFBW' 
 'EmcFbioCfg5': 'EMC_FBIO_CFG5_0', '@FBIO configuration values@@Specifies the value for EMC_FBIO_CFG5' 
 'EmcFbioCfg7': 'EMC_FBIO_CFG7_0', 'Specifies the value for EMC_FBIO_CFG7' 
 'EmcFbioCfg8': 'EMC_FBIO_CFG8_0', 'Specify the value for EMC_FBIO_CFG9' 
 'EmcFbioCfg9': 'EMC_FBIO_CFG9_0', 'Specify the value for EMC_FBIO_CFG9' 
 'EmcCmdMappingCmd0_0_0': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_0_2': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_0': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_2': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_0': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_2': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd1_0_0': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_0_2': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_0': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_2': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_0': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_2': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingByte_0': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcCmdMappingByte_2': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcFbioSpare': 'EMC_FBIO_SPARE_0', 'Specifies the value for EMC_FBIO_SPARE' 
 'EmcCfgRsv': 'EMC_CFG_RSV_0', '@Specifies the value for EMC_CFG_RSV' 
 'EmcMrs': 'EMC_MRS_0', '@MRS command values@@Specifies the value for EMC_MRS' 
 'EmcEmrs': 'EMC_EMRS_0', 'Specifies the MP0 command to initialize mode registers' 
 'EmcEmrs2': 'EMC_EMRS2_0', 'Specifies the MR2 command to initialize mode registers' 
 'EmcEmrs3': 'EMC_EMRS3_0', 'Specifies the MR3 command to initialize mode registers' 
 'EmcMrw1': 'EMC_MRW_0', 'Specifies the programming to LPDDR2 Mode Register 1 at cold boot' 
 'EmcMrw2': 'EMC_MRW2_0', 'Specifies the programming to LPDDR2 Mode Register 2 at cold boot' 
 'EmcMrw3': 'EMC_MRW3_0', 'Specifies the programming to LPDDR2/4 Mode Register 3/13 at cold boot' 
 'EmcMrw4': 'EMC_MRW4_0', 'Specifies the programming to LPDDR2 Mode Register 11 at cold boot' 
 'EmcMrw6': 'EMC_MRW6_0', 'Specifies the programming to LPDDR4 Mode Register 3 at cold boot' 
 'EmcMrw8': 'EMC_MRW8_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot' 
 'EmcMrw9_0': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH0' 
 'EmcMrw9_2': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcMrw10': 'EMC_MRW10_0', 'Specifies the programming to LPDDR4 Mode Register 12 at cold boot' 
 'EmcMrw12': 'EMC_MRW12_0', 'Specifies the programming to LPDDR4 Mode Register 14 at cold boot' 
 'EmcMrw13': 'EMC_MRW13_0', 'Specifies the programming to LPDDR4 Mode Register 14 at cold boot' 
 'EmcMrw14': 'EMC_MRW14_0', 'Specifies the programming to LPDDR4 Mode Register 22 at cold boot' 
 'EmcMrwExtra': 'NA', 'Specifies the programming to extra LPDDR2 Mode Register at cold boot' 
 'EmcWarmBootMrwExtra': 'NA', 'Specifies the programming to extra LPDDR2 Mode Register at warm boot' 
 'EmcWarmBootExtraModeRegWriteEnable': 'NA', 'Specify the enable of extra Mode Register programming at warm boot' 
 'EmcExtraModeRegWriteEnable': 'NA', 'Specify the enable of extra Mode Register programming at cold boot' 
 'EmcMrwResetCommand': 'NA', '@Specifies the EMC_MRW reset command value' 
 'EmcMrwResetNInitWait': 'NA', 'Specifies the EMC Reset wait time (in microseconds)' 
 'EmcMrsWaitCnt': 'EMC_MRS_WAIT_CNT_0', 'Specifies the value for EMC_MRS_WAIT_CNT' 
 'EmcMrsWaitCnt2': 'EMC_MRS_WAIT_CNT2_0', 'Specifies the value for EMC_MRS_WAIT_CNT2' 
 'EmcCfg': 'EMC_CFG_0', '@EMC miscellaneous configurations@@Specifies the value for EMC_CFG' 
 'EmcResetPadCtrl': 'NA', 'Specifies value for EMC_RESET_PAD_CTRL' 
 'EmcCfg2': 'EMC_CFG_2_0', 'Specifies the value for EMC_CFG_2' 
 'EmcCfgPipeClk': 'EMC_CFG_PIPE_CLK_0', 'Specifies the Clock Enable Override for Pipe/Barrelshifters' 
 'EmcFdpdCtrlCmdNoRamp': 'EMC_FDPD_CTRL_CMD_NO_RAMP_0', 'Specifies the value for EMC_FDPD_CTRL_CMD_NO_RAMP' 
 'EmcCfgUpdate': 'EMC_CFG_UPDATE_0', 'specify the value for EMC_CFG_UPDATE' 
 'EmcDbg': 'EMC_DBG_0', 'Specifies the value for EMC_DBG' 
 'EmcDbgWriteMux': 'NA', 'Specifies the value for EMC_DBG at initialization' 
 'EmcCmdQ': 'EMC_CMDQ_0', 'Specifies the value for EMC_CMDQ' 
 'EmcMc2EmcQ': 'EMC_MC2EMCQ_0', 'Specifies the value for EMC_MC2EMCQ' 
 'EmcDynSelfRefControl': 'EMC_DYN_SELF_REF_CONTROL_0', 'Specifies the value for EMC_DYN_SELF_REF_CONTROL' 
 'EmcAsrControl': 'EMC_ASR_CONTROL_0', 'Specifies the value for EMC_ASR_CONTROL' 
 'EmcCfgDigDll': 'EMC_CFG_DIG_DLL_0', '@Specifies the value for EMC_CFG_DIG_DLL' 
 'EmcCfgDigDll_1': 'EMC_CFG_DIG_DLL_1_0', '@Specifies the value for EMC_CFG_DIG_DLL_1' 
 'EmcCfgDigDllPeriod': 'EMC_CFG_DIG_DLL_PERIOD_0', 'Specifies the value for EMC_CFG_DIG_DLL_PERIOD' 
 'EmcDevSelect': 'NA', 'Specifies the vlaue of *DEV_SELECTN of various EMC registers' 
 'EmcSelDpdCtrl': 'EMC_SEL_DPD_CTRL_0', '@Specifies the value for EMC_SEL_DPD_CTRL' 
 'EmcFdpdCtrlDq': 'EMC_FDPD_CTRL_DQ_0', 'Specifies the value for fdpd ctrl delays on dq' 
 'EmcFdpdCtrlCmd': 'EMC_FDPD_CTRL_CMD_0', 'Specifies the value for fdpd ctrl delays on cmd' 
 'EmcPmacroIbVrefDq_0': 'EMC_PMACRO_IB_VREF_DQ_0_0', 'Specifies the value for EMC_PMACRO_IB_VREF_DQ_0' 
 'EmcPmacroIbVrefDq_1': 'EMC_PMACRO_IB_VREF_DQ_1_0', 'Specifies the value for EMC_PMACRO_IB_VREF_DQ_1' 
 'EmcPmacroIbVrefDqs_0': 'EMC_PMACRO_IB_VREF_DQS_0_0', 'Specifies the value for EMC_PMACRO_IB_VREF_DQ_0' 
 'EmcPmacroIbVrefDqs_1': 'EMC_PMACRO_IB_VREF_DQS_1_0', 'Specifies the value for EMC_PMACRO_IB_VREF_DQ_1' 
 'EmcPmacroIbRxrt': 'EMC_PMACRO_IB_RXRT_0', 'Specifies the value for EMC_PMACRO_IB_RXRT' 
 'EmcPmacroObDdllLongDqRank0_0_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH0' 
 'EmcPmacroObDdllLongDqRank0_0_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH0' 
 'EmcPmacroObDdllLongDqRank0_1_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH0' 
 'EmcPmacroObDdllLongDqRank0_4_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH0' 
 'EmcPmacroObDdllLongDqRank0_5_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH0' 
 'EmcPmacroObDdllLongDqRank1_0_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH0' 
 'EmcPmacroObDdllLongDqRank1_1_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH0' 
 'EmcPmacroObDdllLongDqRank1_4_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_0': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_CH0' 
 'EmcPmacroObDdllLongDqRank1_5_2': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH0' 
 'EmcPmacroObDdllLongDqsRank0_0_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH0' 
 'EmcPmacroObDdllLongDqsRank0_1_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH0' 
 'EmcPmacroObDdllLongDqsRank0_4_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH0' 
 'EmcPmacroObDdllLongDqsRank1_0_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH0' 
 'EmcPmacroObDdllLongDqsRank1_1_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_0': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH0' 
 'EmcPmacroObDdllLongDqsRank1_4_2': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_0': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH0' 
 'EmcPmacroIbDdllLongDqsRank0_0_2': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_0': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH0' 
 'EmcPmacroIbDdllLongDqsRank0_1_2': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_0': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH0' 
 'EmcPmacroIbDdllLongDqsRank1_0_2': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_0': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH0' 
 'EmcPmacroIbDdllLongDqsRank1_1_2': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroDdllLongCmd_0_0': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH0' 
 'EmcPmacroDdllLongCmd_0_2': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_1_0': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH0' 
 'EmcPmacroDdllLongCmd_1_2': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_2_0': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH0' 
 'EmcPmacroDdllLongCmd_2_2': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_3_0': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH0' 
 'EmcPmacroDdllLongCmd_3_2': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_4_0': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH0' 
 'EmcPmacroDdllLongCmd_4_2': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcPmacroDdllShortCmd_0': 'EMC_PMACRO_DDLL_SHORT_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_SHORT_CMD_0' 
 'EmcPmacroDdllShortCmd_1': 'EMC_PMACRO_DDLL_SHORT_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_SHORT_CMD_1' 
 'EmcPmacroDdllShortCmd_2': 'EMC_PMACRO_DDLL_SHORT_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_SHORT_CMD_2' 
 'EmcPmacroDdllPeriodicOffset': 'EMC_PMACRO_DDLL_PERIODIC_OFFSET_0', 'Specifies the value for EMC_PMACRO_DDLL_PERIODIC_OFFSET' 
 'WarmBootWait': 'NA', '@Specifies the delay after asserting CKE pin during a WarmBoot0@sequence (in microseconds)' 
 'EmcOdtWrite': 'EMC_ODT_WRITE_0', '@Specifies the value for EMC_ODT_WRITE' 
 'EmcZcalInterval': 'EMC_ZCAL_INTERVAL_0', '@Periodic ZQ calibration@@Specifies the value for EMC_ZCAL_INTERVAL@Value 0 disables ZQ calibration' 
 'EmcZcalWaitCnt': 'EMC_ZCAL_WAIT_CNT_0', 'Specifies the value for EMC_ZCAL_WAIT_CNT' 
 'EmcZcalMrwCmd': 'EMC_ZCAL_MRW_CMD_0', 'Specifies the value for EMC_ZCAL_MRW_CMD' 
 'EmcMrsResetDll': 'NA', '@DRAM initialization sequence flow control@@Specifies the MRS command value for resetting DLL' 
 'EmcZcalInitDev0': 'EMC_ZQ_CAL_0', 'Specifies the command for ZQ initialization of device 0' 
 'EmcZcalInitDev1': 'EMC_ZQ_CAL_0', 'Specifies the command for ZQ initialization of device 1' 
 'EmcZcalInitWait': 'NA', 'Specifies the wait time after programming a ZQ initialization command@(in microseconds)' 
 'EmcZcalWarmColdBootEnables': 'NA', 'Specifies the enable for ZQ calibration at cold boot [bit 0] and warm boot [bit 1]' 
 'EmcMrwLpddr2ZcalWarmBoot': 'NA', 'Specifies the MRW command to LPDDR2 for ZQ calibration on warmboot@Is issued to both devices separately' 
 'EmcZqCalDdr3WarmBoot': 'NA', 'Specifies the ZQ command to DDR3 for ZQ calibration on warmboot@Is issued to both devices separately' 
 'EmcZqCalLpDdr4WarmBoot': 'NA', 'Specifies the ZQ command to LPDDR4 for ZQ calibration on warmboot@Is issued to both devices separately' 
 'EmcZcalWarmBootWait': 'NA', 'Specifies the wait time for ZQ calibration on warmboot@(in microseconds)' 
 'EmcMrsWarmBootEnable': 'NA', 'Specifies the enable for DRAM Mode Register programming at warm boot' 
 'EmcMrsResetDllWait': 'NA', 'Specifies the wait time after sending an MRS DLL reset command@in microseconds)' 
 'EmcMrsExtra': 'NA', 'Specifies the extra MRS command to initialize mode registers' 
 'EmcWarmBootMrsExtra': 'NA', 'Specifies the extra MRS command at warm boot' 
 'EmcEmrsDdr2DllEnable': 'NA', 'Specifies the EMRS command to enable the DDR2 DLL' 
 'EmcMrsDdr2DllReset': 'NA', 'Specifies the MRS command to reset the DDR2 DLL' 
 'EmcEmrsDdr2OcdCalib': 'NA', 'Specifies the EMRS command to set OCD calibration' 
 'EmcDdr2Wait': 'NA', 'Specifies the wait between initializing DDR and setting OCD@calibration (in microseconds)' 
 'EmcClkenOverride': 'EMC_CLKEN_OVERRIDE_0', 'Specifies the value for EMC_CLKEN_OVERRIDE' 
 'EmcExtraRefreshNum': 'NA', 'Specifies LOG2 of the extra refresh numbers after booting@Program 0 to disable' 
 'EmcClkenOverrideAllWarmBoot': 'NA', 'Specifies the master override for all EMC clocks' 
 'McClkenA1OverrideAllWarmBoot': 'NA', 'Specifies the master override for MC interface clkens' 
 'McClkenOverrideAllWarmBoot': 'NA', 'Specifies the master override for all MC clocks' 
 'EmcCfgDigDllPeriodWarmBoot': 'NA', 'Specifies digital dll period, choosing between 4 to 64 ms' 
 'MssAonVddpSel': 'MSS_AON_CFG_XM0_PAD_VDDP_SEL_CTRL_0', '@Pad controls@@Specifies the value for PMC_VDDP_SEL' 
 'MssAonVddpSelWait': 'NA', 'Specifies the wait time after programming PMC_VDDP_SEL' 
 'PmcDdrPwr': 'NA', 'No longer used in MSS INIT' 
 'MssAonXm0DpdIo': 'NA', 'Specifies the value for MSS_AON_CFG_XM0_PAD_DPD_IO_CTRL' 
 'MssAonXm1DpdIo': 'NA', 'Specifies the value for MSS_AON_CFG_XM1_PAD_DPD_IO_CTRL' 
 'MssAonXm2DpdIo': 'NA', 'Specifies the value for MSS_AON_CFG_XM2_PAD_DPD_IO_CTRL' 
 'MssAonXm3DpdIo': 'NA', 'Specifies the value for MSS_AON_CFG_XM3_PAD_DPD_IO_CTRL' 
 'MssAonXm0CmdCtrl': 'NA', 'Specifies the value for MSS_AON_CFG_XM0_PAD_CMD_CTRL' 
 'MssAonXm1CmdCtrl': 'NA', 'Specifies the value for MSS_AON_CFG_XM1_PAD_CMD_CTRL' 
 'MssAonXm2CmdCtrl': 'NA', 'Specifies the value for MSS_AON_CFG_XM2_PAD_CMD_CTRL' 
 'MssAonXm3CmdCtrl': 'NA', 'Specifies the value for MSS_AON_CFG_XM3_PAD_CMD_CTRL' 
 'PmcIoDpd3ReqWait': 'NA', 'Specifies the wait time after programming PMC_IO_DPD3_REQ' 
 'PmcIoDpd4ReqWait': 'NA', 'Specifies the wait time after programming PMC_IO_DPD4_REQ' 
 'PmcBlinkTimer': 'NA', 'Not used in MSS INIT' 
 'MssAonNoIoPower': 'MSS_AON_CFG_XM0_PAD_MISC_CTRL_0', 'Specifies the value for MSS_AON_CFG_XM0_PAD_MISC_CTRL' 
 'MssAonHoldLowWait': 'NA', 'Specifies the wait time after programing PMC_DDR_CNTRL' 
 'RamdumpSeq1CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ1_CTRL_REG' 
 'RamdumpSeq1DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ1_DATA_REG' 
 'RamdumpSeq2CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ2_CTRL_REG' 
 'RamdumpSeq2DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ2_DATA_REG' 
 'RamdumpSeq3CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ3_CTRL_REG' 
 'RamdumpSeq3DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ3_DATA_REG' 
 'RamdumpSeq4CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ4_CTRL_REG' 
 'RamdumpSeq4DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ4_DATA_REG' 
 'RamdumpSeq5CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ5_CTRL_REG' 
 'RamdumpSeq5DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ5_DATA_REG' 
 'RamdumpSeq6CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ6_CTRL_REG' 
 'RamdumpSeq6DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ6_DATA_REG' 
 'RamdumpSeq7CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ7_CTRL_REG' 
 'RamdumpSeq7DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ7_DATA_REG' 
 'RamdumpSeq8CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ8_CTRL_REG' 
 'RamdumpSeq8DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ8_DATA_REG' 
 'RamdumpSeq9CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ9_CTRL_REG' 
 'RamdumpSeq9DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ9_DATA_REG' 
 'RamdumpSeq10CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ10_CTRL_REG' 
 'RamdumpSeq10DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ10_DATA_REG' 
 'RamdumpSeq11CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ11_CTRL_REG' 
 'RamdumpSeq11DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ11_DATA_REG' 
 'RamdumpSeq12CtrlReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ12_CTRL_REG' 
 'RamdumpSeq12DataReg': 'NA', 'Specifies the value for MSS_AON_CFG_RAMDUMP_SEQ12_DATA_REG' 
 'EmcAcpdControl': 'EMC_ACPD_CONTROL_0', 'Specifies the value for EMC_ACPD_CONTROL' 
 'EmcSwizzleRank0Byte0_0': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH0' 
 'EmcSwizzleRank0Byte0_2': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte1_0': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH0' 
 'EmcSwizzleRank0Byte1_2': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte2_0': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH0' 
 'EmcSwizzleRank0Byte2_2': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte3_0': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH0' 
 'EmcSwizzleRank0Byte3_2': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcTxdsrvttgen': 'EMC_TXDSRVTTGEN_0', '@Specifies the value for EMC_TXDSRVTTGEN' 
 'EmcDataBrlshft0_0': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH0' 
 'EmcDataBrlshft0_2': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft1_0': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH0' 
 'EmcDataBrlshft1_2': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcDqsBrlshft0': 'EMC_DQS_BRLSHFT_0_0', 'Specifies the value for EMC_DQS_BRLSHFT_0_CH0' 
 'EmcDqsBrlshft1': 'EMC_DQS_BRLSHFT_1_0', 'Specifies the value for EMC_DQS_BRLSHFT_1_CH0' 
 'EmcCmdBrlshft0': 'EMC_CMD_BRLSHFT_0_0', 'Specifies the value for EMC_CMD_BRLSHFT_0_CH0' 
 'EmcCmdBrlshft1': 'EMC_CMD_BRLSHFT_1_0', 'Specifies the value for EMC_CMD_BRLSHFT_1_CH0' 
 'EmcCmdBrlshft2': 'EMC_CMD_BRLSHFT_2_0', 'Specifies the value for EMC_CMD_BRLSHFT_2_CH0' 
 'EmcCmdBrlshft3': 'EMC_CMD_BRLSHFT_3_0', 'Specifies the value for EMC_CMD_BRLSHFT_3_CH0' 
 'EmcQuseBrlshft0': 'EMC_QUSE_BRLSHFT_0_0', 'Specifies the value for EMC_QUSE_BRLSHFT_0' 
 'EmcQuseBrlshft2': 'EMC_QUSE_BRLSHFT_2_0', 'Specifies the value for EMC_QUSE_BRLSHFT_2' 
 'EmcPmacroDllCfg0': 'EMC_PMACRO_DLL_CFG_0_0', 'Specifies the value for EMC_PMACRO_DLL_CFG_0' 
 'EmcPmacroDllCfg1': 'EMC_PMACRO_DLL_CFG_1_0', 'Specifies the value for EMC_PMACRO_DLL_CFG_1' 
 'EmcPmcScratch1_0': 'EMC_PMC_SCRATCH1_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch2_0': 'EMC_PMC_SCRATCH2_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch3_0': 'EMC_PMC_SCRATCH3_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmacroPadCfgCtrl': 'EMC_PMACRO_PAD_CFG_CTRL_0', 'Specifies the value for EMC_PMACRO_PAD_CFG_CTRL' 
 'EmcPmacroVttgenCtrl0': 'EMC_PMACRO_VTTGEN_CTRL_0_0', 'Specifies the value for EMC_PMACRO_VTTGEN_CTRL_0' 
 'EmcPmacroVttgenCtrl1': 'EMC_PMACRO_VTTGEN_CTRL_1_0', 'Specifies the value for EMC_PMACRO_VTTGEN_CTRL_1' 
 'EmcPmacroVttgenCtrl2': 'EMC_PMACRO_VTTGEN_CTRL_2_0', 'Specifies the value for EMC_PMACRO_VTTGEN_CTRL_2' 
 'EmcPmacroDsrVttgenCtrl0': 'EMC_PMACRO_DSR_VTTGEN_CTRL_0_0', 'Specifies the value for EMC_PMACRO_DSR_VTTGEN_CTRL_0' 
 'EmcPmacroBrickCtrlRfu1': 'EMC_PMACRO_BRICK_CTRL_RFU1_0', 'Specifies the value for EMC_PMACRO_BRICK_CTRL' 
 'EmcPmacroCmdBrickCtrlFdpd': 'EMC_PMACRO_CMD_BRICK_CTRL_FDPD_0', 'Specifies the value for EMC_PMACRO_BRICK_CTRL_FDPD' 
 'EmcPmacroBrickCtrlRfu2': 'EMC_PMACRO_BRICK_CTRL_RFU2_0', 'Specifies the value for EMC_PMACRO_BRICK_CTRL' 
 'EmcPmacroDataBrickCtrlFdpd': 'EMC_PMACRO_DATA_BRICK_CTRL_FDPD_0', 'Specifies the value for EMC_PMACRO_BRICK_CTRL_FDPD' 
 'EmcPmacroBgBiasCtrl0': 'EMC_PMACRO_BG_BIAS_CTRL_0_0', 'Specifies the value for EMC_PMACRO_BG_BIAS_CTRL_0' 
 'EmcPmacroDataPadRxCtrl': 'EMC_PMACRO_DATA_PAD_RX_CTRL_0', 'Specifies the value for EMC_PMACRO_DATA_PAD_RX_CTRL' 
 'EmcPmacroCmdPadRxCtrl': 'EMC_PMACRO_CMD_PAD_RX_CTRL_0', 'Specifies the value for EMC_PMACRO_CMD_PAD_RX_CTRL' 
 'EmcPmacroDataRxTermMode': 'EMC_PMACRO_DATA_RX_TERM_MODE_0', 'Specifies the value for EMC_PMACRO_DATA_RX_TERM_MODE' 
 'EmcPmacroCmdRxTermMode': 'EMC_PMACRO_CMD_RX_TERM_MODE_0', 'Specifies the value for EMC_PMACRO_CMD_RX_TERM_MODE' 
 'EmcPmacroDataPadTxCtrl': 'EMC_PMACRO_DATA_PAD_TX_CTRL_0', 'Specifies the value for EMC_PMACRO_DATA_PAD_TX_CTRL' 
 'EmcPmacroCmdPadTxCtrl': 'EMC_PMACRO_CMD_PAD_TX_CTRL_0', 'Specifies the value for EMC_PMACRO_CMD_PAD_TX_CTRL' 
 'EmcCfg3': 'EMC_CFG_3_0', 'Specifies the value for EMC_CFG_3' 
 'EmcConfigSampleDelay': 'EMC_CONFIG_SAMPLE_DELAY_0', 'Specifies the value for EMC_CONFIG_SAMPLE_DELAY' 
 'EmcPmacroBrickMapping0_0': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH0' 
 'EmcPmacroBrickMapping0_2': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping1_0': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH0' 
 'EmcPmacroBrickMapping1_2': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroPerbitFgcgCtrl0': 'EMC_PMACRO_PERBIT_FGCG_CTRL_0_0', 'Specifies the value for EMC_PMACRO_PERBIT_FGCG_CTRL_0' 
 'EmcPmacroPerbitFgcgCtrl1': 'EMC_PMACRO_PERBIT_FGCG_CTRL_1_0', 'Specifies the value for EMC_PMACRO_PERBIT_FGCG_CTRL_1' 
 'EmcPmacroPerbitFgcgCtrl4': 'EMC_PMACRO_PERBIT_FGCG_CTRL_4_0', 'Specifies the value for EMC_PMACRO_PERBIT_FGCG_CTRL_4' 
 'EmcPmacroPerbitRfuCtrl0': 'EMC_PMACRO_PERBIT_RFU_CTRL_0_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU_CTRL_0' 
 'EmcPmacroPerbitRfuCtrl1': 'EMC_PMACRO_PERBIT_RFU_CTRL_1_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU_CTRL_1' 
 'EmcPmacroPerbitRfuCtrl4': 'EMC_PMACRO_PERBIT_RFU_CTRL_4_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU_CTRL_4' 
 'EmcPmacroPerbitRfu1Ctrl0': 'EMC_PMACRO_PERBIT_RFU1_CTRL_0_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU1_CTRL_0' 
 'EmcPmacroPerbitRfu1Ctrl1': 'EMC_PMACRO_PERBIT_RFU1_CTRL_1_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU1_CTRL_1' 
 'EmcPmacroPerbitRfu1Ctrl4': 'EMC_PMACRO_PERBIT_RFU1_CTRL_4_0', 'Specifies the value for EMC_PMACRO_PERBIT_RFU1_CTRL_4' 
 'EmcPmacroDataPiCtrl': 'EMC_PMACRO_DATA_PI_CTRL_0', 'Specifies the value for EMC_PMACRO_DATA_PI_CTRL' 
 'EmcPmacroCmdPiCtrl': 'EMC_PMACRO_CMD_PI_CTRL_0', 'Specifies the value for EMC_PMACRO_CMD_PI_CTRL' 
 'EmcPmacroDdllBypass': 'EMC_PMACRO_DDLL_BYPASS_0', 'Specifies the value for EMC_PMACRO_DDLL_BYPASS' 
 'EmcPmacroDdllPwrd0': 'EMC_PMACRO_DDLL_PWRD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_PWRD_0' 
 'EmcPmacroDdllPwrd2': 'EMC_PMACRO_DDLL_PWRD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_PWRD_2' 
 'EmcPmacroCmdCtrl0': 'EMC_PMACRO_CMD_CTRL_0_0', 'Specifies the value for EMC_PMACRO_CMD_CTRL_0' 
 'EmcPmacroCmdCtrl1': 'EMC_PMACRO_CMD_CTRL_1_0', 'Specifies the value for EMC_PMACRO_CMD_CTRL_1' 
 'EmcPmacroCmdCtrl2': 'EMC_PMACRO_CMD_CTRL_2_0', 'Specifies the value for EMC_PMACRO_CMD_CTRL_2' 
 'McRegifConfig': 'MC_REGIF_CONFIG_0', 'Specifies the value for MC_REGIF_CONFIG' 
 'McRegifConfig1': 'MC_REGIF_CONFIG_1_0', 'Specifies the value for MC_REGIF_CONFIG_1' 
 'McRegifConfig2': 'MC_REGIF_CONFIG_2_0', 'Specifies the value for MC_REGIF_CONFIG_2' 
 'McRegifBroadcast': 'MC_REGIF_BROADCAST_0', 'Specifies the value for MC_REGIF_BROADCAST' 
 'McRegifBroadcast1': 'NA', 'Specifies the value for MC_REGIF_BROADCAST_1' 
 'McRegifBroadcast2': 'NA', 'Specifies the value for MC_REGIF_BROADCAST_2' 
 'McEmemAdrCfg': 'MC_EMEM_ADR_CFG_0', '@DRAM size information@@Specifies the value for MC_EMEM_ADR_CFG' 
 'McEmemAdrCfgDev0': 'MC_EMEM_ADR_CFG_DEV0_0', 'Specifies the value for MC_EMEM_ADR_CFG_DEV0' 
 'McEmemAdrCfgDev1': 'MC_EMEM_ADR_CFG_DEV1_0', 'Specifies the value for MC_EMEM_ADR_CFG_DEV1' 
 'McEmemAdrCfgChannelEnable': 'MC_EMEM_ADR_CFG_CHANNEL_ENABLE_0', 'Specifies the value for MC_EMEM_ADR_CFG_CHANNEL_ENABLE' 
 'McEmemAdrCfgChannelMask0': 'MC_EMEM_ADR_CFG_CHANNEL_MASK_0', 'Specifies the value for MC_EMEM_ADR_CFG_CHANNEL_MASK' 
 'McEmemAdrCfgChannelMask1': 'MC_EMEM_ADR_CFG_CHANNEL_MASK_1_0', 'Specifies the value for MC_EMEM_ADR_CFG_CHANNEL_MASK_1' 
 'McEmemAdrCfgChannelMask2': 'MC_EMEM_ADR_CFG_CHANNEL_MASK_2_0', 'Specifies the value for MC_EMEM_ADR_CFG_CHANNEL_MASK_2' 
 'McEmemAdrCfgChannelMask3': 'MC_EMEM_ADR_CFG_CHANNEL_MASK_3_0', 'Specifies the value for MC_EMEM_ADR_CFG_CHANNEL_MASK_3' 
 'McEmemAdrCfgBankMask0': 'MC_EMEM_ADR_CFG_BANK_MASK_0_0', 'Specifies the value for MC_EMEM_ADR_CFG_BANK_MASK_0' 
 'McEmemAdrCfgBankMask1': 'MC_EMEM_ADR_CFG_BANK_MASK_1_0', 'Specifies the value for MC_EMEM_ADR_CFG_BANK_MASK_1' 
 'McEmemAdrCfgBankMask2': 'MC_EMEM_ADR_CFG_BANK_MASK_2_0', 'Specifies the value for MC_EMEM_ADR_CFG_BANK_MASK_2' 
 'McEmemCfg': 'MC_EMEM_CFG_0', '@Specifies the value for MC_EMEM_CFG which holds the external memory@size (in KBytes)' 
 'McCifllMisc0': 'MC_CIFLL_MISC0_0', 'Specifies the value for MC_CIFLL_MISC0' 
 'McCifllWrdatWrlimit': 'MC_CIFLL_WRDAT_MT_FIFO_CREDITS_0', 'Specifies the value for MC_CIFLL_WRDAT_MT_FIFO_CREDITS' 
 'McCifllReqWrlimit': 'MC_CIFLL_REQ_MT_FIFO_CREDITS_0', 'Specifies the value for MC_CIFLL_REQ_MT_FIFO_CREDITS' 
 'McEmemArbCfg': 'MC_EMEM_ARB_CFG_0', '@MC arbitration configuration@@Specifies the value for MC_EMEM_ARB_CFG' 
 'McEmemArbOutstandingReq': 'MC_EMEM_ARB_OUTSTANDING_REQ_0', 'Specifies the value for MC_EMEM_ARB_OUTSTANDING_REQ' 
 'McEmemArbOutstandingReqNiso': 'MC_EMEM_ARB_OUTSTANDING_REQ_NISO_0', 'Specifies the value for MC_EMEM_ARB_OUTSTANDING_REQ_NISO' 
 'McEmemArbRefpbHpCtrl': 'MC_EMEM_ARB_REFPB_HP_CTRL_0', 'Specifies the value for MC_EMEM_ARB_REFPB_HP_CTRL' 
 'McEmemArbRefpbBankCtrl': 'MC_EMEM_ARB_REFPB_BANK_CTRL_0', 'Specifies the value for MC_EMEM_ARB_REFPB_BANK_CTRL' 
 'McEmemArbTimingRcd': 'MC_EMEM_ARB_TIMING_RCD_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RCD' 
 'McEmemArbTimingRp': 'MC_EMEM_ARB_TIMING_RP_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RP' 
 'McEmemArbTimingRc': 'MC_EMEM_ARB_TIMING_RC_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RC' 
 'McEmemArbTimingRas': 'MC_EMEM_ARB_TIMING_RAS_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RAS' 
 'McEmemArbTimingFaw': 'MC_EMEM_ARB_TIMING_FAW_0', 'Specifies the value for MC_EMEM_ARB_TIMING_FAW' 
 'McEmemArbTimingRrd': 'MC_EMEM_ARB_TIMING_RRD_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RRD' 
 'McEmemArbTimingRap2Pre': 'MC_EMEM_ARB_TIMING_RAP2PRE_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RAP2PRE' 
 'McEmemArbTimingWap2Pre': 'MC_EMEM_ARB_TIMING_WAP2PRE_0', 'Specifies the value for MC_EMEM_ARB_TIMING_WAP2PRE' 
 'McEmemArbTimingR2R': 'MC_EMEM_ARB_TIMING_R2R_0', 'Specifies the value for MC_EMEM_ARB_TIMING_R2R' 
 'McEmemArbTimingW2W': 'MC_EMEM_ARB_TIMING_W2W_0', 'Specifies the value for MC_EMEM_ARB_TIMING_W2W' 
 'McEmemArbTimingR2W': 'MC_EMEM_ARB_TIMING_R2W_0', 'Specifies the value for MC_EMEM_ARB_TIMING_R2W' 
 'McEmemArbTimingW2R': 'MC_EMEM_ARB_TIMING_W2R_0', 'Specifies the value for MC_EMEM_ARB_TIMING_W2R' 
 'McEmemArbTimingRFCPB': 'MC_EMEM_ARB_TIMING_RFCPB_0', 'Specifies the value for MC_EMEM_ARB_TIMING_RFCPB' 
 'McEmemArbTimingPBR2PBR': 'MC_EMEM_ARB_TIMING_PBR2PBR_0', 'Specifies the value for MC_EMEM_ARB_TIMING_PBR2PBR' 
 'EmcPeriodicTrCtrl1': 'EMC_PERIODIC_TR_CTRL_1_0', 'Specifies the value for EMC_PERIODIC_TR_CTRL_1' 
 'McEmemArbTimingPDEX': 'MC_EMEM_ARB_TIMING_PDEX_0', 'Specifies the value for MC_EMEM_ARB_TIMING_PDEX' 
 'McEmemArbTimingSREX': 'MC_EMEM_ARB_TIMING_SREX_0', 'Specifies the value for MC_EMEM_ARB_TIMING_SREX' 
 'McEmemArbDaTurns': 'MC_EMEM_ARB_DA_TURNS_0', 'Specifies the value for MC_EMEM_ARB_DA_TURNS' 
 'McEmemArbDaCovers': 'MC_EMEM_ARB_DA_COVERS_0', 'Specifies the value for MC_EMEM_ARB_DA_COVERS' 
 'McEmemArbDaHysteresis': 'MC_EMEM_ARB_DA_HYSTERESIS_0', 'Specifies the value for MC_EMEM_ARB_DA_HYSTERESIS' 
 'McEmemArbMisc0': 'MC_EMEM_ARB_MISC0_0', 'Specifies the value for MC_EMEM_ARB_MISC0' 
 'McEmemArbMisc1': 'MC_EMEM_ARB_MISC1_0', 'Specifies the value for MC_EMEM_ARB_MISC1' 
 'McEmemArbMisc2': 'MC_EMEM_ARB_MISC2_0', 'Specifies the value for MC_EMEM_ARB_MISC2' 
 'McEmemArbMisc3': 'MC_EMEM_ARB_MISC3_0', 'Specifies the value for MC_EMEM_ARB_MISC3' 
 'McEmemArbMisc4': 'MC_EMEM_ARB_MISC4_0', 'Specifies the value for MC_EMEM_ARB_MISC4' 
 'McEmemArbRing1Throttle': 'MC_EMEM_ARB_RING1_THROTTLE_0', 'Specifies the value for MC_EMEM_ARB_RING1_THROTTLE' 
 'McEmemArbNisoThrottle': 'MC_EMEM_ARB_NISO_THROTTLE_0', 'Specifies the value for MC_EMEM_ARB_NISO_THROTTLE' 
 'McEmemArbNisoThrottleMask': 'MC_EMEM_ARB_NISO_THROTTLE_MASK_0', 'Specifies the value for MC_EMEM_ARB_NISO_THROTTLE_MASK' 
 'McEmemArbNisoThrottleMask1': 'MC_EMEM_ARB_NISO_THROTTLE_MASK_1_0', 'Specifies the value for MC_EMEM_ARB_NISO_THROTTLE_MASK_1' 
 'McEmemArbNisoThrottleMask2': 'MC_EMEM_ARB_NISO_THROTTLE_MASK_2_0', 'Specifies the value for MC_EMEM_ARB_NISO_THROTTLE_MASK_2' 
 'McEmemArbNisoThrottleMask3': 'MC_EMEM_ARB_NISO_THROTTLE_MASK_3_0', 'Specifies the value for MC_EMEM_ARB_NISO_THROTTLE_MASK_3' 
 'McEmemArbOverride': 'MC_EMEM_ARB_OVERRIDE_0', 'Specifies the value for MC_EMEM_ARB_OVERRIDE' 
 'McEmemArbOverride1': 'MC_EMEM_ARB_OVERRIDE_1_0', 'Specifies the value for MC_EMEM_ARB_OVERRIDE_1' 
 'McEmemArbRsv': 'MC_EMEM_ARB_RSV_0', 'Specifies the value for MC_EMEM_ARB_RSV' 
 'McDaCfg0': 'MC_DA_CONFIG0_0', 'Specifies the value for MC_DA_CONFIG0' 
 'McEmemArbTimingCcdmw': 'MC_EMEM_ARB_TIMING_CCDMW_0', 'specifies the DRAM CAS to CAS delay timing for masked writes' 
 'McClkenA1Override': 'MC_CLKEN_A1_OVERRIDE_0', '@Specifies the value for MC_A1_CLKEN_OVERRIDE' 
 'McClkenOverride': 'MC_CLKEN_OVERRIDE_0', '@Specifies the value for MC_CLKEN_OVERRIDE' 
 'McHubClkenOverride': 'MC_HUB_CLKEN_OVERRIDE_0', '@Specifies the value for MC_HUB_CLKEN_OVERRIDE' 
 'McStatControl': 'MC_STAT_CONTROL_0', '@Specifies the value for MC_STAT_CONTROL' 
 'McEccCfg': 'MC_ECC_CFG_0', '@Specifies the value for MC_ECC_CFG' 
 'McEccControl': 'MC_ECC_CONTROL_0', '@Specifies the value for MC_ECC_CONTROL' 
 'McCfgWcamGobRemap': 'MC_CFG_WCAM_GOB_REMAP_0', '@Specifies the value for MC_CFG_WCAM_GOB_REMAP' 
 'McEccRawModeControl': 'MC_ECC_RAW_MODE_CONTROL_0', '@Specifies the value for MC_ECC_RAW_MODE_CONTROL' 
 'McCfgWcam': 'MC_CFG_WCAM_0', '@Specifies the value for MC_CFG_WCAM' 
 'McRing0MtFifoCredits': 'MC_RING0_MT_FIFO_CREDITS_0', '@Specifies the value for MC_RING0_MT_FIFO_CREDITS' 
 'McVideoProtectBom': 'MC_VIDEO_PROTECT_BOM_0', 'Specifies the value for MC_VIDEO_PROTECT_BOM' 
 'McVideoProtectBomAdrHi': 'MC_VIDEO_PROTECT_BOM_ADR_HI_0', 'Specifies the value for MC_VIDEO_PROTECT_BOM_ADR_HI' 
 'McVideoProtectSizeMb': 'MC_VIDEO_PROTECT_SIZE_MB_0', 'Specifies the value for MC_VIDEO_PROTECT_SIZE_MB' 
 'McVideoProtectVprOverride': 'MC_VIDEO_PROTECT_VPR_OVERRIDE_0', 'Specifies the value for MC_VIDEO_PROTECT_VPR_OVERRIDE' 
 'McVideoProtectVprOverride1': 'MC_VIDEO_PROTECT_VPR_OVERRIDE1_0', 'Specifies the value for MC_VIDEO_PROTECT_VPR_OVERRIDE1' 
 'McVideoProtectVprOverride2': 'MC_VIDEO_PROTECT_VPR_OVERRIDE2_0', 'Specifies the value for MC_VIDEO_PROTECT_VPR_OVERRIDE2' 
 'McVideoProtectGpuOverride0': 'MC_VIDEO_PROTECT_GPU_OVERRIDE_0_0', 'Specifies the value for MC_VIDEO_PROTECT_GPU_OVERRIDE_0' 
 'McVideoProtectGpuOverride1': 'MC_VIDEO_PROTECT_GPU_OVERRIDE_1_0', 'Specifies the value for MC_VIDEO_PROTECT_GPU_OVERRIDE_1' 
 'McSecCarveoutBom': 'MC_SEC_CARVEOUT_BOM_0', 'Specifies the value for MC_SEC_CARVEOUT_BOM' 
 'McSecCarveoutAdrHi': 'MC_SEC_CARVEOUT_ADR_HI_0', 'Specifies the value for MC_SEC_CARVEOUT_ADR_HI' 
 'McSecCarveoutSizeMb': 'MC_SEC_CARVEOUT_SIZE_MB_0', 'Specifies the value for MC_SEC_CARVEOUT_SIZE_MB' 
 'McVideoProtectWriteAccess': 'MC_VIDEO_PROTECT_REG_CTRL_0', 'Specifies the value for MC_VIDEO_PROTECT_REG_CTRL.VIDEO_PROTECT_WRITE_ACCESS' 
 'McSecCarveoutProtectWriteAccess': 'MC_SEC_CARVEOUT_REG_CTRL_0', 'Specifies the value for MC_SEC_CARVEOUT_REG_CTRL.SEC_CARVEOUT_WRITE_ACCESS' 
 'McGSCInitMask': 'NA', 'Specifies which carveouts to program during MSS init' 
 'McGeneralizedCarveout1Bom': 'MC_SECURITY_CARVEOUT1_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_BOM' 
 'McGeneralizedCarveout1BomHi': 'MC_SECURITY_CARVEOUT1_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_BOM_HI' 
 'McGeneralizedCarveout1Size128kb': 'MC_SECURITY_CARVEOUT1_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_SIZE_128KB' 
 'McGeneralizedCarveout1Access0': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS0' 
 'McGeneralizedCarveout1Access1': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS1' 
 'McGeneralizedCarveout1Access2': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS2' 
 'McGeneralizedCarveout1Access3': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS3' 
 'McGeneralizedCarveout1Access4': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS4' 
 'McGeneralizedCarveout1Access5': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS5' 
 'McGeneralizedCarveout1Access6': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS6' 
 'McGeneralizedCarveout1Access7': 'MC_SECURITY_CARVEOUT1_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_ACCESS7' 
 'McGeneralizedCarveout1ForceInternalAccess0': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout1ForceInternalAccess1': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout1ForceInternalAccess2': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout1ForceInternalAccess3': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout1ForceInternalAccess4': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout1ForceInternalAccess5': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout1ForceInternalAccess6': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout1ForceInternalAccess7': 'MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout1Cfg0': 'MC_SECURITY_CARVEOUT1_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT1_CFG0' 
 'McGeneralizedCarveout2Bom': 'MC_SECURITY_CARVEOUT2_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_BOM' 
 'McGeneralizedCarveout2BomHi': 'MC_SECURITY_CARVEOUT2_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_BOM_HI' 
 'McGeneralizedCarveout2Size128kb': 'MC_SECURITY_CARVEOUT2_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_SIZE_128KB' 
 'McGeneralizedCarveout2Access0': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS0' 
 'McGeneralizedCarveout2Access1': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS1' 
 'McGeneralizedCarveout2Access2': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS2' 
 'McGeneralizedCarveout2Access3': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS3' 
 'McGeneralizedCarveout2Access4': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS4' 
 'McGeneralizedCarveout2Access5': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS5' 
 'McGeneralizedCarveout2Access6': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS6' 
 'McGeneralizedCarveout2Access7': 'MC_SECURITY_CARVEOUT2_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_ACCESS7' 
 'McGeneralizedCarveout2ForceInternalAccess0': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout2ForceInternalAccess1': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout2ForceInternalAccess2': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout2ForceInternalAccess3': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout2ForceInternalAccess4': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout2ForceInternalAccess5': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout2ForceInternalAccess6': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout2ForceInternalAccess7': 'MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout2Cfg0': 'MC_SECURITY_CARVEOUT2_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT2_CFG0' 
 'McGeneralizedCarveout3Bom': 'MC_SECURITY_CARVEOUT3_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_BOM' 
 'McGeneralizedCarveout3BomHi': 'MC_SECURITY_CARVEOUT3_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_BOM_HI' 
 'McGeneralizedCarveout3Size128kb': 'MC_SECURITY_CARVEOUT3_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_SIZE_128KB' 
 'McGeneralizedCarveout3Access0': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS0' 
 'McGeneralizedCarveout3Access1': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS1' 
 'McGeneralizedCarveout3Access2': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS2' 
 'McGeneralizedCarveout3Access3': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS3' 
 'McGeneralizedCarveout3Access4': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS4' 
 'McGeneralizedCarveout3Access5': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS5' 
 'McGeneralizedCarveout3Access6': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS6' 
 'McGeneralizedCarveout3Access7': 'MC_SECURITY_CARVEOUT3_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_ACCESS7' 
 'McGeneralizedCarveout3ForceInternalAccess0': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout3ForceInternalAccess1': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout3ForceInternalAccess2': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout3ForceInternalAccess3': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout3ForceInternalAccess4': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout3ForceInternalAccess5': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout3ForceInternalAccess6': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout3ForceInternalAccess7': 'MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout3Cfg0': 'MC_SECURITY_CARVEOUT3_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT3_CFG0' 
 'McGeneralizedCarveout4Bom': 'MC_SECURITY_CARVEOUT4_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_BOM' 
 'McGeneralizedCarveout4BomHi': 'MC_SECURITY_CARVEOUT4_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_BOM_HI' 
 'McGeneralizedCarveout4Size128kb': 'MC_SECURITY_CARVEOUT4_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_SIZE_128KB' 
 'McGeneralizedCarveout4Access0': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS0' 
 'McGeneralizedCarveout4Access1': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS1' 
 'McGeneralizedCarveout4Access2': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS2' 
 'McGeneralizedCarveout4Access3': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS3' 
 'McGeneralizedCarveout4Access4': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS4' 
 'McGeneralizedCarveout4Access5': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS5' 
 'McGeneralizedCarveout4Access6': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS6' 
 'McGeneralizedCarveout4Access7': 'MC_SECURITY_CARVEOUT4_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_ACCESS7' 
 'McGeneralizedCarveout4ForceInternalAccess0': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout4ForceInternalAccess1': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout4ForceInternalAccess2': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout4ForceInternalAccess3': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout4ForceInternalAccess4': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout4ForceInternalAccess5': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout4ForceInternalAccess6': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout4ForceInternalAccess7': 'MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout4Cfg0': 'MC_SECURITY_CARVEOUT4_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT4_CFG0' 
 'McGeneralizedCarveout5Bom': 'MC_SECURITY_CARVEOUT5_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_BOM' 
 'McGeneralizedCarveout5BomHi': 'MC_SECURITY_CARVEOUT5_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_BOM_HI' 
 'McGeneralizedCarveout5Size128kb': 'MC_SECURITY_CARVEOUT5_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_SIZE_128KB' 
 'McGeneralizedCarveout5Access0': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS0' 
 'McGeneralizedCarveout5Access1': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS1' 
 'McGeneralizedCarveout5Access2': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS2' 
 'McGeneralizedCarveout5Access3': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS3' 
 'McGeneralizedCarveout5Access4': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS4' 
 'McGeneralizedCarveout5Access5': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS5' 
 'McGeneralizedCarveout5Access6': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS6' 
 'McGeneralizedCarveout5Access7': 'MC_SECURITY_CARVEOUT5_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_ACCESS7' 
 'McGeneralizedCarveout5ForceInternalAccess0': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout5ForceInternalAccess1': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout5ForceInternalAccess2': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout5ForceInternalAccess3': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout5ForceInternalAccess4': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout5ForceInternalAccess5': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout5ForceInternalAccess6': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout5ForceInternalAccess7': 'MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout5Cfg0': 'MC_SECURITY_CARVEOUT5_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT5_CFG0' 
 'McGeneralizedCarveout6Bom': 'MC_SECURITY_CARVEOUT6_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_BOM' 
 'McGeneralizedCarveout6BomHi': 'MC_SECURITY_CARVEOUT6_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_BOM_HI' 
 'McGeneralizedCarveout6Size128kb': 'MC_SECURITY_CARVEOUT6_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_SIZE_128KB' 
 'McGeneralizedCarveout6Access0': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS0' 
 'McGeneralizedCarveout6Access1': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS1' 
 'McGeneralizedCarveout6Access2': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS2' 
 'McGeneralizedCarveout6Access3': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS3' 
 'McGeneralizedCarveout6Access4': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS4' 
 'McGeneralizedCarveout6Access5': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS5' 
 'McGeneralizedCarveout6Access6': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS6' 
 'McGeneralizedCarveout6Access7': 'MC_SECURITY_CARVEOUT6_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_ACCESS7' 
 'McGeneralizedCarveout6ForceInternalAccess0': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout6ForceInternalAccess1': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout6ForceInternalAccess2': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout6ForceInternalAccess3': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout6ForceInternalAccess4': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout6ForceInternalAccess5': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout6ForceInternalAccess6': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout6ForceInternalAccess7': 'MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout6Cfg0': 'MC_SECURITY_CARVEOUT6_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT6_CFG0' 
 'McGeneralizedCarveout7Bom': 'MC_SECURITY_CARVEOUT7_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_BOM' 
 'McGeneralizedCarveout7BomHi': 'MC_SECURITY_CARVEOUT7_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_BOM_HI' 
 'McGeneralizedCarveout7Size128kb': 'MC_SECURITY_CARVEOUT7_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_SIZE_128KB' 
 'McGeneralizedCarveout7Access0': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS0' 
 'McGeneralizedCarveout7Access1': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS1' 
 'McGeneralizedCarveout7Access2': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS2' 
 'McGeneralizedCarveout7Access3': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS3' 
 'McGeneralizedCarveout7Access4': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS4' 
 'McGeneralizedCarveout7Access5': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS5' 
 'McGeneralizedCarveout7Access6': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS6' 
 'McGeneralizedCarveout7Access7': 'MC_SECURITY_CARVEOUT7_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_ACCESS7' 
 'McGeneralizedCarveout7ForceInternalAccess0': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout7ForceInternalAccess1': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout7ForceInternalAccess2': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout7ForceInternalAccess3': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout7ForceInternalAccess4': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout7ForceInternalAccess5': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout7ForceInternalAccess6': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout7ForceInternalAccess7': 'MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout7Cfg0': 'MC_SECURITY_CARVEOUT7_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT7_CFG0' 
 'McGeneralizedCarveout8Bom': 'MC_SECURITY_CARVEOUT8_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_BOM' 
 'McGeneralizedCarveout8BomHi': 'MC_SECURITY_CARVEOUT8_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_BOM_HI' 
 'McGeneralizedCarveout8Size128kb': 'MC_SECURITY_CARVEOUT8_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_SIZE_128KB' 
 'McGeneralizedCarveout8Access0': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS0' 
 'McGeneralizedCarveout8Access1': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS1' 
 'McGeneralizedCarveout8Access2': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS2' 
 'McGeneralizedCarveout8Access3': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS3' 
 'McGeneralizedCarveout8Access4': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS4' 
 'McGeneralizedCarveout8Access5': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS5' 
 'McGeneralizedCarveout8Access6': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS6' 
 'McGeneralizedCarveout8Access7': 'MC_SECURITY_CARVEOUT8_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_ACCESS7' 
 'McGeneralizedCarveout8ForceInternalAccess0': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout8ForceInternalAccess1': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout8ForceInternalAccess2': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout8ForceInternalAccess3': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout8ForceInternalAccess4': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout8ForceInternalAccess5': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout8ForceInternalAccess6': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout8ForceInternalAccess7': 'MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout8Cfg0': 'MC_SECURITY_CARVEOUT8_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT8_CFG0' 
 'McGeneralizedCarveout9Bom': 'MC_SECURITY_CARVEOUT9_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_BOM' 
 'McGeneralizedCarveout9BomHi': 'MC_SECURITY_CARVEOUT9_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_BOM_HI' 
 'McGeneralizedCarveout9Size128kb': 'MC_SECURITY_CARVEOUT9_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_SIZE_128KB' 
 'McGeneralizedCarveout9Access0': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS0' 
 'McGeneralizedCarveout9Access1': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS1' 
 'McGeneralizedCarveout9Access2': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS2' 
 'McGeneralizedCarveout9Access3': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS3' 
 'McGeneralizedCarveout9Access4': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS4' 
 'McGeneralizedCarveout9Access5': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS5' 
 'McGeneralizedCarveout9Access6': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS6' 
 'McGeneralizedCarveout9Access7': 'MC_SECURITY_CARVEOUT9_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_ACCESS7' 
 'McGeneralizedCarveout9ForceInternalAccess0': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout9ForceInternalAccess1': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout9ForceInternalAccess2': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout9ForceInternalAccess3': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout9ForceInternalAccess4': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout9ForceInternalAccess5': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout9ForceInternalAccess6': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout9ForceInternalAccess7': 'MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout9Cfg0': 'MC_SECURITY_CARVEOUT9_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT9_CFG0' 
 'McGeneralizedCarveout10Bom': 'MC_SECURITY_CARVEOUT10_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_BOM' 
 'McGeneralizedCarveout10BomHi': 'MC_SECURITY_CARVEOUT10_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_BOM_HI' 
 'McGeneralizedCarveout10Size128kb': 'MC_SECURITY_CARVEOUT10_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_SIZE_128KB' 
 'McGeneralizedCarveout10Access0': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS0' 
 'McGeneralizedCarveout10Access1': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS1' 
 'McGeneralizedCarveout10Access2': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS2' 
 'McGeneralizedCarveout10Access3': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS3' 
 'McGeneralizedCarveout10Access4': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS4' 
 'McGeneralizedCarveout10Access5': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS5' 
 'McGeneralizedCarveout10Access6': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS6' 
 'McGeneralizedCarveout10Access7': 'MC_SECURITY_CARVEOUT10_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_ACCESS7' 
 'McGeneralizedCarveout10ForceInternalAccess0': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout10ForceInternalAccess1': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout10ForceInternalAccess2': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout10ForceInternalAccess3': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout10ForceInternalAccess4': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout10ForceInternalAccess5': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout10ForceInternalAccess6': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout10ForceInternalAccess7': 'MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout10Cfg0': 'MC_SECURITY_CARVEOUT10_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT10_CFG0' 
 'McGeneralizedCarveout11Bom': 'MC_SECURITY_CARVEOUT11_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_BOM' 
 'McGeneralizedCarveout11BomHi': 'MC_SECURITY_CARVEOUT11_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_BOM_HI' 
 'McGeneralizedCarveout11Size128kb': 'MC_SECURITY_CARVEOUT11_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_SIZE_128KB' 
 'McGeneralizedCarveout11Access0': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS0' 
 'McGeneralizedCarveout11Access1': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS1' 
 'McGeneralizedCarveout11Access2': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS2' 
 'McGeneralizedCarveout11Access3': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS3' 
 'McGeneralizedCarveout11Access4': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS4' 
 'McGeneralizedCarveout11Access5': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS5' 
 'McGeneralizedCarveout11Access6': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS6' 
 'McGeneralizedCarveout11Access7': 'MC_SECURITY_CARVEOUT11_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_ACCESS7' 
 'McGeneralizedCarveout11ForceInternalAccess0': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout11ForceInternalAccess1': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout11ForceInternalAccess2': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout11ForceInternalAccess3': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout11ForceInternalAccess4': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout11ForceInternalAccess5': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout11ForceInternalAccess6': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout11ForceInternalAccess7': 'MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout11Cfg0': 'MC_SECURITY_CARVEOUT11_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT11_CFG0' 
 'McGeneralizedCarveout12Bom': 'MC_SECURITY_CARVEOUT12_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_BOM' 
 'McGeneralizedCarveout12BomHi': 'MC_SECURITY_CARVEOUT12_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_BOM_HI' 
 'McGeneralizedCarveout12Size128kb': 'MC_SECURITY_CARVEOUT12_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_SIZE_128KB' 
 'McGeneralizedCarveout12Access0': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS0' 
 'McGeneralizedCarveout12Access1': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS1' 
 'McGeneralizedCarveout12Access2': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS2' 
 'McGeneralizedCarveout12Access3': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS3' 
 'McGeneralizedCarveout12Access4': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS4' 
 'McGeneralizedCarveout12Access5': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS5' 
 'McGeneralizedCarveout12Access6': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS6' 
 'McGeneralizedCarveout12Access7': 'MC_SECURITY_CARVEOUT12_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_ACCESS7' 
 'McGeneralizedCarveout12ForceInternalAccess0': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout12ForceInternalAccess1': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout12ForceInternalAccess2': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout12ForceInternalAccess3': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout12ForceInternalAccess4': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout12ForceInternalAccess5': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout12ForceInternalAccess6': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout12ForceInternalAccess7': 'MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout12Cfg0': 'MC_SECURITY_CARVEOUT12_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT12_CFG0' 
 'McGeneralizedCarveout13Bom': 'MC_SECURITY_CARVEOUT13_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_BOM' 
 'McGeneralizedCarveout13BomHi': 'MC_SECURITY_CARVEOUT13_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_BOM_HI' 
 'McGeneralizedCarveout13Size128kb': 'MC_SECURITY_CARVEOUT13_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_SIZE_128KB' 
 'McGeneralizedCarveout13Access0': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS0' 
 'McGeneralizedCarveout13Access1': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS1' 
 'McGeneralizedCarveout13Access2': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS2' 
 'McGeneralizedCarveout13Access3': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS3' 
 'McGeneralizedCarveout13Access4': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS4' 
 'McGeneralizedCarveout13Access5': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS5' 
 'McGeneralizedCarveout13Access6': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS6' 
 'McGeneralizedCarveout13Access7': 'MC_SECURITY_CARVEOUT13_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_ACCESS7' 
 'McGeneralizedCarveout13ForceInternalAccess0': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout13ForceInternalAccess1': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout13ForceInternalAccess2': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout13ForceInternalAccess3': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout13ForceInternalAccess4': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout13ForceInternalAccess5': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout13ForceInternalAccess6': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout13ForceInternalAccess7': 'MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout13Cfg0': 'MC_SECURITY_CARVEOUT13_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT13_CFG0' 
 'McGeneralizedCarveout14Bom': 'MC_SECURITY_CARVEOUT14_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_BOM' 
 'McGeneralizedCarveout14BomHi': 'MC_SECURITY_CARVEOUT14_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_BOM_HI' 
 'McGeneralizedCarveout14Size128kb': 'MC_SECURITY_CARVEOUT14_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_SIZE_128KB' 
 'McGeneralizedCarveout14Access0': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS0' 
 'McGeneralizedCarveout14Access1': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS1' 
 'McGeneralizedCarveout14Access2': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS2' 
 'McGeneralizedCarveout14Access3': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS3' 
 'McGeneralizedCarveout14Access4': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS4' 
 'McGeneralizedCarveout14Access5': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS5' 
 'McGeneralizedCarveout14Access6': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS6' 
 'McGeneralizedCarveout14Access7': 'MC_SECURITY_CARVEOUT14_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_ACCESS7' 
 'McGeneralizedCarveout14ForceInternalAccess0': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout14ForceInternalAccess1': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout14ForceInternalAccess2': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout14ForceInternalAccess3': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout14ForceInternalAccess4': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout14ForceInternalAccess5': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout14ForceInternalAccess6': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout14ForceInternalAccess7': 'MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout14Cfg0': 'MC_SECURITY_CARVEOUT14_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT14_CFG0' 
 'McGeneralizedCarveout15Bom': 'MC_SECURITY_CARVEOUT15_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_BOM' 
 'McGeneralizedCarveout15BomHi': 'MC_SECURITY_CARVEOUT15_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_BOM_HI' 
 'McGeneralizedCarveout15Size128kb': 'MC_SECURITY_CARVEOUT15_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_SIZE_128KB' 
 'McGeneralizedCarveout15Access0': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS0' 
 'McGeneralizedCarveout15Access1': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS1' 
 'McGeneralizedCarveout15Access2': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS2' 
 'McGeneralizedCarveout15Access3': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS3' 
 'McGeneralizedCarveout15Access4': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS4' 
 'McGeneralizedCarveout15Access5': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS5' 
 'McGeneralizedCarveout15Access6': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS6' 
 'McGeneralizedCarveout15Access7': 'MC_SECURITY_CARVEOUT15_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_ACCESS7' 
 'McGeneralizedCarveout15ForceInternalAccess0': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout15ForceInternalAccess1': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout15ForceInternalAccess2': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout15ForceInternalAccess3': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout15ForceInternalAccess4': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout15ForceInternalAccess5': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout15ForceInternalAccess6': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout15ForceInternalAccess7': 'MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout15Cfg0': 'MC_SECURITY_CARVEOUT15_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT15_CFG0' 
 'McGeneralizedCarveout16Bom': 'MC_SECURITY_CARVEOUT16_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_BOM' 
 'McGeneralizedCarveout16BomHi': 'MC_SECURITY_CARVEOUT16_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_BOM_HI' 
 'McGeneralizedCarveout16Size128kb': 'MC_SECURITY_CARVEOUT16_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_SIZE_128KB' 
 'McGeneralizedCarveout16Access0': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS0' 
 'McGeneralizedCarveout16Access1': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS1' 
 'McGeneralizedCarveout16Access2': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS2' 
 'McGeneralizedCarveout16Access3': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS3' 
 'McGeneralizedCarveout16Access4': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS4' 
 'McGeneralizedCarveout16Access5': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS5' 
 'McGeneralizedCarveout16Access6': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS6' 
 'McGeneralizedCarveout16Access7': 'MC_SECURITY_CARVEOUT16_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_ACCESS7' 
 'McGeneralizedCarveout16ForceInternalAccess0': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout16ForceInternalAccess1': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout16ForceInternalAccess2': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout16ForceInternalAccess3': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout16ForceInternalAccess4': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout16ForceInternalAccess5': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout16ForceInternalAccess6': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout16ForceInternalAccess7': 'MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout16Cfg0': 'MC_SECURITY_CARVEOUT16_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT16_CFG0' 
 'McGeneralizedCarveout17Bom': 'MC_SECURITY_CARVEOUT17_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_BOM' 
 'McGeneralizedCarveout17BomHi': 'MC_SECURITY_CARVEOUT17_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_BOM_HI' 
 'McGeneralizedCarveout17Size128kb': 'MC_SECURITY_CARVEOUT17_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_SIZE_128KB' 
 'McGeneralizedCarveout17Access0': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS0' 
 'McGeneralizedCarveout17Access1': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS1' 
 'McGeneralizedCarveout17Access2': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS2' 
 'McGeneralizedCarveout17Access3': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS3' 
 'McGeneralizedCarveout17Access4': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS4' 
 'McGeneralizedCarveout17Access5': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS5' 
 'McGeneralizedCarveout17Access6': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS6' 
 'McGeneralizedCarveout17Access7': 'MC_SECURITY_CARVEOUT17_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_ACCESS7' 
 'McGeneralizedCarveout17ForceInternalAccess0': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout17ForceInternalAccess1': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout17ForceInternalAccess2': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout17ForceInternalAccess3': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout17ForceInternalAccess4': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout17ForceInternalAccess5': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout17ForceInternalAccess6': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout17ForceInternalAccess7': 'MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout17Cfg0': 'MC_SECURITY_CARVEOUT17_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT17_CFG0' 
 'McGeneralizedCarveout18Bom': 'MC_SECURITY_CARVEOUT18_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_BOM' 
 'McGeneralizedCarveout18BomHi': 'MC_SECURITY_CARVEOUT18_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_BOM_HI' 
 'McGeneralizedCarveout18Size128kb': 'MC_SECURITY_CARVEOUT18_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_SIZE_128KB' 
 'McGeneralizedCarveout18Access0': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS0' 
 'McGeneralizedCarveout18Access1': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS1' 
 'McGeneralizedCarveout18Access2': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS2' 
 'McGeneralizedCarveout18Access3': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS3' 
 'McGeneralizedCarveout18Access4': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS4' 
 'McGeneralizedCarveout18Access5': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS5' 
 'McGeneralizedCarveout18Access6': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS6' 
 'McGeneralizedCarveout18Access7': 'MC_SECURITY_CARVEOUT18_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_ACCESS7' 
 'McGeneralizedCarveout18ForceInternalAccess0': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout18ForceInternalAccess1': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout18ForceInternalAccess2': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout18ForceInternalAccess3': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout18ForceInternalAccess4': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout18ForceInternalAccess5': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout18ForceInternalAccess6': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout18ForceInternalAccess7': 'MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout18Cfg0': 'MC_SECURITY_CARVEOUT18_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT18_CFG0' 
 'McGeneralizedCarveout19Bom': 'MC_SECURITY_CARVEOUT19_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_BOM' 
 'McGeneralizedCarveout19BomHi': 'MC_SECURITY_CARVEOUT19_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_BOM_HI' 
 'McGeneralizedCarveout19Size128kb': 'MC_SECURITY_CARVEOUT19_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_SIZE_128KB' 
 'McGeneralizedCarveout19Access0': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS0' 
 'McGeneralizedCarveout19Access1': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS1' 
 'McGeneralizedCarveout19Access2': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS2' 
 'McGeneralizedCarveout19Access3': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS3' 
 'McGeneralizedCarveout19Access4': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS4' 
 'McGeneralizedCarveout19Access5': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS5' 
 'McGeneralizedCarveout19Access6': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS6' 
 'McGeneralizedCarveout19Access7': 'MC_SECURITY_CARVEOUT19_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_ACCESS7' 
 'McGeneralizedCarveout19ForceInternalAccess0': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout19ForceInternalAccess1': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout19ForceInternalAccess2': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout19ForceInternalAccess3': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout19ForceInternalAccess4': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout19ForceInternalAccess5': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout19ForceInternalAccess6': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout19ForceInternalAccess7': 'MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout19Cfg0': 'MC_SECURITY_CARVEOUT19_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT19_CFG0' 
 'McGeneralizedCarveout20Bom': 'MC_SECURITY_CARVEOUT20_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_BOM' 
 'McGeneralizedCarveout20BomHi': 'MC_SECURITY_CARVEOUT20_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_BOM_HI' 
 'McGeneralizedCarveout20Size128kb': 'MC_SECURITY_CARVEOUT20_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_SIZE_128KB' 
 'McGeneralizedCarveout20Access0': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS0' 
 'McGeneralizedCarveout20Access1': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS1' 
 'McGeneralizedCarveout20Access2': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS2' 
 'McGeneralizedCarveout20Access3': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS3' 
 'McGeneralizedCarveout20Access4': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS4' 
 'McGeneralizedCarveout20Access5': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS5' 
 'McGeneralizedCarveout20Access6': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS6' 
 'McGeneralizedCarveout20Access7': 'MC_SECURITY_CARVEOUT20_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_ACCESS7' 
 'McGeneralizedCarveout20ForceInternalAccess0': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout20ForceInternalAccess1': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout20ForceInternalAccess2': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout20ForceInternalAccess3': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout20ForceInternalAccess4': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout20ForceInternalAccess5': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout20ForceInternalAccess6': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout20ForceInternalAccess7': 'MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout20Cfg0': 'MC_SECURITY_CARVEOUT20_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT20_CFG0' 
 'McGeneralizedCarveout21Bom': 'MC_SECURITY_CARVEOUT21_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_BOM' 
 'McGeneralizedCarveout21BomHi': 'MC_SECURITY_CARVEOUT21_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_BOM_HI' 
 'McGeneralizedCarveout21Size128kb': 'MC_SECURITY_CARVEOUT21_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_SIZE_128KB' 
 'McGeneralizedCarveout21Access0': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS0' 
 'McGeneralizedCarveout21Access1': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS1' 
 'McGeneralizedCarveout21Access2': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS2' 
 'McGeneralizedCarveout21Access3': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS3' 
 'McGeneralizedCarveout21Access4': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS4' 
 'McGeneralizedCarveout21Access5': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS5' 
 'McGeneralizedCarveout21Access6': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS6' 
 'McGeneralizedCarveout21Access7': 'MC_SECURITY_CARVEOUT21_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_ACCESS7' 
 'McGeneralizedCarveout21ForceInternalAccess0': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout21ForceInternalAccess1': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout21ForceInternalAccess2': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout21ForceInternalAccess3': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout21ForceInternalAccess4': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout21ForceInternalAccess5': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout21ForceInternalAccess6': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout21ForceInternalAccess7': 'MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout21Cfg0': 'MC_SECURITY_CARVEOUT21_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT21_CFG0' 
 'McGeneralizedCarveout22Bom': 'MC_SECURITY_CARVEOUT22_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_BOM' 
 'McGeneralizedCarveout22BomHi': 'MC_SECURITY_CARVEOUT22_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_BOM_HI' 
 'McGeneralizedCarveout22Size128kb': 'MC_SECURITY_CARVEOUT22_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_SIZE_128KB' 
 'McGeneralizedCarveout22Access0': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS0' 
 'McGeneralizedCarveout22Access1': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS1' 
 'McGeneralizedCarveout22Access2': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS2' 
 'McGeneralizedCarveout22Access3': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS3' 
 'McGeneralizedCarveout22Access4': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS4' 
 'McGeneralizedCarveout22Access5': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS5' 
 'McGeneralizedCarveout22Access6': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS6' 
 'McGeneralizedCarveout22Access7': 'MC_SECURITY_CARVEOUT22_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_ACCESS7' 
 'McGeneralizedCarveout22ForceInternalAccess0': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout22ForceInternalAccess1': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout22ForceInternalAccess2': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout22ForceInternalAccess3': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout22ForceInternalAccess4': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout22ForceInternalAccess5': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout22ForceInternalAccess6': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout22ForceInternalAccess7': 'MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout22Cfg0': 'MC_SECURITY_CARVEOUT22_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT22_CFG0' 
 'McGeneralizedCarveout23Bom': 'MC_SECURITY_CARVEOUT23_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_BOM' 
 'McGeneralizedCarveout23BomHi': 'MC_SECURITY_CARVEOUT23_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_BOM_HI' 
 'McGeneralizedCarveout23Size128kb': 'MC_SECURITY_CARVEOUT23_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_SIZE_128KB' 
 'McGeneralizedCarveout23Access0': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS0' 
 'McGeneralizedCarveout23Access1': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS1' 
 'McGeneralizedCarveout23Access2': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS2' 
 'McGeneralizedCarveout23Access3': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS3' 
 'McGeneralizedCarveout23Access4': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS4' 
 'McGeneralizedCarveout23Access5': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS5' 
 'McGeneralizedCarveout23Access6': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS6' 
 'McGeneralizedCarveout23Access7': 'MC_SECURITY_CARVEOUT23_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_ACCESS7' 
 'McGeneralizedCarveout23ForceInternalAccess0': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout23ForceInternalAccess1': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout23ForceInternalAccess2': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout23ForceInternalAccess3': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout23ForceInternalAccess4': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout23ForceInternalAccess5': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout23ForceInternalAccess6': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout23ForceInternalAccess7': 'MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout23Cfg0': 'MC_SECURITY_CARVEOUT23_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT23_CFG0' 
 'McGeneralizedCarveout24Bom': 'MC_SECURITY_CARVEOUT24_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_BOM' 
 'McGeneralizedCarveout24BomHi': 'MC_SECURITY_CARVEOUT24_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_BOM_HI' 
 'McGeneralizedCarveout24Size128kb': 'MC_SECURITY_CARVEOUT24_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_SIZE_128KB' 
 'McGeneralizedCarveout24Access0': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS0' 
 'McGeneralizedCarveout24Access1': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS1' 
 'McGeneralizedCarveout24Access2': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS2' 
 'McGeneralizedCarveout24Access3': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS3' 
 'McGeneralizedCarveout24Access4': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS4' 
 'McGeneralizedCarveout24Access5': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS5' 
 'McGeneralizedCarveout24Access6': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS6' 
 'McGeneralizedCarveout24Access7': 'MC_SECURITY_CARVEOUT24_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_ACCESS7' 
 'McGeneralizedCarveout24ForceInternalAccess0': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout24ForceInternalAccess1': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout24ForceInternalAccess2': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout24ForceInternalAccess3': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout24ForceInternalAccess4': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout24ForceInternalAccess5': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout24ForceInternalAccess6': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout24ForceInternalAccess7': 'MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout24Cfg0': 'MC_SECURITY_CARVEOUT24_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT24_CFG0' 
 'McGeneralizedCarveout25Bom': 'MC_SECURITY_CARVEOUT25_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_BOM' 
 'McGeneralizedCarveout25BomHi': 'MC_SECURITY_CARVEOUT25_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_BOM_HI' 
 'McGeneralizedCarveout25Size128kb': 'MC_SECURITY_CARVEOUT25_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_SIZE_128KB' 
 'McGeneralizedCarveout25Access0': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS0' 
 'McGeneralizedCarveout25Access1': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS1' 
 'McGeneralizedCarveout25Access2': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS2' 
 'McGeneralizedCarveout25Access3': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS3' 
 'McGeneralizedCarveout25Access4': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS4' 
 'McGeneralizedCarveout25Access5': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS5' 
 'McGeneralizedCarveout25Access6': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS6' 
 'McGeneralizedCarveout25Access7': 'MC_SECURITY_CARVEOUT25_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_ACCESS7' 
 'McGeneralizedCarveout25ForceInternalAccess0': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout25ForceInternalAccess1': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout25ForceInternalAccess2': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout25ForceInternalAccess3': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout25ForceInternalAccess4': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout25ForceInternalAccess5': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout25ForceInternalAccess6': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout25ForceInternalAccess7': 'MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout25Cfg0': 'MC_SECURITY_CARVEOUT25_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT25_CFG0' 
 'McGeneralizedCarveout26Bom': 'MC_SECURITY_CARVEOUT26_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_BOM' 
 'McGeneralizedCarveout26BomHi': 'MC_SECURITY_CARVEOUT26_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_BOM_HI' 
 'McGeneralizedCarveout26Size128kb': 'MC_SECURITY_CARVEOUT26_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_SIZE_128KB' 
 'McGeneralizedCarveout26Access0': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS0' 
 'McGeneralizedCarveout26Access1': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS1' 
 'McGeneralizedCarveout26Access2': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS2' 
 'McGeneralizedCarveout26Access3': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS3' 
 'McGeneralizedCarveout26Access4': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS4' 
 'McGeneralizedCarveout26Access5': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS5' 
 'McGeneralizedCarveout26Access6': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS6' 
 'McGeneralizedCarveout26Access7': 'MC_SECURITY_CARVEOUT26_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_ACCESS7' 
 'McGeneralizedCarveout26ForceInternalAccess0': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout26ForceInternalAccess1': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout26ForceInternalAccess2': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout26ForceInternalAccess3': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout26ForceInternalAccess4': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout26ForceInternalAccess5': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout26ForceInternalAccess6': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout26ForceInternalAccess7': 'MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout26Cfg0': 'MC_SECURITY_CARVEOUT26_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT26_CFG0' 
 'McGeneralizedCarveout27Bom': 'MC_SECURITY_CARVEOUT27_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_BOM' 
 'McGeneralizedCarveout27BomHi': 'MC_SECURITY_CARVEOUT27_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_BOM_HI' 
 'McGeneralizedCarveout27Size128kb': 'MC_SECURITY_CARVEOUT27_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_SIZE_128KB' 
 'McGeneralizedCarveout27Access0': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS0' 
 'McGeneralizedCarveout27Access1': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS1' 
 'McGeneralizedCarveout27Access2': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS2' 
 'McGeneralizedCarveout27Access3': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS3' 
 'McGeneralizedCarveout27Access4': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS4' 
 'McGeneralizedCarveout27Access5': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS5' 
 'McGeneralizedCarveout27Access6': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS6' 
 'McGeneralizedCarveout27Access7': 'MC_SECURITY_CARVEOUT27_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_ACCESS7' 
 'McGeneralizedCarveout27ForceInternalAccess0': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout27ForceInternalAccess1': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout27ForceInternalAccess2': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout27ForceInternalAccess3': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout27ForceInternalAccess4': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout27ForceInternalAccess5': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout27ForceInternalAccess6': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout27ForceInternalAccess7': 'MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout27Cfg0': 'MC_SECURITY_CARVEOUT27_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT27_CFG0' 
 'McGeneralizedCarveout28Bom': 'MC_SECURITY_CARVEOUT28_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_BOM' 
 'McGeneralizedCarveout28BomHi': 'MC_SECURITY_CARVEOUT28_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_BOM_HI' 
 'McGeneralizedCarveout28Size128kb': 'MC_SECURITY_CARVEOUT28_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_SIZE_128KB' 
 'McGeneralizedCarveout28Access0': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS0' 
 'McGeneralizedCarveout28Access1': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS1' 
 'McGeneralizedCarveout28Access2': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS2' 
 'McGeneralizedCarveout28Access3': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS3' 
 'McGeneralizedCarveout28Access4': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS4' 
 'McGeneralizedCarveout28Access5': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS5' 
 'McGeneralizedCarveout28Access6': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS6' 
 'McGeneralizedCarveout28Access7': 'MC_SECURITY_CARVEOUT28_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_ACCESS7' 
 'McGeneralizedCarveout28ForceInternalAccess0': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout28ForceInternalAccess1': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout28ForceInternalAccess2': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout28ForceInternalAccess3': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout28ForceInternalAccess4': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout28ForceInternalAccess5': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout28ForceInternalAccess6': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout28ForceInternalAccess7': 'MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout28Cfg0': 'MC_SECURITY_CARVEOUT28_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT28_CFG0' 
 'McGeneralizedCarveout29Bom': 'MC_SECURITY_CARVEOUT29_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_BOM' 
 'McGeneralizedCarveout29BomHi': 'MC_SECURITY_CARVEOUT29_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_BOM_HI' 
 'McGeneralizedCarveout29Size128kb': 'MC_SECURITY_CARVEOUT29_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_SIZE_128KB' 
 'McGeneralizedCarveout29Access0': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS0' 
 'McGeneralizedCarveout29Access1': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS1' 
 'McGeneralizedCarveout29Access2': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS2' 
 'McGeneralizedCarveout29Access3': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS3' 
 'McGeneralizedCarveout29Access4': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS4' 
 'McGeneralizedCarveout29Access5': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS5' 
 'McGeneralizedCarveout29Access6': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS6' 
 'McGeneralizedCarveout29Access7': 'MC_SECURITY_CARVEOUT29_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_ACCESS7' 
 'McGeneralizedCarveout29ForceInternalAccess0': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout29ForceInternalAccess1': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout29ForceInternalAccess2': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout29ForceInternalAccess3': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout29ForceInternalAccess4': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout29ForceInternalAccess5': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout29ForceInternalAccess6': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout29ForceInternalAccess7': 'MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout29Cfg0': 'MC_SECURITY_CARVEOUT29_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT29_CFG0' 
 'McGeneralizedCarveout30Bom': 'MC_SECURITY_CARVEOUT30_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_BOM' 
 'McGeneralizedCarveout30BomHi': 'MC_SECURITY_CARVEOUT30_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_BOM_HI' 
 'McGeneralizedCarveout30Size128kb': 'MC_SECURITY_CARVEOUT30_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_SIZE_128KB' 
 'McGeneralizedCarveout30Access0': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS0' 
 'McGeneralizedCarveout30Access1': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS1' 
 'McGeneralizedCarveout30Access2': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS2' 
 'McGeneralizedCarveout30Access3': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS3' 
 'McGeneralizedCarveout30Access4': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS4' 
 'McGeneralizedCarveout30Access5': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS5' 
 'McGeneralizedCarveout30Access6': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS6' 
 'McGeneralizedCarveout30Access7': 'MC_SECURITY_CARVEOUT30_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_ACCESS7' 
 'McGeneralizedCarveout30ForceInternalAccess0': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout30ForceInternalAccess1': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout30ForceInternalAccess2': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout30ForceInternalAccess3': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout30ForceInternalAccess4': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout30ForceInternalAccess5': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout30ForceInternalAccess6': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout30ForceInternalAccess7': 'MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout30Cfg0': 'MC_SECURITY_CARVEOUT30_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT30_CFG0' 
 'McGeneralizedCarveout31Bom': 'MC_SECURITY_CARVEOUT31_BOM_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_BOM' 
 'McGeneralizedCarveout31BomHi': 'MC_SECURITY_CARVEOUT31_BOM_HI_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_BOM_HI' 
 'McGeneralizedCarveout31Size128kb': 'MC_SECURITY_CARVEOUT31_SIZE_128KB_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_SIZE_128KB' 
 'McGeneralizedCarveout31Access0': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS0' 
 'McGeneralizedCarveout31Access1': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS1' 
 'McGeneralizedCarveout31Access2': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS2' 
 'McGeneralizedCarveout31Access3': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS3' 
 'McGeneralizedCarveout31Access4': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS4' 
 'McGeneralizedCarveout31Access5': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS5' 
 'McGeneralizedCarveout31Access6': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS6' 
 'McGeneralizedCarveout31Access7': 'MC_SECURITY_CARVEOUT31_CLIENT_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_ACCESS7' 
 'McGeneralizedCarveout31ForceInternalAccess0': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS0_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS0' 
 'McGeneralizedCarveout31ForceInternalAccess1': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS1_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS1' 
 'McGeneralizedCarveout31ForceInternalAccess2': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS2_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS2' 
 'McGeneralizedCarveout31ForceInternalAccess3': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS3_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS3' 
 'McGeneralizedCarveout31ForceInternalAccess4': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS4_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS4' 
 'McGeneralizedCarveout31ForceInternalAccess5': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS5_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS5' 
 'McGeneralizedCarveout31ForceInternalAccess6': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS6_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS6' 
 'McGeneralizedCarveout31ForceInternalAccess7': 'MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS7_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CLIENT_FORCE_INTERNAL_ACCESS7' 
 'McGeneralizedCarveout31Cfg0': 'MC_SECURITY_CARVEOUT31_CFG0_0', 'Specifies the value for MC_SECURITY_CARVEOUT31_CFG0' 
 'McLatencyAllowanceCifllWr0': 'MC_LATENCY_ALLOWANCE_CIFLL_WR_0_0', 'Specifies the value for MC_LATENCY_ALLOWANCE_CIFLL_WR_0' 
 'McEccRegion0Cfg0': 'MC_ECC_REGION0_CFG0_0', 'Specifies the value for MC_ECC_REGION0_CFG0' 
 'McEccRegion0Bom': 'MC_ECC_REGION0_BOM_0', 'Specifies the value for MC_ECC_REGION0_BOM' 
 'McEccRegion0BomHi': 'MC_ECC_REGION0_BOM_HI_0', 'Specifies the value for MC_ECC_REGION0_BOM_HI' 
 'McEccRegion0Size': 'MC_ECC_REGION0_SIZE_0', 'Specifies the value for MC_ECC_REGION0_SIZE' 
 'McEccRegion1Cfg0': 'MC_ECC_REGION1_CFG0_0', 'Specifies the value for MC_ECC_REGION1_CFG0' 
 'McEccRegion1Bom': 'MC_ECC_REGION1_BOM_0', 'Specifies the value for MC_ECC_REGION1_BOM' 
 'McEccRegion1BomHi': 'MC_ECC_REGION1_BOM_HI_0', 'Specifies the value for MC_ECC_REGION1_BOM_HI' 
 'McEccRegion1Size': 'MC_ECC_REGION1_SIZE_0', 'Specifies the value for MC_ECC_REGion1_SIZE' 
 'McEccRegion2Cfg0': 'MC_ECC_REGION2_CFG0_0', 'Specifies the value for MC_ECC_REGION2_CFG0' 
 'McEccRegion2Bom': 'MC_ECC_REGION2_BOM_0', 'Specifies the value for MC_ECC_REGION2_BOM' 
 'McEccRegion2BomHi': 'MC_ECC_REGION2_BOM_HI_0', 'Specifies the value for MC_ECC_REGION2_BOM_HI' 
 'McEccRegion2Size': 'MC_ECC_REGION2_SIZE_0', 'Specifies the value for MC_ECC_REGion2_SIZE' 
 'McEccRegion3Cfg0': 'MC_ECC_REGION3_CFG0_0', 'Specifies the value for MC_ECC_REGION3_CFG0' 
 'McEccRegion3Bom': 'MC_ECC_REGION3_BOM_0', 'Specifies the value for MC_ECC_REGION3_BOM' 
 'McEccRegion3BomHi': 'MC_ECC_REGION3_BOM_HI_0', 'Specifies the value for MC_ECC_REGION3_BOM_HI' 
 'McEccRegion3Size': 'MC_ECC_REGION3_SIZE_0', 'Specifies the value for MC_ECC_REGION3_SIZE' 
 'BootRomPatchControl': 'NA', 'Specifies enable and offset for patched boot rom write' 
 'BootRomPatchData': 'NA', 'Specifies data for patched boot rom write' 
 'McMtsCarveoutBom': 'MC_MTS_CARVEOUT_BOM_0', 'Specifies the value for MC_MTS_CARVEOUT_BOM' 
 'McMtsCarveoutAdrHi': 'MC_MTS_CARVEOUT_ADR_HI_0', 'Specifies the value for MC_MTS_CARVEOUT_ADR_HI' 
 'McMtsCarveoutSizeMb': 'MC_MTS_CARVEOUT_SIZE_MB_0', 'Specifies the value for MC_MTS_CARVEOUT_SIZE_MB' 
 'McMtsCarveoutRegCtrl': 'MC_MTS_CARVEOUT_REG_CTRL_0', 'Specifies the value for MC_MTS_CARVEOUT_REG_CTRL' 
 'McSyncpointBom': 'MC_SYNCPOINT_BOM_0', 'Specifies the value for 1MB aligned value of Syncpoint BOM' 
 'McSyncpointTom': 'MC_SYNCPOINT_TOM_0', 'Specifies the value for 1MB aligned value of Syncpoint TOM' 
 'McSyncpointRegCtrl': 'MC_SYNCPOINT_REG_CTRL_0', 'Specifies the value for MC_SYNCPOINT_REG_CTRL_0' 
 'MssEncryptGenKeys': 'NA', 'Specifies flags for generating keys for encryption regions' 
 'MssEncryptDistKeys': 'NA', 'Specifies flags for distributing encryption keys' 
 'McMcfIreqxVcarbConfig': 'MC_MCF_IREQX_VCARB_CONFIG_0', 'Specifies IREQX VC Manager Arbiter Configuration' 
 'McMcfOreqxVcarbConfig': 'MC_MCF_OREQX_VCARB_CONFIG_0', 'Specifies OREQX VC Manager Arbiter Configuration' 
 'McMcfOreqxLlarbConfig': 'MC_MCF_OREQX_LLARB_CONFIG_0', 'Specifies OREQX LLARB Bypass Configuration' 
 'McMcfOreqxStatControl': 'MC_MCF_OREQX_STAT_CONTROL_0', 'Specifies OREQX STAT Control Configuration' 
 'McMcfIreqxSrcWeight0': 'MC_MCF_IREQX_SRC_WEIGHT_0_0', 'Specifies IREQX source weights' 
 'McMcfIreqxSrcWeight1': 'MC_MCF_IREQX_SRC_WEIGHT_1_0', 'Specifies IREQX source weights' 
 'McMcfIreqxClkenOverride': 'MC_MCF_IREQX_CLKEN_OVERRIDE_0', 'Specifies IREQX Second-level Clock Enable Overrides' 
 'McMcfOreqxClkenOverride': 'MC_MCF_OREQX_CLKEN_OVERRIDE_0', 'Specifies OREQX Second-level Clock Enable Overrides' 
 'McMcfSliceClkenOverride': 'MC_MCF_SLICE_CLKEN_OVERRIDE_0', 'Specifies SLICE Second-level Clock Enable Overrides' 
 'McMcfOrspxClkenOverride': 'MC_MCF_ORSPX_CLKEN_OVERRIDE_0', 'Specifies ORSPX Second-level Clock Enable Overrides' 
 'McMcfIrspxClkenOverride': 'MC_MCF_IRSPX_CLKEN_OVERRIDE_0', 'Specifies IRSPX Second-level Clock Enable Overrides' 
 'McMcfIrspxRdrspOpt': 'MC_MCF_IRSPX_RDRSP_OPT_0', 'Specifies IRSPX Arb optimizations' 
 'McMcfOrspxArbConfig': 'MC_MCF_ORSPX_ARB_CONFIG_0', 'Specifies ORSPX Arb optimizations' 
 'McMssSysramClkenOverride': 'MC_MSS_SYSRAM_CLKEN_OVERRIDE_0', 'Specifies SYSRAM Second-level Clock Enable Overrides' 
 'McMssSbsAsync': 'MC_MSS_SBS_ASYNC_0', 'Specifies SBS aAsync Interface Configuration' 
 'McMssSbsArb': 'MC_MSS_SBS_ARB_0', 'Specifies SBS Arbiter Configuration' 
 'McMssSbsClkenOverride': 'MC_MSS_SBS_CLKEN_OVERRIDE_0', 'Specifies SBS Second-level Clock Enable Overrides' 
 'McMssSbsVcLimit': 'MC_MSS_SBS_VC_LIMIT_0', 'Specifies SBS VC limits' 
 'McMcfSliceCfg': 'MC_MCF_SLICE_CFG_0', 'Specifies if ISO / NISO_REMOTE requests are allowed to be io-coherent' 
 'McMcfSliceFlNisoLimit': 'MC_MCF_SLICE_FL_NISO_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_NISO_LIMIT' 
 'McMcfSliceFlSisoLimit': 'MC_MCF_SLICE_FL_SISO_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_SISO_LIMIT' 
 'McMcfSliceFlIsoLimit': 'MC_MCF_SLICE_FL_ISO_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_ISO_LIMIT' 
 'McMcfSliceFlTransdoneLimit': 'MC_MCF_SLICE_FL_TRANSDONE_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_TRANSDONE_LIMIT' 
 'McMcfSliceFlNisoRemoteLimit': 'MC_MCF_SLICE_FL_NISO_REMOTE_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_NISO_REMOTE_LIMIT' 
 'McMcfSliceFlOrd1Limit': 'MC_MCF_SLICE_FL_ORD1_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_ORD1_LIMIT' 
 'McMcfSliceFlOrd2Limit': 'MC_MCF_SLICE_FL_ORD2_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_ORD2_LIMIT' 
 'McMcfSliceFlOrd3Limit': 'MC_MCF_SLICE_FL_ORD3_LIMIT_0', 'Specifies the value for MC_MCF_SLICE_FL_ORD3_LIMIT' 
 'McSmmuBypassConfig': 'MC_SMMU_BYPASS_CONFIG_0', 'Specifies the value for MC_SMMU_BYPASS_CONFIG' 
 'McClientOrderId0': 'MC_CLIENT_ORDER_ID_0_0', 'Specifies the value for MC_CLIENT_ORDER_ID_0' 
 'McClientOrderId2': 'MC_CLIENT_ORDER_ID_2_0', 'Specifies the value for MC_CLIENT_ORDER_ID_2' 
 'McClientOrderId3': 'MC_CLIENT_ORDER_ID_3_0', 'Specifies the value for MC_CLIENT_ORDER_ID_3' 
 'McClientOrderId4': 'MC_CLIENT_ORDER_ID_4_0', 'Specifies the value for MC_CLIENT_ORDER_ID_4' 
 'McClientOrderId5': 'MC_CLIENT_ORDER_ID_5_0', 'Specifies the value for MC_CLIENT_ORDER_ID_5' 
 'McClientOrderId6': 'MC_CLIENT_ORDER_ID_6_0', 'Specifies the value for MC_CLIENT_ORDER_ID_6' 
 'McClientOrderId7': 'MC_CLIENT_ORDER_ID_7_0', 'Specifies the value for MC_CLIENT_ORDER_ID_7' 
 'McClientOrderId8': 'MC_CLIENT_ORDER_ID_8_0', 'Specifies the value for MC_CLIENT_ORDER_ID_8' 
 'McClientOrderId9': 'MC_CLIENT_ORDER_ID_9_0', 'Specifies the value for MC_CLIENT_ORDER_ID_9' 
 'McClientOrderId10': 'MC_CLIENT_ORDER_ID_10_0', 'Specifies the value for MC_CLIENT_ORDER_ID_10' 
 'McClientOrderId12': 'MC_CLIENT_ORDER_ID_12_0', 'Specifies the value for MC_CLIENT_ORDER_ID_12' 
 'McClientOrderId13': 'MC_CLIENT_ORDER_ID_13_0', 'Specifies the value for MC_CLIENT_ORDER_ID_13' 
 'McClientOrderId14': 'MC_CLIENT_ORDER_ID_14_0', 'Specifies the value for MC_CLIENT_ORDER_ID_14' 
 'McClientOrderId15': 'MC_CLIENT_ORDER_ID_15_0', 'Specifies the value for MC_CLIENT_ORDER_ID_15' 
 'McClientOrderId16': 'MC_CLIENT_ORDER_ID_16_0', 'Specifies the value for MC_CLIENT_ORDER_ID_16' 
 'McClientOrderId17': 'MC_CLIENT_ORDER_ID_17_0', 'Specifies the value for MC_CLIENT_ORDER_ID_17' 
 'McClientOrderId18': 'MC_CLIENT_ORDER_ID_18_0', 'Specifies the value for MC_CLIENT_ORDER_ID_18' 
 'McClientOrderId19': 'MC_CLIENT_ORDER_ID_19_0', 'Specifies the value for MC_CLIENT_ORDER_ID_19' 
 'McClientOrderId20': 'MC_CLIENT_ORDER_ID_20_0', 'Specifies the value for MC_CLIENT_ORDER_ID_20' 
 'McClientOrderId21': 'MC_CLIENT_ORDER_ID_21_0', 'Specifies the value for MC_CLIENT_ORDER_ID_21' 
 'McClientOrderId22': 'MC_CLIENT_ORDER_ID_22_0', 'Specifies the value for MC_CLIENT_ORDER_ID_22' 
 'McClientOrderId23': 'MC_CLIENT_ORDER_ID_23_0', 'Specifies the value for MC_CLIENT_ORDER_ID_23' 
 'McClientOrderId24': 'MC_CLIENT_ORDER_ID_24_0', 'Specifies the value for MC_CLIENT_ORDER_ID_24' 
 'McClientOrderId25': 'MC_CLIENT_ORDER_ID_25_0', 'Specifies the value for MC_CLIENT_ORDER_ID_25' 
 'McClientOrderId26': 'MC_CLIENT_ORDER_ID_26_0', 'Specifies the value for MC_CLIENT_ORDER_ID_26' 
 'McClientOrderId27': 'MC_CLIENT_ORDER_ID_27_0', 'Specifies the value for MC_CLIENT_ORDER_ID_27' 
 'McClientOrderId28': 'MC_CLIENT_ORDER_ID_28_0', 'Specifies the value for MC_CLIENT_ORDER_ID_28' 
 'McClientOrderId29': 'MC_CLIENT_ORDER_ID_29_0', 'Specifies the value for MC_CLIENT_ORDER_ID_29' 
 'McClientOrderId30': 'MC_CLIENT_ORDER_ID_30_0', 'Specifies the value for MC_CLIENT_ORDER_ID_30' 
 'McClientOrderId31': 'MC_CLIENT_ORDER_ID_31_0', 'Specifies the value for MC_CLIENT_ORDER_ID_31' 
 'McConfigTsaSingleArbEnable': 'MC_CONFIG_TSA_SINGLE_ARB_ENABLE_0', 'Specifies the value for MC_CONFIG_TSA_SINGLE_ARB_ENABLE' 
 'McHubPcVcId0': 'MC_HUB_PC_VC_ID_0_0', 'Specifies the value for MC_HUB_PC_VC_ID_0' 
 'McHubPcVcId1': 'MC_HUB_PC_VC_ID_1_0', 'Specifies the value for MC_HUB_PC_VC_ID_1' 
 'McHubPcVcId2': 'MC_HUB_PC_VC_ID_2_0', 'Specifies the value for MC_HUB_PC_VC_ID_2' 
 'McHubPcVcId3': 'MC_HUB_PC_VC_ID_3_0', 'Specifies the value for MC_HUB_PC_VC_ID_3' 
 'McHubPcVcId4': 'MC_HUB_PC_VC_ID_4_0', 'Specifies the value for MC_HUB_PC_VC_ID_4' 
 'McHubPcVcId5': 'MC_HUB_PC_VC_ID_5_0', 'Specifies the value for MC_HUB_PC_VC_ID_5' 
 'McHubPcVcId6': 'MC_HUB_PC_VC_ID_6_0', 'Specifies the value for MC_HUB_PC_VC_ID_6' 
 'McHubPcVcId7': 'MC_HUB_PC_VC_ID_7_0', 'Specifies the value for MC_HUB_PC_VC_ID_7' 
 'McHubPcVcId8': 'MC_HUB_PC_VC_ID_8_0', 'Specifies the value for MC_HUB_PC_VC_ID_8' 
 'McHubPcVcId9': 'MC_HUB_PC_VC_ID_9_0', 'Specifies the value for MC_HUB_PC_VC_ID_9' 
 'McHubPcVcId10': 'MC_HUB_PC_VC_ID_10_0', 'Specifies the value for MC_HUB_PC_VC_ID_10' 
 'McHubPcVcId11': 'MC_HUB_PC_VC_ID_11_0', 'Specifies the value for MC_HUB_PC_VC_ID_11' 
 'McHubPcVcId12': 'MC_HUB_PC_VC_ID_12_0', 'Specifies the value for MC_HUB_PC_VC_ID_12' 
 'McHubPcVcId13': 'MC_HUB_PC_VC_ID_13_0', 'Specifies the value for MC_HUB_PC_VC_ID_13' 
 'McHubPcVcId14': 'MC_HUB_PC_VC_ID_14_0', 'Specifies the value for MC_HUB_PC_VC_ID_14' 
 'McBypassSidInit': 'NA', 'Specifies if Sid programming should be bypassed at init' 
 'EmcTrainingWriteFineCtrl': 'EMC_TRAINING_WRITE_FINE_CTRL_0', 'Specifies the value for TRAINING_WRITE_FINE_CTRL' 
 'EmcTrainingReadFineCtrl': 'EMC_TRAINING_READ_FINE_CTRL_0', 'Specifies the value for TRAINING_READ_FINE_CTRL' 
 'EmcTrainingWriteVrefCtrl': 'EMC_TRAINING_WRITE_VREF_CTRL_0', 'Specifies the value for TRAINING_WRITE_VREF_CTRL' 
 'EmcTrainingReadVrefCtrl': 'EMC_TRAINING_READ_VREF_CTRL_0', 'Specifies the value for TRAINING_READ_VREF_CTRL' 
 'EmcTrainingWriteCtrlMisc': 'EMC_TRAINING_WRITE_CTRL_MISC_0', 'Specifies the value for TRAINING_WRITE_CTRL_MISC' 
 'EmcTrainingReadCtrlMisc': 'EMC_TRAINING_READ_CTRL_MISC_0', 'Specifies the value for TRAINING_READ_CTRL_MISC' 
 'EmcTrainingMpc': 'EMC_TRAINING_MPC_0', 'Specifies the value for TRAINING_MPC' 
 'EmcTrainingCtrl': 'EMC_TRAINING_CTRL_0', 'Specifies the value for TRAINING_CTRL' 
 'EmcTrainingCmd': 'EMC_TRAINING_CMD_0', 'Specifies the value for TRAINING_CMD' 
 'EmcTrainingPatramDq0': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ0' 
 'EmcTrainingPatramDq1': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ1' 
 'EmcTrainingPatramDq2': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ2' 
 'EmcTrainingPatramDq3': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ3' 
 'EmcTrainingPatramDq4': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ4' 
 'EmcTrainingPatramDq5': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ5' 
 'EmcTrainingPatramDq6': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ6' 
 'EmcTrainingPatramDq7': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ7' 
 'EmcTrainingPatramDq8': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ8' 
 'EmcTrainingPatramDq9': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ9' 
 'EmcTrainingPatramDq10': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ10' 
 'EmcTrainingPatramDq11': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ11' 
 'EmcTrainingPatramDq12': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ12' 
 'EmcTrainingPatramDq13': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ13' 
 'EmcTrainingPatramDq14': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ14' 
 'EmcTrainingPatramDq15': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ15' 
 'EmcTrainingPatramDq16': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ16' 
 'EmcTrainingPatramDq17': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ17' 
 'EmcTrainingPatramDq18': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ18' 
 'EmcTrainingPatramDq19': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ19' 
 'EmcTrainingPatramDq20': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ20' 
 'EmcTrainingPatramDq21': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ21' 
 'EmcTrainingPatramDq22': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ22' 
 'EmcTrainingPatramDq23': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ23' 
 'EmcTrainingPatramDq24': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ24' 
 'EmcTrainingPatramDq25': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ25' 
 'EmcTrainingPatramDq26': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ26' 
 'EmcTrainingPatramDq27': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ27' 
 'EmcTrainingPatramDq28': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ28' 
 'EmcTrainingPatramDq29': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ29' 
 'EmcTrainingPatramDq30': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ30' 
 'EmcTrainingPatramDq31': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ31' 
 'EmcTrainingPatramDq32': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ32' 
 'EmcTrainingPatramDq33': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ33' 
 'EmcTrainingPatramDq34': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ34' 
 'EmcTrainingPatramDq35': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ35' 
 'EmcTrainingPatramDq36': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ36' 
 'EmcTrainingPatramDq37': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ37' 
 'EmcTrainingPatramDq38': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ38' 
 'EmcTrainingPatramDq39': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ39' 
 'EmcTrainingPatramDq40': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ40' 
 'EmcTrainingPatramDq41': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ41' 
 'EmcTrainingPatramDq42': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ42' 
 'EmcTrainingPatramDq43': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ43' 
 'EmcTrainingPatramDq44': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ44' 
 'EmcTrainingPatramDq45': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ45' 
 'EmcTrainingPatramDq46': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ46' 
 'EmcTrainingPatramDq47': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ47' 
 'EmcTrainingPatramDq48': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ48' 
 'EmcTrainingPatramDq49': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ49' 
 'EmcTrainingPatramDq50': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ50' 
 'EmcTrainingPatramDq51': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ51' 
 'EmcTrainingPatramDq52': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ52' 
 'EmcTrainingPatramDq53': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ53' 
 'EmcTrainingPatramDq54': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ54' 
 'EmcTrainingPatramDq55': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ55' 
 'EmcTrainingPatramDq56': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ56' 
 'EmcTrainingPatramDq57': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ57' 
 'EmcTrainingPatramDq58': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ58' 
 'EmcTrainingPatramDq59': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ59' 
 'EmcTrainingPatramDq60': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ60' 
 'EmcTrainingPatramDq61': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ61' 
 'EmcTrainingPatramDq62': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ62' 
 'EmcTrainingPatramDq63': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ63' 
 'EmcTrainingPatramDq64': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ64' 
 'EmcTrainingPatramDq65': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ65' 
 'EmcTrainingPatramDq66': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ66' 
 'EmcTrainingPatramDq67': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ67' 
 'EmcTrainingPatramDq68': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ68' 
 'EmcTrainingPatramDq69': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ69' 
 'EmcTrainingPatramDq70': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ70' 
 'EmcTrainingPatramDq71': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ71' 
 'EmcTrainingPatramDq72': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ72' 
 'EmcTrainingPatramDq73': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ73' 
 'EmcTrainingPatramDq74': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ74' 
 'EmcTrainingPatramDq75': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ75' 
 'EmcTrainingPatramDq76': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ76' 
 'EmcTrainingPatramDq77': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ77' 
 'EmcTrainingPatramDq78': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ78' 
 'EmcTrainingPatramDq79': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ79' 
 'EmcTrainingPatramDq80': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ80' 
 'EmcTrainingPatramDq81': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ81' 
 'EmcTrainingPatramDq82': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ82' 
 'EmcTrainingPatramDq83': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ83' 
 'EmcTrainingPatramDq84': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ84' 
 'EmcTrainingPatramDq85': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ85' 
 'EmcTrainingPatramDq86': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ86' 
 'EmcTrainingPatramDq87': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ87' 
 'EmcTrainingPatramDq88': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ88' 
 'EmcTrainingPatramDq89': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ89' 
 'EmcTrainingPatramDq90': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ90' 
 'EmcTrainingPatramDq91': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ91' 
 'EmcTrainingPatramDq92': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ92' 
 'EmcTrainingPatramDq93': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ93' 
 'EmcTrainingPatramDq94': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ94' 
 'EmcTrainingPatramDq95': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ95' 
 'EmcTrainingPatramDq96': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ96' 
 'EmcTrainingPatramDq97': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ97' 
 'EmcTrainingPatramDq98': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ98' 
 'EmcTrainingPatramDq99': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ99' 
 'EmcTrainingPatramDq100': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ100' 
 'EmcTrainingPatramDq101': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ101' 
 'EmcTrainingPatramDq102': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ102' 
 'EmcTrainingPatramDq103': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ103' 
 'EmcTrainingPatramDq104': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ104' 
 'EmcTrainingPatramDq105': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ105' 
 'EmcTrainingPatramDq106': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ106' 
 'EmcTrainingPatramDq107': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ107' 
 'EmcTrainingPatramDq108': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ108' 
 'EmcTrainingPatramDq109': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ109' 
 'EmcTrainingPatramDq110': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ110' 
 'EmcTrainingPatramDq111': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ111' 
 'EmcTrainingPatramDq112': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ112' 
 'EmcTrainingPatramDq113': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ113' 
 'EmcTrainingPatramDq114': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ114' 
 'EmcTrainingPatramDq115': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ115' 
 'EmcTrainingPatramDq116': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ116' 
 'EmcTrainingPatramDq117': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ117' 
 'EmcTrainingPatramDq118': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ118' 
 'EmcTrainingPatramDq119': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ119' 
 'EmcTrainingPatramDq120': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ120' 
 'EmcTrainingPatramDq121': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ121' 
 'EmcTrainingPatramDq122': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ122' 
 'EmcTrainingPatramDq123': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ123' 
 'EmcTrainingPatramDq124': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ124' 
 'EmcTrainingPatramDq125': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ125' 
 'EmcTrainingPatramDq126': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ126' 
 'EmcTrainingPatramDq127': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ127' 
 'EmcTrainingPatramDq128': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ128' 
 'EmcTrainingPatramDq129': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ129' 
 'EmcTrainingPatramDq130': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ130' 
 'EmcTrainingPatramDq131': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ131' 
 'EmcTrainingPatramDq132': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ132' 
 'EmcTrainingPatramDq133': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ133' 
 'EmcTrainingPatramDq134': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ134' 
 'EmcTrainingPatramDq135': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ135' 
 'EmcTrainingPatramDq136': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ136' 
 'EmcTrainingPatramDq137': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ137' 
 'EmcTrainingPatramDq138': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ138' 
 'EmcTrainingPatramDq139': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ139' 
 'EmcTrainingPatramDq140': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ140' 
 'EmcTrainingPatramDq141': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ141' 
 'EmcTrainingPatramDq142': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ142' 
 'EmcTrainingPatramDq143': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ143' 
 'EmcTrainingPatramDq144': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ144' 
 'EmcTrainingPatramDq145': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ145' 
 'EmcTrainingPatramDq146': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ146' 
 'EmcTrainingPatramDq147': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ147' 
 'EmcTrainingPatramDq148': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ148' 
 'EmcTrainingPatramDq149': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ149' 
 'EmcTrainingPatramDq150': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ150' 
 'EmcTrainingPatramDq151': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ151' 
 'EmcTrainingPatramDq152': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ152' 
 'EmcTrainingPatramDq153': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ153' 
 'EmcTrainingPatramDq154': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ154' 
 'EmcTrainingPatramDq155': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ155' 
 'EmcTrainingPatramDq156': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ156' 
 'EmcTrainingPatramDq157': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ157' 
 'EmcTrainingPatramDq158': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ158' 
 'EmcTrainingPatramDq159': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ159' 
 'EmcTrainingPatramDq160': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ160' 
 'EmcTrainingPatramDq161': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ161' 
 'EmcTrainingPatramDq162': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ162' 
 'EmcTrainingPatramDq163': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ163' 
 'EmcTrainingPatramDq164': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ164' 
 'EmcTrainingPatramDq165': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ165' 
 'EmcTrainingPatramDq166': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ166' 
 'EmcTrainingPatramDq167': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ167' 
 'EmcTrainingPatramDq168': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ168' 
 'EmcTrainingPatramDq169': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ169' 
 'EmcTrainingPatramDq170': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ170' 
 'EmcTrainingPatramDq171': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ171' 
 'EmcTrainingPatramDq172': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ172' 
 'EmcTrainingPatramDq173': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ173' 
 'EmcTrainingPatramDq174': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ174' 
 'EmcTrainingPatramDq175': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ175' 
 'EmcTrainingPatramDq176': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ176' 
 'EmcTrainingPatramDq177': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ177' 
 'EmcTrainingPatramDq178': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ178' 
 'EmcTrainingPatramDq179': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ179' 
 'EmcTrainingPatramDq180': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ180' 
 'EmcTrainingPatramDq181': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ181' 
 'EmcTrainingPatramDq182': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ182' 
 'EmcTrainingPatramDq183': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ183' 
 'EmcTrainingPatramDq184': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ184' 
 'EmcTrainingPatramDq185': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ185' 
 'EmcTrainingPatramDq186': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ186' 
 'EmcTrainingPatramDq187': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ187' 
 'EmcTrainingPatramDq188': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ188' 
 'EmcTrainingPatramDq189': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ189' 
 'EmcTrainingPatramDq190': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ190' 
 'EmcTrainingPatramDq191': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ191' 
 'EmcTrainingPatramDq192': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ192' 
 'EmcTrainingPatramDq193': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ193' 
 'EmcTrainingPatramDq194': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ194' 
 'EmcTrainingPatramDq195': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ195' 
 'EmcTrainingPatramDq196': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ196' 
 'EmcTrainingPatramDq197': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ197' 
 'EmcTrainingPatramDq198': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ198' 
 'EmcTrainingPatramDq199': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ199' 
 'EmcTrainingPatramDq200': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ200' 
 'EmcTrainingPatramDq201': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ201' 
 'EmcTrainingPatramDq202': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ202' 
 'EmcTrainingPatramDq203': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ203' 
 'EmcTrainingPatramDq204': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ204' 
 'EmcTrainingPatramDq205': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ205' 
 'EmcTrainingPatramDq206': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ206' 
 'EmcTrainingPatramDq207': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ207' 
 'EmcTrainingPatramDq208': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ208' 
 'EmcTrainingPatramDq209': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ209' 
 'EmcTrainingPatramDq210': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ210' 
 'EmcTrainingPatramDq211': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ211' 
 'EmcTrainingPatramDq212': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ212' 
 'EmcTrainingPatramDq213': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ213' 
 'EmcTrainingPatramDq214': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ214' 
 'EmcTrainingPatramDq215': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ215' 
 'EmcTrainingPatramDq216': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ216' 
 'EmcTrainingPatramDq217': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ217' 
 'EmcTrainingPatramDq218': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ218' 
 'EmcTrainingPatramDq219': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ219' 
 'EmcTrainingPatramDq220': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ220' 
 'EmcTrainingPatramDq221': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ221' 
 'EmcTrainingPatramDq222': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ222' 
 'EmcTrainingPatramDq223': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ223' 
 'EmcTrainingPatramDq224': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ224' 
 'EmcTrainingPatramDq225': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ225' 
 'EmcTrainingPatramDq226': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ226' 
 'EmcTrainingPatramDq227': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ227' 
 'EmcTrainingPatramDq228': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ228' 
 'EmcTrainingPatramDq229': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ229' 
 'EmcTrainingPatramDq230': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ230' 
 'EmcTrainingPatramDq231': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ231' 
 'EmcTrainingPatramDq232': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ232' 
 'EmcTrainingPatramDq233': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ233' 
 'EmcTrainingPatramDq234': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ234' 
 'EmcTrainingPatramDq235': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ235' 
 'EmcTrainingPatramDq236': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ236' 
 'EmcTrainingPatramDq237': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ237' 
 'EmcTrainingPatramDq238': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ238' 
 'EmcTrainingPatramDq239': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ239' 
 'EmcTrainingPatramDq240': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ240' 
 'EmcTrainingPatramDq241': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ241' 
 'EmcTrainingPatramDq242': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ242' 
 'EmcTrainingPatramDq243': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ243' 
 'EmcTrainingPatramDq244': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ244' 
 'EmcTrainingPatramDq245': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ245' 
 'EmcTrainingPatramDq246': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ246' 
 'EmcTrainingPatramDq247': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ247' 
 'EmcTrainingPatramDq248': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ248' 
 'EmcTrainingPatramDq249': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ249' 
 'EmcTrainingPatramDq250': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ250' 
 'EmcTrainingPatramDq251': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ251' 
 'EmcTrainingPatramDq252': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ252' 
 'EmcTrainingPatramDq253': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ253' 
 'EmcTrainingPatramDq254': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ254' 
 'EmcTrainingPatramDq255': 'NA', 'Specifies the value for TRAINING_PATRAM_DQ255' 
 'EmcTrainingPatramDmi0': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI0' 
 'EmcTrainingPatramDmi1': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI1' 
 'EmcTrainingPatramDmi2': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI2' 
 'EmcTrainingPatramDmi3': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI3' 
 'EmcTrainingPatramDmi4': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI4' 
 'EmcTrainingPatramDmi5': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI5' 
 'EmcTrainingPatramDmi6': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI6' 
 'EmcTrainingPatramDmi7': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI7' 
 'EmcTrainingPatramDmi8': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI8' 
 'EmcTrainingPatramDmi9': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI9' 
 'EmcTrainingPatramDmi10': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI10' 
 'EmcTrainingPatramDmi11': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI11' 
 'EmcTrainingPatramDmi12': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI12' 
 'EmcTrainingPatramDmi13': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI13' 
 'EmcTrainingPatramDmi14': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI14' 
 'EmcTrainingPatramDmi15': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI15' 
 'EmcTrainingPatramDmi16': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI16' 
 'EmcTrainingPatramDmi17': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI17' 
 'EmcTrainingPatramDmi18': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI18' 
 'EmcTrainingPatramDmi19': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI19' 
 'EmcTrainingPatramDmi20': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI20' 
 'EmcTrainingPatramDmi21': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI21' 
 'EmcTrainingPatramDmi22': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI22' 
 'EmcTrainingPatramDmi23': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI23' 
 'EmcTrainingPatramDmi24': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI24' 
 'EmcTrainingPatramDmi25': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI25' 
 'EmcTrainingPatramDmi26': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI26' 
 'EmcTrainingPatramDmi27': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI27' 
 'EmcTrainingPatramDmi28': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI28' 
 'EmcTrainingPatramDmi29': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI29' 
 'EmcTrainingPatramDmi30': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI30' 
 'EmcTrainingPatramDmi31': 'NA', 'Specifies the value for TRAINING_PATRAM_DMI31' 
 'EmcTrainingSpare0': 'NA', 'Specifies the value for TRAINING_SPARE0' 
 'EmcTrainingSpare1': 'NA', 'Specifies the value for TRAINING_SPARE1' 
 'EmcTrainingSpare2': 'NA', 'Specifies the value for TRAINING_SPARE2' 
 'EmcTrainingSpare3': 'NA', 'Specifies the value for TRAINING_SPARE3' 
 'EmcTrainingSpare4': 'NA', 'Specifies the value for TRAINING_SPARE4' 
 'EmcTrainingSpare5': 'NA', 'Specifies the value for TRAINING_SPARE5' 
 'EmcTrainingSpare6': 'NA', 'Specifies the value for TRAINING_SPARE6' 
 'EmcTrainingSpare7': 'NA', 'Specifies the value for TRAINING_SPARE7' 
 'EmcTrainingSpare8': 'NA', 'Specifies the value for TRAINING_SPARE8' 
 'EmcTrainingSpare9': 'NA', 'Specifies the value for TRAINING_SPARE9' 
 'EmcTrainingSpare10': 'NA', 'Specifies the value for TRAINING_SPARE10' 
 'EmcTrainingSpare11': 'NA', 'Specifies the value for TRAINING_SPARE11' 
 'EmcTrainingSpare12': 'NA', 'Specifies the value for TRAINING_SPARE12' 
 'EmcTrainingSpare13': 'NA', 'Specifies the value for TRAINING_SPARE13' 
 'EmcTrainingSpare14': 'NA', 'Specifies the value for TRAINING_SPARE14' 
 'EmcTrainingSpare15': 'NA', 'Specifies the value for TRAINING_SPARE15' 
 'EmcTrainingSpare16': 'NA', 'Specifies the value for TRAINING_SPARE16' 
 'EmcTrainingSpare17': 'NA', 'Specifies the value for TRAINING_SPARE17' 
 'EmcTrainingSpare18': 'NA', 'Specifies the value for TRAINING_SPARE18' 
 'EmcTrainingSpare19': 'NA', 'Specifies the value for TRAINING_SPARE19' 
 'EmcTrainingSpare20': 'NA', 'Specifies the value for TRAINING_SPARE20' 
 'EmcTrainingSpare21': 'NA', 'Specifies the value for TRAINING_SPARE21' 
 'EmcTrainingSpare22': 'NA', 'Specifies the value for TRAINING_SPARE22' 
 'EmcTrainingSpare23': 'NA', 'Specifies the value for TRAINING_SPARE23' 
 'EmcTrainingSpare24': 'NA', 'Specifies the value for TRAINING_SPARE24' 
 'EmcTrainingSpare25': 'NA', 'Specifies the value for TRAINING_SPARE25' 
 'EmcTrainingSpare26': 'NA', 'Specifies the value for TRAINING_SPARE26' 
 'EmcTrainingSpare27': 'NA', 'Specifies the value for TRAINING_SPARE27' 
 'EmcTrainingSpare28': 'NA', 'Specifies the value for TRAINING_SPARE28' 
 'EmcTrainingSpare29': 'NA', 'Specifies the value for TRAINING_SPARE29' 
 'EmcTrainingSpare30': 'NA', 'Specifies the value for TRAINING_SPARE30' 
 'EmcTrainingSpare31': 'NA', 'Specifies the value for TRAINING_SPARE31' 
 'McSidStreamidOverrideConfigPtcr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PTCR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PTCR' 
 'McSidStreamidSecurityConfigPtcr': 'MC_SID_STREAMID_SECURITY_CONFIG_PTCR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PTCR' 
 'McSidStreamidOverrideConfigHdar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_HDAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_HDAR' 
 'McSidStreamidSecurityConfigHdar': 'MC_SID_STREAMID_SECURITY_CONFIG_HDAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_HDAR' 
 'McSidStreamidOverrideConfigHost1xdmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_HOST1XDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_HOST1XDMAR' 
 'McSidStreamidSecurityConfigHost1xdmar': 'MC_SID_STREAMID_SECURITY_CONFIG_HOST1XDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_HOST1XDMAR' 
 'McSidStreamidOverrideConfigNvencsrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSRD' 
 'McSidStreamidSecurityConfigNvencsrd': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENCSRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENCSRD' 
 'McSidStreamidOverrideConfigSatar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SATAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SATAR' 
 'McSidStreamidSecurityConfigSatar': 'MC_SID_STREAMID_SECURITY_CONFIG_SATAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SATAR' 
 'McSidStreamidOverrideConfigMpcorer': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MPCORER_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MPCORER' 
 'McSidStreamidSecurityConfigMpcorer': 'MC_SID_STREAMID_SECURITY_CONFIG_MPCORER_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MPCORER' 
 'McSidStreamidOverrideConfigNvencswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSWR' 
 'McSidStreamidSecurityConfigNvencswr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENCSWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENCSWR' 
 'McSidStreamidOverrideConfigHdaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_HDAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_HDAW' 
 'McSidStreamidSecurityConfigHdaw': 'MC_SID_STREAMID_SECURITY_CONFIG_HDAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_HDAW' 
 'McSidStreamidOverrideConfigMpcorew': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MPCOREW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MPCOREW' 
 'McSidStreamidSecurityConfigMpcorew': 'MC_SID_STREAMID_SECURITY_CONFIG_MPCOREW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MPCOREW' 
 'McSidStreamidOverrideConfigSataw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SATAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SATAW' 
 'McSidStreamidSecurityConfigSataw': 'MC_SID_STREAMID_SECURITY_CONFIG_SATAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SATAW' 
 'McSidStreamidOverrideConfigIspra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPRA' 
 'McSidStreamidSecurityConfigIspra': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPRA' 
 'McSidStreamidOverrideConfigIspfalr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPFALR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPFALR' 
 'McSidStreamidSecurityConfigIspfalr': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPFALR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPFALR' 
 'McSidStreamidOverrideConfigIspwa': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPWA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPWA' 
 'McSidStreamidSecurityConfigIspwa': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPWA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPWA' 
 'McSidStreamidOverrideConfigIspwb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPWB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPWB' 
 'McSidStreamidSecurityConfigIspwb': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPWB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPWB' 
 'McSidStreamidOverrideConfigXusb_hostr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_HOSTR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_HOSTR' 
 'McSidStreamidSecurityConfigXusb_hostr': 'MC_SID_STREAMID_SECURITY_CONFIG_XUSB_HOSTR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_XUSB_HOSTR' 
 'McSidStreamidOverrideConfigXusb_hostw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_HOSTW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_HOSTW' 
 'McSidStreamidSecurityConfigXusb_hostw': 'MC_SID_STREAMID_SECURITY_CONFIG_XUSB_HOSTW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_XUSB_HOSTW' 
 'McSidStreamidOverrideConfigXusb_devr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_DEVR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_DEVR' 
 'McSidStreamidSecurityConfigXusb_devr': 'MC_SID_STREAMID_SECURITY_CONFIG_XUSB_DEVR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_XUSB_DEVR' 
 'McSidStreamidOverrideConfigXusb_devw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_DEVW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_XUSB_DEVW' 
 'McSidStreamidSecurityConfigXusb_devw': 'MC_SID_STREAMID_SECURITY_CONFIG_XUSB_DEVW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_XUSB_DEVW' 
 'McSidStreamidOverrideConfigTsecsrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSRD' 
 'McSidStreamidSecurityConfigTsecsrd': 'MC_SID_STREAMID_SECURITY_CONFIG_TSECSRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_TSECSRD' 
 'McSidStreamidOverrideConfigTsecswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSWR' 
 'McSidStreamidSecurityConfigTsecswr': 'MC_SID_STREAMID_SECURITY_CONFIG_TSECSWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_TSECSWR' 
 'McSidStreamidOverrideConfigSdmmcra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCRA' 
 'McSidStreamidSecurityConfigSdmmcra': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCRA' 
 'McSidStreamidOverrideConfigSdmmcr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCR' 
 'McSidStreamidSecurityConfigSdmmcr': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCR' 
 'McSidStreamidOverrideConfigSdmmcrab': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCRAB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCRAB' 
 'McSidStreamidSecurityConfigSdmmcrab': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCRAB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCRAB' 
 'McSidStreamidOverrideConfigSdmmcwa': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCWA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCWA' 
 'McSidStreamidSecurityConfigSdmmcwa': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCWA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCWA' 
 'McSidStreamidOverrideConfigSdmmcw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCW' 
 'McSidStreamidSecurityConfigSdmmcw': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCW' 
 'McSidStreamidOverrideConfigSdmmcwab': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCWAB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SDMMCWAB' 
 'McSidStreamidSecurityConfigSdmmcwab': 'MC_SID_STREAMID_SECURITY_CONFIG_SDMMCWAB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SDMMCWAB' 
 'McSidStreamidOverrideConfigVicsrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VICSRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VICSRD' 
 'McSidStreamidSecurityConfigVicsrd': 'MC_SID_STREAMID_SECURITY_CONFIG_VICSRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VICSRD' 
 'McSidStreamidOverrideConfigVicswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VICSWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VICSWR' 
 'McSidStreamidSecurityConfigVicswr': 'MC_SID_STREAMID_SECURITY_CONFIG_VICSWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VICSWR' 
 'McSidStreamidOverrideConfigViw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VIW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VIW' 
 'McSidStreamidSecurityConfigViw': 'MC_SID_STREAMID_SECURITY_CONFIG_VIW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VIW' 
 'McSidStreamidOverrideConfigNvdecsrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSRD' 
 'McSidStreamidSecurityConfigNvdecsrd': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDECSRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDECSRD' 
 'McSidStreamidOverrideConfigNvdecswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSWR' 
 'McSidStreamidSecurityConfigNvdecswr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDECSWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDECSWR' 
 'McSidStreamidOverrideConfigAper': 'MC_SID_STREAMID_OVERRIDE_CONFIG_APER_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_APER' 
 'McSidStreamidSecurityConfigAper': 'MC_SID_STREAMID_SECURITY_CONFIG_APER_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_APER' 
 'McSidStreamidOverrideConfigApew': 'MC_SID_STREAMID_OVERRIDE_CONFIG_APEW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_APEW' 
 'McSidStreamidSecurityConfigApew': 'MC_SID_STREAMID_SECURITY_CONFIG_APEW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_APEW' 
 'McSidStreamidOverrideConfigNvjpgsrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVJPGSRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVJPGSRD' 
 'McSidStreamidSecurityConfigNvjpgsrd': 'MC_SID_STREAMID_SECURITY_CONFIG_NVJPGSRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVJPGSRD' 
 'McSidStreamidOverrideConfigNvjpgswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVJPGSWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVJPGSWR' 
 'McSidStreamidSecurityConfigNvjpgswr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVJPGSWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVJPGSWR' 
 'McSidStreamidOverrideConfigSesrd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SESRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SESRD' 
 'McSidStreamidSecurityConfigSesrd': 'MC_SID_STREAMID_SECURITY_CONFIG_SESRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SESRD' 
 'McSidStreamidOverrideConfigSeswr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SESWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SESWR' 
 'McSidStreamidSecurityConfigSeswr': 'MC_SID_STREAMID_SECURITY_CONFIG_SESWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SESWR' 
 'McSidStreamidOverrideConfigAxiapr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AXIAPR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AXIAPR' 
 'McSidStreamidSecurityConfigAxiapr': 'MC_SID_STREAMID_SECURITY_CONFIG_AXIAPR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AXIAPR' 
 'McSidStreamidOverrideConfigAxiapw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AXIAPW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AXIAPW' 
 'McSidStreamidSecurityConfigAxiapw': 'MC_SID_STREAMID_SECURITY_CONFIG_AXIAPW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AXIAPW' 
 'McSidStreamidOverrideConfigEtrr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ETRR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ETRR' 
 'McSidStreamidSecurityConfigEtrr': 'MC_SID_STREAMID_SECURITY_CONFIG_ETRR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ETRR' 
 'McSidStreamidOverrideConfigEtrw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ETRW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ETRW' 
 'McSidStreamidSecurityConfigEtrw': 'MC_SID_STREAMID_SECURITY_CONFIG_ETRW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ETRW' 
 'McSidStreamidOverrideConfigTsecsrdb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSRDB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSRDB' 
 'McSidStreamidSecurityConfigTsecsrdb': 'MC_SID_STREAMID_SECURITY_CONFIG_TSECSRDB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_TSECSRDB' 
 'McSidStreamidOverrideConfigTsecswrb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSWRB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_TSECSWRB' 
 'McSidStreamidSecurityConfigTsecswrb': 'MC_SID_STREAMID_SECURITY_CONFIG_TSECSWRB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_TSECSWRB' 
 'McSidStreamidOverrideConfigAxisr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AXISR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AXISR' 
 'McSidStreamidSecurityConfigAxisr': 'MC_SID_STREAMID_SECURITY_CONFIG_AXISR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AXISR' 
 'McSidStreamidOverrideConfigAxisw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AXISW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AXISW' 
 'McSidStreamidSecurityConfigAxisw': 'MC_SID_STREAMID_SECURITY_CONFIG_AXISW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AXISW' 
 'McSidStreamidOverrideConfigEqosr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_EQOSR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_EQOSR' 
 'McSidStreamidSecurityConfigEqosr': 'MC_SID_STREAMID_SECURITY_CONFIG_EQOSR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_EQOSR' 
 'McSidStreamidOverrideConfigEqosw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_EQOSW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_EQOSW' 
 'McSidStreamidSecurityConfigEqosw': 'MC_SID_STREAMID_SECURITY_CONFIG_EQOSW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_EQOSW' 
 'McSidStreamidOverrideConfigUfshcr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_UFSHCR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_UFSHCR' 
 'McSidStreamidSecurityConfigUfshcr': 'MC_SID_STREAMID_SECURITY_CONFIG_UFSHCR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_UFSHCR' 
 'McSidStreamidOverrideConfigUfshcw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_UFSHCW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_UFSHCW' 
 'McSidStreamidSecurityConfigUfshcw': 'MC_SID_STREAMID_SECURITY_CONFIG_UFSHCW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_UFSHCW' 
 'McSidStreamidOverrideConfigNvdisplayr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDISPLAYR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDISPLAYR' 
 'McSidStreamidSecurityConfigNvdisplayr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDISPLAYR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDISPLAYR' 
 'McSidStreamidOverrideConfigBpmpr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPR' 
 'McSidStreamidSecurityConfigBpmpr': 'MC_SID_STREAMID_SECURITY_CONFIG_BPMPR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_BPMPR' 
 'McSidStreamidOverrideConfigBpmpw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPW' 
 'McSidStreamidSecurityConfigBpmpw': 'MC_SID_STREAMID_SECURITY_CONFIG_BPMPW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_BPMPW' 
 'McSidStreamidOverrideConfigBpmpdmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPDMAR' 
 'McSidStreamidSecurityConfigBpmpdmar': 'MC_SID_STREAMID_SECURITY_CONFIG_BPMPDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_BPMPDMAR' 
 'McSidStreamidOverrideConfigBpmpdmaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPDMAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_BPMPDMAW' 
 'McSidStreamidSecurityConfigBpmpdmaw': 'MC_SID_STREAMID_SECURITY_CONFIG_BPMPDMAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_BPMPDMAW' 
 'McSidStreamidOverrideConfigAonr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AONR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AONR' 
 'McSidStreamidSecurityConfigAonr': 'MC_SID_STREAMID_SECURITY_CONFIG_AONR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AONR' 
 'McSidStreamidOverrideConfigAonw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AONW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AONW' 
 'McSidStreamidSecurityConfigAonw': 'MC_SID_STREAMID_SECURITY_CONFIG_AONW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AONW' 
 'McSidStreamidOverrideConfigAondmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AONDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AONDMAR' 
 'McSidStreamidSecurityConfigAondmar': 'MC_SID_STREAMID_SECURITY_CONFIG_AONDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AONDMAR' 
 'McSidStreamidOverrideConfigAondmaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_AONDMAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_AONDMAW' 
 'McSidStreamidSecurityConfigAondmaw': 'MC_SID_STREAMID_SECURITY_CONFIG_AONDMAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_AONDMAW' 
 'McSidStreamidOverrideConfigScer': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SCER_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SCER' 
 'McSidStreamidSecurityConfigScer': 'MC_SID_STREAMID_SECURITY_CONFIG_SCER_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SCER' 
 'McSidStreamidOverrideConfigScew': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SCEW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SCEW' 
 'McSidStreamidSecurityConfigScew': 'MC_SID_STREAMID_SECURITY_CONFIG_SCEW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SCEW' 
 'McSidStreamidOverrideConfigScedmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SCEDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SCEDMAR' 
 'McSidStreamidSecurityConfigScedmar': 'MC_SID_STREAMID_SECURITY_CONFIG_SCEDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SCEDMAR' 
 'McSidStreamidOverrideConfigScedmaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_SCEDMAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_SCEDMAW' 
 'McSidStreamidSecurityConfigScedmaw': 'MC_SID_STREAMID_SECURITY_CONFIG_SCEDMAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_SCEDMAW' 
 'McSidStreamidOverrideConfigApedmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_APEDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_APEDMAR' 
 'McSidStreamidSecurityConfigApedmar': 'MC_SID_STREAMID_SECURITY_CONFIG_APEDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_APEDMAR' 
 'McSidStreamidOverrideConfigApedmaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_APEDMAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_APEDMAW' 
 'McSidStreamidSecurityConfigApedmaw': 'MC_SID_STREAMID_SECURITY_CONFIG_APEDMAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_APEDMAW' 
 'McSidStreamidOverrideConfigNvdisplayr1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDISPLAYR1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDISPLAYR1' 
 'McSidStreamidSecurityConfigNvdisplayr1': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDISPLAYR1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDISPLAYR1' 
 'McSidStreamidOverrideConfigVicsrd1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VICSRD1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VICSRD1' 
 'McSidStreamidSecurityConfigVicsrd1': 'MC_SID_STREAMID_SECURITY_CONFIG_VICSRD1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VICSRD1' 
 'McSidStreamidOverrideConfigNvdecsrd1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSRD1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDECSRD1' 
 'McSidStreamidSecurityConfigNvdecsrd1': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDECSRD1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDECSRD1' 
 'McSidStreamidOverrideConfigVifalr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VIFALR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VIFALR' 
 'McSidStreamidSecurityConfigVifalr': 'MC_SID_STREAMID_SECURITY_CONFIG_VIFALR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VIFALR' 
 'McSidStreamidOverrideConfigVifalw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_VIFALW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_VIFALW' 
 'McSidStreamidSecurityConfigVifalw': 'MC_SID_STREAMID_SECURITY_CONFIG_VIFALW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_VIFALW' 
 'McSidStreamidOverrideConfigDla0rda': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0RDA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0RDA' 
 'McSidStreamidSecurityConfigDla0rda': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA0RDA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA0RDA' 
 'McSidStreamidOverrideConfigDla0falrdb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0FALRDB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0FALRDB' 
 'McSidStreamidSecurityConfigDla0falrdb': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA0FALRDB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA0FALRDB' 
 'McSidStreamidOverrideConfigDla0wra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0WRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0WRA' 
 'McSidStreamidSecurityConfigDla0wra': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA0WRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA0WRA' 
 'McSidStreamidOverrideConfigDla0falwrb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0FALWRB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0FALWRB' 
 'McSidStreamidSecurityConfigDla0falwrb': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA0FALWRB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA0FALWRB' 
 'McSidStreamidOverrideConfigDla1rda': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1RDA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1RDA' 
 'McSidStreamidSecurityConfigDla1rda': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA1RDA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA1RDA' 
 'McSidStreamidOverrideConfigDla1falrdb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1FALRDB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1FALRDB' 
 'McSidStreamidSecurityConfigDla1falrdb': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA1FALRDB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA1FALRDB' 
 'McSidStreamidOverrideConfigDla1wra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1WRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1WRA' 
 'McSidStreamidSecurityConfigDla1wra': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA1WRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA1WRA' 
 'McSidStreamidOverrideConfigDla1falwrb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1FALWRB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1FALWRB' 
 'McSidStreamidSecurityConfigDla1falwrb': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA1FALWRB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA1FALWRB' 
 'McSidStreamidOverrideConfigPva0rda': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDA' 
 'McSidStreamidSecurityConfigPva0rda': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDA' 
 'McSidStreamidOverrideConfigPva0rdb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDB' 
 'McSidStreamidSecurityConfigPva0rdb': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDB' 
 'McSidStreamidOverrideConfigPva0rdc': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDC_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDC' 
 'McSidStreamidSecurityConfigPva0rdc': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDC_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDC' 
 'McSidStreamidOverrideConfigPva0wra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRA' 
 'McSidStreamidSecurityConfigPva0wra': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRA' 
 'McSidStreamidOverrideConfigPva0wrb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRB' 
 'McSidStreamidSecurityConfigPva0wrb': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRB' 
 'McSidStreamidOverrideConfigPva0wrc': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRC_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0WRC' 
 'McSidStreamidSecurityConfigPva0wrc': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRC_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0WRC' 
 'McSidStreamidOverrideConfigPva1rda': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDA' 
 'McSidStreamidSecurityConfigPva1rda': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDA' 
 'McSidStreamidOverrideConfigPva1rdb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDB' 
 'McSidStreamidSecurityConfigPva1rdb': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDB' 
 'McSidStreamidOverrideConfigPva1rdc': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDC_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDC' 
 'McSidStreamidSecurityConfigPva1rdc': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDC_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDC' 
 'McSidStreamidOverrideConfigPva1wra': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRA_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRA' 
 'McSidStreamidSecurityConfigPva1wra': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRA_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRA' 
 'McSidStreamidOverrideConfigPva1wrb': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRB_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRB' 
 'McSidStreamidSecurityConfigPva1wrb': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRB_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRB' 
 'McSidStreamidOverrideConfigPva1wrc': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRC_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1WRC' 
 'McSidStreamidSecurityConfigPva1wrc': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRC_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1WRC' 
 'McSidStreamidOverrideConfigRcer': 'MC_SID_STREAMID_OVERRIDE_CONFIG_RCER_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_RCER' 
 'McSidStreamidSecurityConfigRcer': 'MC_SID_STREAMID_SECURITY_CONFIG_RCER_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_RCER' 
 'McSidStreamidOverrideConfigRcew': 'MC_SID_STREAMID_OVERRIDE_CONFIG_RCEW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_RCEW' 
 'McSidStreamidSecurityConfigRcew': 'MC_SID_STREAMID_SECURITY_CONFIG_RCEW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_RCEW' 
 'McSidStreamidOverrideConfigRcedmar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_RCEDMAR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_RCEDMAR' 
 'McSidStreamidSecurityConfigRcedmar': 'MC_SID_STREAMID_SECURITY_CONFIG_RCEDMAR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_RCEDMAR' 
 'McSidStreamidOverrideConfigRcedmaw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_RCEDMAW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_RCEDMAW' 
 'McSidStreamidSecurityConfigRcedmaw': 'MC_SID_STREAMID_SECURITY_CONFIG_RCEDMAW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_RCEDMAW' 
 'McSidStreamidOverrideConfigNvenc1srd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SRD' 
 'McSidStreamidSecurityConfigNvenc1srd': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SRD' 
 'McSidStreamidOverrideConfigNvenc1swr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SWR' 
 'McSidStreamidSecurityConfigNvenc1swr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SWR' 
 'McSidStreamidOverrideConfigPcie0r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0R' 
 'McSidStreamidSecurityConfigPcie0r': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE0R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE0R' 
 'McSidStreamidOverrideConfigPcie0w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0W' 
 'McSidStreamidSecurityConfigPcie0w': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE0W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE0W' 
 'McSidStreamidOverrideConfigPcie1r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE1R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE1R' 
 'McSidStreamidSecurityConfigPcie1r': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE1R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE1R' 
 'McSidStreamidOverrideConfigPcie1w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE1W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE1W' 
 'McSidStreamidSecurityConfigPcie1w': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE1W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE1W' 
 'McSidStreamidOverrideConfigPcie2ar': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE2AR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE2AR' 
 'McSidStreamidSecurityConfigPcie2ar': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE2AR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE2AR' 
 'McSidStreamidOverrideConfigPcie2aw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE2AW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE2AW' 
 'McSidStreamidSecurityConfigPcie2aw': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE2AW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE2AW' 
 'McSidStreamidOverrideConfigPcie3r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE3R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE3R' 
 'McSidStreamidSecurityConfigPcie3r': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE3R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE3R' 
 'McSidStreamidOverrideConfigPcie3w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE3W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE3W' 
 'McSidStreamidSecurityConfigPcie3w': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE3W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE3W' 
 'McSidStreamidOverrideConfigPcie4r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE4R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE4R' 
 'McSidStreamidSecurityConfigPcie4r': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE4R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE4R' 
 'McSidStreamidOverrideConfigPcie4w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE4W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE4W' 
 'McSidStreamidSecurityConfigPcie4w': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE4W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE4W' 
 'McSidStreamidOverrideConfigPcie5r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5R' 
 'McSidStreamidSecurityConfigPcie5r': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE5R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE5R' 
 'McSidStreamidOverrideConfigPcie5w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5W' 
 'McSidStreamidSecurityConfigPcie5w': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE5W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE5W' 
 'McSidStreamidOverrideConfigIspfalw': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPFALW_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPFALW' 
 'McSidStreamidSecurityConfigIspfalw': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPFALW_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPFALW' 
 'McSidStreamidOverrideConfigDla0rda1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0RDA1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA0RDA1' 
 'McSidStreamidSecurityConfigDla0rda1': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA0RDA1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA0RDA1' 
 'McSidStreamidOverrideConfigDla1rda1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1RDA1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_DLA1RDA1' 
 'McSidStreamidSecurityConfigDla1rda1': 'MC_SID_STREAMID_SECURITY_CONFIG_DLA1RDA1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_DLA1RDA1' 
 'McSidStreamidOverrideConfigPva0rda1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDA1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDA1' 
 'McSidStreamidSecurityConfigPva0rda1': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDA1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDA1' 
 'McSidStreamidOverrideConfigPva0rdb1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDB1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA0RDB1' 
 'McSidStreamidSecurityConfigPva0rdb1': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDB1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA0RDB1' 
 'McSidStreamidOverrideConfigPva1rda1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDA1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDA1' 
 'McSidStreamidSecurityConfigPva1rda1': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDA1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDA1' 
 'McSidStreamidOverrideConfigPva1rdb1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDB1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PVA1RDB1' 
 'McSidStreamidSecurityConfigPva1rdb1': 'MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDB1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PVA1RDB1' 
 'McSidStreamidOverrideConfigPcie5r1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5R1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE5R1' 
 'McSidStreamidSecurityConfigPcie5r1': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE5R1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE5R1' 
 'McSidStreamidOverrideConfigNvencsrd1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSRD1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENCSRD1' 
 'McSidStreamidSecurityConfigNvencsrd1': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENCSRD1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENCSRD1' 
 'McSidStreamidOverrideConfigNvenc1srd1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SRD1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVENC1SRD1' 
 'McSidStreamidSecurityConfigNvenc1srd1': 'MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SRD1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVENC1SRD1' 
 'McSidStreamidOverrideConfigIspra1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_ISPRA1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_ISPRA1' 
 'McSidStreamidSecurityConfigIspra1': 'MC_SID_STREAMID_SECURITY_CONFIG_ISPRA1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_ISPRA1' 
 'McSidStreamidOverrideConfigPcie0r1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0R1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_PCIE0R1' 
 'McSidStreamidSecurityConfigPcie0r1': 'MC_SID_STREAMID_SECURITY_CONFIG_PCIE0R1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_PCIE0R1' 
 'McSidStreamidOverrideConfigNvdec1srd': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SRD_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SRD' 
 'McSidStreamidSecurityConfigNvdec1srd': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SRD_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SRD' 
 'McSidStreamidOverrideConfigNvdec1srd1': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SRD1_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SRD1' 
 'McSidStreamidSecurityConfigNvdec1srd1': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SRD1_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SRD1' 
 'McSidStreamidOverrideConfigNvdec1swr': 'MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SWR_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_NVDEC1SWR' 
 'McSidStreamidSecurityConfigNvdec1swr': 'MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SWR_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_NVDEC1SWR' 
 'McSidStreamidOverrideConfigMiu0r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU0R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU0R' 
 'McSidStreamidSecurityConfigMiu0r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU0R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU0R' 
 'McSidStreamidOverrideConfigMiu0w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU0W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU0W' 
 'McSidStreamidSecurityConfigMiu0w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU0W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU0W' 
 'McSidStreamidOverrideConfigMiu1r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU1R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU1R' 
 'McSidStreamidSecurityConfigMiu1r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU1R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU1R' 
 'McSidStreamidOverrideConfigMiu1w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU1W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU1W' 
 'McSidStreamidSecurityConfigMiu1w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU1W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU1W' 
 'McSidStreamidOverrideConfigMiu2r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU2R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU2R' 
 'McSidStreamidSecurityConfigMiu2r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU2R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU2R' 
 'McSidStreamidOverrideConfigMiu2w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU2W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU2W' 
 'McSidStreamidSecurityConfigMiu2w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU2W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU2W' 
 'McSidStreamidOverrideConfigMiu3r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU3R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU3R' 
 'McSidStreamidSecurityConfigMiu3r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU3R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU3R' 
 'McSidStreamidOverrideConfigMiu3w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU3W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU3W' 
 'McSidStreamidSecurityConfigMiu3w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU3W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU3W' 
 'McSidStreamidOverrideConfigMiu4r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU4R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU4R' 
 'McSidStreamidSecurityConfigMiu4r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU4R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU4R' 
 'McSidStreamidOverrideConfigMiu4w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU4W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU4W' 
 'McSidStreamidSecurityConfigMiu4w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU4W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU4W' 
 'McSidStreamidOverrideConfigMiu5r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU5R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU5R' 
 'McSidStreamidSecurityConfigMiu5r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU5R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU5R' 
 'McSidStreamidOverrideConfigMiu5w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU5W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU5W' 
 'McSidStreamidSecurityConfigMiu5w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU5W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU5W' 
 'McSidStreamidOverrideConfigMiu6r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU6R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU6R' 
 'McSidStreamidSecurityConfigMiu6r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU6R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU6R' 
 'McSidStreamidOverrideConfigMiu6w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU6W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU6W' 
 'McSidStreamidSecurityConfigMiu6w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU6W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU6W' 
 'McSidStreamidOverrideConfigMiu7r': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU7R_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU7R' 
 'McSidStreamidSecurityConfigMiu7r': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU7R_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU7R' 
 'McSidStreamidOverrideConfigMiu7w': 'MC_SID_STREAMID_OVERRIDE_CONFIG_MIU7W_0', 'Specifies the value for MC_SID_STREAMID_OVERRIDE_CONFIG_MIU7W' 
 'McSidStreamidSecurityConfigMiu7w': 'MC_SID_STREAMID_SECURITY_CONFIG_MIU7W_0', 'Specifies the value for MC_SID_STREAMID_SECURITY_CONFIG_MIU7W' 
 'EmcPmacroAutocalCfg0_4': 'EMC_PMACRO_AUTOCAL_CFG_0_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_0_CH2' 
 'EmcPmacroAutocalCfg2_4': 'EMC_PMACRO_AUTOCAL_CFG_2_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_2_CH2' 
 'EmcMrw9_4': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcMrw9_6': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcCmdMappingCmd0_0_4': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_0_6': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_4': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_6': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_4': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_6': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd1_0_4': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_0_6': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_4': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_6': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_4': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_6': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingByte_4': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcCmdMappingByte_6': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcPmacroObDdllLongDqRank0_0_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_0_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_4': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_6': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_4': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_6': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_4': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_6': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_4': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_6': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_4': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_6': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_4': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_6': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroDdllLongCmd_0_4': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_0_6': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_1_4': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_1_6': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_2_4': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_2_6': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_3_4': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_3_6': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_4_4': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcPmacroDdllLongCmd_4_6': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcSwizzleRank0Byte0_4': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte0_6': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte1_4': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte1_6': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte2_4': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte2_6': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte3_4': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcSwizzleRank0Byte3_6': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcPmcScratch1_4': 'EMC_PMC_SCRATCH1_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch2_4': 'EMC_PMC_SCRATCH2_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch3_4': 'EMC_PMC_SCRATCH3_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcDataBrlshft0_4': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft0_6': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft1_4': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcDataBrlshft1_6': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcPmacroBrickMapping0_4': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping0_6': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping1_4': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroBrickMapping1_6': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroAutocalCfg0_8': 'EMC_PMACRO_AUTOCAL_CFG_0_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_0_CH2' 
 'EmcPmacroAutocalCfg0_12': 'EMC_PMACRO_AUTOCAL_CFG_0_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_0_CH2' 
 'EmcPmacroAutocalCfg2_8': 'EMC_PMACRO_AUTOCAL_CFG_2_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_2_CH2' 
 'EmcPmacroAutocalCfg2_12': 'EMC_PMACRO_AUTOCAL_CFG_2_0', 'Specifies the value for EMC_PMACRO_AUTOCAL_CFG_2_CH2' 
 'EmcMrw9_8': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcMrw9_10': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcMrw9_12': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcMrw9_14': 'EMC_MRW9_0', 'Specifies the programming to LPDDR4 Mode Register 11 at cold boot CH2' 
 'EmcCmdMappingCmd0_0_8': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_0_10': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_0_12': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_0_14': 'EMC_CMD_MAPPING_CMD0_0_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_8': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_10': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_12': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_1_14': 'EMC_CMD_MAPPING_CMD0_1_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_8': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_10': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_12': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd0_2_14': 'EMC_CMD_MAPPING_CMD0_2_0', 'Command mapping for CMD brick 0' 
 'EmcCmdMappingCmd1_0_8': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_0_10': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_0_12': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_0_14': 'EMC_CMD_MAPPING_CMD1_0_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_8': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_10': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_12': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_1_14': 'EMC_CMD_MAPPING_CMD1_1_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_8': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_10': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_12': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingCmd1_2_14': 'EMC_CMD_MAPPING_CMD1_2_0', 'Command mapping for CMD brick 1' 
 'EmcCmdMappingByte_8': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcCmdMappingByte_10': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcCmdMappingByte_12': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcCmdMappingByte_14': 'EMC_CMD_MAPPING_BYTE_0', 'Command mapping for DATA bricks' 
 'EmcPmacroObDdllLongDqRank0_0_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_0_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_0_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_0_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_1_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_4_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank0_5_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_0_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_1_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_4_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_8': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_10': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_12': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqRank1_5_14': 'EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_5_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQ_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_0_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_1_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank0_4_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK0_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_0_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_0_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_1_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_1_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_8': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_10': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_12': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroObDdllLongDqsRank1_4_14': 'EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_0', 'Specifies the value for EMC_PMACRO_OB_DDLL_LONG_DQS_RANK1_4_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_8': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_10': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_12': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_0_14': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_8': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_10': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_12': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank0_1_14': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_8': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_10': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_12': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_0_14': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_0_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_0_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_8': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_10': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_12': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroIbDdllLongDqsRank1_1_14': 'EMC_PMACRO_IB_DDLL_LONG_DQS_RANK1_1_0', 'Specifies the value for EMC_PMACRO_IB_DDLL_LONG_DQS_RANK0_1_CH2' 
 'EmcPmacroDdllLongCmd_0_8': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_0_10': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_0_12': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_0_14': 'EMC_PMACRO_DDLL_LONG_CMD_0_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_0_CH2' 
 'EmcPmacroDdllLongCmd_1_8': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_1_10': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_1_12': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_1_14': 'EMC_PMACRO_DDLL_LONG_CMD_1_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_1_CH2' 
 'EmcPmacroDdllLongCmd_2_8': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_2_10': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_2_12': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_2_14': 'EMC_PMACRO_DDLL_LONG_CMD_2_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_2_CH2' 
 'EmcPmacroDdllLongCmd_3_8': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_3_10': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_3_12': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_3_14': 'EMC_PMACRO_DDLL_LONG_CMD_3_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_3_CH2' 
 'EmcPmacroDdllLongCmd_4_8': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcPmacroDdllLongCmd_4_10': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcPmacroDdllLongCmd_4_12': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcPmacroDdllLongCmd_4_14': 'EMC_PMACRO_DDLL_LONG_CMD_4_0', 'Specifies the value for EMC_PMACRO_DDLL_LONG_CMD_4_CH2' 
 'EmcSwizzleRank0Byte0_8': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte0_10': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte0_12': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte0_14': 'EMC_SWIZZLE_RANK0_BYTE0_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE0_CH2' 
 'EmcSwizzleRank0Byte1_8': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte1_10': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte1_12': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte1_14': 'EMC_SWIZZLE_RANK0_BYTE1_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE1_CH2' 
 'EmcSwizzleRank0Byte2_8': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte2_10': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte2_12': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte2_14': 'EMC_SWIZZLE_RANK0_BYTE2_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE2_CH2' 
 'EmcSwizzleRank0Byte3_8': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcSwizzleRank0Byte3_10': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcSwizzleRank0Byte3_12': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcSwizzleRank0Byte3_14': 'EMC_SWIZZLE_RANK0_BYTE3_0', 'Specifies the value for EMC_SWIZZLE_RANK0_BYTE3_CH2' 
 'EmcDataBrlshft0_8': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft0_10': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft0_12': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft0_14': 'EMC_DATA_BRLSHFT_0_0', 'Specifies the value for EMC_DATA_BRLSHFT_0_CH2' 
 'EmcDataBrlshft1_8': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcDataBrlshft1_10': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcDataBrlshft1_12': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcDataBrlshft1_14': 'EMC_DATA_BRLSHFT_1_0', 'Specifies the value for EMC_DATA_BRLSHFT_1_CH2' 
 'EmcPmcScratch1_8': 'EMC_PMC_SCRATCH1_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch1_12': 'EMC_PMC_SCRATCH1_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch2_8': 'EMC_PMC_SCRATCH2_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch2_12': 'EMC_PMC_SCRATCH2_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch3_8': 'EMC_PMC_SCRATCH3_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmcScratch3_12': 'EMC_PMC_SCRATCH3_0', 'Specifiy scratch values for PMC setup at warmboot' 
 'EmcPmacroBrickMapping0_8': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping0_10': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping0_12': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping0_14': 'EMC_PMACRO_BRICK_MAPPING_0_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_0_CH2' 
 'EmcPmacroBrickMapping1_8': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroBrickMapping1_10': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroBrickMapping1_12': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
 'EmcPmacroBrickMapping1_14': 'EMC_PMACRO_BRICK_MAPPING_1_0', 'Specifies the value for EMC_PMACRO_BRICK_MAPPING_1_CH2' 
END2
}
