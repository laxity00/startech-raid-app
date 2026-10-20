# 0.3.5

- Label all 30 SMART attributes for the matched SMI2259XT/Y0507A0 profile, retaining inference limits.
- Expose and log media, interface and remaining-life warning indicators from existing read data.
- Preserve each SMART table expander across refreshes, with mobile horizontal scrolling.
- Fixed requests, read-only disk access and AppArmor permissions unchanged.

# 0.3.0

- Private ingress UI: overview, SSDs and temperature chart, configuration, logs, documentation.
- Periodic collection with bounded error handling, freshness expiration and SMART pause after incomplete reads.
- Token-protected cached API; no exposed host ports or hardware-control endpoints.
- Device-specific permission request and AppArmor profile instead of full_access.
- Companion integration: UI setup/reconfiguration, stable controller/SSD devices, temperature statistics, identity/capacity/health entities, problem sensors and optional raw SMART attributes.
- Redacted diagnostics include a dashboard using actual registered entity IDs.
- Existing tested v0.2.0 backend preserved byte-for-byte; no new hardware commands.

New container permissions and actual HA registration require live installation testing.
