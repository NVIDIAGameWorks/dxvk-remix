# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
import omni.graph.core as og

from lightspeed.trex.logic.ogn._impl.type_resolution import resolve_types, standard_compute, standard_initialize


class Select:
    # fmt: off
    VALID_COMBINATIONS = [
        {"inputs:inputA": og.Type(og.BaseDataType.BOOL), "inputs:inputB": og.Type(og.BaseDataType.BOOL), "outputs:output": og.Type(og.BaseDataType.BOOL)},
        {"inputs:inputA": og.Type(og.BaseDataType.FLOAT), "inputs:inputB": og.Type(og.BaseDataType.FLOAT), "outputs:output": og.Type(og.BaseDataType.FLOAT)},
        {"inputs:inputA": og.Type(og.BaseDataType.FLOAT, 2), "inputs:inputB": og.Type(og.BaseDataType.FLOAT, 2), "outputs:output": og.Type(og.BaseDataType.FLOAT, 2)},
        {"inputs:inputA": og.Type(og.BaseDataType.FLOAT, 3), "inputs:inputB": og.Type(og.BaseDataType.FLOAT, 3), "outputs:output": og.Type(og.BaseDataType.FLOAT, 3)},
        {"inputs:inputA": og.Type(og.BaseDataType.FLOAT, 4), "inputs:inputB": og.Type(og.BaseDataType.FLOAT, 4), "outputs:output": og.Type(og.BaseDataType.FLOAT, 4)},
        {"inputs:inputA": og.Type(og.BaseDataType.TOKEN), "inputs:inputB": og.Type(og.BaseDataType.TOKEN), "outputs:output": og.Type(og.BaseDataType.TOKEN)},
        {"inputs:inputA": og.Type(og.BaseDataType.RELATIONSHIP), "inputs:inputB": og.Type(og.BaseDataType.RELATIONSHIP), "outputs:output": og.Type(og.BaseDataType.RELATIONSHIP)},
    ]
    # fmt: on

    compute = standard_compute
    initialize = standard_initialize

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        resolve_types(node, Select.VALID_COMBINATIONS)
