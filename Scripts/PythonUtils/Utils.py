import platform
import requests
import sys
import time
import os
from subprocess import PIPE, run

from fake_useragent import UserAgent
import urllib3
import winreg
import urllib

from zipfile import ZipFile

# ==================== Downloading ====================
def download_file(url, filepath):
    path = filepath
    filepath = os.path.abspath(filepath)
    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    if (type(url) is list):
        for url_option in url:
            print("Downloading", url_option)
            try:
                download_file(url_option, filepath)
                return
            except urllib3.error.URLError as e:
                print(
                    f"URL Error encountered: {e.reason}. Proceeding with backup...\n\n")
                os.remove(filepath)
                pass
            except urllib.error.HTTPError as e:
                print(
                    f"HTTP Error  encountered: {e.code}. Proceeding with backup...\n\n")
                os.remove(filepath)
                pass
            except:
                print(f"Something went wrong. Proceeding with backup...\n\n")
                os.remove(filepath)
                pass
        raise ValueError(f"Failed to download {filepath}")
    if not (type(url) is str):
        raise TypeError("Argument 'url' must be of type list or string")

    with open(filepath, 'wb') as f:
        headers = {
            'User-Agent': "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_4) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.97 Safari/537.36"}
        response = requests.get(url, headers=headers, stream=True)
        total = response.headers.get('content-length')

        if total is None:
            f.write(response.content)
        else:
            downloaded = 0
            total = int(total)
            startTime = time.time()
            for data in response.iter_content(chunk_size=max(int(total/1000), 1024*1024)):
                downloaded += len(data)
                f.write(data)

                try:
                    done = int(50*downloaded /
                               total) if downloaded < total else 50
                    percentage = (downloaded / total) * \
                        100 if downloaded < total else 100
                except ZeroDivisionError:
                    done = 50
                    percentage = 100
                elapsedTime = time.time() - startTime
                try:
                    avgKBPerSecond = (downloaded / 1024) / elapsedTime
                except ZeroDivisionError:
                    avgKBPerSecond = 0.0

                avgSpeedString = '{:.2f} KB/s'.format(avgKBPerSecond)
                if (avgKBPerSecond > 1024):
                    avgMBPerSecond = avgKBPerSecond / 1024
                    avgSpeedString = '{:.2f} MB/s'.format(avgMBPerSecond)
                sys.stdout.write('\r[{}{}] {:.2f}% ({})     '.format(
                    '█' * done, '.' * (50-done), percentage, avgSpeedString))
                sys.stdout.flush()
    sys.stdout.write('\n')


def yes_or_no():
    while True:
        reply = str(input('[Y/N]: ')).lower().strip()
        if reply[:1] == 'y':
            return True
        if reply[:1] == 'n':
            return False


# ==================== Filesystem ====================
def unzip_file(filepath, deleteZipFile=True):
    zipFilePath = os.path.abspath(filepath) # get full path of files
    zipFileLocation = os.path.dirname(zipFilePath)

    zipFileContent = dict()
    zipFileContentSize = 0
    with ZipFile(zipFilePath, 'r') as zipFileFolder:
        for name in zipFileFolder.namelist():
            zipFileContent[name] = zipFileFolder.getinfo(name).file_size
        zipFileContentSize = sum(zipFileContent.values())
        extractedContentSize = 0
        startTime = time.time()
        for zippedFileName, zippedFileSize in zipFileContent.items():
            UnzippedFilePath = os.path.abspath(f"{zipFileLocation}/{zippedFileName}")
            os.makedirs(os.path.dirname(UnzippedFilePath), exist_ok=True)
            if os.path.isfile(UnzippedFilePath):
                zipFileContentSize -= zippedFileSize
            else:
                zipFileFolder.extract(zippedFileName, path=zipFileLocation, pwd=None)
                extractedContentSize += zippedFileSize
            try:
                done = int(50*extractedContentSize/zipFileContentSize)
                percentage = (extractedContentSize / zipFileContentSize) * 100
            except ZeroDivisionError:
                done = 50
                percentage = 100
            elapsedTime = time.time() - startTime
            try:
                avgKBPerSecond = (extractedContentSize / 1024) / elapsedTime
            except ZeroDivisionError:
                avgKBPerSecond = 0.0
            avgSpeedString = '{:.2f} KB/s'.format(avgKBPerSecond)
            if (avgKBPerSecond > 1024):
                avgMBPerSecond = avgKBPerSecond / 1024
                avgSpeedString = '{:.2f} MB/s'.format(avgMBPerSecond)
            sys.stdout.write('\r[{}{}] {:.2f}% ({})     '.format('█' * done, '.' * (50-done), percentage, avgSpeedString))
            sys.stdout.flush()
    sys.stdout.write('\n')

    if deleteZipFile:
        os.remove(zipFilePath) # delete zip file


# ==================== Commandline ====================
# Class to store the result of a command execution
class CommandResult:
    rc: int = 0
    out: str = ""
    err: str = ""

    def __init__(self, rc, out, err):
        self.rc = rc
        self.out = out
        self.err = err


# Execute a command and store its output and exit code
def execute_command_silent(command):
    # For python 3.5+ it is recommended that you use the run function from the subprocess module
    result = run(command, shell=True, stdin=PIPE, stdout=PIPE,
                 stderr=PIPE, universal_newlines=True)
    return CommandResult(result.returncode, result.stdout, result.stderr)


# ==================== Logging ====================
# Define ANSI escape codes for red color and reset
class Color:
    PURPLE = '\033[95m'
    CYAN = '\033[96m'
    DARKCYAN = '\033[36m'
    BLUE = '\033[94m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    RESET = '\033[0m'


def print_info(message):
    message = message.rstrip('\n')
    print(f"{Color.CYAN}{message}{Color.RESET}")


def print_success(message):
    message = message.rstrip('\n')
    print(f"{Color.GREEN}{message}{Color.RESET}")


def print_warning(message):
    message = message.rstrip('\n')
    print(f"{Color.YELLOW}Warning: {message}{Color.RESET}")


def print_error(message):
    message = message.rstrip('\n')
    print(f"{Color.RED}Error: {message}{Color.RESET}")


def print_linebreak():
    print("------------------------------------------------------------")


# ==================== Misc ====================
def get_python_executable():
    # Get current operating system
    curr_os = platform.system()

    # Define Python executable
    python = "py" if curr_os == "Windows" else "python3"

    return python


def get_platform_full():
    # Get current operating system
    curr_os = platform.system()

    # Define Python executable
    os_full_name = "Windows" if curr_os == "Windows" else "Linux"

    return os_full_name
