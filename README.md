# Matter Air Purifier

A CRBox air purifier that can be controlled through Matter over Thread.



## 1. OTA Update
- Update `CONFIG_DEVICE_SOFTWARE_VERSION` `CONFIG_DEVICE_SOFTWARE_VERSION_NUMBER` in `sdkconfig.defaults.esp32c6`
- Update `PROJECT_VER` `PROJECT_VER_NUMBER` in CMakeLists.txt
- Run `openssl dgst -sha256 -binary matter-air-purifier-ota.bin | base64`
- Copy `matter-air-purifier-ota.bin` to `/addon_configs/core_matter_server/updates/matter-air-purifier-ota.ota`
- Create `matter-air-purifier-ota.json` with the following content
```{
  "modelVersion": {
    "vid": 65521,
    "pid": 32769,
    "softwareVersion": <<VERSION>>,
    "softwareVersionString": "<<VERSION STRING>>",
    "cdVersionNumber": 1,
    "softwareVersionValid": true,
    "otaUrl": "file:///matter-air-purifier-ota.ota",
    "otaChecksum": "<<CHECKSUM GENERATED ABOVE>>",
    "otaChecksumType": 1,
    "minApplicableSoftwareVersion": 0,
    "maxApplicableSoftwareVersion": <<PREVIOUS VERSION>>,
    "releaseNotesUrl": ""
  }
}
```
- Run the update by selecting the node in the Matter Server web ui in the sidebar.
