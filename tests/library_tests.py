#!/usr/bin/env python3
"""Actual client/service HTTP integration with synthetic homebrew bytes only."""
import hashlib,http.server,io,json,pathlib,subprocess,sys,tempfile,threading,zipfile
from urllib.parse import urlsplit,parse_qs
probe=str(pathlib.Path(sys.argv[1]).resolve())
rom=bytearray(32768);rom[0xb2]=0x96;rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255;rom[1024:1033]=b'SRAM_V110'
buf=io.BytesIO()
with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('demo/Homebrew.gba',rom)
archive=buf.getvalue()
def md5(x):return hashlib.md5(x).hexdigest()
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.0'
    saves=[dict(id=99,file_name='fixture.srm',rom_id=100,user_id=1,slot='autosave',emulator='mgba',file_size_bytes=32768,content_hash=md5(b'R'*32768),updated_at='2026-10-22T00:00:00+00:00')]
    data={99:b'R'*32768};acks={};mode='normal';uploads=0;lost_upload_count=None;unknown_count=None;refreshes=0
    def log_message(self,*a):pass
    def send(self,status,obj):
        data=obj if isinstance(obj,bytes) else json.dumps(obj).encode();self.send_response(status);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def auth(self):
        if self.headers.get('Authorization')!='Bearer access':self.send(401,{});return False
        return True
    def add(self,c):
        cls=type(self);rid=max(cls.data)+1;data=bytes([c])*32768 if isinstance(c,int) else bytes(c);row=dict(cls.saves[-1],id=rid,content_hash=md5(data),updated_at='2026-10-22T00:00:00+00:00');cls.saves.append(row);cls.data[rid]=data;return row
    def do_GET(self):
        if not self.auth():return
        u=urlsplit(self.path);q=parse_qs(u.query);cls=type(self)
        if u.path.startswith('/api/test/'):
            mode=u.path.rsplit('/',1)[-1]
            if mode.startswith('remote-'):self.add(ord(mode[-1]));cls.mode='normal'
            elif mode=='assertions':
                assert cls.uploads==4,(cls.uploads,'only definite uploads plus each uncertain attempt once')
                assert cls.unknown_count==cls.uploads,'unknown upload was not resent'
                assert len(cls.saves)>=7,'all history retained'
            else:cls.mode=mode
            return self.send(200,{})
        if u.path=='/api/heartbeat':return self.send(200,{'SYSTEM':{'VERSION':'5.4.0-test'}})
        if u.path=='/api/users/me':return self.send(200,{'id':1,'username':'demo'})
        if u.path=='/api/platforms':return self.send(200,[{'id':1,'name':'GBA','slug':'gba'}])
        if u.path=='/api/roms':
            limit=int(q['limit'][0]);offset=int(q['offset'][0]);row={'id':100,'platform_id':1,'name':'Fixture','fs_name':'Fixture.zip','fs_size_bytes':len(archive),'merged_ra_metadata':'x'*(140000 if cls.mode=='huge' else 40000)}
            return self.send(200,{'items':[dict(row,id=100+i) for i in range(offset,min(offset+limit,45))],'total':45,'offset':offset})
        if u.path=='/api/roms/100':return self.send(200,{'id':100,'platform_id':1,'files':[{'id':7,'file_name':'Fixture.zip','is_top_level':True,'file_size_bytes':len(archive),'md5_hash':md5(archive)}]})
        if u.path.startswith('/api/roms/100/content/'):
            assert q['file_ids']==['7'];return self.send(200,archive)
        if u.path.startswith('/api/devices/'):return self.send(200,{'id':'00000000-0000-0000-0000-000000000001','user_id':1,'sync_enabled':True})
        if u.path=='/api/saves/summary':return self.send(200,{'slots':[{'slot':'autosave','count':len(cls.saves),'latest':cls.saves[-1]}]})
        if u.path=='/api/saves':return self.send(200,cls.saves)
        if u.path.startswith('/api/saves/'):
            parts=u.path.split('/');rid=int(parts[3]);row=next(r for r in cls.saves if r['id']==rid)
            if u.path.endswith('/content'):
                assert q.get('optimistic')==['false'];assert 'device_id' not in q;return self.send(200,cls.data[rid])
            return self.send(200,row)
        return self.send(404,{})
    def do_POST(self):
        cls=type(self);u=urlsplit(self.path);q=parse_qs(u.query);body=self.rfile.read(int(self.headers.get('Content-Length','0')))
        if u.path=='/api/token':
            form=parse_qs(body.decode())
            if form['grant_type']==['password']:
                assert form['username']==['demo'] and form['password']==['correct +&?'];assert 'assets.write' in form['scope'][0]
                assert self.headers.get('Authorization') is None;refresh='refresh-1'
            else:
                assert form['refresh_token']==['refresh-1'];cls.refreshes+=1;refresh='refresh-2'
            return self.send(200,dict(access_token='access',refresh_token=refresh,token_type='bearer'))
        if not self.auth():return
        if u.path=='/api/devices':
            payload=json.loads(body);assert payload['allow_existing'] and payload['sync_mode']=='api';assert 'mac_address' not in payload
            return self.send(201,{'device_id':'00000000-0000-0000-0000-000000000001'})
        if u.path.endswith('/downloaded'):
            rid=int(u.path.split('/')[3]);payload=json.loads(body);row=next(r for r in cls.saves if r['id']==rid)
            assert payload['content_hash']==row['content_hash'];cls.acks[rid]=row['content_hash']
            if cls.mode=='race':self.add(ord('Z'));cls.mode='normal'
            return self.send(200,row)
        if u.path=='/api/saves':
            assert q['overwrite']==['false'] and q['autocleanup']==['false'];assert q['slot']==['autosave'];assert q['emulator']==['mgba'];assert q['device_id']==['00000000-0000-0000-0000-000000000001']
            latest=cls.saves[-1]
            if cls.acks.get(latest['id'])!=latest['content_hash']:return self.send(409,{})
            boundary=self.headers['Content-Type'].split('boundary=')[1].strip('"').encode();data=body.split(b'\r\n\r\n',1)[1].rsplit(b'\r\n--'+boundary,1)[0];assert len(data)==cls.saves[-1]['file_size_bytes'] and md5(data)==q['content_hash'][0]
            cls.uploads+=1
            if cls.mode=='lost-no-commit':cls.unknown_count=cls.uploads;self.close_connection=True;cls.mode='normal';return
            row=self.add(data);cls.acks[row['id']]=row['content_hash']
            if cls.mode=='lost-response':cls.lost_upload_count=cls.uploads;self.close_connection=True;cls.mode='normal';return
            return self.send(200,row)
        return self.send(404,{})
if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='retrosync-v1-') as tmp:
        pathlib.Path(tmp,'sdmc:').mkdir()
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:subprocess.run([probe,f'http://127.0.0.1:{server.server_port}'],cwd=tmp,check=True,timeout=90)
        finally:server.shutdown();server.server_close();thread.join()
