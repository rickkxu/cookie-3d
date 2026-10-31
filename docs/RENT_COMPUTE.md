# Renting a machine for Unreal Engine

The Unreal editor needs a **Windows machine with a real NVIDIA/AMD GPU**. You'll rent one in the cloud
and stream its desktop to whatever device you have.

## What you need on your side

- Any device with a screen and good internet (15+ Mbps, wired or 5 GHz Wi-Fi). A laptop, Chromebook,
  tablet, or even a phone can work.
- **A physical keyboard and mouse.** This is a first-person game with WASD + mouse look. A touchscreen alone
  won't do. A cheap Bluetooth keyboard and mouse is fine for a tablet.

## Minimum spec for the rented machine

| | Minimum | Comfortable |
|---|---|---|
| GPU | 8 GB VRAM (RTX 3060 / A10G class) | 16 GB+ |
| CPU | 8 vCPU | 16 vCPU (C++ compiles much faster) |
| RAM | 32 GB | 64 GB |
| Disk | **250 GB SSD** | 500 GB |
| OS | Windows 10/11 or Windows Server 2022 | |

The disk is the most common gotcha. The engine alone is ~60–100 GB, plus Visual Studio and your project.

## Option A (recommended): a persistent cloud PC, e.g. Shadow

A monthly subscription gives you a Windows PC that stays put between sessions, like owning a computer.
You connect from an app on Windows/Mac/Android/iOS or from a browser.

- **Pros:** simplest by far, no server admin, nothing to forget to switch off, predictable monthly price.
- **Cons:** you don't pick the exact GPU, and you'll need to add storage to reach 250 GB+.
- **Before you pay:** check the current plan's GPU, RAM, and storage add-ons against the table above.

## Option B: hourly GPU server (AWS / Azure / Google Cloud)

For example AWS EC2 `g5.2xlarge` or `g6.2xlarge` (Windows), connected with **Amazon DCV**, which works
in a browser.

- **Pros:** more power, pay only for the hours you use.
- **Cons:** more setup, and new accounts often have a **GPU quota of 0**. You must request a
  "Running On-Demand G and VT instances" quota increase, which can take a day. Disk is billed even
  while the machine is stopped.
- **Stop the instance every time you finish.** A forgotten GPU instance can cost hundreds per month.
- Windows Server doesn't ship `winget`, so install the tools from the script by hand.

**Avoid** Linux-only GPU marketplaces built for AI training (container-only, no desktop). The Unreal
editor is far easier to use on Windows.

## Once the machine is running

1. Connect to its desktop and open **PowerShell as Administrator**.
2. Run [`scripts/setup-windows.ps1`](../scripts/setup-windows.ps1). It installs Git, Visual Studio 2022
   with the C++ game-dev workload, the Epic Games Launcher, and Claude Code.
   - Check the UE 5.8 release notes for the required Visual Studio version, in case it now needs a
     newer one.
3. Open the Epic Games Launcher, sign in, and install **Unreal Engine 5.8** (Library tab, +).
4. Continue with **"Create the project"** in the [README](../README.md).

## Streaming tips

- Set the stream to 60 fps, and lower the resolution before lowering the quality.
- Make sure the streaming client **captures the mouse** (relative mouse mode), or mouse-look will spin
  wildly. Shadow, Parsec, and DCV all have this setting.
