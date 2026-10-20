# StarTech RAID Monitor 0.3.4 — experimental source distribution

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


Periodic read-only controller status and per-SSD SMART monitoring for the narrowly
supported StarTech SM22BU31C3R ASM1352R RAID1 profile on aarch64 Home Assistant OS.
This is independent software, without vendor affiliation.

## Compatibility boundary

This clean distribution removes personal environment information. It does not
broaden hardware support. The fixed profile requires /dev/sda (8:0), USB 174c:55aa,
the factory-style USB serial consisting of nineteen zeroes followed by one,
SCSI StarTech / RAID 1 / revision 0 and firmware 161028_B5_30_81.
Those protocol compatibility constants are retained as safety checks; they are
not an owner's SSD identity, account, hostname or network address.
The controller state address is fixed at 0x0AED and is never user configurable.
Version matching does not prove that two complete firmware images are identical.
Other firmware, RAID modes, device paths and architectures are not supported.

The earlier backend read state 1 and both SSDs successfully on one installation.
The new source subset preserves its monitor routines and nine fixed requests;
its rebuilt app image and HA runtime integration still require live validation.
No hardware is accessed during distribution tests.

## Meaning and limits

1 - Healthy; 2 - Degraded; 3 - Failed; 4 - Rebuilding; other values - Unknown.
Mappings are inferred from static controller firmware analysis. Degraded, failed
and rebuilding physical conditions have not been independently tested. Healthy
does not independently establish complete sector synchronization.
SMART overall stays Unknown because ATA RETURN STATUS is not supported by this
transport. Pre-fail threshold comparisons are separate. Temperatures from raw
attribute 194/190 are provisional; warning defaults are user thresholds, not
manufacturer limits. No self-test start/abort commands are implemented.

## Installation

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


## Monitoring and entities

Defaults: interval_seconds 300 (60–3600), enable_smart true,
temperature_warning_c 65 (20–100), log_level info (info/debug/warning).
Each collection uses one INQUIRY, one firmware version read and one state read,
plus up to three fixed ATA reads for each SSD. Maximum nine requests, 3000 ms
each, no retries, no data-out. Device opens are read-only. There are no arbitrary
device, CDB, address, length or command options, disk writes or firmware operations.

Overview, SSDs, Configuration, Logs and Documentation are available through
authenticated HA ingress. Log rotation is bounded; raw sectors and tokens are
not logged. The API serves cached data with bearer authentication. An incomplete
SMART sample pauses future SMART collection until app restart; RAID reads continue.
Failed/stale samples cannot silently remain Healthy. Refreshing entities reads
the cache and never triggers an extra disk request.

The integration creates a controller device and serial-identified SSD devices,
with RAID/collection problems, raw RAID byte, timestamps and durations, drive
identity, capacity, provisional temperature and pre-fail attribute checks.
Selected SMART counters for the matched profile are enabled by default; other raw counters remain optional. Unverified units are not assigned.
Use HA's actual entity picker for dashboards, history/statistics graphs and alerts.
Download integration diagnostics to obtain a dashboard using registered entity IDs.
Review all diagnostics before sharing. Runtime identities, logs and backups remain
private even though the distribution contains no personal data.

## Updates and recovery

Keep app data and HA configuration in backups. Preserve /data/identity.json to
retain API credentials and device identities. Stop the app before replacing its
sources; rebuild/update it and restart Core after replacing integration files.
For rollback restore the prior app/integration sources and rebuild/restart. Never
install into a container's transient filesystem or /tmp. No automatic publishing,
HAOS deployment, firmware update or remote host access is performed by this bundle.

## Release validation still required

The backend has offline mock tests and native/ARM offline CLI checks. The app has
loopback HTTP tests; the integration has contract doubles, not a live HA runtime.
Docker base image compilation, musl runtime, AppArmor permissions, HA lifecycle
and real installation are not yet verified. Package versions are not locked to
image digests. Review the resolved image dependencies before distributing images.
See THIRD_PARTY.md for original-code provenance and upstream dependency boundaries.

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
