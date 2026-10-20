# 0.3.3

- Fix HAOS system-disk access by declaring full_access, requiring the user to
  disable Protection mode for this app. No settings are changed automatically.
- Retain AppArmor and add denies for other block devices/alternate raw paths.
- Include errno in read-only disk-open errors.
- Retain the same nine fixed requests, identity/version gates and read-only opens.
- Live system-disk app access still requires confirmation from an installation.

# 0.3.3

- Add GitHub app repository metadata and UI installation documentation.
- Compile the original fixed-read backend from source in the app image.
- Show the actual container hostname for companion integration setup.
- Exclude research tools, firmware and user environment details.
- Experimental: live HAOS app validation remains outstanding.
