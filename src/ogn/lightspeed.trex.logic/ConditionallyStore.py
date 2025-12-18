# GENERATED FILE - DO NOT EDIT
# This file is a stub for OmniGraph editor compatibility, and is not used by the Remix Runtime.
import omni.graph.core as og

from lightspeed.trex.logic.ogn._impl.type_resolution import resolve_types, standard_compute, standard_initialize


class ConditionallyStore:
    # fmt: off
    VALID_COMBINATIONS = [
        {"inputs:input": og.Type(og.BaseDataType.BOOL), "outputs:output": og.Type(og.BaseDataType.BOOL)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT), "outputs:output": og.Type(og.BaseDataType.FLOAT)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 2), "outputs:output": og.Type(og.BaseDataType.FLOAT, 2)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 3), "outputs:output": og.Type(og.BaseDataType.FLOAT, 3)},
        {"inputs:input": og.Type(og.BaseDataType.FLOAT, 4), "outputs:output": og.Type(og.BaseDataType.FLOAT, 4)},
        {"inputs:input": og.Type(og.BaseDataType.TOKEN), "outputs:output": og.Type(og.BaseDataType.TOKEN)},
        {"inputs:input": og.Type(og.BaseDataType.RELATIONSHIP), "outputs:output": og.Type(og.BaseDataType.RELATIONSHIP)},
    ]
    # fmt: on

    compute = standard_compute
    initialize = standard_initialize

    @staticmethod
    def on_connection_type_resolve(node) -> None:
        resolve_types(node, ConditionallyStore.VALID_COMBINATIONS)
