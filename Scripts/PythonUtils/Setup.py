#!/usr/bin/env python

import os
import subprocess
import platform

import Vulkan
import CheckPythonPkgs
from Premake import PremakeConfiguration as PremakeRequirements
import Utils

# Make sure current call directory has keyword "Luminex"
# if not os.getcwd().endswith("Luminex"):
#     Utils.print_error("Run this script within the Luminex/ directory.")
#     exit(255)

############################## Python Setup ####################################
# Check if Python is installed
Utils.print_info("Checking Python installation...")

output_info = Utils.execute_command_silent(Utils.get_python_executable() + " --version")
if output_info.rc != 0:
    Utils.print_error(
        "Make sure Python can be started from the command line. Add path to `python.exe` to PATH on Windows.")
    exit(255)
else:
    Utils.print_success(output_info.out)

# Make sure require pip modules we need are installed
Utils.print_info("Checking required pip modules...")

CheckPythonPkgs.validate_packages()

Utils.print_linebreak()


############################## Git Setup #######################################
Utils.print_info("Checking Git installation...")

output_info = Utils.execute_command_silent("git --version")
if output_info.rc != 0:
    Utils.print_error("Install Git first.")
    exit(255)
else:
    Utils.print_success(output_info.out)

# Initialize Git submodules
Utils.print_info("Initializing Git submodules...")
subprocess.call(["git", "submodule", "update", "--init", "--recursive"])

Utils.print_info("Updating Git submodules to latest...")
subprocess.call(["git", "submodule", "update", "--remote", "--merge"])

Utils.print_linebreak()


############################## clang-format Setup ##############################
Utils.print_info("Checking clang-format installation...")

output_info = Utils.execute_command_silent("clang-format --version")
if output_info.rc != 0:
    Utils.print_error("Install LLVM/Clang first. https://github.com/llvm/llvm-project/releases/latest")
    exit(255)
else:
    Utils.print_success(output_info.out)

Utils.print_linebreak()


############################## VulkanSDK Setup #################################
Utils.print_info("Checking VulkanSDK installation...")

# Change from Scripts directory to root
os.chdir('../') if os.path.basename(os.getcwd()) == "Scripts" else None

if (not Vulkan.check_vulkan_sdk()):
    Utils.print_error("Vulkan SDK not installed.")


# if (not Vulkan.CheckVulkanSDKDebugLibs()):
#     print("Vulkan SDK debug libs not found.")

Utils.print_linebreak()


############################## Premake5 Setup ##################################
premake_installed = PremakeRequirements.validate()

if premake_installed:
    # if platform.system() == "Windows":
    #     print("\nRunning premake...")
    #     subprocess.call([os.path.abspath("./scripts/Win-GenProjects.bat"), "nopause"])

    Utils.print_success("Premake5 installed.")
else:
    print("Luminex requires Premake5 to generate project files.")

Utils.print_linebreak()


################################################################################


Utils.print_success("Setup Done.")
