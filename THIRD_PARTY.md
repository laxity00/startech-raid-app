# Implementation and dependencies

The distributed application, integration and fixed-read backend are original
implementations. No vendor utility, vendor firmware, smartctl executable or
smartmontools source is included. The ASM1352R port-select protocol was researched
using upstream smartmontools as a protocol reference:
https://github.com/smartmontools/smartmontools/blob/master/smartmontools/scsiata.cpp
Controller state decoding was informed by static firmware interoperability research;
the proprietary firmware and captures are excluded. This is a source/provenance
review, not a guarantee about every possible intellectual-property claim.

Our authored files are offered under the accompanying MIT license. This does not
relicense third-party components. The source-only bundle contains no compiled
runtime or container layers. Building the app obtains the official Home Assistant
base image, Alpine packages and Python, each retaining its upstream license.
The backend is compiled against the image's musl C library, rather than shipping
the earlier static glibc executable. Home Assistant supplies aiohttp and voluptuous
for the companion integration. Build tooling and Linux headers have their own
licenses and are confined to the builder stage. A published container image needs
an inventory and license review of its actual resolved packages and layers.

Sources: https://github.com/home-assistant/docker-base
https://www.python.org/psf/license/
https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
https://www.alpinelinux.org/about/

StarTech, ASMedia, Home Assistant and related product names identify compatibility.
This independent project is not affiliated with or endorsed by those vendors.
