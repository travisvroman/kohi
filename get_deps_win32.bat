REM

ECHO "Installing dependencies for Windows..."

winget install -e --id LLVM.LLVM
winget install ezwinports.make
winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.Windows11SDK.22000"
winget install khronosgroup.vulkansdk
winget install CreativeTechnology.OpenAL

powershell -Command "[Environment]::SetEnvironmentVariable(\"Path\", [Environment]::GetEnvironmentVariable(\"Path\", \"User\") + \";C:\Program Files\LLVM\bin\", \"User\")"

ECHO "Done installing dependencies for Windows."
