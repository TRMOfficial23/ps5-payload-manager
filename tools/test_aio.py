import ftplib, hashlib, io, json, pathlib, subprocess, urllib.request, urllib.error, socket, struct, time, shutil
root=pathlib.Path('/data/trm-aio-integration')
root.mkdir(parents=True,exist_ok=True)
proc=subprocess.Popen(['./aio-host'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
assert proc.stdout.readline().strip()=='ready'
def api(route,method='GET',**args):
    from urllib.parse import urlencode
    url='http://127.0.0.1:18084/aio/'+route+'?'+urlencode(args)
    with urllib.request.urlopen(urllib.request.Request(url,method=method),timeout=20) as r:
        return json.load(r)
usb=pathlib.Path('/mnt/usb7');mounted=False;created_usb=False
try:
    assert api('files',path='/data')['writable']
    assert '/mnt/usb7' not in api('drives')['drives']
    assert not usb.exists(), 'Dedicated test mount path already exists'
    usb.mkdir(parents=True);created_usb=True
    assert '/mnt/usb7' not in api('drives')['drives']
    assert not any(e['name']=='usb7' for e in api('files',path='/mnt')['entries'])
    subprocess.run(['mount','-t','tmpfs','-o','size=8m','tmpfs',str(usb)],check=True);mounted=True
    assert '/mnt/usb7' in api('drives')['drives']
    assert api('files',path=str(usb))['writable']
    assert any(e['name']=='usb7' for e in api('files',path='/mnt')['entries'])
    original=root/'original';(original/'nested').mkdir(parents=True)
    sample=bytes(range(256))*1024;(original/'nested'/'sample.bin').write_bytes(sample)
    api('copy','POST',path=str(original),to=str(root/'copied'))
    assert (original/'nested'/'sample.bin').read_bytes()==sample
    assert (root/'copied'/'nested'/'sample.bin').read_bytes()==sample
    for dest in [root/'copied',original/'recursive']:
        try:api('copy','POST',path=str(original),to=str(dest));raise AssertionError('Unsafe copy allowed')
        except urllib.error.HTTPError as e:assert e.code==409
    api('move','POST',path=str(root/'copied'),to=str(root/'moved'))
    assert not (root/'copied').exists() and (root/'moved'/'nested'/'sample.bin').read_bytes()==sample
    api('move','POST',path=str(root/'moved'),to=str(usb/'cross-volume'))
    assert not (root/'moved').exists() and (usb/'cross-volume'/'nested'/'sample.bin').read_bytes()==sample
    api('move','POST',path=str(usb/'cross-volume'),to=str(root/'returned'))
    assert not (usb/'cross-volume').exists() and (root/'returned'/'nested'/'sample.bin').read_bytes()==sample
    (original/'symlink').symlink_to('/tmp',target_is_directory=True)
    try:api('copy','POST',path=str(original),to=str(root/'failed-copy'));raise AssertionError('Copied symlink')
    except urllib.error.HTTPError as e:assert e.code==409
    assert not (root/'failed-copy').exists() and not list(root.glob('.trm-copy-*'))
    (original/'symlink').unlink();shutil.rmtree(original);shutil.rmtree(root/'returned')
    subprocess.run(['umount',str(usb)],check=True);mounted=False
    assert '/mnt/usb7' not in api('drives')['drives']
    assert api('ftp')['enabled'] is False
    api('mkdir','POST',path=str(root/'folder'))
    assert any(e['name']=='folder' for e in api('files',path=str(root))['entries'])
    assert api('ftp','POST',enabled=1)['enabled']
    # Exercise startup before any client connects, including repeated toggles.
    time.sleep(1)
    assert proc.poll() is None and api('ftp')['enabled']
    for _ in range(5):
        assert not api('ftp','POST',enabled=0)['enabled']
        assert api('ftp','POST',enabled=1)['enabled']
    assert proc.poll() is None
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
    # Reset a download connection abruptly. The manager must survive SIGPIPE.
    (root/'large.bin').write_bytes(b'0123456789abcdef'*1048576)
    ftp=ftplib.FTP();ftp.connect('127.0.0.1',2121,timeout=10);ftp.login();ftp.cwd(str(root));ftp.voidcmd('TYPE I')
    data=ftp.transfercmd('RETR large.bin');data.setsockopt(socket.SOL_SOCKET,socket.SO_LINGER,struct.pack('ii',1,0));data.close();ftp.close()
    assert api('ftp')['enabled']
    (root/'large.bin').unlink()
    # Off must interrupt an idle connected client, then permit a clean restart.
    ftp=ftplib.FTP();ftp.connect('127.0.0.1',2121,timeout=10);ftp.login()
    assert not api('ftp','POST',enabled=0)['enabled'];ftp.close()
    assert api('ftp','POST',enabled=1)['enabled']
    assert not api('ftp','POST',enabled=0)['enabled']
    print('PASS: file listing, directory creation, downloads, rename/move/delete, FTP passive upload/download/list/rename, byte integrity, write boundaries, symlink boundary, stop active client and restart')
finally:
    proc.communicate('\n',timeout=20)
    if mounted:subprocess.run(['umount',str(usb)],check=True)
    if created_usb:usb.rmdir()
    for p in root.iterdir():
        if p.is_file() or p.is_symlink():p.unlink()
        elif p.is_dir():p.rmdir()
    root.rmdir()
