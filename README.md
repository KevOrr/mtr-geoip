Too early to recommend using it yet. 

The aim of this fork is to make the great `mtr` not only a tool combining `traceroute` and `ping`, but also getting the geolocation of IPs of hosts traversed along the route, esp. in text mode. It works but the new code still needs a lot of testing to be considered usable.

## Current version

Should you try it, please add 2>error.log after the command so as to redirect stderr, otherwise you will just get all the debugging messages. Eg: `mtr 8.8.8.8 2>log`
