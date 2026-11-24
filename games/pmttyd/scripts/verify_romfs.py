#!/usr/bin/env python3
"""启动前验证 IVFC 提取记录与完整资源映像，拒绝半成品。"""
import hashlib
import json
import sys
from pathlib import Path

image = Path(sys.argv[1])
report = json.loads(image.with_name('index.json').read_text())
if report['status'] != 'pass' or len(report['ivfc_blocks']) != 6:
    raise ValueError('RomFS 的六层 IVFC 提取尚未验证完成')
if image.stat().st_size != report['romfs_size']:
    raise ValueError('RomFS 文件大小不匹配')
with image.open('rb') as f:
    digest = hashlib.file_digest(f, 'sha256').hexdigest()
if digest != report['romfs_sha256']:
    raise ValueError('RomFS SHA256 不匹配')
print('RomFS 完整性验证通过')
