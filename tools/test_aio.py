import ftplib, hashlib, io, json, pathlib, subprocess, urllib.request, urllib.error
root=pathlib.Path('/data/trm-aio-integration')
root.mkdir(parents=True,exist_ok=True)
proc=subprocess.Popen(['./aio-host'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert proc.stdout.readline().strip()=='ready'
def api(route,method='GET',**args):
    from urllib.parse import urlencode
    url='http://127.0.0.1:18084/aio/'+route+'?'+urlencode(args)
    with urllib.request.urlopen(urllib.request.Request(url,method=method),timeout=20) as r:
        return json.load(r)
try:
    assert api('ftp')['enabled'] is False
    api('mkdir','POST',path=str(root/'folder'))
    assert any(e['name']=='folder' for e in api('files',path=str(root))['entries'])
    assert api('ftp','POST',enabled=1)['enabled']
    ftp=ftplib.FTP();ftp.connect('127.0.0.1',2121,timeout=10);ftp.login();ftp.cwd(str(root))
    content=bytes(range(256))*1024
    ftp.storbinary('STOR example.bin',io.BytesIO(content))
    assert ftp.size('example.bin')==len(content)
    result=io.BytesIO();ftp.retrbinary('RETR example.bin',result.write);assert result.getvalue()==content
    assert 'example.bin' in ftp.nlst();ftp.rename('example.bin','renamed.bin')
    with urllib.request.urlopen('http://127.0.0.1:18084/aio/download?path='+str(root/'renamed.bin')) as r:assert r.read()==content
    api('rename','POST',path=str(root/'renamed.bin'),to=str(root/'moved.bin'))
    api('delete','POST',path=str(root/'moved.bin'))
    api('delete','POST',path=str(root/'folder'))
    try:api('mkdir','POST',path='/tmp/aio-must-not-create');raise AssertionError('System write allowed')
    except urllib.error.HTTPError as e:assert e.code==403
    (root/'escape').symlink_to('/tmp',target_is_directory=True)
    try:api('mkdir','POST',path=str(root/'escape'/'aio-must-not-create'));raise AssertionError('Symlink escaped write boundary')
    except urllib.error.HTTPError as e:assert e.code==403
    (root/'escape').unlink()
    ftp.quit()
    # Off must interrupt an idle connected client, then permit a clean restart.
    ftp=ftplib.FTP();ftp.connect('127.0.0.1',2121,timeout=10);ftp.login()
    assert not api('ftp','POST',enabled=0)['enabled'];ftp.close()
    assert api('ftp','POST',enabled=1)['enabled']
    assert not api('ftp','POST',enabled=0)['enabled']
    print('PASS: file listing, directory creation, downloads, rename/move/delete, FTP passive upload/download/list/rename, byte integrity, write boundaries, symlink boundary, stop active client and restart')
finally:
    proc.communicate('\n',timeout=20)
    for p in root.iterdir():
        if p.is_file() or p.is_symlink():p.unlink()
        elif p.is_dir():p.rmdir()
    root.rmdir()
