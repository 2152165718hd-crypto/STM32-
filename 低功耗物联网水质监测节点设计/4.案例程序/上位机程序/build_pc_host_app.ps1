$ErrorActionPreference = "Stop"

python -m pip install --upgrade pyinstaller

python -m PyInstaller `
    --noconfirm `
    --clean `
    --windowed `
    --onedir `
    --name WaterHostApp `
    --collect-all matplotlib `
    --collect-submodules serial `
    launch_pc_host_app.py

