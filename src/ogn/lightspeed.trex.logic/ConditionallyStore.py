# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.ConditionallyStoreDatabase import ConditionallyStoreDatabase


class ConditionallyStore:
    @staticmethod
    def compute(_db: ConditionallyStoreDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: input=bool, output=bool, store=bool, storedValue=bool
        # Combination 2: input=float, output=float, store=bool, storedValue=float
        # Combination 3: input=float[2], output=float[2], store=bool, storedValue=float[2]
        # Combination 4: input=float[3], output=float[3], store=bool, storedValue=float[3]
        # Combination 5: input=float[4], output=float[4], store=bool, storedValue=float[4]
        # Combination 6: input=uint, output=uint, store=bool, storedValue=uint
        # Combination 7: input=token, output=token, store=bool, storedValue=token
        # Combination 8: input=target, output=target, store=bool, storedValue=target

        # Get attributes
        input_input = node.get_attribute("inputs:input")
        input_storedValue = node.get_attribute("inputs:storedValue")
        output_output = node.get_attribute("outputs:output")

        # Get current types of connected attributes
        type_input = input_input.get_resolved_type()
        type_storedValue = input_storedValue.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_input == og.Type(og.BaseDataType.BOOL) and 
            type_storedValue == og.Type(og.BaseDataType.BOOL)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.BOOL))
        elif (type_input == og.Type(og.BaseDataType.FLOAT) and 
            type_storedValue == og.Type(og.BaseDataType.FLOAT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_storedValue == og.Type(og.BaseDataType.FLOAT, 2)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_storedValue == og.Type(og.BaseDataType.FLOAT, 3)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_input == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_storedValue == og.Type(og.BaseDataType.FLOAT, 4)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
        elif (type_input == og.Type(og.BaseDataType.UINT) and 
            type_storedValue == og.Type(og.BaseDataType.UINT)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.UINT))
        elif (type_input == og.Type(og.BaseDataType.TOKEN) and 
            type_storedValue == og.Type(og.BaseDataType.TOKEN)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.TOKEN))
        elif (type_input == og.Type(og.BaseDataType.RELATIONSHIP) and 
            type_storedValue == og.Type(og.BaseDataType.RELATIONSHIP)):
            output_output.set_resolved_type(og.Type(og.BaseDataType.RELATIONSHIP))
