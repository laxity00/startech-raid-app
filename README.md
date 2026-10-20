# StarTech RAID Monitor 0.3.2 — experimental

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
