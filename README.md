Fork of the venerable `mtr`. Mtr is that *nix tool that combines `traceroute` and `ping`. Now this fork also provides geolocation of the hosts traversed along the route when ran in **curses** mode (i.e. from the console and/or using the `--curses` switch). 

## Compilation

The autoconf still needs some work. For now you will need to run ./bootstrap, then ./configure and to fix the generated Makefile manually (it should not be difficult to change by hand, it simply misses the files geoip.c and geoip.h).

## Usage

Use it the same way as the regular mtr, e.g. `mtr slackware.com`. If you want
geolocation, simply press **c** for **C**ountry/**C**ity.

## Bugs

This fork works but is still experimental. Feel free to propose modifications.
