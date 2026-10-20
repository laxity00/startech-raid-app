# 0.3.0

- Private ingress UI: overview, SSDs and temperature chart, configuration, logs, documentation.
- Periodic collection with bounded error handling, freshness expiration and SMART pause after incomplete reads.
- Token-protected cached API; no exposed host ports or hardware-control endpoints.
- Device-specific permission request and AppArmor profile instead of full_access.
- Companion integration: UI setup/reconfiguration, stable controller/SSD devices, temperature statistics, identity/capacity/health entities, problem sensors and optional raw SMART attributes.
- Redacted diagnostics include a dashboard using actual registered entity IDs.
- Existing tested v0.2.0 backend preserved byte-for-byte; no new hardware commands.

New container permissions and actual HA registration require live installation testing.
