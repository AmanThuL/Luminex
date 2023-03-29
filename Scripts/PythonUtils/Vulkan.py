import os
from pathlib import Path
import requests
from io import BytesIO
from urllib.request import urlopen
from zipfile import ZipFile

import Utils

# https://sdk.lunarg.com/sdk/download/latest/PLATFORM/vulkan_sdk.[exe|tar.gz|dmg]
VULKAN_SDK_INSTALLER_URL = 'https://sdk.lunarg.com/sdk/download/latest/windows/vulkan_sdk.exe'
VULKAN_VERSION_MIN = '1.2'
VULKAN_VERSION_LATEST = '1.3.231.1'
VULKAN_VERSION_INSTALLED = ''
VULKAN_SDK_EXE_PATH = 'ThirdParty/VulkanSDK/VulkanSDK.exe'
VULKAN_SDK_FOLDER = 'ThirdParty/VulkanSDK/'


def get_latest_vulkan_sdk_ver():
    url = "https://vulkan.lunarg.com/sdk/latest/windows.txt"
    response = requests.get(url)
    # print(response.status_code)

    if response.status_code == 200:
        global VULKAN_VERSION_LATEST
        VULKAN_VERSION_LATEST = response.content.decode()
    else:
        Utils.print_error(response.status_code)


def install_vulkan_sdk():
    Utils.print_info('Downloading {} to {}'.format(
        VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH))
    # Create the directory if it does not exist
    os.makedirs(VULKAN_SDK_FOLDER, exist_ok=True)
    Utils.download_file(VULKAN_SDK_INSTALLER_URL, VULKAN_SDK_EXE_PATH)
    Utils.print_success("Done!")
    Utils.print_info("Running Vulkan SDK installer...")
    os.startfile(os.path.abspath(VULKAN_SDK_EXE_PATH))
    Utils.print_info("Re-run this script after installation")


def install_vulkan_prompt():
    Utils.print_info("Would you like to install the Vulkan SDK?")
    install = Utils.yes_or_no()
    if (install):
        install_vulkan_sdk()
        quit()


def check_vulkan_sdk():
    VULKAN_SDK = os.environ.get('VULKAN_SDK')
    get_latest_vulkan_sdk_ver()
    Utils.print_info(
        'Latest Vulkan SDK version from LunarG is {}'.format(VULKAN_VERSION_LATEST))

    if (VULKAN_SDK is None):
        Utils.print_warning("You don't have the Vulkan SDK installed!")
        install_vulkan_prompt()
        return False
    else:
        # Convert to a raw string
        VULKAN_SDK = r"" + VULKAN_SDK
        global VULKAN_VERSION_INSTALLED
        VULKAN_VERSION_INSTALLED = os.path.basename(VULKAN_SDK)
        Utils.print_info(
            f"Vulkan SDK version you installed: {VULKAN_VERSION_INSTALLED}")

        if (VULKAN_VERSION_INSTALLED < VULKAN_VERSION_MIN):
            Utils.print_info(f"Located Vulkan SDK at {VULKAN_SDK}")
            Utils.print_warning(
                f"You don't have the correct Vulkan SDK version! (Requires {VULKAN_VERSION_MIN}+)")
            install_vulkan_prompt()
            return False

    Utils.print_success(f"Correct Vulkan SDK installed at {VULKAN_SDK}")

    return True


# VULKAN_SDK_DEBUG_LIBS_URL = f'https://files.lunarg.com/SDK-{VULKAN_VERSION_INSTALLED}/VulkanSDK-{VULKAN_VERSION_INSTALLED}-DebugLibs.zip'
# TempZipFile = f"{VULKAN_SDK_FOLDER}VulkanSDK.zip"

# def check_vulkan_sdk_debuglibs():
#     shadercdLib = Path(f"{VULKAN_SDK_FOLDER}Lib/shaderc_sharedd.lib")
#     if (not shadercdLib.exists()):
#         print(f"No Vulkan SDK debug libs found. (Checked {shadercdLib})")
#         print("Downloading", VULKAN_SDK_DEBUG_LIBS_URL)
#         with urlopen(VULKAN_SDK_DEBUG_LIBS_URL) as zipresp:
#             with ZipFile(BytesIO(zipresp.read())) as zfile:
#                 zfile.extractall(VULKAN_SDK_FOLDER)

#     print(f"Vulkan SDK debug libs located at {VULKAN_SDK_FOLDER}")
#     return True
