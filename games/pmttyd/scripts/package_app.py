#!/usr/bin/env python3
"""打包玩家本地 App；包含游戏数据，不能分享，不覆盖已有 App。"""
import argparse
import plistlib
import shutil
import subprocess
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--work',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args();work=args.work.resolve();out=args.out.resolve()
if out.exists():raise SystemExit('输出 App 已存在，请选择新路径，避免覆盖')
game=Path(__file__).resolve().parents[1]
(work/'build/shader-cache/app-cache').mkdir(parents=True,exist_ok=True)
contents=out/'Contents';macos=contents/'MacOS';resources=contents/'Resources';macos.mkdir(parents=True);resources.mkdir()
shutil.copy2(work/'build/pmttyd-native',macos/'pmttyd-native')
info=plistlib.loads((game/'runtime/game_info.plist').read_bytes());info['MNGWorkFolder']=str(work);info['MNGGame']='pmttyd';info['MNGGameVersion']='1.0.0'
(contents/'Info.plist').write_bytes(plistlib.dumps(info))
shutil.copy2(work/'tables-aligned/runtime-tables.bin',resources/'runtime-tables.bin')
(resources/'rewritten').mkdir()
for name in ('main','sdk','subsdk0','rtld'):
 source=work/'rewritten-aligned'/f'{name}.patched.bin';dest=resources/'rewritten'/f'{name}.patched.bin'
 result=subprocess.run(['cp','-c',str(source),str(dest)])
 if result.returncode:shutil.copyfile(source,dest)
result=subprocess.run(['cp','-c',str(work/'romfs/romfs.bin'),str(resources/'romfs.bin')])
if result.returncode:shutil.copyfile(work/'romfs/romfs.bin',resources/'romfs.bin')
service=contents/'build/shader-service';service.mkdir(parents=True);shutil.copy2(work/'build/shader-service/totk_xlate',service/'totk_xlate')
subprocess.run(['codesign','--force','--deep','--sign','-',str(out)],check=True)
print('本地 App 已打包：',out)
