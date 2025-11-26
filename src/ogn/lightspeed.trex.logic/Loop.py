# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
from __future__ import annotations

from typing import TYPE_CHECKING

import omni.graph.core as og

if TYPE_CHECKING:
    from lightspeed.trex.logic.ogn.ogn.LoopDatabase import LoopDatabase


class Loop:
    @staticmethod
    def compute(_db: LoopDatabase):
        return True

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        """Resolve flexible types based on connected attribute types."""
        # Valid type combinations for this component:
        # Combination 1: isReversing=bool, loopedValue=float, loopingType=uint, maxRange=float, minRange=float, value=float
        # Combination 2: isReversing=bool, loopedValue=float[2], loopingType=uint, maxRange=float[2], minRange=float[2], value=float[2]
        # Combination 3: isReversing=bool, loopedValue=float[3], loopingType=uint, maxRange=float[3], minRange=float[3], value=float[3]
        # Combination 4: isReversing=bool, loopedValue=float[4], loopingType=uint, maxRange=float[4], minRange=float[4], value=float[4]

        # Get attributes
        input_value = node.get_attribute("inputs:value")
        input_minRange = node.get_attribute("inputs:minRange")
        input_maxRange = node.get_attribute("inputs:maxRange")
        output_loopedValue = node.get_attribute("outputs:loopedValue")

        # Get current types of connected attributes
        type_value = input_value.get_resolved_type()
        type_minRange = input_minRange.get_resolved_type()
        type_maxRange = input_maxRange.get_resolved_type()

        # Check all valid type combinations and resolve output types
        if (type_value == og.Type(og.BaseDataType.FLOAT) and 
            type_minRange == og.Type(og.BaseDataType.FLOAT) and 
            type_maxRange == og.Type(og.BaseDataType.FLOAT)):
            output_loopedValue.set_resolved_type(og.Type(og.BaseDataType.FLOAT))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_minRange == og.Type(og.BaseDataType.FLOAT, 2) and 
            type_maxRange == og.Type(og.BaseDataType.FLOAT, 2)):
            output_loopedValue.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 2))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_minRange == og.Type(og.BaseDataType.FLOAT, 3) and 
            type_maxRange == og.Type(og.BaseDataType.FLOAT, 3)):
            output_loopedValue.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 3))
        elif (type_value == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_minRange == og.Type(og.BaseDataType.FLOAT, 4) and 
            type_maxRange == og.Type(og.BaseDataType.FLOAT, 4)):
            output_loopedValue.set_resolved_type(og.Type(og.BaseDataType.FLOAT, 4))
