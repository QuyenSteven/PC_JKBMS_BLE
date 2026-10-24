// JK BMS Studio device management.
// This page deliberately does not touch BLE credentials or device flash.
// Until rename/password opcodes are confirmed it supports safe PC-side aliases
// and routes forgotten-password recovery to JK's documented manufacturer flow.
(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const cache = new Map();
  const prefix='jk-bms-studio.alias.';
  let activeAddress='',nativeName='',model='',firmware='',serial='',connected=false;
  const storage={
    read(key){
      try{return window.localStorage?.getItem(prefix+key)||cache.get(key)||'';}
      catch{return cache.get(key)||'';}
    },
    put(key,value){
      cache.set(key,value);
      try{window.localStorage?.setItem(prefix+key,value);}catch{}
    },
    remove(key){
      cache.delete(key);
      try{window.localStorage?.removeItem(prefix+key);}catch{}
    }
  };
  function text(id,value){$(id).textContent=String(value);}
  function fallbackName(){
    const option=$('device-list').selectedOptions?.[0];
    const advertised=option?.textContent?.split(' [')[0]||'';
    return nativeName||advertised||'JK BMS';
  }
  function refresh(){
    const hasAddress=!!activeAddress;
    const local=hasAddress?storage.read(activeAddress):'';
    $('device-local-name').value=local;
    $('device-local-save').disabled=!hasAddress;
    $('device-local-reset').disabled=!hasAddress||!local;
    text('device-native-name',nativeName||'--');
    text('device-native-model',model||'--');
    text('device-native-firmware',firmware?'Firmware '+firmware:'Chưa nhận thông tin');
    text('device-native-serial',serial||'--');
    text('device-admin-status',connected?'Đã kết nối BLE':hasAddress?'Đã chọn · Chưa kết nối':'Chờ BLE');
    text('device-local-feedback',hasAddress?(local?'Tên PC đã lưu riêng cho thiết bị này.':'Chưa đặt tên PC cho thiết bị này.'):'Chọn thiết bị BLE để đặt tên.');
    if(hasAddress){
      text('device-label',local||fallbackName());
    }
  }
  function chooseDevice(){
    activeAddress=$('device-list').value||'';
    nativeName='';model='';firmware='';serial='';
    refresh();
  }
  $('device-list').addEventListener('change',chooseDevice);
  $('device-local-save').addEventListener('click',()=>{
    if(!activeAddress)return;
    const value=$('device-local-name').value.trim();
    if(!value||value.length>32||/[\u0000-\u001f]/.test(value)){
      text('device-local-feedback','Tên phải có 1–32 ký tự hợp lệ.');return;
    }
    storage.put(activeAddress,value);refresh();
    text('device-local-feedback','Đã lưu tên hiển thị PC. Tên BLE thực tế trên BMS không thay đổi.');
  });
  $('device-local-reset').addEventListener('click',()=>{
    if(!activeAddress)return;
    storage.remove(activeAddress);refresh();
    text('device-local-feedback','Đã xóa tên riêng trên PC; quay lại tên BMS.');
  });
  const receive=event=>{
    let message=event.data;
    if(typeof message==='string'){try{message=JSON.parse(message);}catch{return;}}
    if(!message||typeof message!=='object')return;
    if(message.type==='deviceInfo'){
      nativeName=String(message.deviceName||'');
      model=String(message.model||'');
      firmware=String(message.firmware||'');
      serial=String(message.serial||'');
      refresh();
    }else if(message.type==='status'){
      connected=message.message==='connected';
      if(!connected){nativeName='';model='';firmware='';serial='';}
      refresh();
    }else if(message.type==='clearDevices'){
      activeAddress='';nativeName='';model='';firmware='';serial='';refresh();
    }
  };
  window.chrome?.webview?.addEventListener('message',receive);
  refresh();
  window.JkDeviceManager={chooseDevice,receive,readAlias:key=>storage.read(key)};
})();