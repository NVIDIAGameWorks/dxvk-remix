import argparse
import pathlib
from pxr import Usd
from enum import Enum

parser = argparse.ArgumentParser()
parser.add_argument("--golden", required=True)
parser.add_argument("--other", required=True)
parser.add_argument("--requireFlattened", action="store_true", default=False, required=False)
parser.add_argument("--floatTolerance", default=1.0, type=float, required=False)
args = parser.parse_args()

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
            print("SUCCESS: Stages are the same!")
        else:
            print("ERROR: Stages are different!")
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
            print(msg)
            return
        self.__result = self.__diff()

    def __loadStages(self):
        try:
            self._goldenStage = Usd.Stage.Open(self.__goldenStagePath)
        except:
            return [False,("ERROR: Failed to open golden stage: " + self.__goldenStagePath)]
        try:
            self._otherStage = Usd.Stage.Open(self.__otherStagePath)
        except:
            return [False,("ERROR: Failed to open compare stage: " + self.__otherStagePath)]
        return [True,""]

    def __diff(self):
        primSdfPaths = set()
        self.__bGoldenStageHasReferences = False
        for goldenPrim in self._goldenStage.Traverse():
            sdfPath = goldenPrim.GetPrimPath()
            if CaptureDiff.__requireFlattened and goldenPrim.HasAuthoredReferences() and not self.__bGoldenStageHasReferences:
                self.__bGoldenStageHasReferences = True
                print("ERROR: Golden stage has references. Capture was not flattened prior to diff, which may have resulted in several warnings at load and an incomplete diff.")
            primSdfPaths.add(sdfPath)
        self.__bOtherStageHasReferences = False
        for otherPrim in self._otherStage.Traverse():
            sdfPath = otherPrim.GetPrimPath()
            if CaptureDiff.__requireFlattened and otherPrim.HasAuthoredReferences() and not self.__bOtherStageHasReferences:
                self.__bOtherStageHasReferences = True
                print("ERROR: Other stage has references. Capture was not flattened prior to diff, which may have resulted in several warnings at load and an incomplete diff.")
            primSdfPaths.add(sdfPath)
        for primSdfPath in primSdfPaths:
            prim = CaptureDiff.Prim(primSdfPath, self._goldenStage, self._otherStage)
            if prim.result != CaptureDiff.Result.Success:
                self.__diffPrims.append(prim)
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
            if not CaptureDiff.Attribute.__compareValues(goldenType, goldenVal, otherVal): 
                self.goldenVal = goldenVal
                self.otherVal = otherVal
                return CaptureDiff.Result.Diff
            return CaptureDiff.Result.Success
        
        @staticmethod
        def __compareValues(type, goldenVal, otherVal):
            if type.isArray:
                return CaptureDiff.Attribute.__compare_array(type, goldenVal, otherVal)
            else:
                return CaptureDiff.Attribute.__compare_scalar(type, goldenVal, otherVal)

        @staticmethod
        def __compare_array(type, goldenArray, otherArray):
            if not CaptureDiff.Attribute.__is_not_none(goldenArray, otherArray):
                if CaptureDiff.Attribute.__is_valid_none(goldenArray, otherArray):
                    return True
                else:
                    return False
            if len(goldenArray) != len(otherArray):
                return False
            if len(goldenArray) == 0:
                return True
            bMemberIsArray = False
            if CaptureDiff.Attribute.__is_py_array(goldenArray[0]) > 0:
                bMemberIsArray = True
            for i in range(len(goldenArray)-1):
                if bMemberIsArray:
                    if not CaptureDiff.Attribute.__compare_array(type, goldenArray[i], otherArray[i]):
                        return False
                elif not CaptureDiff.Attribute.__compare_scalar(type, goldenArray[i], otherArray[i]):
                    return False
            return True
        
        @staticmethod
        def __is_py_array(goldenArray):
            return hasattr(goldenArray, "__len__") and not isinstance(goldenArray, str)
        
        @staticmethod
        def __compare_scalar(type, goldenScalar, otherScalar):
            if not CaptureDiff.Attribute.__is_not_none(goldenScalar, otherScalar):
                if CaptureDiff.Attribute.__is_valid_none(goldenScalar, otherScalar):
                    return True
                else:
                    return False
            if type == "float":
                if not CaptureDiff.Attribute.__float_diff(goldenScalar, otherScalar):
                    return False
            return goldenScalar == otherScalar
        
        @staticmethod
        def __is_not_none(golden, other):
            if golden is not None and other is not None:
                return True

        @staticmethod
        def __is_valid_none(golden, other):
            if golden is None and other is None:
                return True
            return False

        @staticmethod
        def __float_diff(golden, other):
            absDiff = abs(golden-other)
            if(golden != 0):
                if absDiff / golden > CaptureDiff.floatTolerance:
                    return False
            elif absDiff > CaptureDiff.floatTolerance:
                return False
            return True

    
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
            print("MISSING PRIMS")
            for missingPrim in missingPrims:
                print("  " + str(missingPrim.sdfPath))
        if(len(extraPrims) > 0):
            print("EXTRA PRIMS")
            for extraPrim in extraPrims:
                print("  " + str(extraPrim.sdfPath))
        if(len(diffPrims) > 0):
            print("DIFF PRIMS")
            for diffPrim in diffPrims:
                # print("  " + str(diffPrim.sdfPath))
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
                    print("    MISSING ATTRS")
                    for missingAttr in missingAttrs:
                        print("      " + str(missingAttr.name))
                if(len(extraAttrs) > 0):
                    print("    EXTRA ATTRS")
                    for extraAttr in extraAttrs:
                        print("      " + str(extraAttr.name))
                if(len(diffAttrs) > 0):
                    print("    DIFF ATTRS")
                    for diffAttr in diffAttrs:
                        print("      " + str(diffAttr.name))
                        print("        Golden: " + str(diffAttr.goldenVal))
                        print("        Other:  " + str(diffAttr.otherVal))

diff = CaptureDiff(args.golden, args.other)
diff.print()
if(diff.passed()):
    exit(0)
else:
    exit(1)