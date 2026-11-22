# mac-display: Kobo Sage as an e-ink extended display for a Mac

The Mac gets a virtual second monitor (BetterDisplay); macVNC
(github.com/LibVNC/macVNC, `-display <n>`) serves only that monitor over VNC;
the Kobo shows it with einkvnc (github.com/ivy-rew/eink-vnc v0.2.1, which has
Kobo Sage / "cadmus" support from Plato) over Wi-Fi. View only: type and click
on the Mac.

Kobo files (`.adds/macdisplay/`): `einkvnc` (from the v0.2.1 release
KoboRoot.tgz, unchanged), `kobo-hid` (from ../kobo-hid, used in `watch` mode to
hold buttons/touch away from Nickel and to stop), `mac-display.sh`,
`display.cfg`. Menu: `.adds/nm/macdisplay`.

Stop: power button, or hold both page buttons 1.5 s.
