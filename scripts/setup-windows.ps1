# One-time setup for a rented Windows 10/11 cloud PC.
# Run in PowerShell as Administrator:
#   Set-ExecutionPolicy -Scope Process Bypass; .\setup-windows.ps1

$ErrorActionPreference = "Stop"

if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error "winget not found (common on Windows Server). Install Git, Visual Studio 2022 (Game development with C++), and the Epic Games Launcher by hand."
}

Write-Host "Installing Git..."
winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements

Write-Host "Installing Visual Studio 2022 Community with C++ game development (takes a while)..."
winget install --id Microsoft.VisualStudio.2022.Community -e --accept-source-agreements --accept-package-agreements `
    --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.NativeGame --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"

Write-Host "Installing Epic Games Launcher..."
winget install --id EpicGames.EpicGamesLauncher -e --accept-source-agreements --accept-package-agreements

Write-Host "Installing Claude Code..."
irm https://claude.ai/install.ps1 | iex

Write-Host ""
Write-Host "Done. Next: open the Epic Games Launcher, sign in, and install Unreal Engine 5.8."
Write-Host "Then follow 'Create the project' in README.md."
