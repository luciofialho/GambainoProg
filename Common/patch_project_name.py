# patch_project_name.py
# Post-build script: patches project_name field in esp_app_desc_t of the .bin
# Add to platformio.ini:
#   extra_scripts = post:../Common/patch_project_name.py
#   custom_project_name = YourProjectName

Import("env")
import struct
import hashlib

def patch_app_desc(source, target, env):
    firmware_path = str(target[0])

    try:
        project_name = env.GetProjectOption("custom_project_name")
    except Exception:
        print("[patch] custom_project_name not set in platformio.ini, skipping")
        return

    ESP_APP_DESC_MAGIC = 0xABCD5432

    with open(firmware_path, 'rb') as f:
        data = bytearray(f.read())

    # Detect appended SHA256: SHA256(data[:-32]) == data[-32:]
    has_hash = (hashlib.sha256(data[:-32]).digest() == bytes(data[-32:]))

    # Find esp_app_desc_t by magic word (4-byte aligned scan)
    found_offset = -1
    for i in range(0, len(data) - 256, 4):
        if struct.unpack_from('<I', data, i)[0] == ESP_APP_DESC_MAGIC:
            found_offset = i
            break

    if found_offset < 0:
        print(f"[patch] WARNING: esp_app_desc_t not found in {firmware_path}")
        return

    # esp_app_desc_t layout (IDF 4.x / 5.x):
    #   offset  0: magic_word      (4)
    #   offset  4: secure_version  (4)
    #   offset  8: reserv1[2]      (8)
    #   offset 16: version[32]
    #   offset 48: project_name[32]  <-- we patch this
    project_name_offset = found_offset + 48

    old_name = data[project_name_offset:project_name_offset + 32].rstrip(b'\x00').decode('utf-8', errors='replace')
    name_bytes = project_name.encode('utf-8')[:31]
    data[project_name_offset:project_name_offset + 32] = name_bytes + b'\x00' * (32 - len(name_bytes))

    # Recalculate appended SHA256 if present
    if has_hash:
        data[-32:] = hashlib.sha256(data[:-32]).digest()

    with open(firmware_path, 'wb') as f:
        f.write(bytes(data))

    print(f"[patch] project_name: '{old_name}' -> '{project_name}'" +
          (" (SHA256 recalculated)" if has_hash else ""))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", patch_app_desc)
