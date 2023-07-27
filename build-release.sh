#!/bin/bash

# Convenience build script for Linux and macOS.
OS=$(uname -s)

if [ $OS == 'Linux' ]
then
    echo "Building for Linux..."
    ./build-all.sh linux build release 

elif [[ $OS == 'Darwin' ]]; then
    echo "Building for macOS..." 
    ./build-all.sh macos build release 
fi

