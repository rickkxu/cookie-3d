# Renting a machine for Unreal Engine

The Unreal editor needs a **Windows machine with a real NVIDIA/AMD GPU**. You'll rent one in the cloud
and stream its desktop to whatever device you have.

## What you need on your side

- Your MacBook Air with good internet (15+ Mbps, 5 GHz Wi-Fi or wired).
- **A mouse.** A trackpad is painful for first-person mouse look.

## Minimum spec for the rented machine

| | Minimum | Comfortable |
|---|---|---|
| GPU | 8 GB VRAM (RTX 3060 / A10G class) | 16 GB+ |
| CPU | 8 vCPU | 16 vCPU (C++ compiles much faster) |
| RAM | 32 GB | 64 GB |
| Disk | **250 GB SSD** | 500 GB |
| OS | Windows 10/11 or Windows Server 2022 | |

The disk is the most common gotcha. The engine alone is ~60–100 GB, plus Visual Studio and your project.

## Option A (current plan): Vagon

Hourly cloud PC with Unreal pre-installed. You connect from a Mac app or a browser.

- Start with the cheap trial and test streaming over your own Wi-Fi before paying for more.
- Pick a tier with an **A10G-class GPU** or better for the Unreal editor.
- Check that the pre-installed Unreal is **5.8**, and that **Visual Studio with C++ game development** is
  installed. This project is C++. If Visual Studio is missing, run `scripts/setup-windows.ps1`.
- Check how long storage persists between sessions and what it costs. Push your work to GitHub at
  the end of every session anyway.

## Other options

- **AirGPU:** often cheaper per hour, but you install Unreal yourself (use `scripts/setup-windows.ps1`).
- **Shadow:** monthly plan, worth it only if you use it heavily.
- **AWS (e.g. EC2 g5/g6 + Amazon DCV):** powerful but complicated to set up, and new accounts often need
  a GPU quota increase. Not recommended for a first project.

Prices change often, so check each site before paying. **Shut the machine down whenever you step away**,
which stops the hourly billing. Expect the first session to include 30-60+ minutes of installs and
shader compiling.

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
- Make sure the streaming client **captures the mouse** (relative mouse mode), or mouse look will spin
  wildly.
- On a Mac keyboard, check how Cmd/Ctrl are mapped in the streaming client, since Unreal shortcuts use Ctrl.
