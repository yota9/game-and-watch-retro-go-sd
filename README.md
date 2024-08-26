# Emulator collection for Nintendo® Game & Watch™

This is a port of the [retro-go](https://github.com/ducalex/retro-go) emulator collection that runs on the Nintendo® Game & Watch™
and has **SD card support**.

Supported emulators:

- ColecoVision (col)
- Gameboy / Gameboy Color (gb/gbc)
- Game & Watch / LCD Games (gw)
- Nintendo Entertainment System (nes)
- PC Engine / TurboGrafx-16 (pce)
- Sega Game Gear (gg)
- Sega Master System (sms)
- Sega SG-1000 (sg)

## Table of Contents
- [Emulator collection for Nintendo® Game & Watch™](#emulator-collection-for-nintendo-game--watch)
    - [Table of Contents](#table-of-contents)
    - [Sd card support](#sd-card-support)
        - [Build process](#build-process)
        - [Current status](#current-status)
        - [Current limitations](#current-limitations)
        - [Software information](#software-information)
        - [Hardware information](#hardware-information)
            - [Q&A for the hardware](#qa-for-the-hardware)
    - [Controls](#controls)
        - [Macros](#macros)
    - [Troubleshooting / FAQ](#troubleshooting--faq)
    - [How to build](#how-to-build)
        - [Prerequisites](#prerequisites)
        - [Building](#building)
        - [Information for developers](#information-for-developers)
    - [Build and flash using Docker](#build-and-flash-using-docker)
    - [Backing up and restoring save state files](#backing-up-and-restoring-save-state-files)
    - [Screenshots](#screenshots)
    - [Upgrading the flash](#upgrading-the-flash)
    - [Advanced Flash Examples](#advanced-flash-examples)
        - [Custom Firmware (CFW)](#custom-firmware-cfw)
    - [Discord, support and discussion](#discord-support-and-discussion)
    - [LICENSE](#license)

## Sd card support
This repository contains early PoC code and PCB flex cable design to support SD card on the console. Only NES and GB games were currently tested! The single flash chip support is not broken too! PCB files and images could be found at [pcb directory](https://github.com/yota9/game-and-watch-retro-go-sd/tree/main/pcb). Also PSRAM chip support is added.

![zelda_sd_v1](https://github.com/yota9/game-and-watch-retro-go-sd/blob/main/pcb/zelda_sdv1.jpg)

## Build process
These makefile variables are currently in control for the SD card support:

- `SD_CARD` - set to 1 to enable SD card support
- `EXTFLASH_SIZE_MB` and other extflash-related varialbes are in control of the SD card from now on, 4GB is current limit
- `SPI_FLASH_SIZE_MB` - set size of SPI flash chip if used
- `EXTFLASH_FORCE_SRAM` - set if PSRAM chip is used instead of flash chip

### Current status
- PCB V1 for **Zelda** version of Game and Watch was designed, manufactured and tested. It is fully functional, but V2 was designed with a few cosmetic changes to make it just a bit thinner and longer, but it is untested and V1 is fully working already, be aware of that.
**The Mario version has different PCB layout so it is incompatible with these PCBs!**
- SD card is used as in-place replacement for the external flash. The extflash binary is flashed to the SD card either through dd linux command or through SWD interface and flashapp that was used previously for flash chip, but **no FS support is implemented in this PoC.**
- SD card supports both reading and writing. Although I'm testing it with 32GB card the software limitation is 4GB, since currently ROMs are linked in with the linker and device has 32-bit address space.
- Flash chip is optional, but is is used as a memory-mmaped cache storage for the games that are larger then devices RAM. Simple allocator was written for the flash chip to load the games in round-robin fashion. Loading game in flash from SD takes some time, e.g. 770KB game takes around 11s to fully load. But the second load of the game (assuming it was not overwritten by other games you've played) is instant. The allocation information is stored in the last 4KB of the flash chip and preserved between reboots. The allocation is done by chunks (currently 126 chunks), the size of each chunk depends on the flash chip size, from 8kb for 1MB flash to 2MB for 256MB flash. Without flash chip only games that fit in the RAM could be loaded (e.g. about 500kb for NES games).
- APS6404L-SQH PSRAM chip is tested instead of flash chip (currently tested only SPI mode). In SPI mode it is 2.5x times faster than OSPI flash.

### Current limitations
- In order to fit the SD card slot in the device the 4 buttons supports (A/B/Start/Reset) should be removed from the back lid. The plastic is soft and easily removed with pliers and scalpel.
- The list of ROMs is still located in MCU ROM, so you need to update firmware with SD flash.
- Due to how the games were originally linked in the firmware, maximum 4GB of SD card could be utilized.
- No support for ROM compression on SD card
- No FS support is implemented, so SD card is currently used as in-place replacement for the external flash.

These software limiations are due to how original port was made. The retro-go project was dissected peace-by-peace and I don't see enough reason to add "full support" to the current state of the project. For those who would like to do it I encourage to make new clean retro-go port, the SD card is already supported in the original project. I belive current state of the retro-go project would allow to make it with much less modifications than it was done originally. Also it seems that the retro-go project is constantly updated so probably many bugs and improvements were already made through these years.

### Software information
For SD card support 3 new source files were added to Core/Src directory:

- `gw_sd.c` - SD card initialization and read/write functions
- `softspi.c` - software SPI implementation for the SD card
- `flash_alloc.c` - flash chip round-robin allocator

### Hardware information
BOM for the adapter:

| Qty | Name                                                                  |
|:----|:----------------------------------------------------------------------|
| 1   | [Micro SD card slot](https://vi.aliexpress.com/item/32802051702.html) |
| 1   | TXS0108ERGYR level shifter                                            |
| 1   | SN74LVC1G04DCK single inverter gate                                   |
| 6   | 0402 10k resistors                                                    |
| 1   | 0402 100nF capacitor                                                  |
| 1   | 0402 1uF capacitor                                                    |
| 1   | Optional (but highly recommended) flash chip (used originally in G&W) |

The flex PCB I've used is 0.11mm, 2 layers, no stiffeners (I've added double sided tape under SD card by my self).
Order number location is already set under SD card slot if you would use JLCPCB service (click "Specify a location" option).

#### Q&A for the hardware
- Q: Is it hard to solder the flex cable?
- A: The flex cable is quite easy to solder to the main board, the pads are quite big and located on the both sides of the PCB, you just need to heat them from the top through PCB. But soldering the components on the flex cable is quite tricky especially the level shifter, you have to be well experienced and the easiest way is to use heat table and soldering paste, although I recomened to order pre-assembled PCBs if you're not sure about your soldering skills.

- Q: At what operation mode does flash chip works?
- A: The flash chip works in quad mode, like it originally did.

- Q: Why did you use level shifter?
- A: The SD card operates at 3.3V, while the MCU operates at 1.8V. The level shifter is used to shift the voltage levels from 1.8V to 3.3V and vice versa.

- Q: Why did you use the inverter gate?
- A: The inverter gate is used to invert ChipSelect signal from the MCU to the SD card. This way single line could be used to select both the flash chip and the SD card.

- Q: Why TXS0108 is used, not TXB0104?
- A: This choice was made simply because my PoC was made with this chip I had in my storage. Although I'm pretty sure it would work fine with TXB0104 I didn't want to risk it and I've used the chip that I was sure would work.

## Controls

Buttons are mapped as you would expect for each emulator. `GAME` is mapped to `START`,
and `TIME` is mapped to `SELECT`. `PAUSE/SET` brings up the emulator menu.

By default, pressing the power-button while in a game will automatically trigger
a save-state prior to putting the system to sleep. Note that this WILL overwrite
the previous save-state for the current game.

### Macros

Holding the `PAUSE/SET` button while pressing other buttons have the following actions:

| Button combination    | Action                                                                 |
| --------------------- | ---------------------------------------------------------------------- |
| `PAUSE/SET` + `GAME`  | Store a screenshot. (Disabled by default on 1MB flash builds)          |
| `PAUSE/SET` + `TIME`  | Toggle speedup between 1x and the last non-1x speed. Defaults to 1.5x. |
| `PAUSE/SET` + `UP`    | Brightness up.                                                         |
| `PAUSE/SET` + `DOWN`  | Brightness down.                                                       |
| `PAUSE/SET` + `RIGHT` | Volume up.                                                             |
| `PAUSE/SET` + `LEFT`  | Volume down.                                                           |
| `PAUSE/SET` + `B`     | Load state.                                                            |
| `PAUSE/SET` + `A`     | Save state.                                                            |
| `PAUSE/SET` + `POWER` | Poweroff WITHOUT save-stating.                                         |

## Troubleshooting / FAQ

- Run `make help` to get a list of options to configure the build, and targets to perform various actions.
- Add `STATE_SAVING=0` as a parameter to `make` to disable save state support if more space is required.
- Do you have any changed files, even if you didn't intentionally change them? Please run `git reset --hard` to ensure an unchanged state.
- Did you run `git pull` but forgot to update the submodule? Run `git submodule update --init --recursive` to ensure that the submodules are in sync or run `git pull --recurse-submodules` instead.
- Run `make clean` and then build again. The makefile should handle incremental builds, but please try this first before reporting issues.
- If you have limited resources on your computer, remove the `-j$(nproc)` flag from the `make` command, i.e. run `make flash`.
- If you have changed the external flash and are having problems:
  - Run `make flash_test` to test it. This will erase the flash, write, read and verify the data.
  - If your chip was bought from e.g. ebay, aliexpress or similar places, you might have gotten a fake or bad clone chip. You can set `EXTFLASH_FORCE_SPI=1` to disable quad mode which seems to help for some chips.
- It is still not working? Try the classic trouble shooting methods: Disconnect power to your debugger and G&W and connect again. Try programming the [Base](https://github.com/ghidraninja/game-and-watch-base) project first to ensure you can actually program your device.
- Still not working? Ok, head over to #support on the discord and let's see what's going on.

## How to build

### Prerequisites

- You will need version 10 or later of [arm-gcc-none-eabi toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads). **10.2.0 and later are known to work well**. Please make sure it's installed either in your PATH, or set the environment variable `GCC_PATH` to the `bin` directory inside the extracted directory (e.g. `/opt/gcc-arm-none-eabi-10-2020-q4-major/bin`, `/Applications/ARM/bin` for macOS).
- In order to run this on a Nintendo® Game & Watch™ [you need to first unlock it](https://github.com/ghidraninja/game-and-watch-backup/).

### Building

Note: `make -j8` is used as an example. You may use `make -j$(nproc)` on Linux or `make -j$(sysctl -n hw.logicalcpu)` on Mac, or just write the number of threads you want to use, e.g. `make -j8`.

```bash
# Configure the debug adapter you want to use.
# stlink is also the default, but you may set it to something else:
# export ADAPTER=jlink
# export ADAPTER=rpi
export ADAPTER=stlink

# Clone this repo with submodules:

git clone --recurse-submodules https://github.com/kbeckmann/game-and-watch-retro-go

cd game-and-watch-retro-go

# Install python dependencies, this is optional for basic uses (but recommended!)
python3 -m pip install -r requirements.txt

# Place roms in the appropriate folders:
# cp /path/to/rom.gb ./roms/gb/
# cp /path/to/rom.nes ./roms/nes/
# etc. for each rom-emulator combination.

# On a Mac running make < v4 you have to manually download the HAL package by running:
# make download_sdk

# Build and program external and internal flash.
# Notes:
#     * If you are using a modified unit with a larger external flash,
#       set the EXTFLASH_SIZE_MB to its size in megabytes (MB) (16MB used in the example):
#           make -j8 EXTFLASH_SIZE_MB=16 flash
#     * If you have the Zelda version you can set GNW_TARGET=zelda to have the appropriate
#       flash size and theme set. If you want to stick with the red theme you can set
#       EXTFLASH_SIZE_MB=4 on your Zelda model.

make -j8 flash
```

### Information for developers

If you need to change the project settings and generate c-code from stm32cubemx, make sure to not have a dirty working copy as the tool will overwrite files that will need to be perhaps partially reverted. Also update Makefile.common in case new drivers are used.

## Build and flash using Docker

<details>
  <summary>
    If you are familiar with Docker and prefer a solution where you don't have to manually install toolchains and so on, expand this section and read on.
  </summary>

  To reduce the number of potential pitfalls in installation of various software, a Dockerfile is provided containing everything needed to compile and flash retro-go to your Nintendo® Game & Watch™: Super Mario Bros. system. This Dockerfile is written tageting an x86-64 machine running Linux.

  Steps to build and flash from a docker container (running on Linux, e.g. Archlinux or Ubuntu):

  ```bash
  # Clone this repo
  git clone --recursive https://github.com/kbeckmann/game-and-watch-retro-go

  # cd into it
  cd game-and-watch-retro-go

  # Place roms in the appropriate directory inside ./roms/

  # Build the docker image (takes a while)
  make docker_build

  # Run the container.
  # The current directory will be mounted into the container and the current user/group will be used.
  # In order to be able to flash the device, the container is started with --priviliged and also mounts
  # in /dev/bus/usb. See Makefile.common for the exact command line that is executed if curious.
  make docker

  # Build and flash from inside the container:
  docker@76f83f2fc562:/opt/workdir$ make ADAPTER=stlink EXTFLASH_SIZE_MB=1 -j$(nproc) flash
  ```

</details>

## Backing up and restoring save state files

Save states can be backed up using either `./scripts/saves_backup.sh build/gw_retro_go.elf` or by running `make flash_saves_backup`. Make sure to use the elf file that matches what is running on your device! It is a good idea to keep this elf file in case you want to back up at a later time.

:exclamation: Note the same variables that were used to flash have to be set here as well, i.e. `ADAPTER`, `EXTFLASH_SIZE_MB`, `EXTFLASH_OFFSET`, `INTFLASH_BANK` etc. This is best done with `export VARIABLE=value`.

This downloads all save states to the local directory `./save_states`. Each save state will be located in `./save_states/<emu>/<rom name>.save`.

:exclamation: Make sure to keep a backup of your elf file (`build/gw_retro_go.elf`) if you intend to make backups at a later time. The elf file has to match what's running on the device.

After this, it's safe to change roms, pull new code and build & flash the device.

Save states can then be programmed to the device using a newer elf file with new code and roms. To do this, run `./scripts/saves_restore.sh build/gw_retro_go.elf` - this time with the _new_ elf file that matches what's running on the device. Save this elf file for backup later on. This can also be achieved with `make flash_saves_restore`.

`saves_restore.sh` will upload all save state files that you have backed up that are also included in the elf file. E.g Let's say you back up saves for rom A, B and C. Later on, you add a new rom D but remove A, then build and flash. When running the script, the save states for B and C will be programmed and nothing else.

You can also erase all of the save slots by running `make flash_saves_erase`.

## Screenshots

Screenshots can be captured by pressing `PAUSE/SET` + `GAME`. This feature is disabled by default if the external flash is 1MB (stock units), because it takes up 150kB in the external flash.

Screenshots can be downloaded by running `make dump_screenshot`, and will be saved as a 24-bit RGB PNG.

## Upgrading the flash

The Nintendo® Game & Watch™ comes with a 1MB external flash. This can be upgraded.

The flash operates at 1.8V so make sure the one you change to also matches this.

The recommended flash to upgrade to is MX25U12835FM2I-10G. It's 16MB, the commands are compatible with the stock firmware and it's also the largest flash that comes in the same package as the original.

:exclamation: Make sure to backup and unlock your device before changing the external flash. The backup process requires the external flash to contain the original data.

## Advanced Flash Examples

### Custom Firmware (CFW)
In order to install both the CFW (modified stock rom) and retro-go at the same time, a [patched version of openocd](https://github.com/kbeckmann/ubuntu-openocd-git-builder) needs to be installed and used.

In this example, we'll be compiling retro-go to be used with a 64MB (512Mb) `MX25U51245GZ4I00` flash chip and [custom firmware](https://github.com/BrianPugh/game-and-watch-patch). The internal custom firmware will be located at `0x08000000`, which corresponds to `INTFLASH_BANK=1`. The internal retro-go firmware will be flashed to `0x08100000`, which corresponds to `INTFLASH_BANK=2`. The configuration of custom firmware described below won't use any extflash, so no `EXTFLASH_OFFSET` is specified. We can now build and flash the firmware with the following command:

```bash
make clean
make -j8 EXTFLASH_SIZE_MB=64 INTFLASH_BANK=2 flash
```

To flash the custom firmware, [follow the CFW README](https://github.com/BrianPugh/game-and-watch-patch#retro-go). But basically, after you install the dependencies and place the correct files in the directory, run:
```bash
# In the game-and-watch-patch folder
make PATCH_PARAMS="--internal-only" flash_patched_int
```

## Discord, support and discussion

Please join the [Discord](https://discord.gg/vVcwrrHTNJ).

## LICENSE

This project is licensed under the GPLv2. Some components are also available under the MIT license. Respective copyrights apply to each component.
