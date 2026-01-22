#!/usr/bin/python3
# Copyright (c) 2022-2024, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

from abc import ABC, abstractmethod
import argparse
import subprocess
import sys
import logging
import shutil
import os.path
from multiprocessing import Pool
import tempfile
import glob
import re
import time
import traceback
import hashlib
import json


from flashtools_nverror import nverror, AbnormalTermination
from customer_data_parser import CustomerDataProcessor

logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s [%(name)s] %(levelname)s:%(message)s')
logger = logging.getLogger("Post Processing Tool")


class CommandFactory():
    """Factory class to extract and create command objects
    """
    command_registry = {}

    @classmethod
    def register(cls, cmd_name, cmd_desc, required_args, optional_args=[], not_required_args=[]):
        """Register commands and build a factory

        Args:
            cmd_name (str): Name of the command
            cmd_desc (str): Description of the command
            required_args (list): List of regex pattern for required arguments to execute the command
            optional_args (list, optional): List of optional arguments for the command. Defaults to [].
            not_required_args (list, optional): List of regular expression arguments which should not be
                                                present for triggering the command
        """
        def register_wrapper(command_class):
            if (cmd_name in cls.command_registry):
                logger.warning(f"Command {cmd_name} already registered")

            cls.command_registry[cmd_name] = {
                "cls": command_class,
                "required_args": required_args,
                "command_description": cmd_desc,
                "optional_args": optional_args,
                "not_required_args": not_required_args
            }

            return command_class

        return register_wrapper

    @classmethod
    def all_supported_commands(cls):
        """Get all the supported commands and their required arguments

        Returns:
            list[list]: [
                            [command name,
                            [list of required arguments for the command],
                            [list of not required arguments for the command]
                        ], ...]
        """
        return [
            [cmd_name,
             cls.command_registry[cmd_name]['required_args'],
             cls.command_registry[cmd_name]['not_required_args']
            ] for cmd_name in cls.command_registry.keys()
        ]

    @classmethod
    def get_command_object(cls, cmd_name, sysv_args):
        """Get the command object including its arguments
           from command line argument dictionary

        Args:
            cmd_name (str): Name of the command
            sysv_args (dict): Dictionary of command line argument and values

        Returns:
            Command: Command object
        """
        cmd_class = cls.command_registry[cmd_name]["cls"]
        required_args = cls.command_registry[cmd_name]["required_args"]
        command_description = cls.command_registry[cmd_name]["command_description"]
        optional_args = cls.command_registry[cmd_name]["optional_args"]

        # Extract the dictionary of args for the given command
        cmd_args = cls._extract_command_args(
            required_args, optional_args, sysv_args)

        return cmd_class(command_description, cmd_name, cmd_args)

    @classmethod
    def _extract_command_args(cls, required_args, optional_args, sysv_args):
        """Get all the arguments needed to execute the command

        Args:
            required_args (list): List of required arguments for the command
            optional_args (list): List of optional arguments for the command
            sysv_args (dict): Dictionary of command line arguments specified
                              by the user

        Returns:
            dict: Dictionary of all the arguments needed to execute the command
        """
        return {key: sysv_args[key] for key in required_args[:] + optional_args if key in sysv_args}

    @classmethod
    def _get_arg(cls, required_args, optional_args, sysv_args):
        """Get all the arguments needed to execute the command

        Args:
            required_args (list): List of required arguments for the command
            optional_args (list): List of optional arguments for the command
            sysv_args (dict): Dictionary of command line arguments specified
                              by the user

        Returns:
            dict: Dictionary of all the arguments needed to execute the command
        """
        return {key: sysv_args[key] for key in required_args[:] + optional_args if key in sysv_args}

class Command(ABC):
    """Abstract base class for the command
    """

    def __init__(self, description, name, args):
        self.name = name
        self.description = description
        self.args = args

    def preprocess(self):
        """Perform any preprocessing steps needed
           before executing the command
        """
        return

    @abstractmethod
    def execute(self):
        """Execute the command
        """
        raise AbnormalTermination(
            "Can't execute abstractmethod!", nverror.NvError_InvalidState)

    def cleanup(self):
        """Perform any clean up needed after executing the command
        """
        return

class SequentialCompositeCommand(Command):
    """Class to allow executing commands sequentially.
       Concrete command classes can inherit from this base class
       and add child commands which are executed sequentially.
    """
    def __init__(self, description, name, args):
        super(SequentialCompositeCommand, self).__init__(description, name, args)
        self.command_list = []

    def add_child(self, cmd_ob):
        """Add child commands to the command list for sequential execution

        Args:
            cmd_ob (Command): Object of sub-class of Command class
        """
        self.command_list.append(cmd_ob)

    @abstractmethod
    def gather_commands(self):
        """Subclass will override this method to add child commands
        """
        AbnormalTermination("Can't call abstract method", nverror.NvError_InvalidState)

    def execute(self):
        """Execute the commands in sequence
        """
        # Gather commands to execute
        self.gather_commands()

        for cmd in self.command_list:
            logger.debug(f"Arguments for the command: {cmd.args}")
            CommandExecutor.execute_cmd(cmd)

class ParallelCompositeCommand(Command):
    """Class to allow executing commands in parallel.
       Concrete command classes can inherit from this base class
       and add child commands which are executed parallelly.
    """
    def __init__(self, description, name, args):
        super(ParallelCompositeCommand, self).__init__(description, name, args)

        # Creating multiple processes causes nvimagesign to crash
        # Keeping the number of processes to one until it is fixed.
        self.pool = Pool(1)
        self.results = []
        self.command_list = []

    def add_child(self, cmd_ob):
        """Add child commands to the command list for parallel execution

        Args:
            cmd_ob (Command): Object of sub-class of Command class
        """
        self.command_list.append(cmd_ob)

    @abstractmethod
    def gather_commands(self):
        """Subclass will override this method to add child commands
        """
        AbnormalTermination("Can't call abstract method", nverror.NvError_InvalidState)

    def execute(self):
        """Execute the commands in sequence
        """
        # Gather commands to execute
        self.gather_commands()

        for cmd in self.command_list:
            logger.debug(f"Arguments for the command: {cmd.args}")
            self.results.append(self.pool.apply_async(CommandExecutor.execute_cmd, args=(cmd, )))

        try:
            for result in self.results:
                result.get()
        except Exception as err:
            logger.info(f"{traceback.print_exc(err)}")
            raise AbnormalTermination(err, nverror.NvError_InvalidState)
        finally:
            self.pool.close()
            self.pool.join()


class CommandExecutor():
    """Exector class for the commands.

       Tasks:
           1. Extract commands from command line arguments
           2. Execute the command

    """

    @classmethod
    def extract_commands(cls, sysv_args):
        """Extracts list of commands from command line arguments

        Returns:
            list[Command]: List of extracted command objects
        """
        command_list = []

        # Get valid arguments
        valid_arguments_dict = {
            key: value for key, value in sysv_args.items()
            if value is not None and value is not False
        }
        valid_arguments_keys = ' '.join(valid_arguments_dict.keys())
        supported_commands = CommandFactory.all_supported_commands()

        # Extract commands using required args and command line arguments
        for cmd_name, required_args, not_required_args in supported_commands:
            arg_match_list = [re.search(arg, valid_arguments_keys) for arg in required_args]
            arg_match_list = [match_ob if match_ob and match_ob.group(0) != '' else None for match_ob in arg_match_list]
            not_required_arg_match_list = [re.search(arg, valid_arguments_keys) for arg in not_required_args]
            not_required_arg_match_list = [match_ob if match_ob and match_ob.group(0) != '' else None for match_ob in not_required_arg_match_list]

            if (all(arg_match_list) and not any(not_required_arg_match_list)):
                logger.info(f"Found Command {cmd_name}")
                cmd_ob = CommandFactory.get_command_object(
                    cmd_name, sysv_args)
                command_list.append(cmd_ob)

        return command_list

    @classmethod
    def execute_cmd(cls, command):
        """Execute a command
           Steps:
               1. Preprocess
               2. Execute
               3. Cleanup
        Args:
            command (_type_): _description_
        """
        try:
            # Preprocessing
            command.preprocess()
            command.execute()
        finally:
            command.cleanup()

        logger.debug(f"Successfully executed {command.description} command.")

class FileToFlashInterpreter():
    """FileToFlash.txt iterator class
    """

    class ImageData():
        """Data class for image data
        """

        def __init__(self):
            self.line_parts = None

        def __repr__(self):
            return f"binary name: {self.binary_name}, "\
                   f"partition name: {self.partition_name}"

        @property
        def binary_name(self):
            """Return binary name from FileToFlash.txt line

            Returns:
                str: Binary name
            """
            if (self.line_parts is not None):
                return os.path.basename(self.line_parts[2])

        @property
        def partition_name(self):
            """Return partition name from FileToFlash.txt line

            Returns:
                str: Partition Name
            """
            if (self.line_parts is not None):
                return self.line_parts[1]

        @property
        def has_bch(self):
            """Returns True if bch is marked present in the
            FileToFlash.txt entry for a given row
            """
            if (self.line_parts is not None):
                if (self.line_parts[9] != '4' and self.line_parts[9] != '13'):
                    return True

            return False

    def __init__(self, filename):
        with open(filename, 'r', encoding='utf-8') as file_to_flash:
            self.__lines = file_to_flash.readlines()
            self.__image_data = None

    @property
    def chain_list(self):
        """Get list of chain ids from FileToFlash.txt

        Returns:
            list: List of chain ids
        """
        regex = re.compile('^[A-Z]_*')
        chain_dict = {}
        for line in self.__lines:
            line = line.strip()
            partition_name = line.split(' ')[1]
            if (line[0] != '#' and regex.match(partition_name)):
                chain_dict[partition_name[0]] = 1
        return sorted(chain_dict.keys())

    def all_lines(self):
        """All lines iterator

        Yields:
            str: FileToFlash.txt line string
        """
        for line in self.__lines:
            line = line.strip()
            line_parts = line.split(' ')
            self.__image_data = FileToFlashInterpreter.ImageData()
            self.__image_data.line_parts = line_parts
            yield [line, self.__image_data]

    def all_l1_images(self):
        """L1 partition images generator function

        Yields:
            ImageData object: ImageData object for L1 image
        """
        regex = re.compile('^(?![A-Z]_).*')
        for line in self.__lines:
            line = line.strip()
            line_parts = line.split(' ')
            if (line[0] == '#'):
                continue

            if (regex.match(line_parts[1])):
                self.__image_data = FileToFlashInterpreter.ImageData()
                self.__image_data.line_parts = line_parts
                yield self.__image_data

    def chain_specific_images(self, chain_id):
        """Chain specific images generator function

        Args:
            chain_id (str): Chain id

        Yields:
            ImageData object: ImageData object for chain specific image
        """
        for line in self.__lines:
            line = line.strip()
            line_parts = line.split(' ')
            if (line[0] == '#'):
                continue

            if (line_parts[1][0] == chain_id):
                self.__image_data = FileToFlashInterpreter.ImageData()
                self.__image_data.line_parts = line_parts
                yield self.__image_data

    def all_images(self):
        """All images generator function

        Yields:
            ImageData object: ImageData object for all images
        """
        for line in self.__lines:
            line = line.strip()
            line_parts = line.split(' ')
            if (line[0] == '#'):
                continue

            self.__image_data = FileToFlashInterpreter.ImageData()
            self.__image_data.line_parts = line_parts
            yield self.__image_data


class EnvironmentConfig():
    """Class for managing and processing environment configuration
    """
    TEGRA_TOP = os.getenv('TEGRA_TOP')
    PDK_TOP = os.getenv('PDK_TOP')
    NV_OUTDIR = os.getenv('NV_OUTDIR')
    TARGET_BOARD = os.getenv('TARGET_BOARD', 'generic')
    BUILD_FLAVOR = os.getenv('BUILD_FLAVOR', 'release')
    NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT = os.getenv(
        'NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT', 'none')
    WITH_ENV = True
    NV_SDK_NAME_FOUNDATION = 'unified_flash'


    if (NV_OUTDIR is None):
        WITH_ENV = False

    DIR_BEING_RUN_FROM = os.path.dirname(os.path.realpath(__file__))

    @classmethod
    def _is_pdk_env(cls):
        file_path = os.path.join(
            cls.DIR_BEING_RUN_FROM, "..", "..", "..", "tools", "flashtools")
        return os.path.isdir(file_path)

    @classmethod
    def _get_pdk_top(cls):
        if (cls.PDK_TOP is None):
            # Assume they are in the bootburn directory and try and find it
            pdk_top = os.path.abspath(os.path.join("..", "..", ".."))
            if(os.path.isdir(os.path.join(pdk_top, "..", cls.NV_SDK_NAME_FOUNDATION)) is True):
                pdk_top = os.path.abspath(os.path.join(pdk_top, ".."))
            else:
                msg = "Can't determine PDK top folder from PWD"
                AbnormalTermination(msg, nverror.NvError_BadParameter)
            cls.PDK_TOP = os.path.abspath(pdk_top)
            logger.debug(f"PDK TOP found: {cls.PDK_TOP}")

    @classmethod
    def _get_nv_outdir(cls):
        """Get output directory for NV binaries
        """
        if (not cls._is_pdk_env()):
            out_dir = cls._get_out_dir(
                "foundation", cls.TARGET_BOARD, cls.BUILD_FLAVOR, cls.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT)
            cls.NV_OUTDIR = os.path.join(cls.TEGRA_TOP, "out", out_dir[0])
        else:
            cls._get_pdk_top()
            foundation = os.path.join(
                EnvironmentConfig.PDK_TOP, EnvironmentConfig.NV_SDK_NAME_FOUNDATION)
            nv_flashtools = os.path.join(foundation, "tools", "flashtools")
            cls.NV_OUTDIR = os.path.join(nv_flashtools, "flash")

    @classmethod
    def _get_out_dir(cls, base_build_type, target_board, build_flavor, config_variant=None):
        top_dir = cls.TEGRA_TOP
        base_build_type = base_build_type.strip()

        pwd = os.getcwd()
        os.chdir(os.path.dirname(os.path.realpath(__file__)))
        top = os.path.abspath(os.path.join("..", "..", "..", ".."))
        if(os.path.isdir(os.path.join(top, "flash-tools", "legacy"))):
            # need absolute path not relative
            cls.TEGRA_TOP = os.path.abspath(top)
            top_dir = cls.TEGRA_TOP
        os.chdir(pwd)

        if (cls.WITH_ENV and base_build_type == "foundation"):
            return [cls.NV_OUTDIR, top_dir]

        out_dir = os.path.join(top_dir, "out", "embedded-" +
                               base_build_type + "-" + target_board + "-" + build_flavor)

        if(base_build_type == "foundation"):
            out_dir = out_dir + "-" + config_variant
        else:
            AbnormalTermination(f"{base_build_type} is not supported!")

        return [out_dir, top_dir]

    @classmethod
    def get_tegrasign_path(cls):
        """Get path for tegrasign scripts

        Returns:
            str: Path to tegrasign scripts
        """
        cls._get_nv_outdir()
        if (not cls._is_pdk_env()):
            tegraflash_path = os.path.join(
                cls.TEGRA_TOP, "flash-tools", "legacy", 'tegrasign_v3'
            )
        else:
            tegraflash_path = cls.NV_OUTDIR

        return tegraflash_path

    @classmethod
    def get_nvskuinfo_path(cls):
        """Get path to nvskuinfo

        Returns:
            str: Path to nvskuinfo
        """
        cls._get_nv_outdir()
        if (not cls._is_pdk_env()):
            nvskuinfo_path = os.path.join(
                EnvironmentConfig.NV_OUTDIR, "nvidia", "qb_flashtools"
            )
        else:
            nvskuinfo_path = cls.NV_OUTDIR

        return nvskuinfo_path

    @classmethod
    def get_nvimagesign_path(cls):
        """Get path to nvimagesign

        Returns:
            str: Path to nvimagesign
        """
        cls._get_nv_outdir()
        if (not cls._is_pdk_env()):
            nvimagesign_path = os.path.join(
                EnvironmentConfig.NV_OUTDIR, "nvidia", "qb_flashtools"
            )
        else:
            nvimagesign_path = cls.NV_OUTDIR

        return nvimagesign_path

    @classmethod
    def get_tegraopenssl_path(cls):
        """Get path to tegraopenssl executable

        Returns:
            str: Path to tegraopenssl executable
        """
        cls._get_nv_outdir()
        if (not cls._is_pdk_env()):
            tegraopenssl_path = os.path.join(
                EnvironmentConfig.NV_OUTDIR, "nvidia", "flash-tools",
                "legacy", "tegraopenssl-hostcc"
            )
        else:
            tegraopenssl_path = cls.NV_OUTDIR

        return tegraopenssl_path

    @classmethod
    def get_hsm_dummy_keys_path(cls):
        """Get path to HSM dummy keys

        Returns:
            str: Path to HSM dummy keys
        """
        return os.path.dirname(os.path.realpath(__file__))

    @classmethod
    def get_customer_data_schema_path(cls):
        """Get path to directory containing customer data schema file

        Returns:
            str: Path to directory containing default customer data
                 schema file
        """

        return cls.DIR_BEING_RUN_FROM

class CopyNvImageSignCommand(Command):
    """Command to copy nvimagesign binary
    """

    def __init__(self, dest_directory):
        super().__init__("Copy nvimagesign binary", 'copy_nvimagesign', None)

        self.dest_directory = dest_directory
        self.nvimagesign_executables = ["nvimagesign"]

    def execute(self):
        for filename in self.nvimagesign_executables:
            shutil.copy(
                os.path.join(
                    EnvironmentConfig.get_nvimagesign_path(),
                    filename
                ),
                self.dest_directory
            )



class CopyNvSkuInfoCommand(Command):
    """Command to copy nvskuinfo binary
    """

    def __init__(self, dest_directory):
        super().__init__("Copy nvskuinfo executable", 'copy_nvskuinfo', None)

        self.dest_directory = dest_directory
        self.nvskuinfo_executables = ["nvskuinfo"]

    def execute(self):
        for filename in self.nvskuinfo_executables:
            shutil.copy(
                os.path.join(
                    EnvironmentConfig.get_nvskuinfo_path(),
                    filename
                ),
                self.dest_directory
            )


class CopyTegraSignCommand(Command):
    """Command to copy Tegrasign binaries
    """

    def __init__(self, dest_directory):
        super().__init__("Copy tegrasign scripts", 'copy_tegrasign', None)

        self.dest_directory = dest_directory
        self.tegrasign_files = ["tegrasign_v3.py", "tegrasign_v3_internal.py", "tegrasign_v3_debug.yaml",
                                "tegrasign_v3_util.py", "tegrasign_v3_hsm.py", "tegrasign_v3_oemkey_t234.yaml"]
        self.tegraopenssl_executables = ["tegraopenssl"]

    def execute(self):
        tegrasign_file_paths = [os.path.join(
            EnvironmentConfig.get_tegrasign_path(), filename) for filename in self.tegrasign_files]
        for filename in tegrasign_file_paths:
            shutil.copy(filename, self.dest_directory)

        tegraopenssl_file_paths = [os.path.join(EnvironmentConfig.get_tegraopenssl_path(), filename)
                                   for filename in self.tegraopenssl_executables]
        for filename in tegraopenssl_file_paths:
            shutil.copy(filename, self.dest_directory)

class GenPcp(Command):
    """Command to generate PCP file
    """

    def __init__(self, key_file, hsm_str, pcp_file):
        super().__init__("generate PCP file", 'gen_pcp', None)
        self.key_file = key_file
        self.hsm_str = hsm_str
        self.pcp_file = pcp_file

    def execute(self):
        l_tegrasign="./tegrasign_v3.py"
        tegraSignCommand = l_tegrasign
        if(self.key_file is None):
            tegraSignCommand += " --hsm " + self.hsm_str
        else:
            tegraSignCommand += ' --key ' + self.key_file
        tegraSignCommand += " --pubkeyhash " + self.pcp_file
        os.system(tegraSignCommand)
        #output = subprocess.check_output(tegraSignCommand, stderr=subprocess.STDOUT, encoding='utf-8')
        #logger.debug(f"{output}")

class CopyDummyHsmKeysCommand(Command):
    """Command to copy dummy keys required in HSM mode
       by nvimagesign
    """

    def __init__(self, dest_directory):
        super().__init__("Copy dummy hsm keys", 'copy_dummy_hsm_keys', None)

        self.dest_directory = dest_directory
        self.dummy_hsm_keys = ["sign_eddsa_dummy_key.txt", "sign_rsa_dummy_key.txt"]

    def execute(self):
        dummy_hsm_keys_paths = [os.path.join(
            EnvironmentConfig.get_hsm_dummy_keys_path(), filename) for filename in self.dummy_hsm_keys]
        for filename in dummy_hsm_keys_paths:
            shutil.copy(filename, self.dest_directory)

class NvSkuInfoCommand(Command):
    """Command class for executing nvskuinfo command
    """

    def __init__(self):
        super().__init__("nvskuinfo", "nvskuinfo", None)
        self.cmd = ["./nvskuinfo"]

        # Keeping type as both instead of just signed or unsigned
        # to allow processing for both signed and unsigned customer data
        self.cmd.extend(["-t", "b"])

    def input_bct(self, input_bct):
        self.cmd.extend(["--inbct", input_bct])
        return self

    def output_bct(self, output_bct):
        self.cmd.extend(["--outbct", output_bct])
        return self

    def chip(self, chip_id):
        self.cmd.extend(["--chip", chip_id])
        return self

    def customer_data_blob(self, blob_file):
        self.cmd.extend(["--customer-data-blob", blob_file])
        return self

    def skip_validate(self):
        self.cmd.extend(["--skipValidate"])
        return self

    def execute(self):
        try:
            logger.info(f"Executing command: {' '.join(self.cmd)}\n")
            subprocess.check_output(self.cmd, stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as err:
            logger.debug(f"Command: {err.cmd}")
            logger.debug(f"Return code: {err.returncode}")
            logger.debug(f"Output: {err.output}")
            raise err


class GenerateCustomerDataBlobCommand(Command):
    """Customer data blob generator command
       Inputs - Customer data json file and
                Customer data schema file
    """

    def __init__(self, args, customer_data_blob_file_path):
        super(GenerateCustomerDataBlobCommand, self).__init__(
            "generate customer data blob",
            "customer_data_blob_generator",
            args
        )
        self.out_blob_file = customer_data_blob_file_path

    def execute(self):
        # 1. Load schema
        customer_data_processor = CustomerDataProcessor()
        customer_data_processor.load_schema(self.args['customer_data_schema'])

        # Process customer data and generate blob
        customer_data_blob_file_name = "customer_data_blob.bin"
        customer_data_processor.process(self.args['customer_data'], customer_data_blob_file_name)

@CommandFactory.register(
    "update_customer_data",
    "update customer data",
    ["chip", "brbct", "customer_data"],
    ["customer_data_schema"]
)
class UpdateCustomerDataCommand(SequentialCompositeCommand):
    """Command class for updating customer data
    """
    default_schema_file = "nv-customer-data-schema.json"

    def __init__(self, description, name, args):
        super(UpdateCustomerDataCommand, self).__init__(description, name, args)
        if (self.args["customer_data_schema"] is None):
            schema_file_dir = EnvironmentConfig.get_customer_data_schema_path()
            default_schema_path = os.path.join(schema_file_dir, self.default_schema_file)
            self.args["customer_data_schema"] = default_schema_path

            # This file is used to store intermediate data generated from
            # preprocessing customer data json file
        self.temp_customer_data_file = "tmp_customer_data.json"

    def preprocess(self):
        if (not os.path.exists(self.args["customer_data"])):
            raise AbnormalTermination(
                "Customer data file %s doesn't exist" % self.args["customer_data"],
                nverror.NvError_FileNotFound
            )

        # Following fields should not be allowed in unsinged data
        # even though schema allows it, so check for these fields
        # and fail if they are provided in the data file
        not_allowed_unsigned_fields_list = [
            "macInUseCount", "macId0", "macId1", "macId2", "macId3",
            "macId4", "macId5", "macId6", "macId7", "boardSerial"
        ]

        with open(self.args["customer_data"], "r", encoding="utf-8") as customer_data_file:
            customer_data = json.load(customer_data_file)

        unsigned_customer_data = customer_data.get("customer-data-unsigned", {})
        signed_customer_data = customer_data.get("customer-data-signed", {})
        for field in not_allowed_unsigned_fields_list:
            if (field in unsigned_customer_data):
                raise AbnormalTermination(
                    f"Customer data {field} is not allowed in customer-data-unsigned section",
                    nverror.NvError_BadParameter
                )
            # Copy data from signed to unsigned for these fields
            if (field in signed_customer_data):
                unsigned_customer_data[field] = signed_customer_data[field]

        final_customer_data = {"customer-data-unsigned": unsigned_customer_data,
                               "customer-data-signed": signed_customer_data}

        with open(self.temp_customer_data_file, "w", encoding="utf-8") as customer_data_file:
            json.dump(final_customer_data, customer_data_file, indent=4)

        self.args["customer_data"] = self.temp_customer_data_file

    def gather_commands(self):
        # Generate customer data blob
        customer_data_blob_file_name = "customer_data_blob.bin"
        blob_file_path = os.path.abspath(customer_data_blob_file_name)
        self.add_child(GenerateCustomerDataBlobCommand(self.args, blob_file_path))

        # Run nvskuinfo command to update BCT with customer data

        bcts = glob.glob(os.path.join(
                self.args['brbct'], "[A|B]_bct_[BR|asym_default]*"))

        for bct in bcts:
            if ("fskp" in bct):
                continue
            self.add_child(
                NvSkuInfoCommand()\
                    .chip(self.args['chip'])\
                    .input_bct(bct)\
                    .output_bct(bct)\
                    .customer_data_blob(blob_file_path)
                )

class NvImageSignBinCommand(Command):
    """Command to resign binaries and extract resigned BCHs
    """

    def __init__(self):
        super(NvImageSignBinCommand, self).__init__(
            "nvimagesign resign binary", 'nvimagesign_sign_bin', None)

        self.cmd = ["./nvimagesign"]
        self.cmd.extend(["--log_lvl", "6"])

    def chip(self, chip_id):
        self.cmd.extend(["--chip", chip_id])
        return self

    def hsm(self, hsm):
        if (hsm):
            self.cmd.extend(["--hsm", hsm])
        return self

    def signing_key(self, signing_key):
        if (signing_key):
            self.cmd.extend(["--sign", signing_key])
        return self

    def image(self, input_file):
        self.cmd.extend(["--image", input_file])
        return self

    def image_list(self, input_file):
        self.cmd.extend(["--image_list", input_file])
        return self

    def batch_sign(self):
        self.cmd.extend(["--batch_sign"])
        return self

    def output_file(self, out_file):
        self.cmd.extend(["--out_file", out_file])
        return self

    def delta(self, out_file):
        if (out_file):
            self.cmd.extend(["--delta", out_file])
        return self
    def delta_list(self, delta_file):
        self.cmd.extend(["--delta_list", delta_file])
        return self

    def pcp(self, pcp_file):
        self.cmd.extend(["--pcp", pcp_file])
        return self

    def revoke(self, revoke_key):
        if revoke_key:
            rk_val = 0
            if revoke_key == "h0":
                rk_val = 1
            elif revoke_key == "h1":
                rk_val = 2
            elif revoke_key == "h0+h1":
                rk_val = 3
            else :
                raise AbnormalTermination(f"Reovke key {revoke_key} is not supported\n")
            self.cmd.extend(["--revoke", str(rk_val)])
        return self

    def execute(self):
        try:
            logger.info(f"Executing command: {' '.join(self.cmd)}\n")
            output = subprocess.check_output(self.cmd, stderr=subprocess.STDOUT, encoding='utf-8')
            logger.debug(f"{output}")
        except subprocess.CalledProcessError as err:
            logger.debug(f"Command: {err.cmd}")
            logger.debug(f"Return code: {err.returncode}")
            logger.debug(f"Output: {err.output}")
            if (err.returncode != 0x25):
                raise err

class ResignImageListCommand(Command):
    """Command class to allow image list resigning
    """
    def __init__(self, image_list, args, pcp_file):
        super().__init__("Sign image List", "sign_image_list", args)
        self.image_list = image_list
        self.pcp_file = pcp_file

    def execute(self):
        final_list = []
        delta_list = []
        for image in self.image_list:
            final_list.append(os.path.join(self.args['images'], image))
            if self.args['signing_key'] == "none":
                final_list.append(os.path.join(self.args['headers_output_dir'], image))
                shutil.copy(os.path.join(self.args['images'], "FileToFlash.txt"), self.args['headers_output_dir'])
            else :
                out_file_name_without_ext, _ = os.path.splitext(image)
                out_file_path_without_ext = os.path.join(self.args['headers_output_dir'], out_file_name_without_ext)
                if('_bct_BR_' in image or '_bct_asym_default' in image):
                    final_list.append(out_file_path_without_ext + '_resigned.bct')
                elif re.search("^[0-9]_PT.bin$", os.path.basename(image)):
                    final_list.append(out_file_path_without_ext + '_resigned.bin')
                else:
                    final_list.append(out_file_path_without_ext + '_resigned.bch')
                    if('delta' in self.args and self.args['delta']):
                        delta_list.append(out_file_path_without_ext + '_resigned.bch')
                        delta_list.append(os.path.join(self.args['delta'], out_file_name_without_ext) + ".delta")

        final_list.insert(0, str(len(final_list)))
        with open("sign_img_list.txt", 'w') as fp:
            fp.write('\n'.join(final_list))
        if('delta' in self.args and self.args['delta']):
            delta_list.insert(0, str(len(delta_list)))
            with open("delta_img_list.txt", 'w') as fp:
                fp.write('\n'.join(delta_list))

        command = NvImageSignBinCommand()\
            .image_list("sign_img_list.txt")\
            .hsm(self.args['hsm'])\
            .signing_key(self.args['signing_key'])\
            .chip(self.args['chip'])\
            .revoke(self.args['revoke'])

        if(self.args['batch_sign']):
            command = command.batch_sign()

        if self.args['signing_key'] != "zero" and self.args['signing_key'] != "none":
            command = command.pcp(self.pcp_file)

        if (command is not None and 'delta' in self.args and self.args['delta']):
                command = command.delta_list("delta_img_list.txt")

        if (command is not None):
            CommandExecutor.execute_cmd(command)

@CommandFactory.register(
    "resign",
    "resign images",
    ["chip", "images", "headers_output_dir"],
    ["signing_key", "hsm", "delta", "batch_sign", "revoke"],
    ["asymmetric", "delta"]
)
class ResignCommand(ParallelCompositeCommand):
    """Composite resign BCH and BCT binaries given the
       input images
    """

    def __init__(self, description, name, args):
        super(ResignCommand, self).__init__(description, name, args)

    def preprocess(self):
        # Check if input directory exists
        if (not os.path.exists(self.args['images'])):
            raise AbnormalTermination(
                f"Input images directory {self.args['images']} does not exist")

        if (os.path.exists(self.args['headers_output_dir'])):
            shutil.rmtree(self.args['headers_output_dir'])

        os.makedirs(self.args['headers_output_dir'])
        logger.info(
            f"Headers output directory path is: {os.path.abspath(self.args['headers_output_dir'])}")

        # Make sure FileToFlash.txt exists in the images directory
        if (not os.path.exists(os.path.join(self.args['images'], "FileToFlash.txt"))):
            AbnormalTermination(
                f"FileToFlash.txt not found in {self.args['images']}")

    def gather_commands(self):
        # Loop through FileToFlash.txt and signing each binary
        # along with separating the BCH into its own directory
        file_to_flash_object = FileToFlashInterpreter(
            os.path.join(self.args['images'], "FileToFlash.txt"))
        logger.debug(f"List of chains: {file_to_flash_object.chain_list}")

        img_list = []

        # Re-sign all L1 partitions
        l1_images = file_to_flash_object.all_l1_images()
        for image_data in l1_images:
            if image_data.partition_name == 'bct':
                img_list.append(image_data.binary_name)
            elif image_data.has_bch:
                img_list.append(image_data.binary_name)

        # Resign all chain specific paritions
        for chain_id in file_to_flash_object.chain_list:
            chain_speficic_images = file_to_flash_object.chain_specific_images(
                chain_id)
            for image_data in chain_speficic_images:
                if image_data.has_bch:
                    img_list.append(image_data.binary_name)
        command = ResignImageListCommand(img_list, self.args, "pcp_file.bin")
        self.add_child(command)

@CommandFactory.register(
    "asymmetric_resign",
    "resign asymmetric images",
    ["chip", "images", "headers_output_dir", "asymmetric"],
    ["signing_key", "hsm", "batch_sign", "revoke"]
)
class AsymmetricResignCommand(ParallelCompositeCommand):
    """Composite resign BCH and BCT binaries given the
       input images
    """

    def __init__(self, description, name, args):
        super(AsymmetricResignCommand, self).__init__(description, name, args)

    def preprocess(self):
        # Check if input directory exists
        if (not os.path.exists(self.args['images'])):
            raise AbnormalTermination(
                f"Input images directory {self.args['images']} does not exist")

        if (os.path.exists(self.args['headers_output_dir'])):
            shutil.rmtree(self.args['headers_output_dir'])

        os.makedirs(self.args['headers_output_dir'])
        logger.info(
            f"Headers output directory path is: {self.args['headers_output_dir']}")

        # Make sure FileToFlash.txt exists in the images directory
        if (not os.path.exists(os.path.join(self.args['images'], "FileToFlash.txt"))):
            AbnormalTermination(
                f"FileToFlash.txt not found in {self.args['images']}")

    def gather_commands(self):
        file_to_flash_object = FileToFlashInterpreter(
            os.path.join(self.args['images'], "FileToFlash.txt"))

        img_list = []

        # Resign L1 PT
        l1_images = file_to_flash_object.all_l1_images()
        for image_data in l1_images:
            if image_data.partition_name == 'bct':
                img_list.append(image_data.binary_name)
            else:
                if image_data.has_bch:
                    img_list.append(image_data.binary_name)

        # Resign all chain B specific paritions
        chain_speficic_images = file_to_flash_object.chain_specific_images('B')
        for image_data in chain_speficic_images:
            if image_data.has_bch:
                img_list.append(image_data.binary_name)

        # Sign the chain B default BCT and chain B BCT
        bcts = glob.glob(os.path.join(
                self.args['images'], "B_bct_[BR|asym_default]*"))

        for bct in bcts:
            img_list.append(os.path.basename(bct))

        command = ResignImageListCommand(img_list, self.args, "pcp_file.bin")
        self.add_child(command)


class GenerateDeltaPackageCommand(Command):
    """Command class to allow generating delta package after
       nvimagesign has generated resigned BCT and generated delta files
    """
    def __init__(self, args):
        super(GenerateDeltaPackageCommand, self).__init__("generate delta package", "gen_delta_pkg", args)

        file_to_flash_path = os.path.join(self.args['images'], "FileToFlash.txt")
        self.file_to_flash_object = FileToFlashInterpreter(file_to_flash_path)

    def preprocess(self):
        # Copy resigned BCT to delta package
        all_images_data = self.file_to_flash_object.all_images() # BCT is the L1 partitions
        for image_data in all_images_data:
            if (image_data.partition_name == 'bct'):
                # Get the resigned BCT file from headers directory
                resigned_bct_name_without_ext = os.path.splitext(image_data.binary_name)[0] + "_resigned"
                resigned_bct_name = resigned_bct_name_without_ext + ".bct"
                resigned_bct_path = os.path.join(self.args['headers_output_dir'], resigned_bct_name)
                shutil.copy(resigned_bct_path, self.args['delta'])
            elif (image_data.partition_name == 'pt'):
               # Get the resigned BCT file from headers directory
               resigned_pt_name_without_ext = os.path.splitext(image_data.binary_name)[0] + "_resigned"
               resigned_pt_name = resigned_pt_name_without_ext + ".bin"
               resigned_pt_path = os.path.join(self.args['headers_output_dir'], resigned_pt_name)
               shutil.copy(resigned_pt_path, self.args['delta'])

    def _get_sha512_hash(self, file_name):
        buf_size = 8192
        sha512_digest = hashlib.sha512()
        with open(file_name, 'rb') as in_file:
            data = in_file.read(buf_size)
            if not data:
                raise AbnormalTermination("Invalid file for SHA512 hash calculation: {file_name}")
            sha512_digest.update(data)

        return sha512_digest.hexdigest()


    def _populate_metadata(self, fp, orig_image_data, delta_file_name, resigned_file_name):
        old_file_path = os.path.join(self.args['images'], orig_image_data.binary_name)
        fp.write(orig_image_data.partition_name + ',')
        fp.write(delta_file_name + ',')
        fp.write(self._get_sha512_hash(old_file_path) + ',')
        fp.write(self._get_sha512_hash(resigned_file_name))
        fp.write('\n')

    def _populate_bct_metadata(self, fp, orig_image_data, delta_file_name):
        fp.write(orig_image_data.partition_name + ',')
        fp.write(delta_file_name + ',')
        fp.write('0' + ',')
        fp.write('0')
        fp.write('\n')

    def execute(self):
        # Copy resigned BCT to delta package
        logger.info("Creating delta package for DU")
        pwd = os.getcwd()
        os.chdir(os.path.abspath(self.args['delta']))
        all_images_data = self.file_to_flash_object.all_images()
        with open(os.path.join(self.args['delta'], 'Metadata.txt'), 'w', encoding='utf-8') as metadata_file:
            metadata_file.write('Partition Name, Delta Binary Name, Old SHA512 Hash, New SHA512 Hash\n')
            for image_data in all_images_data:
                file_name_without_ext = os.path.splitext(image_data.binary_name)[0]
                resigned_file_name_without_ext = file_name_without_ext + "_resigned"
                if (image_data.partition_name == 'bct'):
                    self._populate_bct_metadata(metadata_file, image_data, resigned_file_name_without_ext + ".bct")
                elif (image_data.partition_name == 'pt'):
                    self._populate_bct_metadata(metadata_file, image_data, resigned_file_name_without_ext + ".bin")
                elif (os.path.exists(file_name_without_ext + ".delta")):
                    delta_file_name = file_name_without_ext + ".delta"
                    resigned_file_name = os.path.join(
                                            self.args['headers_output_dir'],
                                            resigned_file_name_without_ext + ".bch"
                                        )
                    self._populate_metadata(metadata_file, image_data, delta_file_name, resigned_file_name)
                else:
                    continue
        logger.info("Successfully created delta package for DU")
        os.chdir(pwd)

@CommandFactory.register(
    "du_delta_package",
    "DU delta package",
    ["chip", "images", "headers_output_dir", "delta"],
    ["signing_key", "hsm", "batch_sign", "revoke"],
)
class DeltaPackageCommand(SequentialCompositeCommand):
    """Delta package command:
        - Resigns all images and generates delta binaries
        - Generates delta package with resigned BCT, delta binaries
          and Metadata.txt files with following information:
            1. Partition Name
            2. Delta binary name
            3. SHA512 hash of old image
            4. SHA512 of the new BCH image
    """
    def __init__(self, description, name, args):
        super(DeltaPackageCommand, self).__init__(description, name, args)

    def preprocess(self):
        # Create delta package directory
        if (os.path.exists(self.args['delta'])):
            shutil.rmtree(self.args['delta'])
        os.makedirs(self.args['delta'])

    def gather_commands(self):
        self.add_child(
            ResignCommand(
                "Resign and generate delta",
                "du_resign_generate_delta",
                self.args
            )
        )

        self.add_child(
            GenerateDeltaPackageCommand(
                self.args
            )
        )


@CommandFactory.register(
    "du_base_package",
    "DU base package",
    ["chip", "images", "base_package_dir"]
)
class DuBasePackageCommand(Command):
    """DU base package generator command class
    """

    def preprocess(self):
        # Check if bsa directory exists
        if (not os.path.exists(self.args['images'])):
            raise AbnormalTermination(
                f"Input images directory {self.args['images']} does not exist")

        # Create base package output directory
        if (os.path.exists(self.args['base_package_dir'])):
            shutil.rmtree(self.args['base_package_dir'])

        logger.info(
            f"DU Base Package directory directory path is: {self.args['base_package_dir']}")

        # Make sure FileToFlash.txt exists in the images directory
        if (not os.path.exists(os.path.join(self.args['images'], "FileToFlash.txt"))):
            AbnormalTermination(
                f"FileToFlash.txt not found in {self.args['images']}")

    def execute(self):
        logger.info("Generating base package for DU")
        # Copy input directory to base package directory
        shutil.copytree(self.args['images'], self.args['base_package_dir'])

        # Remove BR BCT entry from FileToFlash.txt
        file_to_flash_object = FileToFlashInterpreter(os.path.join(self.args['images'], "FileToFlash.txt"))
        all_lines_image_data = file_to_flash_object.all_lines()

        with open(os.path.join(self.args['base_package_dir'], "FileToFlash.txt"), 'w', encoding='utf-8') as file_to_flash:
            for line, image_data in all_lines_image_data:
                if (image_data.partition_name == 'bct'):
                    continue

                file_to_flash.write(line + "\n")

        logger.info(f"Successfully generated base package for DU at {self.args['base_package_dir']}")


def main():
    start_time = time.time()

    parser = argparse.ArgumentParser()

    parser.add_argument('--chip', required=True,
                        metavar="CHIP_ID", default=None, help="Chip Id")

    parser.add_argument('--brbct', metavar="BR_BCT_DIR",
                        default=None, help="BR BCT DIR path")

    parser.add_argument('--customer-data', metavar="CUST_DATA_FILE",
                        default=None, help="Customer Data JSON file path")

    parser.add_argument('--customer-data-schema', metavar="CUST_DATA_SCHEMA_FILE",
                        default=None, help="Customer Data Schema JSON file path")

    parser.add_argument('--images', metavar='IMAGES',
                        default=None, help="Input images directory path (output of create_bsp_images.py")

    parser.add_argument('--headers-output-dir', metavar='HEADERS_IMAGES_DIR',
                        default=None, help="Resigned headers's output directory path")

    parser.add_argument('--signing-key', metavar='SIGNING_KEY',
                        default=None, help="Signing key")

    parser.add_argument('--hsm', metavar='HSM_SIGNING_ALGORITHM',
                        default=None, help="HSM signing lgorithm")

    parser.add_argument('--debug', action='store_true',
                        help="Enable debugging")

    parser.add_argument('--delta', metavar='DELTA_IMAGE',
                        default=None, help="Delta output image path")

    parser.add_argument('--base-package-dir', metavar='BASE_PACKAGE',
                        default=None, help="Generate base package for DU")

    parser.add_argument('--asymmetric', action='store_true',
                        help="Process asymmetric chain output")

    parser.add_argument('--keep', action='store_true',
                        help="Keep temporary intermediate directory for debugging")

    parser.add_argument('--batch_sign', action='store_true',
                        help="enable signing in batch mode instead one file at a time")

    parser.add_argument('--revoke', metavar='REVOKE_KEY',
                        default=None, help="Generate BRBCT with revoke bit set.\nPass 'h0' to revoke h0 key.\nPass 'h1' to revoke h1 key.\n Pass 'h0+h1' to revoke h0 and h1 keys.\n")

    # Basic validation: Only make sure there is no error in the arguments
    # Command specific argument validation is handled in each command
    args = parser.parse_args(sys.argv[1:])

    logger.setLevel(logging.INFO)
    if (args.debug):
        logger.setLevel(logging.DEBUG)

    # Create temporary directory
    tempdir = tempfile.mkdtemp()
    logger.debug(f"Temporary directory {tempdir} created!")

    # TODO: These pre-requisite commands are not required for all
    #       the commands. So fix it by making them pre-requisite
    #       for specific commands that require these commands
    #
    # Internal pre-requisite command
    tegrasign_cmd = CopyTegraSignCommand(tempdir)
    tegrasign_cmd.execute()

    # Copy nvimagesign command
    nvimagesign_cmd = CopyNvImageSignCommand(tempdir)
    nvimagesign_cmd.execute()

    # Copy nvskuinfo command
    nvskuinfo_cmd = CopyNvSkuInfoCommand(tempdir)
    nvskuinfo_cmd.execute()

    # Copy Dummy HSM keys
    dummy_hsm_key_copy_cmd = CopyDummyHsmKeysCommand(tempdir)
    dummy_hsm_key_copy_cmd.execute()

    # Change directory to temporary directory
    pwd = os.getcwd()

    os.chdir(tempdir)

    # Skip Pcp Generate if we have no key
    if (args.signing_key != None or args.hsm != None):
        pcp_cmd = GenPcp(args.signing_key, args.hsm, os.path.join(tempdir, "pcp_file.bin"))
        pcp_cmd.execute()

    commands = CommandExecutor.extract_commands(vars(args))

    try:
        for command in commands:
            CommandExecutor.execute_cmd(command)
    except OSError as err:
        logger.error(f"Error executing post processing tool: {err}")
        sys.exit(err.errno)
    finally:
        end_time = time.time()
        logger.info(f"Time Taken: {round(end_time - start_time, 2)}s")
        if (not args.keep):
            shutil.rmtree(tempdir)
        else:
            logger.debug(f"Temporary directory: {tempdir}")

        os.chdir(pwd)

    logger.info("Successfully executed post processing command!")

if __name__ == '__main__':
    main()
