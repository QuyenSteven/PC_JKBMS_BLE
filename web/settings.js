/* JK BMS Studio configuration editor.
   Changes never leave the PC until the user explicitly reviews and confirms.
   Writes are serialized. Only the C++ backend may mark a write verified. */
(() => {
'use strict';
const el = id => document.getElementById(id);
const sendCommand = command => {
  if(window.chrome && window.chrome.webview)window.chrome.webview.postMessage(command);
};
let online=false,ready=false,current=null,meta=null;
const edits=new Map();
const rows=new Map();
let pending=null,queue=[];
let importSource=null;
const same=(a,b,precision)=>Math.abs(Number(a)-Number(b))<Math.pow(10,-Math.min(precision+1,6));
const fmt=(v,n)=>Number(v).toFixed(Math.min(5,Math.max(0,n||0)));
function toast(message){if(typeof note==='function')note('CONFIG  '+message);el('config-operation').textContent=message;}
function setText(id,value){const node=el(id);if(node)node.textContent=String(value);}
function guardReady(){return online&&ready&&current instanceof Map&&meta?.model&&meta?.protocol;}
function changed(){
  const out=[];
  for(const [key,value] of edits){
    const field=current?.get(key);
    if(field&&field.writable&&!same(value,field.value,field.precision))out.push({key,value,field});
  }
  return out;
}
function updateButtons(){
  const count=changed().length;
  setText('settings-dirty-count',count);
  setText('settings-apply','Ghi thay đổi ('+count+')');
  el('settings-backup').disabled=!current||!current.size;
  el('settings-revert').disabled=!edits.size||!!pending;
  el('settings-apply').disabled=!guardReady()||count===0||!!pending;
  el('settings-refresh').disabled=!online||!!pending;
  el('settings-import').disabled=!!pending;
  const switches=[['charge_mos','ctrl-charge-toggle'],['discharge_mos','ctrl-discharge-toggle'],['balance_switch','ctrl-balance-toggle']];
  for(const [key,id] of switches){
    const descriptor=current?.get(key);
    el(id).disabled=!guardReady()||!!pending||!descriptor?.writable||!Number.isFinite(descriptor.value);
  }
  el('btn-reboot').disabled=!guardReady()||!!pending;
  setText('ctrl-write-access',guardReady()?'Cho phép ghi có xác nhận và đọc lại':'Chờ nhận dạng model + Settings hợp lệ');
}
function stateRow(key,kind,message){
 const row=rows.get(key);
 if(!row)return;
 row.classList.remove('config-pending','config-failed','config-verified');
 const stateNode=row.querySelector('.config-state');
 if(kind==='pending'){row.classList.add('config-pending');stateNode.className='config-state pending';}
 else if(kind==='failed'){row.classList.add('config-failed');stateNode.className='config-state failed';}
 else if(kind==='verified'){row.classList.add('config-verified');stateNode.className='config-state verified';}
 else if(kind==='edited'){stateNode.className='config-state edited';}
 else stateNode.className='config-state';
 stateNode.textContent=message;
}
function render(){
 const body=el('protection-body');rows.clear();
 const fragment=document.createDocumentFragment();
 if(!current || !current.size) {
  const tr=document.createElement('tr');const td=document.createElement('td');td.colSpan=6;td.className='config-waiting';
  td.textContent='Đang chờ khung Settings có thể giải mã từ BMS.';tr.appendChild(td);body.replaceChildren(tr);updateButtons();return;
 }
 for(const data of current.values()){
  const tr=document.createElement('tr');tr.dataset.key=data.id;
  const cell=value=>{const td=document.createElement('td');td.textContent=String(value);tr.appendChild(td);return td;};
  cell(data.group);cell(data.label);
  cell(fmt(data.value,data.precision));
  const editCell=document.createElement('td');
  if(data.writable){
   const input=document.createElement('input');input.className='config-input';input.type='number';
   input.min=String(data.min);input.max=String(data.max);
   input.step=String(data.precision>0?Math.pow(10,-data.precision):1);
   input.value=String(edits.has(data.id)?edits.get(data.id):data.value);
   input.disabled=!guardReady()||!!pending;
   input.addEventListener('change',()=>{
    const v=Number(input.value);
    if(!Number.isFinite(v)||!input.value.trim()||v<data.min||v>data.max){
      input.value=String(edits.get(data.id)??data.value);
      toast(data.label+': giá trị ngoài giới hạn '+data.min+'–'+data.max);
      return;
    }
    if(same(v,data.value,data.precision))edits.delete(data.id);
    else edits.set(data.id,v);
    updateButtons();stateRow(data.id,edits.has(data.id)?'edited':'idle',edits.has(data.id)?'Đang chỉnh sửa':'Đã đọc');
   });
   editCell.appendChild(input);
  }else{
   editCell.textContent='Chỉ đọc';
  }
  tr.appendChild(editCell);
  cell(data.unit||'—');
  const stateCell=cell('');
  const status=document.createElement('span');status.className='config-state';
  status.textContent=data.writable?'Đã đọc':'Chỉ đọc';
  stateCell.appendChild(status);
  fragment.appendChild(tr);rows.set(data.id,tr);
 }
 body.replaceChildren(fragment);
 for(const key of edits.keys())stateRow(key,'edited','Chờ ghi');
 if(pending)stateRow(pending.key,'pending','Đang đọc lại');
 updateButtons();
}
function settingsArrived(data){
 if(!Array.isArray(data.fields)||!data.protocol)return;
 const newModel=(data.model||'').trim(),newFirmware=(data.firmware||'').trim();
 const newSerial=(data.serial||'').trim(),newMac=(data.mac||'').trim();
 const fingerprint=newModel+'|'+newFirmware+'|'+data.protocol+'|'+newSerial+'|'+newMac;
 if(meta && meta.fingerprint!==fingerprint){
  edits.clear();queue=[];pending=null;importSource=null;
  toast('Đã chuyển model hoặc phiên bản phần mềm; mọi chỉnh sửa chưa ghi đã hủy.');
 }
 meta={model:newModel,firmware:newFirmware,protocol:data.protocol,serial:newSerial,mac:newMac,fingerprint};
 ready=data.ready===true;
 current=new Map(data.fields.filter(x=>x&&typeof x.id==='string'&&Number.isFinite(x.value)).map(x=>[x.id,x]));
 for(const key of Array.from(edits.keys()))if(!current.has(key))edits.delete(key);
 setText('settings-model',newModel||'Đang xác định');
 setText('settings-software',(newFirmware?'Firmware '+newFirmware+' · ':'')+data.protocol);
 setText('settings-frame-state',data.fields.length+' mục');
 setText('settings-frame-detail',ready?'Đã giải mã Settings '+data.protocol:'Chờ nhận dạng để mở ghi');
 setText('protection-status',ready?'Đã đọc '+data.fields.length+' tham số':'Đã nhận cấu hình · Chờ nhận dạng');
 setText('control-status',ready?'Sẵn sàng xác nhận thay đổi':'Chỉ đọc');
 render();
}
function clearSettings(){
 ready=false;current=null;meta=null;pending=null;queue=[];edits.clear();importSource=null;
 setText('settings-model','--');setText('settings-software','Chưa có thiết bị');
 setText('settings-frame-state','--');setText('settings-frame-detail','Chờ BLE 0x01');
 setText('settings-write-state','Chưa có chỉnh sửa');setText('protection-status','Chờ kết nối BMS');
 render();
}
function previewChangeList(changes){
 return changes.map(({key,value,field})=>field.label+': '+fmt(field.value,field.precision)+' → '+fmt(value,field.precision)+' '+field.unit).join('\n');
}
function apply(){
 if(!guardReady()||pending) {toast('Không thể ghi: chưa xác minh thiết bị hoặc đang xử lý.');return;}
 let changes=changed();
 if(!changes.length)return;
 for(const {key,value,field} of changes){
  if(!field.writable||!Number.isFinite(value)||value<field.min||value>field.max) {
   toast('Giới hạn không hợp lệ: '+field.label);return;
  }
 }
 const message='Ghi '+changes.length+' mục lên '+meta.model+' ('+meta.firmware+')?\n\n'
  +previewChangeList(changes).slice(0,2100)
  +'\n\nThay đổi sai có thể ngắt sạc/xả. Mỗi mục sẽ được ghi và kiểm tra lại trước khi ghi mục tiếp.';
 if(!window.confirm(message))return;
 const phrase=window.prompt('Nhập GHI để xác nhận thay đổi trên BMS thật:','');
 if(phrase!=='GHI')return;
 queue=changes.map(({key,value})=>({key,value}));
 toast('Đang ghi theo thứ tự từng tham số và đối chiếu readback.');
 nextWrite();
}
function nextWrite(){
 if(pending||!queue.length)return;
 if(!guardReady()){queue=[];toast('Ngắt kết nối hoặc model thay đổi, dừng ghi.');return;}
 const change=queue.shift();
 const field=current.get(change.key);
 if(!field||!field.writable){queue=[];toast('Trường không còn được hỗ trợ; dừng ghi.');return;}
 pending=change;
 stateRow(change.key,'pending','Đang gửi/đối chiếu');
 setText('settings-write-state','Đang ghi '+field.label);
 sendCommand('settings:write:'+change.key+':'+Number(change.value));
 updateButtons();
}
function writeStatus(data){
 if(!data||!data.key)return;
 if(data.key==='restart') {toast(data.message||data.state);return;}
 const key=data.key;
 if(data.message)toast(data.message);
 if(data.state==='pending'){stateRow(key,'pending','Đang đối chiếu');return;}
 if(data.state==='failed'){
  stateRow(key,'failed','Không xác minh');
  if(pending&&pending.key===key){pending=null;queue=[];}
  updateButtons();return;
 }
 if(data.state==='verified'){
  stateRow(key,'verified','Đã xác minh');
  edits.delete(key);
  if(pending&&pending.key===key){pending=null;}
  render();
  if(queue.length)nextWrite();
  else{
   toast('Đã kiểm tra lại tất cả các mục đã gửi.');updateButtons();
  }
 }
}
function exportBackup(){
 if(!current||!current.size||!meta)return;
 const values={};for(const [key,entry] of current)values[key]=Number(entry.value);
 const snapshot={format:'JK_BMS_STUDIO_CONFIG_V1',timestamp:new Date().toISOString(),model:meta.model,
  firmware:meta.firmware,protocol:meta.protocol,serial:meta.serial,mac:meta.mac,values};
 const blob=new Blob([JSON.stringify(snapshot,null,2)],{type:'application/json'});
 const url=URL.createObjectURL(blob),link=document.createElement('a');
 const safe=(meta.model||'JK-BMS').replace(/[^a-z0-9_-]/gi,'_');
 link.href=url;link.download=safe+'_'+meta.protocol+'_settings.json';
 document.body.appendChild(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),1500);
 toast('Đã xuất cấu hình JSON đã đọc trực tiếp từ BMS.');
}
async function importBackup(file){
 if(!file||file.size>262144){toast('Tập tin không hợp lệ hoặc quá 256 KB.');return;}
 let data;
 try{data=JSON.parse(await file.text());}catch(e){toast('JSON không hợp lệ: '+e.message);return;}
 if(!data||data.format!=='JK_BMS_STUDIO_CONFIG_V1'||!data.values||typeof data.values!=='object'){
  toast('Đây không phải tệp sao lưu JK BMS Studio.');return;
 }
 if(!guardReady()){toast('Hãy kết nối và đọc Settings từ BMS trước khi khôi phục.');return;}
 if(data.model!==meta.model||data.firmware!==meta.firmware||data.protocol!==meta.protocol||data.serial!==meta.serial||data.mac!==meta.mac){
  toast('Không trùng model, firmware, serial hoặc địa chỉ BLE. Không khôi phục sang BMS khác.');return;
 }
 const changes=[];for(const [key,value] of Object.entries(data.values)){
  const f=current.get(key);
  if(!f||!f.writable||!Number.isFinite(value)||value<f.min||value>f.max)continue;
  if(!same(value,f.value,f.precision))changes.push({key,value,field:f});
 }
 if(!changes.length){toast('Tệp không có thay đổi có thể áp dụng cho thiết bị này.');return;}
 if(!window.confirm('Xem trước '+changes.length+' mục từ bản sao lưu:\n\n'+previewChangeList(changes).slice(0,2400)
   +'\n\nChỉ đưa vào danh sách chỉnh sửa; CHƯA GỬI lệnh ghi tới BMS.'))return;
 edits.clear();
 for(const item of changes)edits.set(item.key,item.value);
 importSource=file.name;
 render();
 toast('Đã nạp '+changes.length+' thay đổi từ '+file.name+'. Kiểm tra và nhấn Ghi thay đổi để áp dụng.');
}
function toggleSwitch(key){
 if(!guardReady()||pending)return;
 const field=current.get(key);
 if(!field||!field.writable)return;
 const value=field.value===1?0:1;
 if(!window.confirm('Chuyển '+field.label+' sang '+(value===1?'BẬT':'TẮT')+
    '?\nThay đổi MOSFET/cân bằng có thể lập tức ảnh hưởng pin hoặc tải đang kết nối.'))return;
 const phrase=window.prompt('Nhập GHI để xác nhận thao tác trên BMS:','');
 if(phrase!=='GHI')return;
 queue=[{key,value}];nextWrite();
}
function restart(){
 if(!guardReady()||pending)return;
 if(!window.confirm('Reboot BMS '+meta.model+'?\nBMS có thể ngắt sạc/xả và mất liên lạc BLE trong quá trình restart.'))return;
 if(window.prompt('Nhập REBOOT để gửi lệnh restart 0x66:','')!=='REBOOT')return;
 sendCommand('bms:restart');
 toast('Đã yêu cầu gửi lệnh restart; đang chờ kết quả BLE.');
}
document.getElementById('settings-refresh').addEventListener('click',()=>{
 sendCommand('settings:read');toast('Đã yêu cầu BMS đọc lại Settings 0x01.');
});
el('settings-backup').addEventListener('click',exportBackup);
el('settings-import').addEventListener('click',()=>el('settings-file').click());
el('settings-file').addEventListener('change',async e=>{
 const file=e.target.files?.[0];if(file)await importBackup(file);e.target.value='';
});
el('settings-revert').addEventListener('click',()=>{if(pending)return;edits.clear();importSource=null;render();toast('Đã bỏ chỉnh sửa chưa ghi.');});
el('settings-apply').addEventListener('click',apply);
el('ctrl-charge-toggle').addEventListener('click',()=>toggleSwitch('charge_mos'));
el('ctrl-discharge-toggle').addEventListener('click',()=>toggleSwitch('discharge_mos'));
el('ctrl-balance-toggle').addEventListener('click',()=>toggleSwitch('balance_switch'));
el('btn-reboot').addEventListener('click',restart);
const receive=event=>{
 let data=event.data;if(typeof data==='string'){try{data=JSON.parse(data);}catch{return;}}
 if(!data||typeof data!=='object')return;
 if(data.type==='status'){online=data.message==='connected';if(!online)clearSettings();else updateButtons();}
 else if(data.type==='settings')settingsArrived(data);
 else if(data.type==='settingsClear')clearSettings();
 else if(data.type==='settingWrite')writeStatus(data);
 else if(data.type==='deviceInfo'){setText('settings-model',data.model||'JK BMS');setText('settings-software','Firmware '+(data.firmware||'--'));}
};
if(window.chrome?.webview)window.chrome.webview.addEventListener('message',receive);
clearSettings();
window.JkSettingsUI={getCurrent:()=>current,getMeta:()=>meta,stage:(key,value)=>{edits.set(key,value);render();},getDirty:()=>changed(),
 receive: data=>receive({data})};
})();
