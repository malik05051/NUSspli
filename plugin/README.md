# NUSspli Background Downloads

An Aroma plugin that finishes the downloads of NUSspli while something else is
running: a game, the Wii U Menu, any title. It shows how far it is on screen
while it works and says when it is done.

NUSspli downloads a title into a folder below `SD:/install/` and writes
`title.tmd`, `title.tik` and `title.cert` into it before the contents follow.
The TMD names every content and the size it has when it is complete, so the
plugin can see for itself which download was left unfinished - the plugin and
NUSspli do not talk to each other, they only share the folder on the SD card.

So the usual way to use it is: queue what you want in NUSspli, leave the app
whenever you like, and the rest of the download follows while you play.

## What it does

- On every title start it looks through `SD:/install/` for downloads whose
  contents are missing or only partly there.
- It downloads what is missing from the same server NUSspli uses, continuing
  the files instead of starting over.
- A notification tells you how many downloads it took over and that the
  progress can be seen - the progress itself is a notification that stays on
  screen and counts up, and the config menu of the plugin shows the same line.
- When everything is in, a notification says that the downloads are finished
  and that NUSspli installs them. That one does not fade: the install is the
  one thing left to do, so it stays on screen until it is hidden with PLUS.
  It goes by itself when the title ends, and NUSspli asks about the finished
  download the next time it starts, so nothing is lost either way.
- While it downloads it holds the automatic power down of the console off, so
  a queue left to finish on the HOME Menu is not cut short by the console
  switching itself off. The setting of the user is put back afterwards, and
  only the value in force for this boot is touched, never the one the console
  stores: a plugin that dies without putting it back cannot leave the console
  unable to power itself down.

## What it does not do

- **It installs nothing.** Installing a title needs the console to itself, so
  the plugin only downloads and leaves the install to NUSspli.
- **SD card only.** A plugin cannot reach the USB and MLC install folders, so
  downloads NUSspli saves there are left to NUSspli.
- It stays out of the way while NUSspli itself is running: NUSspli downloads on
  its own then, and two downloaders in one folder would fight over the files.
- A download it cannot finish is not broken - the bytes stay on the SD card and
  NUSspli, or the next title start, continues where it stopped.
- **It cannot download in standby.** A plugin is code inside the title that
  runs, and standby runs none: the console tears the title down and the plugin
  with it. The download stops there and continues at the next title start.
  What does download with the console switched off is the Wii U Download
  Management, which NUSspli hands titles to itself - but that one installs
  with the ticket the console has, so it only works for titles that have one
  there, the ones NUSspli installed included: an install leaves its ticket on
  the console. The plugin needs none and downloads whatever NUSspli can.

## Requirements

- [Aroma](https://aroma.foryour.cafe) with the WiiUPluginLoaderBackend.
- The [NotificationModule](https://github.com/wiiu-env/NotificationModule) for
  the notifications. Without it the downloads still run, only silently.

## Install

Move `NUSspli-BackgroundDownloads.wps` to `SD:/wiiu/environments/aroma/plugins/`.
The settings (on or off, notifications on or off) and the progress are in the
Aroma plugin menu.

## Building

```sh
docker build -t nussplipluginbuilder .
docker run --rm -v ${PWD}:/project nussplipluginbuilder make
```

`make debug` builds the same plugin with its log going to the system log.
