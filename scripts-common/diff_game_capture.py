import argparse
import datetime
import os
import pathlib
import sys
import traceback

def _log(msg):
    """Print with timestamp, flushing immediately so CI log captures don't lose output on crash."""
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{ts}] {msg}", flush=True)

_log(f"Python {sys.version}")
_log(f"Working directory: {os.getcwd()}")

import numpy
_log("numpy imported OK")

# Serialize USD work to avoid TBB worker-thread crashes on CI. Must precede pxr import.
os.environ["PXR_WORK_THREAD_LIMIT"] = "1"

try:
    from pxr import Usd
    v = Usd.GetVersion()
    _log(f"pxr.Usd imported OK  (pxr version {v[0]}.{v[1]}.{v[2]})")
except Exception as e:
    _log(f"ERROR: Failed to import USD Python bindings: {e}")
    _log("Make sure they are installed and on the PYTHONPATH.")
    traceback.print_exc()
    sys.exit(1)

from enum import Enum

parser = argparse.ArgumentParser()
parser.add_argument("--golden", required=True)
parser.add_argument("--other", required=True)
parser.add_argument("--requireFlattened", action="store_true", default=False, required=False)
parser.add_argument("--floatTolerance", default=1.0, type=float, required=False)
args = parser.parse_args()

def _log_file_info(label, path):
    abs_path = os.path.abspath(path)
    _log(f"{label}: {abs_path}")
    if os.path.exists(abs_path):
        stat = os.stat(abs_path)
        mtime = datetime.datetime.fromtimestamp(stat.st_mtime).strftime("%Y-%m-%d %H:%M:%S")
        _log(f"  size={stat.st_size} bytes  mtime={mtime}")
    else:
        _log(f"  *** FILE DOES NOT EXIST ***")

_log_file_info("Golden USD", args.golden)
_log_file_info("Other USD ", args.other)
_log(f"floatTolerance={args.floatTolerance}%  requireFlattened={args.requireFlattened}")

class CaptureDiff:
    __requireFlattened = args.requireFlattened
    floatTolerance = args.floatTolerance/100

    class Result(Enum):
        Success = 0
        Failure = 1
        Extra = 2
        Missing = 3
        Diff = 4

    def print(self):
        if self.__result == CaptureDiff.Result.Success:
            _log("SUCCESS: Stages are the same!")
        else:
            _log("ERROR: Stages are different!")
            diff.__print_errors()

    def passed(self):
        return self.__result == CaptureDiff.Result.Success

    def __init__(self, goldenStagePath, otherStagePath):
        self.__goldenStagePath = goldenStagePath
        self.__otherStagePath = otherStagePath
        self.__diffPrims = []
        self.__result = CaptureDiff.Result.Failure
        [bLoadSuccess,msg] = self.__loadStages()
        if not bLoadSuccess:
            _log(msg)
            return
        self.__result = self.__diff()

    def __loadStages(self):
        _log("Opening golden stage...")
        t0 = datetime.datetime.now()
        try:
            self._goldenStage = Usd.Stage.Open(self.__goldenStagePath)
            elapsed = (datetime.datetime.now() - t0).total_seconds()
            _log(f"Golden stage opened OK ({elapsed:.3f}s)")
        except Exception as e:
            elapsed = (datetime.datetime.now() - t0).total_seconds()
            _log(f"ERROR: Failed to open golden stage after {elapsed:.3f}s — {e}")
            return [False, f"ERROR: Failed to open golden stage: {self.__goldenStagePath}"]
        _log("Opening other stage...")
        t0 = datetime.datetime.now()
        try:
            self._otherStage = Usd.Stage.Open(self.__otherStagePath)
            elapsed = (datetime.datetime.now() - t0).total_seconds()
            _log(f"Other stage opened OK ({elapsed:.3f}s)")
        except Exception as e:
            elapsed = (datetime.datetime.now() - t0).total_seconds()
            _log(f"ERROR: Failed to open other stage after {elapsed:.3f}s — {e}")
            return [False, f"ERROR: Failed to open other stage: {self.__otherStagePath}"]
        return [True,""]

    def __diff(self):
        primSdfPaths = set()
        self.__bGoldenStageHasReferences = False
        goldenPrimCount = 0
        for goldenPrim in self._goldenStage.Traverse():
            goldenPrimCount += 1
            sdfPath = goldenPrim.GetPrimPath()
            if CaptureDiff.__requireFlattened and goldenPrim.HasAuthoredReferences() and not self.__bGoldenStageHasReferences:
                self.__bGoldenStageHasReferences = True
                _log("ERROR: Golden stage has references. Capture was not flattened prior to diff, which may have resulted in several warnings at load and an incomplete diff.")
            primSdfPaths.add(sdfPath)
        _log(f"Golden stage: {goldenPrimCount} prims traversed")

        self.__bOtherStageHasReferences = False
        otherPrimCount = 0
        for otherPrim in self._otherStage.Traverse():
            otherPrimCount += 1
            sdfPath = otherPrim.GetPrimPath()
            if CaptureDiff.__requireFlattened and otherPrim.HasAuthoredReferences() and not self.__bOtherStageHasReferences:
                self.__bOtherStageHasReferences = True
                _log("ERROR: Other stage has references. Capture was not flattened prior to diff, which may have resulted in several warnings at load and an incomplete diff.")
            primSdfPaths.add(sdfPath)
        _log(f"Other stage:  {otherPrimCount} prims traversed")
        _log(f"Union of prim paths: {len(primSdfPaths)}")

        for primSdfPath in primSdfPaths:
            prim = CaptureDiff.Prim(primSdfPath, self._goldenStage, self._otherStage)
            if prim.result != CaptureDiff.Result.Success:
                self.__diffPrims.append(prim)

        missingCount = sum(1 for p in self.__diffPrims if p.result == CaptureDiff.Result.Missing)
        extraCount   = sum(1 for p in self.__diffPrims if p.result == CaptureDiff.Result.Extra)
        diffCount    = sum(1 for p in self.__diffPrims if p.result == CaptureDiff.Result.Diff)
        _log(f"Diff summary: {len(primSdfPaths) - len(self.__diffPrims)} prims matched, "
             f"{missingCount} missing, {extraCount} extra, {diffCount} with differing attributes")

        if len(self.__diffPrims) > 0:
            return CaptureDiff.Result.Failure
        return CaptureDiff.Result.Success

    class Prim:
        def __init__(self, sdfPath, goldenStage, otherStage):
            self.sdfPath = sdfPath
            self.diffAttrs = []
            self.result = self.__diff(goldenStage, otherStage)

        def __diff(self, goldenStage, otherStage):
            goldenPrim = goldenStage.GetPrimAtPath(self.sdfPath)
            if not goldenPrim:
                return CaptureDiff.Result.Extra
            otherPrim = otherStage.GetPrimAtPath(self.sdfPath)
            if not otherPrim:
                return CaptureDiff.Result.Missing
            return self.__compareUsd(goldenPrim, otherPrim)

        def __compareUsd(self, goldenPrim, otherPrim):
            attrNames = set()
            for goldenAttr in goldenPrim.GetAttributes():
                attrNames.add(goldenAttr.GetName())
            for otherAttr in otherPrim.GetAttributes():
                attrNames.add(otherAttr.GetName())
            for attrName in attrNames:
                attr = CaptureDiff.Attribute(attrName, goldenPrim, otherPrim)
                if attr.result != CaptureDiff.Result.Success:
                    self.diffAttrs.append(attr)
            if len(self.diffAttrs) > 0:
                return CaptureDiff.Result.Diff
            return CaptureDiff.Result.Success

    class Attribute:
        def __init__(self, name, goldenPrim, otherPrim):
            self.name = name
            self.goldenVal = None
            self.otherVal = None
            self.relDiff = None
            self.result = self.__diff(goldenPrim, otherPrim)

        def __diff(self, goldenPrim, otherPrim):
            goldenAttr = goldenPrim.GetAttribute(self.name)
            if not goldenAttr:
                return CaptureDiff.Result.Extra
            otherAttr = otherPrim.GetAttribute(self.name)
            if not otherAttr:
                return CaptureDiff.Result.Missing
            return self.__compareUsd(goldenAttr, otherAttr)

        def __compareUsd(self, goldenAttr, otherAttr):
            goldenType = goldenAttr.GetTypeName()
            otherType = otherAttr.GetTypeName()
            if(goldenType != otherType):
                self.goldenVal = f"(type) {goldenType}"
                self.otherVal  = f"(type) {otherType}"
                return CaptureDiff.Result.Diff
            goldenVal = goldenAttr.Get()
            otherVal = otherAttr.Get()
            # Need to reduce asset paths to just the hash names
            # The rest of the paths will certainly cause diff
            if goldenType == "asset":
                goldenVal = "".join(str(goldenVal).split("@"))
                goldenVal = pathlib.Path(str(goldenVal)).name
                otherVal = "".join(str(goldenVal).split("@"))
                otherVal = pathlib.Path(str(goldenVal)).name
            if not self.__compareValues(str(goldenType), goldenVal, otherVal):
                self.goldenVal = goldenVal
                self.otherVal = otherVal
                # Relative diff for float scalars, to help triage tolerance
                if self.__is_float_type(str(goldenType)):
                    try:
                        g, o = float(goldenVal), float(otherVal)
                        if g != 0:
                            self.relDiff = abs((g - o) / g)
                        else:
                            self.relDiff = abs(g - o)
                    except (TypeError, ValueError):
                        pass
                return CaptureDiff.Result.Diff
            return CaptureDiff.Result.Success

        def __compareValues(self, type, goldenVal, otherVal):
            if self.__is_array(goldenVal, type):
                return self.__compare_array(type, goldenVal, otherVal)
            else:
                return self.__compare_scalar(type, goldenVal, otherVal)

        def __compare_array(self, type, goldenArray, otherArray):
            if not self.__are_not_none(goldenArray, otherArray):
                if self.__are_valid_none(goldenArray, otherArray):
                    return True
                else:
                    return False
            if len(goldenArray) != len(otherArray):
                return False
            if len(goldenArray) == 0:
                return True
            memberType = self.__parse_member_type(type)
            bMemberIsArray = self.__is_array(goldenArray[0],memberType)
            if(bMemberIsArray):
                bMembersMatch = True
                for i in range(len(goldenArray)):
                    bMembersMatch = bMembersMatch and \
                        self.__compare_array(memberType, goldenArray[i], otherArray[i])
                return bMembersMatch
            else:
                bMembersMatch = True
                for i in range(len(goldenArray)):
                    bMembersMatch = bMembersMatch and \
                        self.__compare_scalar(memberType, goldenArray[i], otherArray[i])
                return bMembersMatch

        def __compare_scalar(self, type, goldenScalar, otherScalar):
            if not self.__are_not_none(goldenScalar, otherScalar):
                return self.__are_valid_none(goldenScalar, otherScalar)
            if self.__is_float_type(type):
                return self.__float_diff(goldenScalar, otherScalar)
            return goldenScalar == otherScalar

        def __is_array(self, potentialArray, memberType):
            return self.__is_py_array(potentialArray) or \
                (memberType.find("[]") > -1) or \
                (memberType.find("3f") > -1) or \
                (memberType.find("2f") > -1) or \
                (memberType.find("float3") > -1) or \
                (memberType.find("double3") > -1)

        def __is_py_array(self, potentialArray):
            return hasattr(potentialArray, "__len__") and not isinstance(potentialArray, str)

        def __parse_member_type(self, arrayType):
            if str(arrayType).find("[]") > -1:
                return str(type).split("[]")[0]
            if (str(arrayType).find("3f") > -1) or \
               (str(arrayType).find("2f") > 1) or \
               arrayType :
                return "float"

        def __are_not_none(self, golden, other):
            return golden is not None and other is not None

        def __are_valid_none(self, golden, other):
            return golden is None and other is None

        def __is_float_type(self, type):
            return type == "float" or type == "texCoord2f" or type == "normal3f"

        def __float_diff(self, golden, other):
            if numpy.isnan(golden) or numpy.isnan(other):
                return numpy.isnan(golden) and numpy.isnan(other)
            absDiff = abs(golden-other)
            if(golden != 0):
                return abs(absDiff / golden) <= CaptureDiff.floatTolerance
            else:
                return absDiff <= CaptureDiff.floatTolerance

    def __print_errors(self):
        missingPrims = []
        extraPrims = []
        diffPrims = []
        for diffPrim in self.__diffPrims:
            if diffPrim.result == CaptureDiff.Result.Missing:
                missingPrims.append(diffPrim)
            elif diffPrim.result == CaptureDiff.Result.Extra:
                extraPrims.append(diffPrim)
            elif diffPrim.result == CaptureDiff.Result.Diff:
                diffPrims.append(diffPrim)
        if(len(missingPrims) > 0):
            _log(f"MISSING PRIMS ({len(missingPrims)})")
            for missingPrim in missingPrims:
                _log("  " + str(missingPrim.sdfPath))
        if(len(extraPrims) > 0):
            _log(f"EXTRA PRIMS ({len(extraPrims)})")
            for extraPrim in extraPrims:
                _log("  " + str(extraPrim.sdfPath))
        if(len(diffPrims) > 0):
            _log(f"DIFF PRIMS ({len(diffPrims)})")
            for diffPrim in diffPrims:
                _log("  " + str(diffPrim.sdfPath))
                missingAttrs = []
                extraAttrs = []
                diffAttrs = []
                for diffAttr in diffPrim.diffAttrs:
                    if diffAttr.result == CaptureDiff.Result.Missing:
                        missingAttrs.append(diffAttr)
                    elif diffAttr.result == CaptureDiff.Result.Extra:
                        extraAttrs.append(diffAttr)
                    elif diffAttr.result == CaptureDiff.Result.Diff:
                        diffAttrs.append(diffAttr)
                if(len(missingAttrs) > 0):
                    _log("    MISSING ATTRS")
                    for missingAttr in missingAttrs:
                        _log("      " + str(missingAttr.name))
                if(len(extraAttrs) > 0):
                    _log("    EXTRA ATTRS")
                    for extraAttr in extraAttrs:
                        _log("      " + str(extraAttr.name))
                if(len(diffAttrs) > 0):
                    _log("    DIFF ATTRS")
                    for diffAttr in diffAttrs:
                        relDiffStr = f"  rel_diff={diffAttr.relDiff:.6f}  tolerance={CaptureDiff.floatTolerance:.6f}" \
                            if diffAttr.relDiff is not None else ""
                        _log("      " + str(diffAttr.name) + relDiffStr)
                        _log("        Golden: " + str(diffAttr.goldenVal))
                        _log("        Other:  " + str(diffAttr.otherVal))

try:
    _log("Starting diff...")
    diff = CaptureDiff(args.golden, args.other)
    diff.print()
    passed = diff.passed()
except Exception as e:
    _log("ERROR: Exception during diff: " + str(e))
    traceback.print_exc()
    sys.exit(1)

# os._exit() bypasses USD's C++ teardown, which has crashed on CI and corrupted the
# exit code after a successful diff. It skips stdio flush, so flush first.
sys.stdout.flush()
sys.stderr.flush()
os._exit(0 if passed else 1)
