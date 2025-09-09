
import argparse
import multiprocessing
import os
import re
import signal
import string
import subprocess
import sys
import time
import threading
import ctypes
import depfile

report_lock = threading.Lock()
task_lock = threading.Lock()
terminate = False

def sigint_handler(signal, frame):
    global terminate
    terminate = True

signal.signal(signal.SIGINT, sigint_handler)

parser = argparse.ArgumentParser(description='Compiles DXVK-RT shaders.')
parser.add_argument('-glslang', required=True, type=str, dest='glslang')
parser.add_argument('-slangc', required=True, type=str, dest='slangc')
parser.add_argument('-spirvval', required=True, type=str, dest='spirvval')
parser.add_argument('-input', required=False, type=str, dest='input', default='.')
parser.add_argument('-I', '-include', action='append', type=str, dest='includes', default=[])
parser.add_argument('-output', required=True, type=str, dest='output')
parser.add_argument('-force', action='store_true', dest='force')
parser.add_argument('-parallel', action='store_true', dest='parallel')
parser.add_argument('-binary', action='store_true', dest='binary')
parser.add_argument('-debug', action='store_true', dest='debug')
args = parser.parse_args()

# Set to True to generate Slang repro file when compiling shaders
generateSlangRepro = False

includePaths = ' '.join([f'-I{path}' for path in args.includes])
slangDll = os.path.join(os.path.dirname(args.slangc), 'slang.dll')

tools = [args.glslang, args.slangc, slangDll, __file__]
newestTool = max([os.path.getmtime(x) for x in tools])

# Note: -Os (Optimize Size) used here as while one might typically expect optimizing for size to comprimise speed optimizations,
# the glslang optimizer actually just enables more optimizations when this option is specified, meaning it is probably good to enable
# always (assuming that data wouldn't help the actual driver compiler at least, and we've observed it to make a slight speedup overall):
# https://github.com/KhronosGroup/glslang/blob/master/SPIRV/SpvTools.cpp#L213
glslangFlags = '--quiet --target-env vulkan1.2 -Os'

# Note: Debug is used for Debug and DebugOptimized currently, so it does not disable optimizations persay
# (as otherwise -Od should be passed and be mutually exclusive with -Os), just means to generate debug info.
if args.debug:
    glslangFlags += ' -g'

os.makedirs(args.output, exist_ok = True)


def printFromThread(what):
    report_lock.acquire()
    print(what)
    report_lock.release()


class Task:
    outputs = []
    inputs = []
    commands = []
    customName = None

    def needsBuild(self):
        if args.force:
            return True

        mostRecentInput = None
        for input in self.inputs:
            if os.path.exists(input):
                inputTime = os.path.getmtime(input)
                if mostRecentInput is None:
                    mostRecentInput = inputTime
                else:
                    mostRecentInput = max(mostRecentInput, inputTime)
            else:
                return True

        oldestOutput = None
        for output in self.outputs:
            if os.path.exists(output):
                outputTime = os.path.getmtime(output)
                if oldestOutput is None:
                    oldestOutput = outputTime
                else:
                    oldestOutput = min(oldestOutput, outputTime)
            else:
                return True

        if mostRecentInput is None or oldestOutput is None:
            return True

        # Force rebuilds when a compiler or this script changes
        mostRecentInput = max(mostRecentInput, newestTool)

        return mostRecentInput > oldestOutput

    def build(self):
        allCommandOutputs = ''
        commandName = ''

        for command in self.commands:
            #print(command)
            timeStart = time.time()

            process = subprocess.Popen(command, shell = True, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
            out, err = process.communicate()

            duration = time.time() - timeStart

            commandName = os.path.basename(command.split(' ')[0])
            printFromThread(f'[{duration:5.2f}s] {commandName}: {self.getName()}')

            combinedOutput = (out + err).decode("utf-8").strip()
            if len(combinedOutput):
                if len(allCommandOutputs):
                    allCommandOutputs += '\n'
                allCommandOutputs += combinedOutput

            if process.returncode != 0:
                # Convert the process exit code from uint32 to int32
                exitCode = ctypes.c_long(process.returncode).value
                return (exitCode, allCommandOutputs, commandName)

        return (0, allCommandOutputs, commandName)


    def getName(self):
        if self.customName is not None:
            return self.customName
        if len(self.inputs):
            return os.path.basename(self.inputs[0])
        if len(self.outputs):
            return os.path.basename(self.outputs[0])
        return "<UnknownTask>"

def runTasks(tasks):
    global terminate
    while True:
        if terminate:
            break

        task = None

        task_lock.acquire()
        tasksLeft = len(tasks)
        if tasksLeft > 0:
            task = tasks[0]
            del tasks[0]
        task_lock.release()

        if task is None:
            break
            
        # Workaround for occasional crashes of slangc.exe on the build farm
        # Not necessary anymore
        maxAttempts = 1
        for attempt in range(maxAttempts):
            try:
                exitCode, output, lastCommand = task.build()
            except:
                terminate = True
                raise

            if len(output):
                printFromThread(f'\n{lastCommand} output for {task.getName()}:\n{output}')

            elif exitCode != 0:
                printFromThread(f'\n{lastCommand} exited with code {exitCode} and no output for {task.getName()}, possibly crashed (attempt {attempt+1}/{maxAttempts}).')

                if (exitCode == 1) and (attempt < maxAttempts - 1):
                    continue
            break

        if exitCode != 0:
            terminate = True

def getShaderName(inputFile):
    return os.path.splitext(os.path.basename(inputFile))[0]


def createBasicTask(inputFile, destFile, targetName, depFile):
    task = Task()
    try:
        lines = open(depFile, 'r').readlines()
        task.inputs = depfile.parse(lines, targetName)
    except:
        task.inputs = []
    task.outputs = [destFile, depFile]
    return task

def createGlslangTask(inputFile):
    shaderName = getShaderName(inputFile)
    destExtension = '.spv' if args.binary else '.h'
    destFile = os.path.join(args.output, shaderName + destExtension)
    depFile = os.path.join(args.output, shaderName + ".d")
    task = createBasicTask(inputFile, destFile, destFile, depFile)
    variableName = '' if args.binary else f'--vn {shaderName}'

    command = f'{args.glslang} {glslangFlags} {includePaths} -V {variableName} -o {destFile} ' \
            + f'--depfile {depFile} {inputFile}'
    task.commands = [command]
    return task

def createSlangTask(inputFile, variantSpec):
    # Ensure slang runs validation
    os.environ['SLANG_RUN_SPIRV_VALIDATION'] = '1'

    inputName, inputType = os.path.splitext(getShaderName(inputFile))
    variantName, variantType = os.path.splitext(variantSpec[0])

    variantDefines = ' '.join([f'-D{x}' for x in variantSpec[1:]])
    destFile = os.path.join(args.output, variantName + ".spv")
    headerFile = os.path.join(args.output, variantName + ".h")
    depFile = os.path.join(args.output, variantName + ".d")

    # Create task to resolve dep file for the compiler output (.spv)
    task = createBasicTask(inputFile, destFile, destFile, depFile)

    if variantName != inputName:
        task.customName = f'{os.path.basename(inputFile)} ({variantName})'

    command1 = f'{args.slangc} -entry main -target spirv -zero-initialize -emit-spirv-directly -verbose-paths {includePaths} ' \
            + f'-depfile {depFile} {inputFile} -D__SLANG__ {variantDefines} ' \
            + f'-matrix-layout-column-major ' \
            + f'-Wno-30081 '

    # Force scalar block layout in shaders - buffers are required to be aligned as such by Neural Radiance Cache
    command1 += f'-fvk-use-scalar-layout '

    if generateSlangRepro:
      reproFile = os.path.join(args.output, variantName + ".slangRepro")
      command1 += f'-dump-repro {reproFile}'

    command1 += f'-o {destFile}'

    # -binary switch just writes the SPV binary
    if args.binary:
        task.commands = [command1]
    else:
        # Command to convert SPV into c array header
        script_dir = os.path.dirname(os.path.realpath(__file__))
        shader_xxd = os.path.join(script_dir, 'shader_xxd.py')
        command2 = f'"{sys.executable}" {shader_xxd} -i {destFile} -o {headerFile}'

        task.commands = [command1, command2]

    return task

# Read the shader variant specifications from the source code.
# The specifications must follow this pattern:
#    //!variant <name1> <defines...>
#    //!>       <defines-continued...>
# The defines can be all specified on the first line or split into multiple lines.
# Defines with values are supported, just use the NAME=VALUE syntax.
# After all variants are declared, a closing statement must be used:
#    //!end-variants
#
# The function returns a list of variantSpec items, where each item is [name, define1, define2...]
# An empty result means there was an error parsing the specifications.
#
# For example, if a shader my_shader.slang has these two variants:
#    //!variant my_shader_a.comp MY_CONST=0
#    //!variant my_shader_b.rgen MY_CONST=1
#    //!end-variants
# Then two outputs will be produced after the script finishes:
#    my_shader_a.comp -> my_shader_a.h with `const uint32_t my_shader_a[]`
#    my_shader_b.rgen -> my_shader_b.h with `const uint32_t my_shader_b[]`
def parseShaderVariants(inputFile):
    result = []
    lineno = 0
    inputWithType = getShaderName(inputFile)
    inputName, inputType = os.path.splitext(inputWithType)
    endvariantsFound = False
    with open(inputFile, "r") as file:
        for line in file:
            lineno += 1
            if line.startswith("//!variant"):
                parts = line.split()
                if len(parts) < 2:
                    print(f'{inputFile}:{lineno}: invalid shader variant specification')
                    return []

                # Parse the variant name and see if it has a shader type override
                variantName, variantType = os.path.splitext(parts[1])
                if len(variantType) != 0:
                    variantWithType = parts[1]
                else:
                    if len(inputType) == 0:
                        print(f'{inputFile}:{lineno}: shader type not specified here or in the file name')
                        return []
                    variantWithType = variantName + inputType

                # Concatenate the variant name back with the defines
                result.append([variantWithType] + parts[2:])

            elif line.startswith("//!>"):
                parts = line.split()
                if len(result) == 0:
                    print(f'{inputFile}:{lineno}: variant continuation must follow a declaration')
                    return []

                if len(parts) > 1:
                    # Append the declarations found on this line to the previous variant
                    result[-1] += parts[1:]

            elif line.startswith("//!end-variants"):
                endvariantsFound = True
                break

            elif (line.startswith("//!") or line.startswith("//>")) and len(result) != 0:
                print(f'{inputFile}:{lineno}: warning: this looks like a variant declaration but is not one')

    if len(result) == 0:
        result = [[inputWithType]]
    elif not endvariantsFound:
        # If there are any !variant declarations, there must be an !endvariants statement somewhere
        print(f'{inputFile}:{lineno}: no !end-variants found in the file')
        return []
    return result

tasks = []

for root, dirs, files in os.walk(args.input):
    for name in files:
        task = None
        inputFile = os.path.join(root, name)
        if name.endswith(".comp") \
        or name.endswith(".vert") \
        or name.endswith(".geom") \
        or name.endswith(".frag") \
        or name.endswith(".rgen") \
        or name.endswith(".rchit") \
        or name.endswith(".rahit") \
        or name.endswith(".rmiss") \
        or name.endswith(".rint"):
            task = createGlslangTask(inputFile)
            if task.needsBuild():
                tasks.append(task)

        elif name.endswith(".slang"):
            variants = parseShaderVariants(inputFile)

            if len(variants) == 0:
                # Couldn't parse the variant specifications, exit with an error code
                sys.exit(2)

            # Create tasks for each variant
            for variantSpec in variants:
                task = createSlangTask(inputFile, variantSpec)
                if task.needsBuild():
                    tasks.append(task)

if len(tasks):
    threads = []
    threadCount = multiprocessing.cpu_count() if args.parallel else 1
    for i in range(threadCount):
        thread = threading.Thread(target = runTasks, args = (tasks,))
        thread.start()
        threads.append(thread)
        
    for thread in threads:
        thread.join()

if terminate:
    sys.exit(1)
