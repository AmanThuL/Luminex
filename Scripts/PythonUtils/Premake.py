import sys
import os
from pathlib import Path

import Utils

class PremakeConfiguration:
    premakeVersion = "5.0.0-beta1"
    premakeZipUrls = f"https://github.com/premake/premake-core/releases/download/v{premakeVersion}/premake-{premakeVersion}-windows.zip"
    premakeLicenseUrl = "https://raw.githubusercontent.com/premake/premake-core/master/LICENSE.txt"
    premakeDirectory = f"./Tools/Premake/Bin/{Utils.get_platform_full()}"

    @classmethod
    def validate(cls):
        if (not cls.check_if_premake_installed()):
            Utils.print_warning("Premake is not installed.")
            return False

        Utils.print_success(f"Correct Premake located at {os.path.abspath(cls.premakeDirectory)}")
        return True

    @classmethod
    def check_if_premake_installed(cls):
        premakeExe = Path(f"{cls.premakeDirectory}/premake5.exe");
        if (not premakeExe.exists()):
            return cls.install_premake()

        return True

    @classmethod
    def install_premake(cls):
        permissionGranted = False
        while not permissionGranted:
            reply = str(input("Premake not found. Would you like to download Premake {0:s}? [Y/N]: ".format(cls.premakeVersion))).lower().strip()[:1]
            if reply == 'n':
                return False
            permissionGranted = (reply == 'y')

        premakePath = f"{cls.premakeDirectory}/premake-{cls.premakeVersion}-windows.zip"
        Utils.print_info("Downloading {0:s} to {1:s}".format(cls.premakeZipUrls, premakePath))
        Utils.download_file(cls.premakeZipUrls, premakePath)
        Utils.print_info(f"Extracting {premakePath}")
        Utils.unzip_file(premakePath, deleteZipFile=True)
        Utils.print_success(f"Premake {cls.premakeVersion} has been downloaded to '{cls.premakeDirectory}'")

        premakeLicensePath = f"{cls.premakeDirectory}/LICENSE.txt"
        Utils.print_info("Downloading {0:s} to {1:s}".format(cls.premakeLicenseUrl, premakeLicensePath))
        Utils.download_file(cls.premakeLicenseUrl, premakeLicensePath)
        Utils.print_success(f"Premake License file has been downloaded to '{cls.premakeDirectory}'")

        return True