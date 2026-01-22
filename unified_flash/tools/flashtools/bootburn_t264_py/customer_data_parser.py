#!/usr/bin/env python
#
# Copyright (c) 2020, NVIDIA Corporation.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#


from __future__ import print_function

import json
import struct
import sys
import re
from abc import ABCMeta, abstractmethod
import os.path
import logging

from flashtools_nverror import nverror, AbnormalTermination

logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s %(name)s %(levelname)s:%(message)s')
logger = logging.getLogger(__name__)


class CustomerInputDataParser(object):
    """Parse the customer data input string using parser schema

    Args:
        data (string): customer data string
        parser_metadata (dict): Dictionary of parser metadata

    Returns:
        list: list of values parsed from the input string
    """
    @staticmethod
    def parse_data(data, parser_metadata):
        separator = parser_metadata['data-separator-string']
        if (separator != None):
            return [data.strip() for data in re.split(separator, data) if data != ""]
        else:
            return [data]


class DataProcessor(object):
    """Abstarct Base class for processing various customer data types
    """
    __metaclass__ = ABCMeta

    data_processor_registry = {}

    def __init__(self, value, schema):
        self.value = value
        self.schema = schema
        self.__binary_output = bytearray()

    @classmethod
    def register(cls, data_processor_type):
        """Method to register data processor sub classes

        Args:
            data_processor_type (str): Type of the data processor
        """
        def wrapper(wrapped_class):
            if data_processor_type in cls.data_processor_registry:
                logger.warning(
                    'Data processor %s already exists. it will be replaced',
                    data_processor_type)
            cls.data_processor_registry[data_processor_type] = wrapped_class
            return wrapped_class
        return wrapper

    @abstractmethod
    def process(self):
        """This method needs to be overloaded by derived classes

           Process the data based on the schema

        """
        raise RuntimeError("Can't call abstract method!")

    def get_bytes(self):
        """Get the bytes after processing the data

        Returns:
            bytearray: Array of bytes of processed data
        """
        return self.__binary_output

    def _pack_output(self, offset, data):
        """Pack the processed output according the desired blob format

        Args:
            offset (int): Offset value for the type
            data (bytearray): Data byte array
        """
        self.__binary_output += bytearray(struct.pack("I", offset))
        self.__binary_output += bytearray(struct.pack("I", len(data)))
        self.__binary_output += data

    @classmethod
    def get_data_processor(cls, data_value, data_schema):
        """Static factory method to instatiate type specific processor

        Args:
            data_value (string): Parsed customer data value
            data_schema (dict): Schema dictionary for the data value

        Returns:
            DataProcessor object: Type specific data processor object if found
            None: If data processor for the data type is not found

        """
        processor_type = data_schema["type"]
        if (processor_type not in cls.data_processor_registry):
            logger.warning(
                "Data processor for %s is not supported", processor_type)
            return None

        data_processor_class = cls.data_processor_registry[processor_type]
        data_processor = data_processor_class(data_value, data_schema)
        return data_processor


@DataProcessor.register("decimal")
class DecimalDataProcessor(DataProcessor):
    """Decimal data processor
    """

    def __init__(self, data_value, data_schema):
        super(DecimalDataProcessor, self).__init__(data_value, data_schema)

    def process(self):
        int_val = int(self.value)
        binary_data = struct.pack(self.schema["format-string"], int_val)
        self._pack_output(self.schema["offset"], bytearray(binary_data))

@DataProcessor.register("string")
class StringDataProcessor(DataProcessor):
    """String data processor
    """

    def __init__(self, data_value, data_schema):
        super(StringDataProcessor, self).__init__(data_value, data_schema)
        self.value = self.value.encode('utf-8')

    def process(self):
        # Make sure input string is within the bounds
        if (len(self.value) > self.schema["max-len"] or len(self.value) < self.schema["min-len"]):
            raise RuntimeError("Invalid input data %s".format(self.value))

        binary_data = struct.pack(
            str(self.schema["max-len"] + 1) + self.schema["format-string"],
            self.value)

        self._pack_output(self.schema["offset"], bytearray(binary_data))

@DataProcessor.register("hex")
class HexDataProcessor(DataProcessor):
    """Hex data processor
    """

    def __init__(self, data_value, data_schema):
        super(HexDataProcessor, self).__init__(data_value, data_schema)

    def process(self):
        # Remove 0x from hex string if it is starting with 0x prefix
        if (self.value[0:1] != "0x"):
            self.value = self.value[2:]

        binary_data = bytearray.fromhex(self.value)
        padding = bytearray(b'\0' * (self.schema["max-len"]- len(binary_data)))

        # Prepend zero bytes if neccessary
        binary_data = padding + binary_data

        self._pack_output(self.schema["offset"], bytearray(binary_data))

@DataProcessor.register("char")
class CharDataProcessor(DataProcessor):
    """Char data processor
    """

    def __init__(self, data_value, data_schema):
        super(CharDataProcessor, self).__init__(data_value, data_schema)
        self.value = self.value.encode('utf-8')

    def process(self):
        # Make sure input charaters length is within the bounds specified in the schema
        if (len(self.value) > self.schema["max-len"] or len(self.value) < self.schema["min-len"]):
            raise RuntimeError("invalid input data {}".format(self.value))

        # Get the total number of charaters in the input
        num_chars = len(self.value)

        binary_data = b''
        for charIndex in range(num_chars):
            binary_data += struct.pack(
                self.schema["format-string"],
                self.value[charIndex: charIndex + 1])

        self._pack_output(self.schema["offset"], bytearray(binary_data))

@DataProcessor.register("unsigned-char")
class UnsignedCharDataProcessor(DataProcessor):
    """Unsinged char data processor
    """

    def __init__(self, data_value, data_schema):
        super(UnsignedCharDataProcessor, self).__init__(data_value, data_schema)
        self.value = data_value

    def process(self):
        #check to make sure we are unicode
        if (self.schema["format-string"] == 'B' and isinstance(self.value, str)) or (not isinstance(self.value, bool) and not isinstance(self.value, str) and
            not isinstance(self.value, int)):
           self.value=self.value.encode('utf-8')
           self.value=int(self.value)
        binary_data = b''
        binary_data += struct.pack(self.schema["format-string"], self.value)

        self._pack_output(self.schema["offset"], bytearray(binary_data))

@DataProcessor.register("blob")
class BinaryBlobDataProcessor(DataProcessor):
    """Process binary blob data
    """

    def __init__(self, data_value, data_schema):
        self.value = data_value
        self.schema = data_schema

    def process(self):
        # Make sure the file path is present on the disk
        if (not os.path.exists(self.value)):
            raise RuntimeError(
                "Binary blob file doesn't exisit: %s".format(self.value))

        # Check the length of the file is matching with schema
        file_size = os.path.getsize(self.value)
        if (file_size != self.schema["max-len"]):
            raise RuntimeError("Blob file size %d for %s doesn't match the expected size %d".format(
                file_size, self.value, self.schema["max-len"]))

        # Read the bytes from the file
        with open(self.value, "rb") as f_blob_file:
            binary_data = f_blob_file.read()

        self._pack_output(self.schema["offset"], binary_data)


class CustomerDataProcessor():
    """Main class to process customer data given the schema and data file

       Example usage:
            schema_file = "customer_data_schema.json"
            data_file = "customer_data.json"
            output_blob_file = "customer_data.bin"

            customer_data_processor = CustomerDataProcessor()
            customer_data_processor.load_schema(schema_file)
            if (customer_data_processor.is_schema_loaded()):
                customer_data_processor.process(data_file, output_blob_file)
    """

    def __init__(self):
        self.schema = None

        # Section type to section processor object mapping
        self.section_processor_dict = {}

    def __validate_schema_item_offset(self, allowed_offset_range_list, schema_item_data):
        if (isinstance(schema_item_data, dict)):
            # This is the only allowed schema for schema_item
            # so check offset
            # Iterate through allowed offset range list
            for allowed_offset_range in allowed_offset_range_list:
                if (schema_item_data["offset"] < allowed_offset_range[0] or
                    schema_item_data["offset"] > allowed_offset_range[1]):
                    return False
                else:
                    return True
        else:
            # This is a list of allowed schemas for schema_item_word
            # so iterate through each allowed schema and check offset
            for schema_item in schema_item_data:
                # Iterate through allowed offset range list
                return self.__validate_schema_item_offset(allowed_offset_range_list, schema_item)

    def __validate_schema_offsets(self, allowed_offset_range_list, schema_data, data_item):
        # Iterate through each schema item
        for schema_item, schema_item_value in schema_data.items():
            is_offset_valid = self.__validate_schema_item_offset(
                allowed_offset_range_list, schema_item_value)

            if (not is_offset_valid):
                logger.error(
                    "Specified offset is out of range for %s in %s!",
                    schema_item, data_item)
                AbnormalTermination("Specified offset is out of range for {} in {}!".format(
                    schema_item, data_item), nverror.NvError_InvalidArgument)


    def __validate_schema_key(self):
        for section in self.schema.keys():
            data_dict = {}
            for schema_data in self.schema[section]["data"].keys():
                if (schema_data in data_dict.keys()):
                    logger.error(
                        "Duplicate data item %s is not allowed", schema_data)
                    AbnormalTermination(
                        "Duplicate data item %s is not allowed".format(
                            schema_data))
                else:
                    data_dict[schema_data] = 1

    def __validate_schema_structure(self, allowed_schema, input_cfg):
        if(isinstance(input_cfg, list)):
            if (isinstance(allowed_schema, list)):

                if (len(input_cfg) == 0 or len(allowed_schema) == 0):
                    AbnormalTermination("Invalid input schema found", nverror.NvError_InvalidState)

                for item in input_cfg:
                    self.__validate_schema_structure(allowed_schema[0], item)

            elif (isinstance(allowed_schema, dict)):
                for item in input_cfg:
                    self.__validate_schema_structure(allowed_schema, item)

        elif(isinstance(input_cfg, dict)):
            for key, value in input_cfg.items():
                key_matched = False
                schema_key = None
                for key_pattern in allowed_schema.keys():
                    if(re.match(key_pattern, key) != None):
                        key_matched = True
                        schema_key = key_pattern
                        break

                if (not key_matched):
                    logger.error(
                        "Option %s is schema is not supported!",
                        key)
                    AbnormalTermination("Invalid input configuration!",
                                        nverror.NvError_InvalidArgument)

                if (value == None):
                    continue

                # Decode unicode value into utf-8
                if (not isinstance(value, bool) and not isinstance(value, dict) and
                    not isinstance(value, str) and not isinstance(value, int) and
                    not isinstance(value, list)):
                    # This means it is unicode value
                    input_cfg[key] = value = value.encode('utf-8')

                if (key == "data"):
                    for key, value in input_cfg[key].items():
                        self.__validate_schema_structure(
                            allowed_schema[schema_key], {key: value})
                else:
                    self.__validate_schema_structure(
                        allowed_schema[schema_key], value)
        else:
            if (input_cfg != None and re.match(allowed_schema, str(input_cfg)) == None):
                logger.error(
                    "Invalid value found in the schema: %s", str(input_cfg))
                AbnormalTermination(
                    "Invalid value found in the schema: {}".format(input_cfg),
                    nverror.NvError_InvalidArgument)

    def __validate_schema(self, allowed_schema):
        # Validate schema structure
        for key, value in self.schema.items():
            self.__validate_schema_structure(allowed_schema, {key: value})

        # Validate offset values are within the allowed range
        # Iterate through each section in the schema
        for section in self.schema.keys():
            # Get allowed offset range for the section
            allowed_offset_range = self.schema[section]["allowed-offset-ranges"]

            for data_item, data_schema_dict in self.schema[section]["data"].items():
                self.__validate_schema_offsets(
                    allowed_offset_range, data_schema_dict["schema"],
                    data_item)

        # Validate schema key is only present in one section
        self.__validate_schema_key()

    def load_schema(self, schema_file):
        """Loads the schema file

        Args:
            schema_file (str): File path of schema file
        """
        with open(schema_file, "r") as in_schema_f:
            self.schema = json.load(in_schema_f)

        allowed_schema = {
            "[a-z0-9-]*": {
                "allowed-offset-ranges": [[r"\d+", r"\d+"]],
                "data": {
                    "[a-zA-Z0-9]*": {
                        "parser-metadata": {
                            "data-separator-string": r"-\|\s|-|\s",
                            "number-of-words": r"\d+"
                        },
                        "schema": {
                            "version": {
                                "type": r"unsigned-char",
                                "format-string": r"B",
                                "offset": r"\d+"
                            },
                            "value": [{
                                "type": r"decimal|char|hex|string|blob|unsigned-char",
                                "format-string": r"I|H|c|s|Q|B",
                                "offset": r"\d+",
                                "min-len": r"\d+",
                                "max-len": r"\d+"
                            }]
                        }
                    }
                }
            }
        }

        self.__validate_schema(allowed_schema)

        self.__create_section_processor_dict()

    def is_schema_loaded(self):
        return self.schema != None

    def __create_section_processor_dict(self):
        if (self.is_schema_loaded()):
            for section_type in self.schema.keys():
                # Get the schema for the section
                section_schema = self.schema[section_type]
                section_processor = SectionProcessor.get_schema_section_processor(
                    section_schema, section_type)

                if (section_processor is None):
                    AbnormalTermination("{} type is not supported".format(section_type))

                self.section_processor_dict[section_type] = section_processor

    def process(self, data_file, output_blob_file_name):
        """Processes customer data and schema file and generates binary blob file

        Args:
            data_file (str): File path of customer data values file
            output_blob_file_name (str): File path of output blob file

        Raises:
            RuntimeError: If data item is not supported by the schema
            RuntimeError: If invalid input data is given
            RuntimeError: If  schema is not loaded before calling this function
        """

        if (self.schema == None):
            AbnormalTermination(
                "Customer data schema is not loaded before processing the data!",
                nverror.NvError_InvalidState)

        with open(data_file, "r") as in_data_f:
            data = json.load(in_data_f)

        # Cleanup the processor bytes if any
        for processor in self.section_processor_dict.values():
            processor.clean()

        with open(output_blob_file_name, "wb") as f_output_blob:
            for section_type in data.keys():
                # Get the section processor object
                current_section_processor = self.section_processor_dict[section_type]

                for key, value in data[section_type].items():
                    if (current_section_processor.can_handle(key)):
                        current_section_processor.process(key, value)
                    else:
                        # if data key is not present in schema, raise exception
                        AbnormalTermination(
                            "Data item {} is not supported in section {}".format(key, section_type),
                            nverror.NvError_InvalidArgument)

            # Get bytes from each processor and write it to the blob
            for processor in self.section_processor_dict.values():
                section_bytes = processor.get_section_bytes()
                f_output_blob.write(section_bytes)


class SectionProcessor():
    """Abstract base class to process customer data section

       Section specific processor needs to register with base class
       and must inherit abstract methods
    """

    # Maintains registry of supported section processors
    section_processor_registry = {}

    __metaclass__ = ABCMeta

    def __init__(self, schema, schema_type):
        self.schema = schema["data"]
        self.bytes = bytearray()
        self.schema_type = schema_type

    @classmethod
    def register(cls, section_type):
        """Method to register customer data section processor sub classes

        Args:
            section_type (str): Type of the customer data section
        """
        def wrapper(wrapped_class):
            if section_type in cls.section_processor_registry:
                logger.warning(
                    'Section processor %s already exists. it will be replaced',
                    section_type)
            cls.section_processor_registry[section_type] = wrapped_class
            return wrapped_class
        return wrapper

    def clean(self):
        self.bytes = bytearray()

    def __process_data(self, schema, data_value):
        data_processor = DataProcessor.get_data_processor(data_value, schema)

        if (data_processor == None):
            AbnormalTermination("Type {} is not supported for {}".format(
                    schema["type"], data_value),
                    nverror.NvError_InvalidArgument)

        data_processor.process()

        self.bytes += data_processor.get_bytes()

    def process(self, key, value):
        """Process sections data values based on schema and collect
           all the data bytes for section

        Args:
            key (str): Customer data type key
            value (str): Customer data value string for the given data type

        Raises:
            Exception: If key is not supported by the section processor
            Exception: If the data processor for the format is not supported
            Exception: If data value is not in the expected format specified
                       in the schema
        """

        if (key not in self.schema.keys()):
            AbnormalTermination("{} is not supported".format(key))

        # If version is present in the schema, process version
        if ("version" in self.schema[key]["schema"].keys()):
            # Default version to 1
            version = 1

            if (isinstance(value, dict)):
                if ("version" not in value.keys() or "value" not in value.keys()):
                    logger.error("Invalid format for data {} found".format(key))
                    AbnormalTermination("Invalid format for data {} found".format(key))

                version = value["version"]
                value = value["value"]

            self.__process_data(self.schema[key]["schema"]["version"], version)

        # Parse the user input data
        parsed_metadata = CustomerInputDataParser.parse_data(
            value, self.schema[key]["parser-metadata"])

        if (len(parsed_metadata) != len(self.schema[key]["schema"]["value"])):
            AbnormalTermination(
                "Data item format for {} is not supported: {}".format(
                    key, value), nverror.NvError_InvalidArgument)

        for schema_item, data_value in zip(
                self.schema[key]["schema"]["value"],
                parsed_metadata):
            if (isinstance(schema_item, list)):
                # More than one schema for the data item found, so process one by one until
                # one that can be used for parsing the input data
                # throw error otherwise
                for schema_item_index in range(len(schema_item)):
                    try:
                        self.__process_data(schema_item[schema_item_index], data_value)
                        break
                    except Exception as err:
                        # if the last schema is processed, then re-raise the exception
                        # otherwise continue
                        if (schema_item_index == len(schema_item) - 1):
                            raise err
            else:
                try:
                    self.__process_data(schema_item, data_value)
                except Exception as err:
                    err_msg = "Unable to process {}: {}. Please make sure correct format is used.".format(key, data_value)
                    logger.error(err_msg + str(err))
                    AbnormalTermination(err_msg, nverror.NvError_InvalidArgument)

    def can_handle(self, key):
        """Returns true if the section processor can handle the data type

        Args:
            key (str): Data type string

        Returns:
            bool: Return True if the data type is supported, False otherwise
        """
        return key in self.schema.keys()

    @classmethod
    def get_schema_section_processor(cls, schema, section_type):
        """Static factory method to instatiate type specific processor

        Args:
            schema (dict): Customer data section schema
            section_type (str): Type of customer data section

        Returns:
            SectionProcessor object: Type specific section processor object if found
            None: If section processor for the section type is not found

        """
        if (section_type not in cls.section_processor_registry):
            logger.warning(
                "Schema processor for %s is not supported", section_type)
            return None

        section_processor_class = cls.section_processor_registry[section_type]
        section_processor = section_processor_class(schema)
        return section_processor

    def get_section_bytes(self):
        """Get the customer data section bytes

        Returns:
            bytearray: Array of bytes for customer data section
        """
        # Add section type integer
        section_bytes = bytearray()
        if (len(self.bytes) != 0):
            section_bytes += bytearray(struct.pack("I", self.get_section_id()))

            # Add section length integer
            section_bytes += bytearray(struct.pack("I", len(self.bytes)))

            # Add the actual section bytes data
            section_bytes += self.bytes

        return section_bytes

    @abstractmethod
    def get_section_id(self):
        """Get the id for section
           Each section needs to have unique id

        Raises:
            RuntimeError: Call chain should not reach here as sub classes need to inherit this method
        """
        raise RuntimeError("Can't call abstract method. It must be inherited in the section specific processors")


@SectionProcessor.register('customer-data-unsigned')
class UnsignedSectionProcessor(SectionProcessor):
    """Section processor class for unsigned customer data
    """

    def __init__(self, schema):
        super(UnsignedSectionProcessor, self).__init__(
            schema, "customer-data-unsigned")

    def get_section_id(self):
        return 1

@SectionProcessor.register('customer-data-signed')
class SignedSectionProcessor(SectionProcessor):
    """Section processor class for unsigned customer data
    """

    def __init__(self, schema):
        super(SignedSectionProcessor, self).__init__(
            schema, "customer-data-signed")

    def get_section_id(self):
        return 2
