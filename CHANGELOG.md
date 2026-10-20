# 0.3.5

SMART candidate labels, conservative warning entities and important counter enablement.
Preserve SMART table expansion during refresh. Fixed hardware request set unchanged.

# 0.3.5

- Move the exclusive cooperative lock from the read-only block device to a checked regular runtime file; retain a shared read lock on the disk.
- Preserve AppArmor raw-write denials and the approved hardware permission scope.
- Separate fstat, block identity, runtime lock and disk read-lock errors.
- Test lock cleanup and zero-request failure paths.

# 0.3.3

- Declare opt-in full_access for the HAOS system disk; the user must disable Protection mode for this app.
- Retain AppArmor and deny other block devices and alternate raw paths.
- Include errno in disk-open errors.

# 0.3.2

- Publish clean app repository metadata and installation documentation.
- Build the fixed-read monitor from source inside the app image.
- Show the actual app hostname for the companion integration.
