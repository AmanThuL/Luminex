import subprocess
import pkg_resources

import Utils


def install(package):
    Utils.print_info(f"Installing {package} using pip...")
    subprocess.check_call([Utils.get_python_executable(), '-m', 'pip', 'install', '-U', package])
    Utils.print_success(f"{package} installed.")


def validate_package(package):
    required = {package}
    installed = {pkg.key for pkg in pkg_resources.working_set}
    missing = required - installed

    if missing:
        install(package)
    else:
        Utils.print_success(f"{package} is installed")


def validate_packages():
    validate_package('requests')
    validate_package('fake-useragent')
