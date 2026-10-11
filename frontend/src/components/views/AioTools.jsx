import React, { useEffect, useState } from 'react'
import { Folder, File, RefreshCw, ArrowUp, Download, Pencil, Trash2, FolderPlus, Copy, Scissors, X } from 'lucide-react'

async function request(url, method='GET') {
  const r=await fetch(url,{method,cache:'no-store'});const data=await r.json()
  if(!r.ok)throw new Error(data.error || 'Request failed');return data
}
const child=(path,name)=>`${path==='/'?'':path}/${name}`
const size=n=>n>=1073741824?`${(n/1073741824).toFixed(1)} GB`:n>=1048576?`${(n/1048576).toFixed(1)} MB`:n>=1024?`${(n/1024).toFixed(1)} KB`:`${n} B`

export function FtpSettings({ip}) {
  const [state,setState]=useState(null),[error,setError]=useState(''),[busy,setBusy]=useState(false)
  useEffect(()=>{let live=true;const refresh=()=>request('/aio/ftp').then(s=>{if(live)setState(s)}).catch(e=>{if(live)setError(e.message)});refresh();const id=setInterval(refresh,4000);return()=>{live=false;clearInterval(id)}},[])
  async function toggle(){setBusy(true);setError('');try{setState(await request(`/aio/ftp?enabled=${state.enabled?0:1}`,'POST'))}catch(e){setError(e.message)}finally{setBusy(false)}}
  return <section className="aio-panel"><div className="aio-section-heading"><div><h3>FTP server</h3><p>Transfer files between your computer and PS5.</p></div><button role="switch" aria-checked={!!state?.enabled} disabled={busy||!state} onClick={toggle} className={`aio-toggle ${state?.enabled?'on':''}`}>{busy?'Updating…':state?.enabled?'On':'Off'}</button></div>
    <p>{state?.enabled?`Connect to ${ip || 'your PS5 IP'}:${state.port} · ${state.transfers} active transfer`:'Disabled. Enable when you need to transfer files.'}</p>
    {state?.enabled && <dl className="aio-ftp-info"><div><dt>Server address</dt><dd>ftp://{ip || 'your PS5 IP'}:{state.port}</dd></div><div><dt>Port</dt><dd>{state.port}</dd></div></dl>}
    <p className="aio-muted">It stays on until disabled or Payload Manager closes. Writes are available in /data and USB storage; system locations are read-only.</p>{error&&<p role="alert" className="aio-error">{error}</p>}</section>
}

export function FileManager() {
  const [path,setPath]=useState('/data'),[input,setInput]=useState('/data'),[data,setData]=useState(null),[error,setError]=useState(''),[busy,setBusy]=useState(false),[drives,setDrives]=useState([]),[clipboard,setClipboard]=useState(null),[message,setMessage]=useState('')
  async function refresh(next=path){setBusy(true);setError('');try{const [d,devices]=await Promise.all([request(`/aio/files?path=${encodeURIComponent(next)}`),request('/aio/drives')]);setData(d);setDrives(devices.drives);setPath(d.path);setInput(d.path)}catch(e){setError(e.message)}finally{setBusy(false)}}
  useEffect(()=>{refresh('/data')},[])
  useEffect(()=>{let live=true;const timer=setInterval(()=>request('/aio/drives').then(d=>{if(live)setDrives(d.drives)}).catch(()=>{}),5000);return()=>{live=false;clearInterval(timer)}},[])
  async function mutate(action,file,to){setBusy(true);setError('');try{await request(`/aio/${action}?path=${encodeURIComponent(file)}${to?`&to=${encodeURIComponent(to)}`:''}`,'POST');await refresh()}catch(e){setError(e.message)}finally{setBusy(false)}}
  async function paste(){if(!clipboard)return;setBusy(true);setError('');setMessage('');try{await request(`/aio/${clipboard.action}?path=${encodeURIComponent(clipboard.path)}&to=${encodeURIComponent(child(path,clipboard.name))}`,'POST');setMessage(`${clipboard.name} ${clipboard.action==='move'?'moved':'copied'} to ${path}`);setClipboard(null);await refresh()}catch(e){setError(e.message)}finally{setBusy(false)}}
  const entries=[...(data?.entries||[])].filter(e=>path!=='/mnt'||!/^usb[0-7]$/.test(e.name)||drives.includes(`/mnt/${e.name}`)).sort((a,b)=>Number(b.directory)-Number(a.directory)||a.name.localeCompare(b.name))
  return <section className="aio-panel"><div className="aio-section-heading"><div><h2>File Manager</h2><p>Browse your console and USB drives.</p></div><button disabled={busy} onClick={()=>refresh()} aria-label="Refresh files"><RefreshCw size={20}/></button></div>
    <div className="aio-toolbar"><button disabled={busy||path==='/'} aria-label="Parent folder" onClick={()=>refresh(path.slice(0,path.lastIndexOf('/'))||'/')}><ArrowUp size={20}/></button><form onSubmit={e=>{e.preventDefault();refresh(input)}}><input aria-label="Folder path" value={input} onChange={e=>setInput(e.target.value)}/><button disabled={busy}>Go</button></form><button disabled={busy} onClick={()=>{const n=window.prompt('New folder name');if(n&&n!=='.'&&n!=='..'&&!n.includes('/'))mutate('mkdir',child(path,n))}}><FolderPlus size={18}/> New folder</button></div>
    <div className="aio-shortcuts">{['/','/data','/user',...drives].map(p=><button key={p} disabled={busy} onClick={()=>refresh(p)}>{p}</button>)}</div>
    {clipboard&&<div className="aio-clipboard"><div><strong>{clipboard.action==='move'?'Move':'Copy'}: {clipboard.name}</strong><p className="aio-muted">From {clipboard.path}</p></div><button disabled={busy||!data?.writable} onClick={paste}>{clipboard.action==='move'?<Scissors size={18}/>:<Copy size={18}/>} {clipboard.action==='move'?'Move':'Copy'} to current folder</button><button disabled={busy} aria-label="Clear selected file" onClick={()=>setClipboard(null)}><X size={18}/></button></div>}
    {error&&<p role="alert" className="aio-error">{error}</p>}{busy&&<p role="status">Loading…</p>}
    {message&&<p role="status">{message}</p>}
    <p className="aio-muted">Select Copy or Move, open your destination, then choose “to current folder.” Folders include their contents. Existing destinations are kept; symlinks and special files cannot be copied. Uploads use FTP. Delete removes files and empty folders.</p>
    <div className="aio-file-list">{entries.map(e=>{const full=child(path,e.name);return <div className="aio-file-row" key={e.name}><button disabled={busy||(!e.directory&&!e.symlink)} onClick={()=>refresh(full)} className="aio-file-name">{e.directory?<Folder/>:<File/>}<span>{e.name}{e.symlink?' ↗':''}</span></button><span className="aio-muted">{e.directory?'Folder':size(e.size)}</span><div className="aio-file-actions"><button disabled={busy||e.symlink} title="Copy" aria-label={`Copy ${e.name}`} onClick={()=>{setClipboard({action:'copy',path:full,name:e.name});setMessage('')}}><Copy size={18}/></button><button disabled={busy||e.symlink||!data?.writable} title="Move" aria-label={`Move ${e.name}`} onClick={()=>{setClipboard({action:'move',path:full,name:e.name});setMessage('')}}><Scissors size={18}/></button>{!e.directory&&<a href={`/aio/download?path=${encodeURIComponent(full)}`} download={e.name} aria-label={`Download ${e.name}`}><Download size={18}/></a>}<button disabled={busy} aria-label={`Rename ${e.name}`} onClick={()=>{const n=window.prompt('New name or full destination path',e.name);if(n&&n!==e.name)mutate('rename',full,n.startsWith('/')?n:child(path,n))}}><Pencil size={18}/></button><button disabled={busy} aria-label={`Delete ${e.name}`} onClick={()=>{if(window.confirm(`Delete ${e.name}?`))mutate('delete',full)}}><Trash2 size={18}/></button></div></div>})}{!busy&&data&&!entries.length&&<p>This folder is empty.</p>}</div></section>
}

export function DashboardSummary({payloads,config,onNavigate}) {
  const [ftp,setFtp]=useState(null)
  useEffect(()=>{let live=true;const f=()=>request('/aio/ftp').then(s=>{if(live)setFtp(s)}).catch(()=>{});f();const id=setInterval(f,4000);return()=>{live=false;clearInterval(id)}},[])
  const queue=(config.AUTOLOAD_LIST||'').split(',').filter(Boolean)
  return <><div className="aio-stats"><div className="aio-panel"><h3>Installed payloads</h3><strong>{String(payloads.length).padStart(2,'0')}</strong><p>Ready to launch</p></div><div className="aio-panel"><h3>Startup queue</h3><strong>{String(queue.length).padStart(2,'0')}</strong><p>{config.AUTOLOAD_ENABLED?'Loads when Payload Manager starts':'Startup loading disabled'}</p></div><div className="aio-panel"><h3>Active transfers</h3><strong>{ftp?String(ftp.transfers).padStart(2,'0'):'—'}</strong><p>{ftp?.enabled?'FTP server enabled':'FTP server disabled'}</p></div></div><section className="aio-panel aio-queue"><div className="aio-section-heading"><h3>Startup queue</h3><button onClick={()=>onNavigate('autoload')}>Edit startup order</button></div>{queue.length?queue.map((p,i)=><div key={`${i}-${p}`}><span>{String(i+1).padStart(2,'0')}</span>{p.startsWith('!')?`Delay ${Number(p.slice(1))/1000}s`:p.split('/').pop().replace(/\.(elf|bin)$/i,'')}</div>):<p>No startup payloads configured.</p>}</section></>
}

