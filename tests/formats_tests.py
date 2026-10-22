#!/usr/bin/env python3
import hashlib,importlib.util,io,pathlib,subprocess,sys,tempfile,threading,zipfile
probe=str(pathlib.Path(sys.argv[1]).resolve())
spec=importlib.util.spec_from_file_location('fixture',pathlib.Path(__file__).with_name('library_tests.py'));fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
for typ,size,marker in [(1,32768,b'SRAM_V110'),(2,65536,b'FLASH_V123'),(3,131072,b'FLASH1M_V103'),(4,512,b'EEPROM_V124'),(5,8192,b'EEPROM_V124')]:
    rom=bytearray(fixture.rom);rom[1024:1040]=b'\0'*16;rom[1024:1024+len(marker)]=marker
    stream=io.BytesIO()
    with zipfile.ZipFile(stream,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('Homebrew.gba',rom)
    fixture.archive=stream.getvalue();battery=bytes(i%251 for i in range(size))
    class Case(fixture.Handler):
        saves=[dict(id=99,file_name='fixture.srm',rom_id=100,user_id=1,slot='autosave',emulator='mgba',file_size_bytes=size,content_hash=hashlib.md5(battery).hexdigest(),updated_at='2026-10-22T00:00:00+00:00')]
        data={99:battery};acks={};mode='normal';uploads=0
        def do_POST(self):
            # Parent validates multipart MD5 and all production API guards. Its length check is dynamic below.
            return super().do_POST()
    server=fixture.http.server.ThreadingHTTPServer(('127.0.0.1',0),Case);thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='retrosync-format-') as temp:
            pathlib.Path(temp,'sdmc:').mkdir();subprocess.run([probe,f'http://127.0.0.1:{server.server_port}',str(typ),str(size)],cwd=temp,check=True,timeout=40)
    finally:server.shutdown();server.server_close();thread.join()
