Pi Apps {{VERSION}}: one SD card for the **Raspberry Pi 3 B+** with a menu of bare-metal programs: Typing Adventure and the Graphing Calculator. No operating system, no network, Wi-Fi or Bluetooth support.

Built from commit {{COMMIT}}.

## New SD card

1. Download **`pi-apps-{{VERSION}}.img.xz`**.
2. Flash it to a microSD card with [Raspberry Pi Imager](https://www.raspberrypi.com/software/): *Choose OS → Use custom*, pick the file, then choose your card. Flashing erases the card.
3. Put the card in the Pi 3 B+. Connect an HDMI screen, a USB keyboard and headphones or speakers, then power on.

## Moving from a Typing Adventure card (keeps players and progress)

Copy everything from **`pi-apps-{{VERSION}}-sdcard-files.zip`** onto the card, including the `apps` folder, and replace `kernel8.img`. `progress.txt` and `cmdline.txt` stay as they are.

## Using the menu

- **← →** choose a program, **Enter** starts it. The number keys start a program directly.
- After power-on, the program used last starts by itself after 10 seconds. Press any key to choose another one.
- To come back to the menu: **Esc** on Typing Adventure's players screen, **F10** in the calculator.

## Settings

- `apps/apps.txt` lists the programs and the autostart time (`autostart=0` turns it off). Parents can edit it.
- `cmdline.txt` holds the sound setting for all programs (`sounddev=sndpwm` headphone jack, `sndhdmi` HDMI, `none`).

## Updating one program

Copy its new `kernel8.img` into the `apps` folder under the program's name (`apps/typing.img`, `apps/graphcalc.img`). Each program's saved work is kept.

## If something doesn't work

- **F12** opens the system log in the menu and in each program. Take a photo of it.
- **`kernel8-debug.img`** is the menu with extra USB logging. Copy it onto the card and rename it to `kernel8.img`.

## Files

| File | What it is |
|---|---|
| `pi-apps-{{VERSION}}.img.xz` | Complete SD card image: menu and programs |
| `pi-apps-{{VERSION}}-sdcard-files.zip` | The same files, to copy onto a FAT32 card |
| `kernel8.img` | The menu only |
| `kernel8-debug.img` | The menu with extra USB logging |
| `SHA256SUMS.txt` | Checksums of the files above |
