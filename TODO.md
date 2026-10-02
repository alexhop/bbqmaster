# TODO

## Known Issues

- [ ] Probes are never dropped from the cache. Once a probe has been seen, its
      last reading keeps being posted every 3 seconds after it goes out of
      range or back on its charger. Track a last-seen time and expire entries
- [ ] The `c6` build has no camera but still sends `streamUrl` and
      `snapshotUrl` with every probe reading
- [ ] Pressure is posted with type `temperature` and gas resistance with type
      `co2`; give them their own types
- [ ] Battery percentage uses a linear voltage mapping, which is inaccurate
      across the flat middle of a LiPo discharge curve

## Unfinished

- [ ] Periodic snapshot upload: `CAM_SNAPSHOT_INTERVAL_SEC`,
      `cameraCaptureJpeg()` and `cameraReleaseFrame()` exist but nothing calls
      them
- [ ] `networkHeartbeat()` takes a `hasCamera` argument that is not sent
- [ ] Only the probe advertisement format is parsed; Combustion's other
      products (display, booster, gauge) are ignored

## Housekeeping

- [ ] Unit tests for the advertisement parser (13-bit temperature unpacking,
      virtual sensor mapping)
- [ ] Hardware build notes: enclosure, mounting, heat near the grill
