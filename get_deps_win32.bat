REM TODO: implement the Windows version of this.
REM

ECHO "Installing dependencies for Windows..."

winget install exwinports.make
winget install Microsoft.VisualStudio.2022.BuildTools
winget install git.git
winget install khronosgroup.vulkansdk
winget install OpenAL.OpenAL

ECHO "Done installing dependencies for Windows."
