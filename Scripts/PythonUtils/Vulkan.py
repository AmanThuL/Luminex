import os
import subprocess
import sys
from pathlib import Path
import requests

import Utils

from io import BytesIO
from urllib.request import urlopen
from zipfile import ZipFile

# https://sdk.lunarg.com/sdk/download/latest/PLATFORM/vulkan_sdk.[exe|tar.gz|dmg]
VULKAN_SDK_INSTALLER_URL = 'https://sdk.lunarg.com/sdk/download/latest/windows/vulkan_sdk.exe'
VULKAN_VERSION_MIN = '1.3'
VULKAN_VERSION_LATEST = '1.3.231.1'
VULKAN_SDK_EXE_PATH = 'ThirdParty/VulkanSDK/VulkanSDK.exe'
VULKAN_SDK_FOLDER = 'ThirdParty/VulkanSDK/'


def GetLatestVulkanSDKVer():
    url = "https://vulkan.lunarg.com/sdk/latest/windows.txt"
    response = requests.get(url)
    print(response.status_code)

    if response.status_code == 200:
        global VULKAN_VERSION_LATEST
        VULKAN_VERSION_LATEST = response.content.decode()
    else:
        print("Error:", response.status_code)


def InstallVulkanSDK():
    GetLatestVulkanSDKVer()
    print('Latest Vulkan SDK version is {}'.format(VULKAN_VERSION_LATEST))

    print('Downloading {} to {}'.format(
        VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH))
    # Create the directory if it does not exist
    os.makedirs(VULKAN_SDK_FOLDER, exist_ok=True)
    Utils.DownloadFile(VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH)
    print("Done!")
    print("Running Vulkan SDK installer...")
    os.startfile(os.path.abspath(VULKAN_SDK_EXE_PATH))
    print("Re-run this script after installation")


def InstallVulkanPrompt():
    print("Would you like to install the Vulkan SDK?")
    install = Utils.YesOrNo()
    if (install):
        InstallVulkanSDK()
        quit()


def CheckVulkanSDK():
    VULKAN_SDK = os.environ.get('VULKAN_SDK')
    if (VULKAN_SDK is None):
        print("You don't have the Vulkan SDK installed!")
        InstallVulkanPrompt()
        return False
    else:
        # Convert to a raw string
        VULKAN_SDK = r"" + VULKAN_SDK
        if (os.path.basename(VULKAN_SDK) < VULKAN_VERSION_MIN):
            print(f"Located Vulkan SDK at {VULKAN_SDK}")
            print(
                f"You don't have the correct Vulkan SDK version! (Requires {VULKAN_VERSION_MIN}+)")
            InstallVulkanPrompt()
            return False

    print(f"Correct Vulkan SDK located at {VULKAN_SDK}")
    return True


# VulkanSDKDebugLibsURL = 'https://files.lunarg.com/SDK-1.2.170.0/VulkanSDK-1.2.170.0-DebugLibs.zip'
# OutputDirectory = "QAQ/vendor/VulkanSDK"
# TempZipFile = f"{OutputDirectory}/VulkanSDK.zip"


# def CheckVulkanSDKDebugLibs():
#     shadercdLib = Path(f"{OutputDirectory}/Lib/shaderc_sharedd.lib")
#     if (not shadercdLib.exists()):
#         print(f"No Vulkan SDK debug libs found. (Checked {shadercdLib})")
#         print("Downloading", VulkanSDKDebugLibsURL)
#         with urlopen(VulkanSDKDebugLibsURL) as zipresp:
#             with ZipFile(BytesIO(zipresp.read())) as zfile:
#                 zfile.extractall(OutputDirectory)

#     print(f"Vulkan SDK debug libs located at {OutputDirectory}")
#     return True
