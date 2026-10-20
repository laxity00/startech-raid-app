# StarTech RAID Monitor 0.3.4 — experimental

## Read-only lock correction in 0.3.4

AppArmor checks an exclusive flock on a block device as a write-class operation.
The previous backend requested LOCK_EX on /dev/sda while the profile correctly
denied raw disk writes. This could fail before INQUIRY even after opening the disk.

The exclusive cooperative collector lock now uses a root-owned regular 0600 file
at /run/startech-raid-status.lock. It is opened without following symlinks and is
checked for regular-file type, ownership, mode and link count before locking.
Its contents are not written. /dev/sda remains O_RDONLY and receives LOCK_SH,
which still conflicts with older collectors' exclusive device locks. Independent
containers have separate runtime lock files and shared device locks do not
exclude other shared readers; do not run duplicate collectors. Locks are advisory.
No disk-write permission, new request, retry or address override is added.

Identity, fstat, runtime-lock and disk-read-lock failures now report separately.
No device request is sent if any of these gates fails. Keep the 0.3.3 Protection
mode setting and leave AppArmor enabled. Stop, update to 0.3.4, start, then inspect
a fresh sample. No integration update or Core restart is required.

Kernel reference:
https://github.com/torvalds/linux/blob/master/security/apparmor/lsm.c


## System-disk access in 0.3.3

Supervisor 2026.10.2 refuses ordinary device permissions for disks used by HAOS,
even when an app declares /dev/sda and SYS_RAWIO. Version 0.3.2 consequently cannot
open a protected HAOS boot/data disk. This is a permissions failure, not a RAID
failure, and no device request follows a failed open.

This release declares full_access: true. It takes effect only with Protection
mode OFF for this app. This grants a broad device-cgroup permission and access to
the host device tree, so it is a real reduction in container isolation. It is not
equivalent to an ordinary single-device grant. No protection setting is changed
automatically. Leave Protection mode on if you do not accept that scope; the app
will report Unknown when the system disk cannot be opened.

To try the update: stop the app, refresh the App Store, update to 0.3.4, then turn
off Protection mode for StarTech RAID Monitor only and start it. AppArmor remains
enabled. Keep all other app protection settings unchanged. If the toggle is not
visible, enable Advanced mode in your HA user profile. Restarting this app resets
the paused SMART latch; no Home Assistant Core restart or integration update is
needed. After startup inspect a fresh sample and the logs before enabling boot.

The backend still opens only /dev/sda with O_RDONLY and the same identity/version
checks and fixed read requests. No address scan, data-out, firmware operation,
SMART self-test or write-access fallback is added. AppArmor denies raw disk writes,
other block-device paths, partition nodes and alternate raw SCSI/USB paths. The
profile reduces exposure but does not turn full_access into a single-device grant.

If access still fails, the new message includes errno. Share that message and
relevant Supervisor/AppArmor denials after removing identifying information.
Do not disable AppArmor or alter permissions on other apps as a troubleshooting step.

Primary references:
https://github.com/home-assistant/supervisor/blob/2026.10.2/supervisor/hardware/policy.py
https://github.com/home-assistant/supervisor/blob/2026.10.2/supervisor/docker/app.py


Independent read-only RAID and SSD monitoring for the narrowly supported
StarTech SM22BU31C3R / ASM1352R profile. Not affiliated with StarTech or ASMedia.

## Install through Home Assistant

1. Open Settings → Apps → App Store → menu → Repositories. Add:
   https://github.com/laxity00/startech-raid-app
2. Install StarTech RAID Monitor. The app builds its image from the supplied
   source using the official Home Assistant base and Alpine packages.
3. Start it and inspect its logs. Open Web UI → Configuration and note the
   internal hostname, port 8098 and generated token. Keep that token private.
4. In HACS → menu → Custom repositories, add https://github.com/laxity00/startech-raid-integration, category
   Integration. Download StarTech RAID Monitor and restart Home Assistant Core.
5. Settings → Devices & services → Add integration → StarTech RAID Monitor.
   Enter the hostname shown by the app, port and token. Enable Start on boot
   only after the app has collected a fresh valid sample.

Do not run older collectors concurrently. Preserve backups and previous versions.
The repositories must actually be published before these links can be installed.

## Supported profile and limitations

Only aarch64 HAOS and the fixed /dev/sda, USB 174c:55aa, factory-style USB serial
of nineteen zeroes followed by one, SCSI StarTech / RAID 1 / revision 0 and
firmware 161028_B5_30_81 are supported. Identity mismatch stops collection.
This is not a general ASM1352R firmware scanner or autodiscovery tool.

RAID states: 1 - Healthy; 2 - Degraded; 3 - Failed; 4 - Rebuilding;
other values - Unknown. Labels come from static firmware analysis. Only raw state
1 has runtime evidence; degraded/failed/rebuilding physical conditions remain
untested. A matching version does not verify complete firmware identity.
SMART overall remains Unknown. Pre-fail thresholds and provisional temperatures
are reported separately. There are no self-tests, writes, firmware operations,
arbitrary commands or automatic retries. See the full app guide for details.

## Validation status

The original fixed monitor reads succeeded on one installation. Offline mock,
CLI, app HTTP and integration contract tests passed for the clean source subset.
The GitHub workflow builds and smoke-tests the ARM64 app image without devices.
That workflow's result, HAOS permissions, integration lifecycle and physical
installation must be verified before calling this a validated public release.
No successful cloud build or physical app installation is implied by this README.

## Data and licensing

Published source contains no original device captures, SSD identities, credentials,
vendor firmware or utilities. Runtime SSD serials and credentials are private data;
do not attach backups, raw logs or tokens to issues. Authored code is MIT licensed.
See LICENSE and THIRD_PARTY.md. The source is original interoperability code;
the container's third-party components retain their own licences.

## Documentation

Full app guide: https://github.com/laxity00/startech-raid-app/blob/main/startech_raid/DOCS.md
App issues: https://github.com/laxity00/startech-raid-app/issues
Integration issues: https://github.com/laxity00/startech-raid-integration/issues

## SMART labels and warning entities in 0.3.5

Update both the app and HACS integration, then restart Home Assistant Core.
The app reads the controller signature from the already collected SMART sector;
no additional hardware commands or permissions are added. Labels apply only to
the SMI2259XT signature with SSD firmware Y0507A0. They are inferred from family
documentation, not an Integral-certified attribute specification. Other drives
retain generic raw labels and these extra warning states remain Unknown.

All 30 attributes receive candidate names for this profile. Raw values stay raw:
241/242/245 are not converted into bytes, and no write amplification is calculated.
The candidate life percentage is raw attribute 169, not normalized Current=100.
Initial bad blocks (163), rated erase ceiling (168), corrected ECC (195), power
events (192), and workload counters do not alone trigger media failure warnings.

Enabled by default per SSD: media error indicators (any nonzero value among
5/160/175/176/178/181/182/196/197/198), interface CRC errors (199), and estimated
remaining life at or below 20% (169, valid range 0..100). These supplement the
existing pre-fail threshold and user temperature warnings. Counters may record
historical events: an On warning means investigate, not that failure is imminent.
Missing or invalid evidence is Unknown/unavailable. SMART overall stays Unknown.

Raw counters 5/9/160/161/169/175/176/178/181/182/196/197/198/199 are now enabled
for matched drives. Existing unique IDs and entity IDs are preserved. Counters
disabled by the integration are enabled on update; user-disabled entities stay
disabled. No notifications are sent automatically. Select these problem entities
in an HA automation if you want alerts, including collection and RAID problems.
Add any sensor to an editable dashboard through Add card > By entity. Temperature
already supports history/statistics; raw vendor counters have no invented units.

SMART table expansion is preserved independently for each port across the
15-second page refresh and navigation within the current page session.

Sources: [Transcend MTS960T v1.4 tables 18/19](https://docs.rs-online.com/3aeb/A700000012186708.pdf),
[smartmontools database](https://github.com/smartmontools/smartmontools/blob/master/smartmontools/drivedb.h).
No vendor firmware, captures, identifiers or third-party implementation is bundled.
