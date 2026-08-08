#!/bin/bash
if [ "$(id -u)" == "0" ]; then
	echo -e "\ninitial_setup.sh should not be run as root."
	echo -e "Please run this as a user with admin privileges.\n"
	exit 1
fi

if [ "$(uname -s)" == "Linux" ]; then
	echo -e "\nRunning Linux first-time setup...\n"

    ./get_deps_linux.sh
    ERRORLEVEL=$?
    if [ $ERRORLEVEL -ne 0 ]; then
        exit 1
    fi

    make setup
    ERRORLEVEL=$?
    if [ $ERRORLEVEL -ne 0 ]; then
        exit 1
    fi

    ./build-debug.sh
    ERRORLEVEL=$?
    if [ $ERRORLEVEL -ne 0 ]; then
        exit 1
    fi

    ./import-updated-assets.sh
    ERRORLEVEL=$?
    if [ $ERRORLEVEL -ne 0 ]; then
        exit 1
    fi

    printf "Initial setup completed \033[0;32msuccessfully\033[0m.\n"
fi