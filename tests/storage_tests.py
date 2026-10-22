#!/usr/bin/env python3
import io,os,pathlib,struct,subprocess,sys,tempfile,zipfile
probe=str(pathlib.Path(sys.argv[1]).resolve())
rom=bytearray(32768);rom[0xb2]=0x96;rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255
checks=0
with tempfile.TemporaryDirectory(prefix='retrosync-storage-') as temp:
    root=pathlib.Path(temp);(root/'sdmc:').mkdir();subprocess.run([probe],cwd=root,check=True)
    def run(name,entries,expected,method=zipfile.ZIP_DEFLATED,cancel=False,mutate=None):
        global checks
        archive=root/(name+'.zip')
        with zipfile.ZipFile(archive,'w',method) as z:
            for path,data in entries:z.writestr(path,data)
        if mutate:archive.write_bytes(mutate(archive.read_bytes()))
        out=root/(name+'.gba');result=subprocess.run([probe,str(archive),str(out),'cancel' if cancel else 'run'],capture_output=True,text=True,check=True)
        assert result.stdout.strip()==('OK' if expected else 'REFUSED'),(name,result.stdout);checks+=1
        assert (out.read_bytes()==rom) if expected else not out.exists(),name;checks+=1
        return archive,out
    run('deflate',[('sub/Homebrew.gba',rom)],True)
    run('stored',[('Homebrew.gba',rom)],True,zipfile.ZIP_STORED)
    run('two-roms',[('one.gba',rom),('two.gba',rom)],False)
    run('traversal',[('../escape.gba',rom)],False)
    run('absolute',[('/escape.gba',rom)],False)
    run('backslash',[(r'folder\escape.gba',rom)],False)
    run('no-rom',[('README.txt',b'Nothing playable')],False)
    run('truncated',[('Homebrew.gba',rom)],False,mutate=lambda b:b[:-9])
    run('cancel',[('Homebrew.gba',rom)],False,cancel=True)
    def broken_crc(b):
        b=bytearray(b);central=b.index(b'PK\x01\x02');b[central+16]^=1;return bytes(b)
    run('crc',[('Homebrew.gba',rom)],False,mutate=broken_crc)
    def encrypted(b):
        b=bytearray(b);central=b.index(b'PK\x01\x02');b[central+8]|=1;b[6]|=1;return bytes(b)
    run('encrypted',[('Homebrew.gba',rom)],False,mutate=encrypted)
    def bomb(b):
        b=bytearray(b);central=b.index(b'PK\x01\x02');struct.pack_into('<I',b,central+24,33*1024*1024);return bytes(b)
    run('bomb',[('Homebrew.gba',rom)],False,mutate=bomb)
    sym=zipfile.ZipInfo('link.gba');sym.create_system=3;sym.external_attr=(0o120777<<16)
    run('symlink',[(sym,b'/outside.gba')],False)
    archive,out=run('existing',[('Homebrew.gba',rom)],True)
    out.write_bytes(b'User file');r=subprocess.run([probe,str(archive),str(out),'run'],capture_output=True,text=True,check=True)
    assert r.stdout.strip()=='REFUSED' and out.read_bytes()==b'User file';checks+=1
print(f'PASS {checks} ZIP safety checks')
